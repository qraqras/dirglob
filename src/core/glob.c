#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define RBC_GLOB_MAX_PATH 4096
#define RBC_RESULTS_CAPACITY 64

// Flags (should match rbc.h)
#define RBC_FNM_NOESCAPE 0x01
#define RBC_FNM_PATHNAME 0x02
#define RBC_FNM_DOTMATCH 0x04
#define RBC_FNM_CASEFOLD 0x08
#define RBC_FNM_EXTGLOB 0x10

/// @brief Glob results storage
typedef struct rbc_results_s
{
    char **items;
    size_t *lengths;
    size_t count;
    size_t capacity;
} rbc_results_t;

/// @brief Segment type classification (Ruby Dir.glob compatible)
typedef enum rbc_segment_type_e
{
    RBC_SEG_LITERAL,   // `literal`
    RBC_SEG_DOT,       // `.`
    RBC_SEG_DOTDOT,    // `..`
    RBC_SEG_ANY,       // `*`
    RBC_SEG_RECURSIVE, // `**`
    RBC_SEG_MAGICAL,   // `*`, `?`, `[...]`, and combinations
    RBC_SEG_ROOT,      // `/` (absolute path root)
} rbc_segment_type_t;

/// @brief Pattern segment information (for streaming)
typedef struct rbc_segment_s
{
    const char *start;       // Segment start pointer
    size_t len;              // Segment length
    rbc_segment_type_t type; // Segment type classification
    bool starts_with_dot;    // Starts with '.'
    size_t trailing_slashes; // Number of trailing slashes after segment
} rbc_segment_t;

/// @brief Brace expansion result (array-based)
typedef struct rbc_brace_result_s
{
    char **patterns; // Array of expanded patterns
    size_t count;    // Number of patterns
    size_t capacity; // Allocated capacity
} rbc_brace_result_t;

// ============================================================================
// Directory Entry Management (sorted readdir for consistent results)
// ============================================================================

/// @brief Sorted directory entry list
typedef struct
{
    char **names;   // Array of entry names (sorted)
    size_t count;   // Number of entries
    size_t current; // Current read position
} rbc_dir_entries_t;

/// @brief String comparison for sorting (unified)
static int rbc_strcmp_wrapper(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/// @brief Open directory and read all entries (optionally sorted)
static rbc_dir_entries_t *rbc_opendir_sorted(const char *path, bool sort)
{
    DIR *dirp = opendir(path[0] ? path : ".");
    if (!dirp)
        return NULL;

    rbc_dir_entries_t *entries = malloc(sizeof(rbc_dir_entries_t));
    if (!entries)
    {
        closedir(dirp);
        return NULL;
    }

    size_t capacity = 64;
    entries->names = malloc(sizeof(char *) * capacity);
    entries->count = 0;
    entries->current = 0;

    if (!entries->names)
    {
        free(entries);
        closedir(dirp);
        return NULL;
    }

    // Read all entries
    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL)
    {
        if (entries->count >= capacity)
        {
            size_t new_cap = capacity * 2;
            char **new_names = realloc(entries->names, sizeof(char *) * new_cap);
            if (!new_names)
            {
                // Cleanup on allocation failure
                for (size_t i = 0; i < entries->count; i++)
                {
                    free(entries->names[i]);
                }
                free(entries->names);
                free(entries);
                closedir(dirp);
                return NULL;
            }
            entries->names = new_names;
            capacity = new_cap;
        }

        // Duplicate entry name
        entries->names[entries->count] = strdup(entry->d_name);
        if (!entries->names[entries->count])
        {
            // Cleanup on strdup failure
            for (size_t i = 0; i < entries->count; i++)
            {
                free(entries->names[i]);
            }
            free(entries->names);
            free(entries);
            closedir(dirp);
            return NULL;
        }
        entries->count++;
    }

    closedir(dirp);

    if (sort && entries->count > 1)
        qsort(entries->names, entries->count, sizeof(char *), rbc_strcmp_wrapper);

    return entries;
}

/// @brief Get next entry name from sorted list
static const char *rbc_readdir_sorted(rbc_dir_entries_t *entries)
{
    if (!entries || entries->current >= entries->count)
        return NULL;
    return entries->names[entries->current++];
}

/// @brief Close and free sorted directory entries
static void rbc_closedir_sorted(rbc_dir_entries_t *entries)
{
    if (!entries)
        return;

    for (size_t i = 0; i < entries->count; i++)
    {
        free(entries->names[i]);
    }
    free(entries->names);
    free(entries);
}

