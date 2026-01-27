/**
 * @file glob_v2.c
 * @brief Ruby 4.0 Dir.glob compatible implementation - v2 redesign
 *
 * Design goals:
 * - High performance: minimal syscalls, minimal allocations
 * - Lightweight: target ~800 lines (down from ~1100)
 * - Simple: unified walker, arena-based results
 *
 * See: DOCS/DESIGN_V2.md for detailed specifications
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

// ============================================================================
// Constants and Flags
// ============================================================================

#define RBC_GLOB_MAX_PATH 4096
#define RBC_RESULTS_INIT_DATA (64 * 1024) // 64KB initial data buffer
#define RBC_RESULTS_INIT_COUNT 256        // Initial path count

// Flags (Ruby File::FNM_* compatible)
#define RBC_FNM_NOESCAPE 0x01
#define RBC_FNM_PATHNAME 0x02
#define RBC_FNM_DOTMATCH 0x04
#define RBC_FNM_CASEFOLD 0x08

// ============================================================================
// Segment Types
// ============================================================================

typedef enum
{
    SEG_LITERAL,   // Literal string (e.g., "foo", "bar.txt")
    SEG_DOT,       // "." (current directory)
    SEG_DOTDOT,    // ".." (parent directory)
    SEG_WILDCARD,  // Wildcard patterns (*, ?, [...], **, .** at end)
    SEG_RECURSIVE, // **/ only (recursive descent)
} rbc_seg_type_t;

// ============================================================================
// Arena-based Results Buffer
// ============================================================================

typedef struct
{
    char *data;       // Contiguous buffer for all paths
    size_t *offsets;  // Offset of each path in data
    size_t count;     // Number of paths
    size_t data_used; // Bytes used in data
    size_t data_cap;  // Capacity of data
    size_t off_cap;   // Capacity of offsets array
} rbc_results_t;

static bool rbc_results_init(rbc_results_t *r)
{
    r->data = malloc(RBC_RESULTS_INIT_DATA);
    r->offsets = malloc(RBC_RESULTS_INIT_COUNT * sizeof(size_t));
    if (!r->data || !r->offsets)
    {
        free(r->data);
        free(r->offsets);
        return false;
    }
    r->count = 0;
    r->data_used = 0;
    r->data_cap = RBC_RESULTS_INIT_DATA;
    r->off_cap = RBC_RESULTS_INIT_COUNT;
    return true;
}

static bool rbc_results_add(rbc_results_t *r, const char *path, size_t len)
{
    // Grow offsets array if needed
    if (r->count >= r->off_cap)
    {
        size_t new_cap = r->off_cap * 2;
        size_t *new_off = realloc(r->offsets, new_cap * sizeof(size_t));
        if (!new_off)
            return false;
        r->offsets = new_off;
        r->off_cap = new_cap;
    }

    // Grow data buffer if needed (include null terminator)
    size_t needed = r->data_used + len + 1;
    if (needed > r->data_cap)
    {
        size_t new_cap = r->data_cap;
        while (new_cap < needed)
            new_cap *= 2;
        char *new_data = realloc(r->data, new_cap);
        if (!new_data)
            return false;
        r->data = new_data;
        r->data_cap = new_cap;
    }

    // Store offset and copy path
    r->offsets[r->count++] = r->data_used;
    memcpy(r->data + r->data_used, path, len);
    r->data[r->data_used + len] = '\0';
    r->data_used += len + 1;

    return true;
}

static void rbc_results_free(rbc_results_t *r)
{
    free(r->data);
    free(r->offsets);
}

// ============================================================================
// Path Utilities (snprintf-free)
// ============================================================================

/**
 * @brief Normalize path: remove consecutive slashes
 * @return Length of normalized path
 */
__attribute__((unused)) static size_t rbc_normalize_path(char *dst, size_t dst_size,
                                                         const char *src, size_t src_len)
{
    if (src_len == 0 || dst_size == 0)
    {
        if (dst_size > 0)
            dst[0] = '\0';
        return 0;
    }

    size_t j = 0;
    bool prev_slash = false;

    for (size_t i = 0; i < src_len && j < dst_size - 1; i++)
    {
        if (src[i] == '/')
        {
            if (!prev_slash)
            {
                dst[j++] = '/';
                prev_slash = true;
            }
            // Skip consecutive slashes
        }
        else
        {
            dst[j++] = src[i];
            prev_slash = false;
        }
    }

    dst[j] = '\0';
    return j;
}

