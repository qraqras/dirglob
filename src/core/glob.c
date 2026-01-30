#include "platform.h"
#include "rbc/rbc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define RBC_GLOB_MAX_PATH RBC_MAX_PATH
#define RBC_RESULTS_INIT_DATA (64 * 1024) // 64KB initial data buffer
#define RBC_RESULTS_INIT_COUNT 256        // Initial path count

/// @defgroup Segment Character Flags
/// @{
#define RBC_SEG_CONTAINS_STAR 0x01     // Contains '*'
#define RBC_SEG_CONTAINS_QUESTION 0x02 // Contains '?'
#define RBC_SEG_CONTAINS_BRACKET 0x04  // Contains '[...]'
#define RBC_SEG_CONTAINS_ESCAPE 0x08   // Contains escape sequences
#define RBC_SEG_CONTAINS_REGULAR 0x10  // Contains regular characters
/// @}

/// @defgroup Walker State Flags
/// @{
#define RBC_WALK_HAS_WILDCARD_ANCESTOR 0x01
#define RBC_WALK_QUEUE_SIZE 1024
/// @}

/// @brief Segment Types
typedef enum rbc_segment_type_e
{
    SEG_LITERAL,   // `abc`
    SEG_DOT,       // `.`
    SEG_DOTDOT,    // `..`
    SEG_WILDCARD,  // `*`, `.*`, `?`, `.?`, `[abc]` etc.
    SEG_RECURSIVE, // `**/`
} rbc_segment_type_t;

/// @brief Segment Structure
typedef struct rbc_segment_s
{
    const char *start;       // Segment start in pattern
    size_t len;              // Segment length
    rbc_segment_type_t type; // Segment classification
    bool starts_with_dot;    // Segment starts with '.'
    bool has_trailing_slash; // Pattern has '/' after this segment
    bool is_last;            // This is the last segment
} rbc_segment_t;

/// @brief Walker Frame Structure
typedef struct rbc_walk_frame_s
{
    char path[RBC_GLOB_MAX_PATH]; // Current path
    uint16_t path_len;            // Length of current path
    const char *pattern;          // Current position in pattern
    uint8_t flags;                // WALK_* flags
} rbc_walk_frame_t;

/// @brief Walker Queue Structure
typedef struct rbc_walk_queue_s
{
    rbc_walk_frame_t frames[RBC_WALK_QUEUE_SIZE]; // Frame storage
    size_t head;                                  // Index to dequeue from
    size_t tail;                                  // Index to enqueue to
    size_t count;                                 // Number of frames in queue
} rbc_walk_queue_t;

/// @defgroup Results
/// @{

/// @brief Results Buffer Structure
typedef struct rbc_results_s
{
    char *arena;             // Arena buffer storing all paths contiguously
    size_t *offsets;         // Offset of each path in arena (in bytes)
    size_t count;            // Number of stored paths
    size_t arena_used;       // Bytes used in arena
    size_t arena_capacity;   // Total capacity of arena (in bytes)
    size_t offsets_capacity; // Capacity of offsets array (in elements)
} rbc_results_t;

/// @brief Initialize results buffer
/// @param[out] r Results buffer
/// @return true on success, false on failure
static bool rbc_results_init(rbc_results_t *r)
{
    r->arena = malloc(RBC_RESULTS_INIT_DATA);
    r->offsets = malloc(RBC_RESULTS_INIT_COUNT * sizeof(size_t));
    if (!r->arena || !r->offsets)
    {
        free(r->arena);
        free(r->offsets);
        return false;
    }
    r->count = 0;
    r->arena_used = 0;
    r->arena_capacity = RBC_RESULTS_INIT_DATA;
    r->offsets_capacity = RBC_RESULTS_INIT_COUNT;
    return true;
}