// ============================================================================
// Results Management
// ============================================================================

static bool rbc_glob_results_add(rbc_results_t *results, const char *path)
{
    if (results->count >= results->capacity)
    {
        size_t new_cap = results->capacity * 2;
        char **new_items = realloc(results->items, sizeof(char *) * new_cap);
        size_t *new_lens = realloc(results->lengths, sizeof(size_t) * new_cap);
        if (!new_items || !new_lens)
            return false;
        results->items = new_items;
        results->lengths = new_lens;
        results->capacity = new_cap;
    }
    size_t len = strlen(path);

    char *p = malloc(len + 1);
    if (!p)
        return false;
    memcpy(p, path, len + 1);

    results->items[results->count] = p;
    results->lengths[results->count] = len;
    results->count++;
    return true;
}

// ============================================================================
// Pattern Analysis (Streaming - Single Pass)
// ============================================================================

/// @brief Segment character type flags
#define SEG_HAS_STAR 0x01     // Contains '*'
#define SEG_HAS_QUESTION 0x02 // Contains '?'
#define SEG_HAS_BRACKET 0x04  // Contains '['
#define SEG_HAS_ESCAPE 0x08   // Contains escaped characters
#define SEG_HAS_REGULAR 0x10  // Contains escape sequences

/// @brief Extract next segment from pattern
static bool rbc_glob_next_segment(const char **pattern, unsigned flags, rbc_segment_t *seg)
{
    const char *p = *pattern;
    if (*p == '\0')
        return false;

    // Handle leading '/' as root segment
    if (*p == '/')
    {
        seg->start = p;
        seg->len = 1;
        seg->type = RBC_SEG_ROOT;
        seg->starts_with_dot = false;
        seg->trailing_slashes = 0;
        p++;
        *pattern = p;
        return true;
    }

    const char *seg_start = p;
    seg->start = seg_start;
    seg->len = 0;
    seg->type = RBC_SEG_LITERAL;
    seg->starts_with_dot = false;
    seg->trailing_slashes = 0;

    // Check for leading dot
    if (*p == '.')
    {
        p++;
        seg->starts_with_dot = true;

        // Special cases for single '.' or '..' segments
        if (*p == '\0' || *p == '/')
        {
            seg->type = RBC_SEG_DOT;
            goto segment_end;
        }
        else if (*p == '.' && (*(p + 1) == '\0' || *(p + 1) == '/'))
        {
            seg->type = RBC_SEG_DOTDOT;
            goto segment_end;
        }
    }

    // Scan segment and collect character types
    const char *pattern_part = p; // Pattern analysis starts here
    unsigned char char_flags = 0;
    int in_bracket = 0;

    while (*p)
    {
        if (*p == '\\' && *(p + 1) && !(flags & RBC_FNM_NOESCAPE))
        {
            char_flags |= SEG_HAS_REGULAR | SEG_HAS_ESCAPE;
            p += 2;
            continue;
        }
        switch (*p)
        {
        case '/':
            if (in_bracket == 0)
                goto segment_end;
            break;
        case '*':
            char_flags |= SEG_HAS_STAR;
            break;
        case '?':
            char_flags |= SEG_HAS_QUESTION;
            break;
        case '[':
            char_flags |= SEG_HAS_BRACKET;
            in_bracket++;
            break;
        case ']':
            if (in_bracket > 0)
                in_bracket--;
            else
                char_flags |= SEG_HAS_REGULAR;
            break;
        default:
            char_flags |= SEG_HAS_REGULAR;
            break;
        }
        p++;
    }

segment_end:
    seg->len = p - seg_start;
    size_t pattern_len = p - pattern_part;

    if (char_flags == SEG_HAS_STAR && pattern_len == 2)
    {
        if (*p == '\0')
            seg->type = RBC_SEG_ANY;
        else
            seg->type = RBC_SEG_RECURSIVE;
    }
    else if (char_flags == SEG_HAS_STAR && pattern_len >= 1)
        seg->type = RBC_SEG_ANY;
    else if (char_flags & SEG_HAS_ESCAPE)
        seg->type = RBC_SEG_MAGICAL;
    else if (char_flags == SEG_HAS_REGULAR || char_flags == 0)
        seg->type = RBC_SEG_LITERAL;
    else
        seg->type = RBC_SEG_MAGICAL;

    // Count trailing slashes
    while (*p == '/')
    {
        seg->trailing_slashes++;
        p++;
    }

    *pattern = p;
    return true;
}