/**
 * @brief Build path: base + '/' + name (no snprintf)
 * @return Length of resulting path
 */
static size_t rbc_build_path(char *buf, size_t buf_size,
                             const char *base, size_t base_len,
                             const char *name, size_t name_len)
{
    if (base_len == 0)
    {
        if (name_len >= buf_size)
            name_len = buf_size - 1;
        memcpy(buf, name, name_len);
        buf[name_len] = '\0';
        return name_len;
    }

    // Check if base already ends with '/'
    bool base_has_slash = (base_len > 0 && base[base_len - 1] == '/');
    size_t sep_len = base_has_slash ? 0 : 1;

    size_t total = base_len + sep_len + name_len;
    if (total >= buf_size)
    {
        total = buf_size - 1;
    }

    memcpy(buf, base, base_len);
    if (!base_has_slash)
    {
        buf[base_len] = '/';
    }

    size_t copy_name = (total > base_len + sep_len) ? total - base_len - sep_len : 0;
    memcpy(buf + base_len + sep_len, name, copy_name);
    buf[total] = '\0';

    return total;
}

/**
 * @brief Append trailing slash to path
 */
static size_t rbc_append_slash(char *buf, size_t len, size_t buf_size)
{
    if (len > 0 && len < buf_size - 1 && buf[len - 1] != '/')
    {
        buf[len++] = '/';
        buf[len] = '\0';
    }
    return len;
}

// ============================================================================
// Directory Entry Helpers (d_type optimization)
// ============================================================================

/**
 * @brief Check if dirent is a directory using d_type when available
 * Falls back to stat() only when necessary
 */