/// @brief Add a path to results buffer
/// @param[in, out] r Results buffer
/// @param[in] path Path string
/// @param[in] len Length of path
/// @return true on success, false on failure
static bool rbc_results_add(rbc_results_t *r, const char *path, size_t len)
{
    // Grow offsets array if needed
    if (r->count >= r->offsets_capacity)
    {
        size_t new_cap = r->offsets_capacity * 2;
        size_t *new_off = realloc(r->offsets, new_cap * sizeof(size_t));
        if (!new_off)
            return false;
        r->offsets = new_off;
        r->offsets_capacity = new_cap;
    }

    // Grow arena buffer if needed (include null terminator)
    size_t needed = r->arena_used + len + 1;
    if (needed > r->arena_capacity)
    {
        size_t new_cap = r->arena_capacity;
        while (new_cap < needed)
            new_cap *= 2;
        char *new_arena = realloc(r->arena, new_cap);
        if (!new_arena)
            return false;
        r->arena = new_arena;
        r->arena_capacity = new_cap;
    }

    // Store offset and copy path
    r->offsets[r->count++] = r->arena_used;
    memcpy(r->arena + r->arena_used, path, len);
    r->arena[r->arena_used + len] = '\0';
    r->arena_used += len + 1;

    return true;
}

/// @brief Free results buffer
/// @param[in] r Results buffer
static void rbc_results_free(rbc_results_t *r)
{
    free(r->arena);
    free(r->offsets);
}

/// @}

// ============================================================================
// Path Utilities (snprintf-free)
// ============================================================================

/// @brief Build path: base + '/' + component
/// @pre base must be normalized (no backslashes, no consecutive or trailing slashes)
/// @pre component should be a single path component (filename/dirname)
/// @param[out] buf Output buffer
/// @param[in] buf_size Size of output buffer
/// @param[in] base Base path (must be normalized: forward slashes only, no trailing slash)
/// @param[in] base_len Length of base
/// @param[in] component Path component to append (single component, not a path)
/// @param[in] component_len Length of component
/// @return Length of resulting path on success, 0 on error (buffer overflow)
static size_t rbc_path_join(char *buf, size_t buf_size, const char *base, size_t base_len, const char *component, size_t component_len)
{
    if (base_len == 0)
    {
        if (component_len >= buf_size)
            return 0;
        memcpy(buf, component, component_len);
        buf[component_len] = '\0';
        return component_len;
    }

    bool should_append_slash = base[base_len - 1] != '/';
    size_t pos = base_len;
    if (should_append_slash)
        pos++;

    size_t result_len = pos + component_len;
    if (result_len >= buf_size)
        return 0;

    memcpy(buf, base, base_len);
    if (should_append_slash)
        buf[base_len] = '/';
    memcpy(buf + pos, component, component_len);
    buf[result_len] = '\0';

    return result_len;
}

/// @brief Append trailing slash to path buffer if not present
/// @param[in, out] buf Buffer containing the path
/// @param[in] buf_size Size of the buffer
/// @param[in] path_len Current length of the path in the buffer
/// @return New length of the path after appending slash, or 0 on error
/// @note Empty path is treated as error because appending "/" would change semantics ("" → "/" converts relative to absolute root)
/// @note Returns path_len unchanged if path already has trailing slash
static size_t rbc_path_append_slash(char *buf, size_t buf_size, size_t path_len)
{
    if (path_len == 0)
        return 0;
    if (buf_size <= path_len + 1)
        return 0;
    if (buf[path_len - 1] == '/')
        return path_len;

    buf[path_len++] = '/';
    buf[path_len] = '\0';
    return path_len;
}

// ============================================================================
// Pattern Parsing
// ============================================================================

/**
 * @brief Parse next segment from pattern (single-pass, glob.c style)
 * @return true if segment found, false if end of pattern
 *
 * Single pass through segment to:
 * 1. Find segment boundaries
 * 2. Collect character type flags for classification
 */