// ============================================================================
// Path Building Helpers
// ============================================================================

/**
 * @brief Build a path with trailing slashes from pattern
 *
 * Design: trailing_slashes = パターン由来のスラッシュ総数（分離用の1つを含む）
 *
 * Invariant: base must NEVER end with '/' (caller's responsibility)
 *
 * Examples:
 *   Pattern 'a/b'   → seg='a', trailing_slashes=1
 *     build("", "a", 1) → "a/"
 *     build("a", "b", 0) → "a/b"  (最終セグメント)
 *
 *   Pattern 'a//b'  → seg='a', trailing_slashes=2
 *     build("", "a", 2) → "a//"
 *     build("a", "b", 0) → "a/b"  (注: baseは正規化された "a")
 *
 *   Pattern 'a/b/'  → last seg='b', trailing_slashes=1
 *     build("a", "b", 1) → "a/b/"
 *
 * Algorithm:
 *   1. If base is non-empty: result = base + '/' + name
 *   2. If base is empty: result = name
 *   3. Append exactly (trailing_slashes) '/' characters
 *
 * @param buf Output buffer
 * @param buf_size Size of output buffer
 * @param base Base path (must NOT end with '/')
 * @param base_len Length of base path
 * @param name Name to append
 * @param trailing_slashes Number of '/' to append
 * @return Length of resulting path
 */
static size_t rbc_build_path_with_slashes(char *buf, size_t buf_size,
                                          const char *base, size_t base_len,
                                          const char *name, size_t trailing_slashes)
{
    size_t len;

    if (base_len > 0)
    {
        // base + '/' + name
        len = snprintf(buf, buf_size, "%s/%s", base, name);
    }
    else
    {
        // name only
        len = snprintf(buf, buf_size, "%s", name);
    }

    // Append exactly trailing_slashes '/' characters
    if (trailing_slashes > 0 && len < buf_size - 1)
    {
        for (size_t i = 0; i < trailing_slashes && len < buf_size - 1; i++)
        {
            buf[len++] = '/';
        }
        buf[len] = '\0';
    }

    return len;
}

// ============================================================================
// Brace Expansion (Array-based preprocessing)
// ============================================================================

/// @brief Find matching closing brace
static const char *rbc_find_matching_brace(const char *p)
{
    int depth = 1;
    p++; // Skip opening '{'

    while (*p && depth > 0)
    {
        if (*p == '\\' && *(p + 1))
        {
            p += 2;
            continue;
        }
        if (*p == '{')
            depth++;
        else if (*p == '}')
            depth--;
        if (depth > 0)
            p++;
    }

    return (*p == '}') ? p : NULL;
}

/// @brief Add a pattern to brace expansion result
static bool rbc_brace_result_add(rbc_brace_result_t *result, const char *pattern)
{
    if (result->count >= result->capacity)
    {
        size_t new_cap = result->capacity * 2;
        char **new_patterns = realloc(result->patterns, sizeof(char *) * new_cap);
        if (!new_patterns)
            return false;
        result->patterns = new_patterns;
        result->capacity = new_cap;
    }

    result->patterns[result->count] = strdup(pattern);
    if (!result->patterns[result->count])
        return false;

    result->count++;
    return true;
}

// Forward declaration
static void rbc_brace_expand_impl(const char *pattern, size_t pattern_len, rbc_brace_result_t *result);

/// @brief Helper to build and recursively expand a brace option
static void rbc_expand_brace_option(
    const char *prefix,
    size_t prefix_len,
    const char *option,
    size_t option_len,
    const char *suffix,
    size_t suffix_len,
    rbc_brace_result_t *result)
{
    char temp[RBC_GLOB_MAX_PATH];
    size_t pos = 0;

    memcpy(temp + pos, prefix, prefix_len);
    pos += prefix_len;
    memcpy(temp + pos, option, option_len);
    pos += option_len;
    memcpy(temp + pos, suffix, suffix_len);
    pos += suffix_len;

    rbc_brace_expand_impl(temp, pos, result);
}