static inline bool rbc_is_dir_entry(struct dirent *e, const char *full_path)
{
#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_DIR)
    if (e->d_type == DT_DIR)
        return true;
    if (e->d_type != DT_UNKNOWN && e->d_type != DT_LNK)
        return false;
#endif
    // Fallback to stat for DT_UNKNOWN, DT_LNK, or systems without d_type
    struct stat st;
    return (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
}

/**
 * @brief Check if path exists
 */
static inline bool rbc_path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/**
 * @brief Check if path is a directory
 */
static inline bool rbc_is_dir(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

// ============================================================================
// Pattern Parsing
// ============================================================================

typedef struct
{
    const char *start;       // Segment start in pattern
    size_t len;              // Segment length
    rbc_seg_type_t type;     // Segment classification
    bool starts_with_dot;    // Segment starts with '.'
    bool has_trailing_slash; // Pattern has '/' after this segment
    bool is_last;            // This is the last segment
} rbc_segment_t;

/**
 * @brief Check if segment contains wildcard characters
 */
static bool rbc_has_wildcard(const char *s, size_t len, unsigned flags)
{
    bool in_bracket = false;
    for (size_t i = 0; i < len; i++)
    {
        if (!(flags & RBC_FNM_NOESCAPE) && s[i] == '\\' && i + 1 < len)
        {
            i++; // Skip escaped character
            continue;
        }
        switch (s[i])
        {
        case '*':
        case '?':
            return true;
        case '[':
            in_bracket = true;
            break;
        case ']':
            if (in_bracket)
                return true; // Complete bracket found
            break;
        }
    }
    return false;
}

/**
 * @brief Parse next segment from pattern
 * @return true if segment found, false if end of pattern
 */
static bool rbc_next_segment(const char **pattern_ptr, unsigned flags,
                             rbc_segment_t *seg)
{
    const char *p = *pattern_ptr;

    // Skip leading slashes (normalize)
    while (*p == '/')
        p++;

    if (*p == '\0')
        return false;

    seg->start = p;
    seg->starts_with_dot = (*p == '.');

    // Find segment end
    const char *seg_start = p;
    while (*p != '\0' && *p != '/')
    {
        if (!(flags & RBC_FNM_NOESCAPE) && *p == '\\' && *(p + 1))
        {
            p += 2; // Skip escaped char
        }
        else
        {
            p++;
        }
    }

    seg->len = p - seg_start;
    seg->has_trailing_slash = (*p == '/');

    // Skip trailing slashes for next call
    while (*p == '/')
        p++;
    seg->is_last = (*p == '\0');
    *pattern_ptr = p;

    // Classify segment
    if (seg->len == 1 && seg_start[0] == '.')
    {
        seg->type = SEG_DOT;
    }
    else if (seg->len == 2 && seg_start[0] == '.' && seg_start[1] == '.')
    {
        seg->type = SEG_DOTDOT;
    }
    else if (seg->len == 2 && seg_start[0] == '*' && seg_start[1] == '*')
    {
        // ** is RECURSIVE only with trailing slash and not at end without it
        if (seg->has_trailing_slash || !seg->is_last)
        {
            seg->type = SEG_RECURSIVE;
        }
        else
        {
            // ** at end without trailing slash = * (WILDCARD)
            seg->type = SEG_WILDCARD;
        }
    }
    else if (seg->len == 3 && seg_start[0] == '.' &&
             seg_start[1] == '*' && seg_start[2] == '*')
    {
        // .** always treated as .* (WILDCARD, not recursive)
        seg->type = SEG_WILDCARD;
    }
    else if (rbc_has_wildcard(seg_start, seg->len, flags))
    {
        seg->type = SEG_WILDCARD;
    }
    else
    {
        seg->type = SEG_LITERAL;
    }

    return true;
}

// ============================================================================
// Pattern Matching (fnmatch wrapper)
// ============================================================================

// External fnmatch implementation
bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags);

/**
 * @brief Match name against segment pattern
 */
static bool rbc_match_segment(const rbc_segment_t *seg, const char *name,
                              unsigned flags)
{
    // Prepare null-terminated pattern
    char pattern_buf[RBC_GLOB_MAX_PATH];
    if (seg->len >= sizeof(pattern_buf))
        return false;
    memcpy(pattern_buf, seg->start, seg->len);
    pattern_buf[seg->len] = '\0';

    switch (seg->type)
    {
    case SEG_LITERAL:
        return strcmp(pattern_buf, name) == 0;
    case SEG_DOT:
        return strcmp(name, ".") == 0;
    case SEG_DOTDOT:
        return strcmp(name, "..") == 0;
    case SEG_WILDCARD:
        return rbc_fnmatch(pattern_buf, name, flags);
    case SEG_RECURSIVE:
        // RECURSIVE segments don't directly match names
        return false;
    }
    return false;
}

// ============================================================================
// Glob Walker (Stack-based, unified)
// ============================================================================

// Walker state flags
#define WALK_HAS_WILDCARD_ANCESTOR 0x01

typedef struct
{
    char path[RBC_GLOB_MAX_PATH];
    uint16_t path_len;
    const char *pattern; // Current position in pattern
    uint8_t flags;       // WALK_* flags
} rbc_walk_frame_t;

#define WALK_STACK_SIZE 64

typedef struct
{
    rbc_walk_frame_t frames[WALK_STACK_SIZE];
    size_t depth;
} rbc_walk_stack_t;

static bool rbc_walk_push(rbc_walk_stack_t *stack,
                          const char *path, size_t path_len,
                          const char *pattern, uint8_t flags)
{
    if (stack->depth >= WALK_STACK_SIZE)
        return false;
    rbc_walk_frame_t *f = &stack->frames[stack->depth++];

    if (path_len >= RBC_GLOB_MAX_PATH)
        path_len = RBC_GLOB_MAX_PATH - 1;
    memcpy(f->path, path, path_len);
    f->path[path_len] = '\0';
    f->path_len = (uint16_t)path_len;
    f->pattern = pattern;
    f->flags = flags;
    return true;
}

static bool rbc_walk_pop(rbc_walk_stack_t *stack, rbc_walk_frame_t *out)
{
    if (stack->depth == 0)
        return false;
    *out = stack->frames[--stack->depth];
    return true;
}

// ============================================================================
// Glob Core Implementation
// ============================================================================

/**
 * @brief Add result path to results, stripping base prefix
 *
 * For relative paths: strips baselen and leading slashes
 * For absolute paths (baselen == 0 and path starts with /): keeps leading slash
 */
static void rbc_add_result(rbc_results_t *results, const char *path,
                           size_t baselen, bool is_absolute)
{
    const char *result = path + baselen;

    if (is_absolute)
    {
        // Absolute path: keep as-is (path already starts with /)
        // Just skip separator between base "/" and rest
        if (baselen == 1 && path[0] == '/')
        {
            // path is like "/foo/bar", result should be "/foo/bar"
            result = path; // Keep the full absolute path
        }
    }
    else
    {
        // Relative path: skip leading slashes after base
        while (*result == '/')
            result++;
        if (*result == '\0')
            result = ".";
    }

    rbc_results_add(results, result, strlen(result));
}

/**
 * @brief Check if entry should be skipped based on dotfile rules
 *
 * Rules:
 * - "." and ".." are special
 * - Dotfiles (starting with '.') require DOTMATCH or pattern starting with '.'
 */
static bool rbc_should_skip_entry(const char *name, const rbc_segment_t *seg,
                                  unsigned flags, bool has_wildcard_ancestor)
{
    // "." entry rules:
    // - Skip if reached via wildcard ancestor (prevents path duplication)
    // - Otherwise allow for explicit matching
    if (name[0] == '.' && name[1] == '\0')
    {
        if (has_wildcard_ancestor)
            return true;
        if (seg->type != SEG_DOT && seg->type != SEG_WILDCARD)
            return true;
        if (seg->type == SEG_WILDCARD && !seg->starts_with_dot &&
            !(flags & RBC_FNM_DOTMATCH))
            return true;
        return false;
    }

    // ".." is always excluded from wildcard matching
    if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
    {
        if (seg->type != SEG_DOTDOT)
            return true;
        return false;
    }

    // Other dotfiles
    if (name[0] == '.')
    {
        if (!seg->starts_with_dot && !(flags & RBC_FNM_DOTMATCH))
            return true;
    }

    return false;
}

/**
 * @brief Process a single directory with a normal segment (non-recursive)
 */
static void rbc_glob_process_dir(const char *dir_path, size_t dir_len,
                                 size_t baselen, const rbc_segment_t *seg,
                                 const char *remaining_pattern,
                                 unsigned flags, rbc_results_t *results,
                                 rbc_walk_stack_t *stack,
                                 bool has_wildcard_ancestor)
{
    DIR *dirp = opendir(dir_len > 0 ? dir_path : ".");
    if (!dirp)
        return;

    struct dirent *entry;
    char pathbuf[RBC_GLOB_MAX_PATH];

    // Determine if next segment is a wildcard
    bool next_has_wildcard = has_wildcard_ancestor;
    if (seg->type == SEG_WILDCARD || seg->type == SEG_RECURSIVE)
    {
        next_has_wildcard = true;
    }

    while ((entry = readdir(dirp)) != NULL)
    {
        const char *name = entry->d_name;

        // Apply skip rules
        if (rbc_should_skip_entry(name, seg, flags, has_wildcard_ancestor))
        {
            continue;
        }

        // Match against segment
        if (!rbc_match_segment(seg, name, flags))
        {
            continue;
        }

        // Build full path
        size_t name_len = strlen(name);
        size_t new_len = rbc_build_path(pathbuf, sizeof(pathbuf),
                                        dir_path, dir_len, name, name_len);

        if (seg->is_last)
        {
            // Last segment: add to results if it exists
            // If trailing slash, must be directory
            if (seg->has_trailing_slash)
            {
                if (rbc_is_dir_entry(entry, pathbuf))
                {
                    new_len = rbc_append_slash(pathbuf, new_len, sizeof(pathbuf));
                    rbc_add_result(results, pathbuf, baselen, baselen == 0);
                }
            }
            else
            {
                // No trailing slash - any entry type
                if (rbc_path_exists(pathbuf))
                {
                    rbc_add_result(results, pathbuf, baselen, baselen == 0);
                }
            }
        }
        else
        {
            // Intermediate segment: must be directory, push to stack
            if (rbc_is_dir_entry(entry, pathbuf))
            {
                uint8_t next_flags = next_has_wildcard ? WALK_HAS_WILDCARD_ANCESTOR : 0;
                rbc_walk_push(stack, pathbuf, new_len, remaining_pattern, next_flags);
            }
        }
    }

    closedir(dirp);
}

/**
 * @brief Process recursive (**) pattern
 */
static void rbc_glob_process_recursive(const char *dir_path, size_t dir_len,
                                       size_t baselen,
                                       const rbc_segment_t *rec_seg,
                                       const char *after_recursive,
                                       unsigned flags, rbc_results_t *results,
                                       rbc_walk_stack_t *stack,
                                       bool has_wildcard_ancestor)
{
    (void)has_wildcard_ancestor; // Reserved for future use
    // Parse the segment after **
    rbc_segment_t next_seg;
    const char *pattern_after = after_recursive;
    bool has_next = rbc_next_segment(&pattern_after, flags, &next_seg);

    // Skip consecutive ** segments (folding)
    while (has_next && next_seg.type == SEG_RECURSIVE)
    {
        has_next = rbc_next_segment(&pattern_after, flags, &next_seg);
    }

    // Check if ** should match directories only (trailing slash case)
    bool match_dirs_only = rec_seg->has_trailing_slash && !has_next;

    DIR *dirp = opendir(dir_len > 0 ? dir_path : ".");
    if (!dirp)
        return;

    struct dirent *entry;
    char pathbuf[RBC_GLOB_MAX_PATH];

    while ((entry = readdir(dirp)) != NULL)
    {
        const char *name = entry->d_name;

        // Skip "." - never recurse (infinite loop prevention)
        if (name[0] == '.' && name[1] == '\0')
            continue;

        // Skip ".." - always excluded
        if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
            continue;

        // Dotfile visibility for **
        bool is_dotfile = (name[0] == '.');
        if (is_dotfile)
        {
            // ** descends into dotfiles only with DOTMATCH
            // or if next segment explicitly starts with '.'
            if (!(flags & RBC_FNM_DOTMATCH) &&
                (!has_next || !next_seg.starts_with_dot))
            {
                continue;
            }
        }

        // Build path
        size_t name_len = strlen(name);
        size_t new_len = rbc_build_path(pathbuf, sizeof(pathbuf),
                                        dir_path, dir_len, name, name_len);

        bool is_dir = rbc_is_dir_entry(entry, pathbuf);

        if (has_next)
        {
            // Try matching against next segment
            // For "." entry skipping with wildcard ancestor
            if (!rbc_should_skip_entry(name, &next_seg, flags, true))
            {
                if (rbc_match_segment(&next_seg, name, flags))
                {
                    if (next_seg.is_last)
                    {
                        // Match found at this level
                        if (next_seg.has_trailing_slash)
                        {
                            if (is_dir)
                            {
                                rbc_append_slash(pathbuf, new_len, sizeof(pathbuf));
                                rbc_add_result(results, pathbuf, baselen, baselen == 0);
                            }
                        }
                        else
                        {
                            rbc_add_result(results, pathbuf, baselen, baselen == 0);
                        }
                    }
                    else if (is_dir)
                    {
                        // Continue matching rest of pattern
                        rbc_walk_push(stack, pathbuf, new_len, pattern_after,
                                      WALK_HAS_WILDCARD_ANCESTOR);
                    }
                }
            }
        }
        else if (match_dirs_only && is_dir)
        {
            // **/ with nothing after: add all directories
            rbc_append_slash(pathbuf, new_len, sizeof(pathbuf));
            rbc_add_result(results, pathbuf, baselen, baselen == 0);
        }

        // Recurse into subdirectories
        if (is_dir)
        {
            // Push recursive frame to continue ** matching in subdirectory
            rbc_glob_process_recursive(pathbuf, new_len, baselen, rec_seg,
                                       after_recursive, flags, results,
                                       stack, true);
        }
    }

    closedir(dirp);
}

/**
 * @brief Main glob walker
 */
static void rbc_glob_walk(const char *base, size_t baselen,
                          const char *pattern, unsigned flags,
                          rbc_results_t *results)
{
    rbc_walk_stack_t stack = {.depth = 0};

    // Handle absolute path
    if (pattern[0] == '/')
    {
        // Skip leading slashes
        while (*pattern == '/')
            pattern++;
        rbc_walk_push(&stack, "/", 1, pattern, 0);
        baselen = 0; // Absolute path ignores base
    }
    else
    {
        // Relative path
        rbc_walk_push(&stack, base, baselen, pattern, 0);
    }

    rbc_walk_frame_t frame;
    while (rbc_walk_pop(&stack, &frame))
    {
        rbc_segment_t seg;
        const char *pat_ptr = frame.pattern;

        if (!rbc_next_segment(&pat_ptr, flags, &seg))
        {
            continue; // Empty pattern
        }

        bool has_wildcard_ancestor = (frame.flags & WALK_HAS_WILDCARD_ANCESTOR) != 0;

        switch (seg.type)
        {
        case SEG_RECURSIVE:
            // First try zero-depth match (** matches zero directories)
            if (pat_ptr[0] != '\0')
            {
                rbc_walk_push(&stack, frame.path, frame.path_len, pat_ptr,
                              WALK_HAS_WILDCARD_ANCESTOR);
            }
            // Then recursive descent
            rbc_glob_process_recursive(frame.path, frame.path_len, baselen,
                                       &seg, pat_ptr, flags, results,
                                       &stack, has_wildcard_ancestor);
            break;

        case SEG_DOT:
        case SEG_DOTDOT:
        case SEG_LITERAL:
        case SEG_WILDCARD:
            rbc_glob_process_dir(frame.path, frame.path_len, baselen,
                                 &seg, pat_ptr, flags, results, &stack,
                                 has_wildcard_ancestor);
            break;
        }
    }
}

// ============================================================================
// Brace Expansion (Preprocessor)
// ============================================================================

#define BRACE_MAX_EXPANSIONS 256
#define BRACE_MAX_DEPTH 8

typedef struct
{
    char **patterns;
    size_t count;
    size_t capacity;
} rbc_brace_result_t;

static bool rbc_brace_add(rbc_brace_result_t *r, const char *pattern, size_t len)
{
    if (r->count >= r->capacity)
    {
        size_t new_cap = r->capacity * 2;
        if (new_cap > BRACE_MAX_EXPANSIONS)
            new_cap = BRACE_MAX_EXPANSIONS;
        if (r->count >= new_cap)
            return false; // Limit reached
        char **new_patterns = realloc(r->patterns, new_cap * sizeof(char *));
        if (!new_patterns)
            return false;
        r->patterns = new_patterns;
        r->capacity = new_cap;
    }

    char *copy = malloc(len + 1);
    if (!copy)
        return false;
    memcpy(copy, pattern, len);
    copy[len] = '\0';
    r->patterns[r->count++] = copy;
    return true;
}

static void rbc_brace_expand_impl(const char *pattern, size_t len,
                                  rbc_brace_result_t *result, int depth);

static void rbc_brace_expand_option(const char *prefix, size_t prefix_len,
                                    const char *option, size_t option_len,
                                    const char *suffix, size_t suffix_len,
                                    rbc_brace_result_t *result, int depth)
{
    if (depth > BRACE_MAX_DEPTH)
        return;

    char buf[RBC_GLOB_MAX_PATH];
    size_t total = prefix_len + option_len + suffix_len;
    if (total >= sizeof(buf))
        return;

    memcpy(buf, prefix, prefix_len);
    memcpy(buf + prefix_len, option, option_len);
    memcpy(buf + prefix_len + option_len, suffix, suffix_len);

    rbc_brace_expand_impl(buf, total, result, depth + 1);
}

static void rbc_brace_expand_impl(const char *pattern, size_t len,
                                  rbc_brace_result_t *result, int depth)
{
    if (result->count >= BRACE_MAX_EXPANSIONS)
        return;

    char buf[RBC_GLOB_MAX_PATH];
    size_t buf_pos = 0;
    const char *p = pattern;
    const char *end = pattern + len;

    while (p < end)
    {
        if (*p == '\\' && p + 1 < end)
        {
            buf[buf_pos++] = *p++;
            buf[buf_pos++] = *p++;
            continue;
        }

        if (*p == '{')
        {
            // Find matching close brace
            const char *opt_starts[256];
            size_t opt_count = 0;
            const char *scan = p + 1;
            const char *close = NULL;
            int brace_depth = 0;

            opt_starts[opt_count++] = scan;

            while (scan < end)
            {
                if (*scan == '\\' && scan + 1 < end)
                {
                    scan += 2;
                    continue;
                }
                if (*scan == '{')
                {
                    brace_depth++;
                }
                else if (*scan == '}')
                {
                    if (brace_depth == 0)
                    {
                        close = scan;
                        break;
                    }
                    brace_depth--;
                }
                else if (brace_depth == 0 && *scan == ',' && opt_count < 256)
                {
                    opt_starts[opt_count++] = scan + 1;
                }
                scan++;
            }

            if (!close)
            {
                buf[buf_pos++] = *p++;
                continue;
            }

            // Expand all options
            size_t suffix_len = end - (close + 1);
            for (size_t i = 0; i < opt_count; i++)
            {
                const char *opt_start = opt_starts[i];
                const char *opt_end = (i + 1 < opt_count) ? opt_starts[i + 1] - 1 : close;
                size_t opt_len = opt_end - opt_start;

                rbc_brace_expand_option(buf, buf_pos, opt_start, opt_len,
                                        close + 1, suffix_len, result, depth);
            }
            return;
        }

        buf[buf_pos++] = *p++;
    }

    // No brace found - add as final pattern
    buf[buf_pos] = '\0';
    rbc_brace_add(result, buf, buf_pos);
}

static rbc_brace_result_t *rbc_brace_expand(const char *pattern)
{
    rbc_brace_result_t *result = malloc(sizeof(rbc_brace_result_t));
    if (!result)
        return NULL;

    result->patterns = malloc(8 * sizeof(char *));
    result->count = 0;
    result->capacity = 8;

    if (!result->patterns)
    {
        free(result);
        return NULL;
    }

    rbc_brace_expand_impl(pattern, strlen(pattern), result, 0);
    return result;
}

static void rbc_brace_free(rbc_brace_result_t *result)
{
    if (!result)
        return;
    for (size_t i = 0; i < result->count; i++)
    {
        free(result->patterns[i]);
    }
    free(result->patterns);
    free(result);
}

// ============================================================================
// Result Conversion and Sorting
// ============================================================================

static int rbc_strcmp_wrapper(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/**
 * @brief Convert arena results to output format
 */
static bool rbc_results_to_output(rbc_results_t *r, bool sort,
                                  char ***out, size_t *count, size_t **lengths)
{
    *count = r->count;

    if (r->count == 0)
    {
        *out = NULL;
        if (lengths)
            *lengths = NULL;
        return true;
    }

    // Allocate output arrays
    char **items = malloc(r->count * sizeof(char *));
    size_t *lens = lengths ? malloc(r->count * sizeof(size_t)) : NULL;

    if (!items || (lengths && !lens))
    {
        free(items);
        free(lens);
        return false;
    }

    // Copy paths from arena to individual allocations
    for (size_t i = 0; i < r->count; i++)
    {
        const char *src = r->data + r->offsets[i];
        size_t len = strlen(src);
        items[i] = malloc(len + 1);
        if (!items[i])
        {
            for (size_t j = 0; j < i; j++)
                free(items[j]);
            free(items);
            free(lens);
            return false;
        }
        memcpy(items[i], src, len + 1);
        if (lens)
            lens[i] = len;
    }

    // Sort if requested
    if (sort && r->count > 1)
    {
        qsort(items, r->count, sizeof(char *), rbc_strcmp_wrapper);
    }

    *out = items;
    if (lengths)
        *lengths = lens;
    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool rbc_glob(const char **patterns, size_t npatterns, unsigned flags,
              const char *base, bool sort,
              char ***out, size_t *count, size_t **lengths)
{
    if (!patterns || npatterns == 0 || !out || !count)
        return false;

    rbc_results_t results;
    if (!rbc_results_init(&results))
        return false;

    // Calculate base length
    size_t baselen = 0;
    const char *actual_base = "";
    if (base && base[0] != '\0')
    {
        baselen = strlen(base);
        // Strip trailing slashes
        while (baselen > 0 && base[baselen - 1] == '/')
            baselen--;
        actual_base = base;
    }

    // Process each pattern
    for (size_t i = 0; i < npatterns; i++)
    {
        // Expand braces
        rbc_brace_result_t *expanded = rbc_brace_expand(patterns[i]);
        if (!expanded)
            continue;

        // Process each expanded pattern
        for (size_t j = 0; j < expanded->count; j++)
        {
            rbc_glob_walk(actual_base, baselen, expanded->patterns[j],
                          flags, &results);
        }

        rbc_brace_free(expanded);
    }

    // Convert to output format
    bool ok = rbc_results_to_output(&results, sort, out, count, lengths);
    rbc_results_free(&results);

    return ok;
}

void rbc_glob_free(char **list, size_t count, size_t *lengths)
{
    if (!list)
        return;
    for (size_t i = 0; i < count; i++)
    {
        free(list[i]);
    }
    free(list);
    free(lengths);
}