static bool rbc_next_segment(const char **pattern_ptr, unsigned flags, rbc_segment_t *seg)
{
    const char *p = *pattern_ptr;

    // Skip leading slashes (normalize)
    while (*p == '/')
        p++;

    if (*p == '\0')
        return false;

    seg->start = p;
    seg->starts_with_dot = (*p == '.');

    // Single-pass: find segment end and collect character flags
    const char *seg_start = p;
    unsigned char char_flags = 0;
    int in_bracket = 0;

    while (*p != '\0' && (*p != '/' || in_bracket > 0))
    {
        if (!(flags & RBC_FNM_NOESCAPE) && *p == '\\' && *(p + 1))
        {
            char_flags |= RBC_SEG_CONTAINS_ESCAPE;
            p += 2; // Skip escaped char
            continue;
        }

        switch (*p)
        {
        case '*':
            char_flags |= RBC_SEG_CONTAINS_STAR;
            break;
        case '?':
            char_flags |= RBC_SEG_CONTAINS_QUESTION;
            break;
        case '[':
            in_bracket++;
            break;
        case ']':
            if (in_bracket > 0)
            {
                in_bracket--;
                char_flags |= RBC_SEG_CONTAINS_BRACKET; // Complete bracket
            }
            else
            {
                char_flags |= RBC_SEG_CONTAINS_REGULAR;
            }
            break;
        default:
            char_flags |= RBC_SEG_CONTAINS_REGULAR;
            break;
        }
        p++;
    }

    seg->len = p - seg_start;
    seg->has_trailing_slash = (*p == '/');

    // Classify segment based on collected flags
    // Do this BEFORE skipping trailing slashes to correctly identify ** vs **/

    // Check for ** (pure double-star, no other chars)
    if (seg->len == 2 && char_flags == RBC_SEG_CONTAINS_STAR &&
        seg_start[0] == '*' && seg_start[1] == '*')
    {
        // ** with trailing slash = RECURSIVE (directory descent)
        // ** without trailing slash = WILDCARD (match like *)
        if (seg->has_trailing_slash)
        {
            seg->type = SEG_RECURSIVE;
            // Skip trailing slashes
            while (*p == '/')
                p++;
        }
        else
        {
            seg->type = SEG_WILDCARD;
        }
        seg->is_last = (*p == '\0');
        *pattern_ptr = p;
        return true;
    }

    // For non-** segments, skip trailing slashes normally
    while (*p == '/')
        p++;
    seg->is_last = (*p == '\0');
    *pattern_ptr = p;

    // Check for special single-char segments first
    if (seg->len == 1 && seg_start[0] == '.')
    {
        seg->type = SEG_DOT;
    }
    else if (seg->len == 2 && seg_start[0] == '.' && seg_start[1] == '.')
    {
        seg->type = SEG_DOTDOT;
    }
    // .** pattern (dot followed by **)
    else if (seg->len == 3 && seg_start[0] == '.' &&
             seg_start[1] == '*' && seg_start[2] == '*')
    {
        // .** always treated as wildcard (not recursive)
        seg->type = SEG_WILDCARD;
    }
    // Any wildcard or escape → delegate to fnmatch
    else if (char_flags & (RBC_SEG_CONTAINS_STAR | RBC_SEG_CONTAINS_QUESTION |
                           RBC_SEG_CONTAINS_BRACKET | RBC_SEG_CONTAINS_ESCAPE))
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
static bool rbc_match_segment(const rbc_segment_t *seg, const char *name, unsigned flags)
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
        if (flags & RBC_FNM_CASEFOLD)
        {
            // Case-insensitive comparison
            const char *p = pattern_buf;
            const char *n = name;
            while (*p && *n)
            {
                unsigned char pc = (unsigned char)*p++;
                unsigned char nc = (unsigned char)*n++;
                if (pc >= 'A' && pc <= 'Z')
                    pc += 'a' - 'A';
                if (nc >= 'A' && nc <= 'Z')
                    nc += 'a' - 'A';
                if (pc != nc)
                    return false;
            }
            return *p == '\0' && *n == '\0';
        }
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
// Glob Walker (Queue-based for correct traversal order)
// ============================================================================

// Walker state flags

static bool rbc_walk_enqueue(rbc_walk_queue_t *q, const char *path, size_t path_len, const char *pattern, uint8_t flags)
{
    if (q->count >= RBC_WALK_QUEUE_SIZE)
        return false;

    rbc_walk_frame_t *f = &q->frames[q->tail];
    q->tail = (q->tail + 1) % RBC_WALK_QUEUE_SIZE;
    q->count++;

    if (path_len >= RBC_GLOB_MAX_PATH)
        path_len = RBC_GLOB_MAX_PATH - 1;
    memcpy(f->path, path, path_len);
    f->path[path_len] = '\0';
    f->path_len = (uint16_t)path_len;
    f->pattern = pattern;
    f->flags = flags;
    return true;
}

static bool rbc_walk_dequeue(rbc_walk_queue_t *q, rbc_walk_frame_t *out)
{
    if (q->count == 0)
        return false;
    *out = q->frames[q->head];
    q->head = (q->head + 1) % RBC_WALK_QUEUE_SIZE;
    q->count--;
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
static void rbc_add_result(rbc_results_t *results, const char *path, size_t baselen, bool is_absolute)
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
static bool rbc_should_skip_entry(const char *name, const rbc_segment_t *seg, unsigned flags, bool has_wildcard_ancestor)
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
static void rbc_glob_process_dir(
    const char *dir_path,
    size_t dir_len,
    size_t baselen,
    const rbc_segment_t *seg,
    const char *remaining_pattern,
    unsigned flags,
    rbc_results_t *results,
    rbc_walk_queue_t *queue,
    bool has_wildcard_ancestor)
{
    rbc_dir_t *dirp = rbc_opendir(dir_len > 0 ? dir_path : ".");
    if (!dirp)
        return;

    rbc_dirent_t entry;
    char pathbuf[RBC_GLOB_MAX_PATH];

    // Determine if next segment is a wildcard
    bool next_has_wildcard = has_wildcard_ancestor;
    if (seg->type == SEG_WILDCARD || seg->type == SEG_RECURSIVE)
    {
        next_has_wildcard = true;
    }

    while (rbc_readdir(dirp, &entry))
    {
        const char *name = entry.name;

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
        size_t new_len = rbc_path_join(pathbuf, sizeof(pathbuf),
                                       dir_path, dir_len, name, name_len);
        if (new_len == 0)
            continue; // Path too long, skip this entry

        if (seg->is_last)
        {
            // Last segment: add to results if it exists
            // If trailing slash, must be directory
            if (seg->has_trailing_slash)
            {
                if (entry.is_dir)
                {
                    new_len = rbc_path_append_slash(pathbuf, sizeof(pathbuf), new_len);
                    if (new_len == 0)
                        continue; // Error: buffer too small
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
            // Intermediate segment: must be directory, enqueue
            if (entry.is_dir)
            {
                uint8_t next_flags = next_has_wildcard ? RBC_WALK_HAS_WILDCARD_ANCESTOR : 0;
                rbc_walk_enqueue(queue, pathbuf, new_len, remaining_pattern, next_flags);
            }
        }
    }

    rbc_closedir(dirp);
}

/**
 * @brief Process recursive (**) pattern
 *
 * RECURSIVE only handles directory traversal, NOT matching.
 * Matching is delegated to the walker by pushing remaining pattern to queue.
 *
 * Design:
 * - Double-star matches "zero or more directories"
 * - At each directory level, enqueue remaining pattern for matching
 * - Recurse into subdirectories (except dotdirs without DOTMATCH)
 * - For trailing slash case, add directories directly
 */
static void rbc_glob_process_recursive(
    const char *dir_path, size_t dir_len,
    size_t baselen,
    const rbc_segment_t *rec_seg,
    const char *after_recursive,
    unsigned flags, rbc_results_t *results,
    rbc_walk_queue_t *queue,
    bool has_wildcard_ancestor)
{
    // Check if **/ at end (match directories only)
    bool match_dirs_only = (rec_seg->has_trailing_slash && after_recursive[0] == '\0');

    // Parse the next segment from after_recursive for direct matching
    rbc_segment_t next_seg;
    const char *next_remaining = after_recursive;
    bool has_next_seg = (after_recursive[0] != '\0') &&
                        rbc_next_segment(&next_remaining, flags, &next_seg);

    rbc_dir_t *dirp = rbc_opendir(dir_len > 0 ? dir_path : ".");
    if (!dirp)
        return;

    rbc_dirent_t entry;
    char pathbuf[RBC_GLOB_MAX_PATH];

    while (rbc_readdir(dirp, &entry))
    {
        const char *name = entry.name;

        // ".." is always excluded from glob results
        if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
            continue;

        // "." handling:
        // - Without DOTMATCH: always skip
        // - With DOTMATCH at depth 0 (no wildcard ancestor): allow matching
        // - With DOTMATCH at depth > 0 (has wildcard ancestor): skip
        // This matches Ruby's behavior where **/'s zero-depth match is not
        // considered "through a wildcard"
        bool is_dot_entry = (name[0] == '.' && name[1] == '\0');
        if (is_dot_entry)
        {
            if (!(flags & RBC_FNM_DOTMATCH) || has_wildcard_ancestor)
                continue;
        }

        // Check if this is a dotfile/dotdir
        bool is_dotfile = (name[0] == '.');

        // For directory descent: ** does NOT descend into dotfiles/dotdirs without DOTMATCH
        // But we still need to try matching after_recursive pattern on dotfiles
        bool skip_descent = is_dotfile && !(flags & RBC_FNM_DOTMATCH);

        // Build path
        size_t name_len = strlen(name);
        size_t new_len = rbc_path_join(pathbuf, sizeof(pathbuf),
                                       dir_path, dir_len, name, name_len);
        if (new_len == 0)
            continue; // Path too long, skip this entry

        bool is_dir = entry.is_dir;

        if (match_dirs_only && !skip_descent)
        {
            // **/ at end: add all directories (except dotdirs without DOTMATCH)
            // Never add "." as it's redundant (same as parent directory)
            if (is_dir && !is_dot_entry)
            {
                size_t slash_len = rbc_path_append_slash(pathbuf, sizeof(pathbuf), new_len);
                if (slash_len > 0)
                    rbc_add_result(results, pathbuf, baselen, baselen == 0);
            }
        }

        // Direct matching of after_recursive pattern (** matches zero directories)
        if (has_next_seg)
        {
            // At depth 0 (no wildcard ancestor), "." can match with DOTMATCH
            // At depth > 0, "." is always skipped (passed through wildcard)
            if (!rbc_should_skip_entry(name, &next_seg, flags, has_wildcard_ancestor) &&
                rbc_match_segment(&next_seg, name, flags))
            {
                if (next_seg.is_last)
                {
                    // Final segment matched - add to results
                    if (next_seg.has_trailing_slash)
                    {
                        if (is_dir)
                        {
                            new_len = rbc_path_append_slash(pathbuf, sizeof(pathbuf), new_len);
                            if (new_len == 0)
                                continue; // Error: buffer too small
                            rbc_add_result(results, pathbuf, baselen, baselen == 0);
                        }
                    }
                    else
                    {
                        if (rbc_path_exists(pathbuf))
                        {
                            rbc_add_result(results, pathbuf, baselen, baselen == 0);
                        }
                    }
                }
                else if (is_dir)
                {
                    // More segments to match - enqueue
                    rbc_walk_enqueue(queue, pathbuf, new_len, next_remaining,
                                     RBC_WALK_HAS_WILDCARD_ANCESTOR);
                }
            }
        }

        // Recurse into subdirectories (only if allowed)
        // Never recurse into "." (would cause infinite loop)
        if (is_dir && !skip_descent && !is_dot_entry)
        {
            // Continue ** matching in subdirectory
            rbc_glob_process_recursive(pathbuf, new_len, baselen, rec_seg,
                                       after_recursive, flags, results,
                                       queue, true);
        }
    }

    rbc_closedir(dirp);
}

/**
 * @brief Main glob walker
 */
static void rbc_glob_walk(const char *base, size_t baselen, const char *pattern, unsigned flags, rbc_results_t *results)
{
    rbc_walk_queue_t queue = {.head = 0, .tail = 0, .count = 0};

    // Handle absolute path
    if (rbc_is_absolute_path(pattern))
    {
        // Skip leading slashes (or drive letter + slash on Windows)
        const char *after_slash = pattern;
#ifdef _WIN32
        // Skip drive letter (C:) if present
        if (((after_slash[0] >= 'A' && after_slash[0] <= 'Z') ||
             (after_slash[0] >= 'a' && after_slash[0] <= 'z')) &&
            after_slash[1] == ':')
        {
            after_slash += 2;
        }
#endif
        while (*after_slash == '/' || *after_slash == '\\')
            after_slash++;

        // Pattern is just "/" or "C:\" - return root itself
        if (*after_slash == '\0')
        {
#ifdef _WIN32
            // Return the drive root if present
            if (pattern[1] == ':')
            {
                char root[4] = {pattern[0], ':', '/', '\0'};
                rbc_results_add(results, root, 3);
            }
            else
            {
                rbc_results_add(results, "/", 1);
            }
#else
            rbc_results_add(results, "/", 1);
#endif
            return;
        }

#ifdef _WIN32
        // Enqueue with drive root if present
        if (pattern[1] == ':')
        {
            char root[4] = {pattern[0], ':', '/', '\0'};
            rbc_walk_enqueue(&queue, root, 3, after_slash, 0);
        }
        else
        {
            rbc_walk_enqueue(&queue, "/", 1, after_slash, 0);
        }
#else
        rbc_walk_enqueue(&queue, "/", 1, after_slash, 0);
#endif
        baselen = 0; // Absolute path ignores base
    }
    else
    {
        // Relative path
        rbc_walk_enqueue(&queue, base, baselen, pattern, 0);
    }

    rbc_walk_frame_t frame;
    while (rbc_walk_dequeue(&queue, &frame))
    {
        rbc_segment_t seg;
        const char *pat_ptr = frame.pattern;

        if (!rbc_next_segment(&pat_ptr, flags, &seg))
        {
            continue; // Empty pattern
        }

        bool has_wildcard_ancestor = (frame.flags & RBC_WALK_HAS_WILDCARD_ANCESTOR) != 0;

        switch (seg.type)
        {
        case SEG_RECURSIVE:
        {
            // Collapse consecutive **/ segments (Ruby behavior)
            // e.g., **/**/ -> **/
            // But keep trailing ** without slash as after_recursive pattern
            const char *after_recursive = pat_ptr;
            while (*after_recursive != '\0')
            {
                // Check for **
                if (after_recursive[0] == '*' && after_recursive[1] == '*')
                {
                    if (after_recursive[2] == '/')
                    {
                        // **/ - collapse it
                        after_recursive += 2;
                        while (*after_recursive == '/')
                            after_recursive++;
                        continue;
                    }
                    // ** at end - keep as after_recursive pattern
                    break;
                }
                break;
            }

            // Update seg.is_last based on collapsed pattern
            seg.is_last = (*after_recursive == '\0');

            // Zero-depth match (** matches zero directories) is handled
            // inside rbc_glob_process_recursive, not here
            if (seg.has_trailing_slash && *after_recursive == '\0')
            {
                // **/ at end: add current directory itself
                char pathbuf[RBC_GLOB_MAX_PATH];
                size_t len = frame.path_len;
                if (len > 0)
                {
                    memcpy(pathbuf, frame.path, len);
                    len = rbc_path_append_slash(pathbuf, sizeof(pathbuf), len);
                    if (len > 0)
                        rbc_add_result(results, pathbuf, baselen, baselen == 0);
                }
            }
            // Recursive descent
            rbc_glob_process_recursive(frame.path, frame.path_len, baselen,
                                       &seg, after_recursive, flags, results,
                                       &queue, has_wildcard_ancestor);
            break;
        }

        case SEG_DOT:
        case SEG_DOTDOT:
        case SEG_LITERAL:
        case SEG_WILDCARD:
            rbc_glob_process_dir(frame.path, frame.path_len, baselen,
                                 &seg, pat_ptr, flags, results, &queue,
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
 * @brief Sort a range of results within the arena
 *
 * Ruby's Dir.glob with sort:true sorts each brace-expanded pattern's
 * results individually, then concatenates them in brace expansion order.
 * This function sorts results from index `start` to `end` (exclusive).
 *
 * @param r Results structure
 * @param start Start index (inclusive)
 * @param end End index (exclusive)
 */
static void rbc_results_sort_range(rbc_results_t *r, size_t start, size_t end)
{
    if (end <= start + 1)
        return; // 0 or 1 element, nothing to sort

    size_t n = end - start;

    // Create temporary array of string pointers for sorting
    const char **ptrs = malloc(n * sizeof(const char *));
    if (!ptrs)
        return;

    // Build pointer array from offsets
    for (size_t i = 0; i < n; i++)
    {
        ptrs[i] = r->arena + r->offsets[start + i];
    }

    // Sort the pointer array
    qsort(ptrs, n, sizeof(const char *), rbc_strcmp_wrapper);

    // Rebuild offsets array from sorted pointers
    for (size_t i = 0; i < n; i++)
    {
        r->offsets[start + i] = (size_t)(ptrs[i] - r->arena);
    }

    free(ptrs);
}

/**
 * @brief Convert arena results to output format
 */
static bool rbc_results_to_output(rbc_results_t *r,
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
        const char *src = r->arena + r->offsets[i];
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
    char base_norm_buf[RBC_GLOB_MAX_PATH];

    if (base && base[0] != '\0')
    {
        // Normalize path separators (Windows: \ -> /)
        actual_base = rbc_normalize_path(base, base_norm_buf, sizeof(base_norm_buf));
        baselen = strlen(actual_base);
        // Strip trailing slashes
        while (baselen > 0 && actual_base[baselen - 1] == '/')
            baselen--;
    }

    // Process each pattern
    for (size_t i = 0; i < npatterns; i++)
    {
        // Normalize path separators (Windows: \ -> /)
        char norm_buf[RBC_GLOB_MAX_PATH];
        const char *pattern = rbc_normalize_path(patterns[i], norm_buf, sizeof(norm_buf));

        // Expand braces
        rbc_brace_result_t *expanded = rbc_brace_expand(pattern);
        if (!expanded)
            continue;

        // Process each expanded pattern
        for (size_t j = 0; j < expanded->count; j++)
        {
            size_t count_before = results.count;

            rbc_glob_walk(actual_base, baselen, expanded->patterns[j],
                          flags, &results);

            // Sort results for this brace-expanded pattern
            // Ruby sorts each pattern's results individually, then concatenates
            if (sort && results.count > count_before)
            {
                rbc_results_sort_range(&results, count_before, results.count);
            }
        }

        rbc_brace_free(expanded);
    }

    // Convert to output format (already sorted per-pattern)
    bool ok = rbc_results_to_output(&results, out, count, lengths);
    rbc_results_free(&results);

    return ok;
}

void rbc_glob_free(char **list, size_t count, size_t *lengths)
{
    if (!list)
        return;
    for (size_t i = 0; i < count; i++)
        free(list[i]);
    free(list);
    free(lengths);
}