/// @brief Expand braces recursively (array-based, optimized single-pass)
static void rbc_brace_expand_impl(
    const char *pattern,
    size_t pattern_len,
    rbc_brace_result_t *result)
{
    char buf[RBC_GLOB_MAX_PATH];
    size_t buf_pos = 0;
    const char *p = pattern;
    const char *end = pattern + pattern_len;

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
            // Single-pass: find all commas and close brace in one scan
            const char *brace_start = p;
            const char *opt_positions[256]; // Option start positions
            size_t opt_count = 0;
            const char *scan = p + 1;
            const char *close = NULL;
            int depth = 0;

            opt_positions[opt_count++] = scan; // First option starts here

            // One pass to find all comma positions and close brace
            while (scan < end)
            {
                if (*scan == '\\' && scan + 1 < end)
                {
                    scan += 2;
                    continue;
                }

                if (*scan == '{')
                {
                    depth++;
                }
                else if (*scan == '}')
                {
                    if (depth == 0)
                    {
                        close = scan;
                        break;
                    }
                    depth--;
                }
                else if (depth == 0 && *scan == ',' && opt_count < 256)
                {
                    opt_positions[opt_count++] = scan + 1; // Next option starts after comma
                }
                scan++;
            }

            if (!close || close >= end)
            {
                // Not a valid brace expansion
                buf[buf_pos++] = *p++;
                continue;
            }

            // Expand all options using collected positions
            size_t rest_len = end - (close + 1);
            for (size_t i = 0; i < opt_count; i++)
            {
                const char *opt_start = opt_positions[i];
                const char *opt_end = (i + 1 < opt_count) ? opt_positions[i + 1] - 1 : close;
                size_t opt_len = opt_end - opt_start;

                rbc_expand_brace_option(buf, buf_pos, opt_start, opt_len, close + 1, rest_len, result);
            }

            return;
        }

        buf[buf_pos++] = *p++;
    }

    buf[buf_pos] = '\0';
    rbc_brace_result_add(result, buf);
}

/// @brief Expand braces in pattern to array of patterns
static rbc_brace_result_t *rbc_brace_expand(const char *pattern, size_t pattern_len)
{
    rbc_brace_result_t *result = malloc(sizeof(rbc_brace_result_t));
    if (!result)
        return NULL;

    result->capacity = 8;
    result->count = 0;
    result->patterns = malloc(sizeof(char *) * result->capacity);
    if (!result->patterns)
    {
        free(result);
        return NULL;
    }

    rbc_brace_expand_impl(pattern, pattern_len, result);

    return result;
}

/// @brief Free brace expansion result
static void rbc_brace_result_free(rbc_brace_result_t *result)
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
// Glob Core Logic (Ruby Dir.glob compatible implementation)
// ============================================================================

bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags); // External

// Forward declarations
static void rbc_glob_match(const char *path, size_t path_len, size_t baselen,
                           const char *pattern, unsigned flags, rbc_results_t *results,
                           bool sort);

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Check if path is a directory
static inline bool is_directory_path(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/// @brief Add result to results list, extracting relative path from base
static void add_result_from_path(const char *path, size_t baselen,
                                 bool with_slash, rbc_results_t *results)
{
    const char *result = path + baselen;
    if (baselen > 0)
    {
        while (*result == '/')
            result++;
    }
    if (*result == '\0')
        result = ".";

    if (with_slash)
    {
        char result_with_slash[RBC_GLOB_MAX_PATH];
        snprintf(result_with_slash, sizeof(result_with_slash), "%s/", result);
        rbc_glob_results_add(results, result_with_slash);
    }
    else
    {
        rbc_glob_results_add(results, result);
    }
}

/// @brief Match name against segment pattern
static bool match_segment(const rbc_segment_t *seg, const char *name,
                          const char *pattern_buf, unsigned flags)
{
    switch (seg->type)
    {
    case RBC_SEG_LITERAL:
        return strcmp(pattern_buf, name) == 0;
    case RBC_SEG_DOT:
        return strcmp(name, ".") == 0;
    case RBC_SEG_DOTDOT:
        return strcmp(name, "..") == 0;
    case RBC_SEG_ANY:
    case RBC_SEG_MAGICAL:
        return rbc_fnmatch(pattern_buf, name, flags);
    case RBC_SEG_ROOT:
        // Root segment doesn't match names (handled specially)
        return false;
    default:
        return false;
    }
}

/// @brief Handle RECURSIVE (**) pattern matching with simple DFS
static void rbc_glob_recursive_helper(
    const char *path,
    size_t path_len,
    size_t baselen,
    const rbc_segment_t *recursive_seg,
    const char *remaining_pattern,
    unsigned flags,
    rbc_results_t *results,
    bool sort)
{
    const char *next_pattern = remaining_pattern;
    if (*next_pattern == '/')
        next_pattern++;

    bool add_dirs = (recursive_seg->trailing_slashes > 0);

    // Skip continuous RECURSIVE (**) patterns (Ruby: **/** → **)
    while (*next_pattern != '\0')
    {
        rbc_segment_t peek_seg;
        const char *peek_ptr = next_pattern;
        if (!rbc_glob_next_segment(&peek_ptr, flags, &peek_seg))
            break;

        if (peek_seg.type != RBC_SEG_RECURSIVE)
            break;

        // Update trailing slash status
        if (peek_seg.trailing_slashes > 0)
            add_dirs = true;

        // Skip this redundant ** segment
        next_pattern = peek_ptr;
        if (*next_pattern == '/')
            next_pattern++;
    }

    // Check if pattern ends here (e.g., "**/" or "**")
    bool pattern_ends = (*next_pattern == '\0');

    // Add current directory if pattern matches
    if (pattern_ends && add_dirs)
    {
        add_result_from_path(path, baselen, true, results);
    }

    rbc_dir_entries_t *entries = rbc_opendir_sorted((path_len > 0) ? path : ".", sort);
    if (!entries)
        return;

    char pathbuf[RBC_GLOB_MAX_PATH];
    const char *name;

    while ((name = rbc_readdir_sorted(entries)) != NULL)
    {
        // Skip "." and ".."
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        // Check dotfile visibility
        bool is_dotfile = (name[0] == '.');
        if (is_dotfile && !recursive_seg->starts_with_dot && !(flags & RBC_FNM_DOTMATCH))
            continue;

        // Build full path
        size_t new_len = rbc_build_path_with_slashes(pathbuf, sizeof(pathbuf),
                                                     path, path_len, name, 0);

        // Try to match current entry with pattern
        if (!pattern_ends)
        {
            // Check if current entry matches the next pattern segment
            rbc_segment_t next_seg;
            const char *pat_copy = next_pattern;
            if (rbc_glob_next_segment(&pat_copy, flags, &next_seg))
            {
                char pattern_buf[RBC_GLOB_MAX_PATH];
                memcpy(pattern_buf, next_seg.start, next_seg.len);
                pattern_buf[next_seg.len] = '\0';

                if (match_segment(&next_seg, name, pattern_buf, flags))
                {
                    struct stat st;
                    if (stat(pathbuf, &st) == 0)
                    {
                        // If pattern has trailing slash, only match directories
                        if (next_seg.trailing_slashes == 0 || S_ISDIR(st.st_mode))
                        {
                            add_result_from_path(pathbuf, baselen, next_seg.trailing_slashes > 0, results);
                        }
                    }
                }
            }
        }

        // Recurse into directories
        if (is_directory_path(pathbuf))
        {
            rbc_glob_recursive_helper(pathbuf, new_len, baselen,
                                      recursive_seg, remaining_pattern,
                                      flags, results, sort);
        }
    }

    rbc_closedir_sorted(entries);
}

/// @brief Handle normal segment matching (LITERAL, DOT, DOTDOT, ANY, MAGICAL)
static void rbc_glob_match_normal(
    const char *path,
    size_t path_len,
    size_t baselen,
    const rbc_segment_t *seg,
    const char *remaining_pattern,
    unsigned flags,
    rbc_results_t *results,
    bool sort)
{
    rbc_dir_entries_t *entries = rbc_opendir_sorted((path_len > 0) ? path : ".", sort);
    if (!entries)
        return;

    char pathbuf[RBC_GLOB_MAX_PATH];
    char pattern_buf[RBC_GLOB_MAX_PATH];
    memcpy(pattern_buf, seg->start, seg->len);
    pattern_buf[seg->len] = '\0';

    const char *name;
    while ((name = rbc_readdir_sorted(entries)) != NULL)
    {
        if (strcmp(name, "..") == 0)
        {
            if (seg->type != RBC_SEG_DOTDOT)
                continue;
        }

        // Skip dotfiles unless pattern starts with "." or DOTMATCH flag is set
        if (name[0] == '.' && !seg->starts_with_dot && !(flags & RBC_FNM_DOTMATCH))
            continue;

        // Match entry name against segment pattern
        if (!match_segment(seg, name, pattern_buf, flags))
            continue;

        // Build new path
        size_t new_len;
        if (path_len > 0)
            new_len = snprintf(pathbuf, sizeof(pathbuf), "%s/%s", path, name);
        else
            new_len = snprintf(pathbuf, sizeof(pathbuf), "%s", name);

        // Check if more segments remain
        if (*remaining_pattern == '\0')
        {
            // Last segment - verify file exists and add result
            struct stat st;
            if (stat(pathbuf, &st) == 0)
            {
                // If pattern has trailing slash, only match directories
                if (seg->trailing_slashes > 0 && !S_ISDIR(st.st_mode))
                    continue;

                add_result_from_path(pathbuf, baselen, seg->trailing_slashes > 0, results);
            }
        }
        else
        {
            // Intermediate segment - must be a directory to continue
            if (is_directory_path(pathbuf))
            {
                rbc_glob_match(pathbuf, new_len, baselen, remaining_pattern, flags, results, sort);
            }
        }
    }

    rbc_closedir_sorted(entries);
}

/// @brief Match pattern against directory entries (segment-based processing)
static void rbc_glob_match(
    const char *path,
    size_t path_len,
    size_t baselen,
    const char *pattern,
    unsigned flags,
    rbc_results_t *results,
    bool sort)
{
    rbc_segment_t seg;
    const char *pat_ptr = pattern;

    // Get next segment
    if (!rbc_glob_next_segment(&pat_ptr, flags, &seg))
        return;

    // Dispatch to appropriate handler based on segment type
    switch (seg.type)
    {
    case RBC_SEG_ROOT:
        // Absolute path: start from root directory
        rbc_glob_match("/", 1, 0, pat_ptr, flags, results, sort);
        break;

    case RBC_SEG_RECURSIVE:
        // Recursive pattern: delegate to specialized handler
        rbc_glob_recursive_helper(path, path_len, baselen, &seg, pat_ptr, flags, results, sort);
        break;

    case RBC_SEG_DOT:
    case RBC_SEG_DOTDOT:
    case RBC_SEG_LITERAL:
    case RBC_SEG_ANY:
    case RBC_SEG_MAGICAL:
        // Normal segment: enumerate directory and match
        rbc_glob_match_normal(path, path_len, baselen, &seg, pat_ptr, flags, results, sort);
        break;

    default:
        // Unknown segment type: skip
        break;
    }
}

// ============================================================================
// Public API
// ============================================================================

bool rbc_glob(
    const char **patterns,
    size_t npatterns,
    unsigned flags,
    const char *base,
    bool sort,
    char ***out,
    size_t *count,
    size_t **lengths)
{
    if (!patterns || npatterns == 0 || !out || !count)
        return false;

    // Initialize results
    rbc_results_t results;
    results.capacity = RBC_RESULTS_CAPACITY;
    results.items = malloc(sizeof(char *) * results.capacity);
    results.lengths = malloc(sizeof(size_t) * results.capacity);
    results.count = 0;
    if (!results.items || !results.lengths)
    {
        free(results.items);
        free(results.lengths);
        return false;
    }

    // Calculate base length (excluding trailing slashes)
    size_t baselen = 0;
    if (base && base[0] != '\0')
    {
        baselen = strlen(base);
        while (baselen > 0 && base[baselen - 1] == '/')
            baselen--;
    }

    const char *actual_base = (baselen > 0) ? base : "";
    size_t base_path_len = baselen;
    if (base_path_len > 0 && actual_base[base_path_len - 1] == '/')
        base_path_len--;

    // Process each pattern with brace expansion preprocessing
    for (size_t i = 0; i < npatterns; i++)
    {
        // Preprocess: expand braces
        rbc_brace_result_t *expanded = rbc_brace_expand(patterns[i], strlen(patterns[i]));
        if (!expanded)
            continue;

        // Process each expanded pattern
        for (size_t j = 0; j < expanded->count; j++)
        {
            size_t count_before = results.count;

            rbc_glob_match(
                actual_base,
                base_path_len,
                baselen,
                expanded->patterns[j],
                flags,
                &results,
                sort);

            // Sort results for this pattern
            if (sort && results.count > count_before)
                qsort(&results.items[count_before], results.count - count_before,
                      sizeof(char *), rbc_strcmp_wrapper);
        }

        rbc_brace_result_free(expanded);
    }

    // Return results
    *count = results.count;
    *out = results.items;
    if (lengths)
        *lengths = results.lengths;

    return true;
}

void rbc_glob_free(char **list, size_t count, size_t *lengths)
{
    if (!list)
        return;

    // Free each string individually
    for (size_t i = 0; i < count; i++)
        if (list[i])
            free(list[i]);

    // Free the arrays
    free(list);
    if (lengths)
        free(lengths);
}
