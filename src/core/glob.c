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

// Internal flags (not exposed in API, use high bits)
#define RBC_INTERNAL_IN_DOUBLESTAR 0x80000000 // Currently recursing from **
#define RBC_GLOB_SKIPDOT 0x40000000           // Skip "." entry (set for all intermediate segments)
#define RBC_INTERNAL_FIRST_CALL 0x20000000    // First directory enumeration (allow "." with DOTMATCH)

/// @brief Glob results storage
typedef struct
{
    char **items;
    size_t *lengths;
    size_t count;
    size_t capacity;
    void *ctx; // Unused, for compatibility
} rbc_results_t;

/// @brief Segment type classification (MRI-compatible)
typedef enum
{
    RBC_SEG_LITERAL,   // Plain text, no metacharacters
    RBC_SEG_ANY,       // Single star "*" or terminal "**" (same as *)
    RBC_SEG_RECURSIVE, // "**" with following pattern (recursive search)
    RBC_SEG_BRACE,     // Brace expansion segment
    RBC_SEG_MAGICAL,   // Contains wildcards: *, ?, [
} rbc_segment_type_t;

/// @brief Pattern segment information (for streaming)
typedef struct
{
    const char *start;       // Segment start pointer
    size_t len;              // Segment length
    rbc_segment_type_t type; // Segment type classification
    bool starts_with_dot;    // Starts with '.'
    size_t trailing_slashes; // Number of trailing slashes after segment
} rbc_segment_t;

// ============================================================================
// Pattern Normalization (MRI-compatible)
// ============================================================================

/// @brief Normalize glob pattern by folding continuous ** patterns
/// MRI behavior: **/**/ → **/ (fold continuous RECURSIVEs)
/// This prevents duplicate results in patterns like **/**/ or **/**/
static void rbc_normalize_pattern(const char *pattern, char *normalized, size_t max_len)
{
    const char *src = pattern;
    char *dst = normalized;
    char *end = normalized + max_len - 1;

    while (*src && dst < end)
    {
        // Check for **/ followed by another **/
        if (src[0] == '*' && src[1] == '*' && src[2] == '/')
        {
            // Found **/
            *dst++ = '*';
            *dst++ = '*';
            *dst++ = '/';
            src += 3;

            // Skip any following **/ patterns (fold continuous **)
            while (src[0] == '*' && src[1] == '*' && src[2] == '/' && dst < end)
            {
                src += 3; // Skip the redundant **/
            }
        }
        else
        {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

// ============================================================================
// Directory Entry Management (MRI-compatible sorted readdir)
// ============================================================================

/// @brief Sorted directory entry list
typedef struct
{
    char **names;   // Array of entry names (sorted)
    size_t count;   // Number of entries
    size_t current; // Current read position
} rbc_dir_entries_t;

/// @brief Compare function for sorting entry names
static int rbc_entry_name_cmp(const void *a, const void *b)
{
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    return strcmp(s1, s2);
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

    // Sort if requested
    if (sort && entries->count > 1)
    {
        qsort(entries->names, entries->count, sizeof(char *), rbc_entry_name_cmp);
    }

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

    // Allocate string with strdup instead of arena (for independent free)
    char *p = malloc(len + 1);
    if (!p)
        return false;
    memcpy(p, path, len + 1);

    results->items[results->count] = p;
    results->lengths[results->count] = len;
    results->count++;
    return true;
}

static int rbc_glob_results_path_cmp(const void *a, const void *b)
{
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    return strcmp(s1, s2);
}

static void rbc_glob_results_sort(rbc_results_t *results)
{
    if (results->count > 1)
    {
        qsort(results->items, results->count, sizeof(char *), rbc_glob_results_path_cmp);
    }
}

// ============================================================================
// Pattern Analysis (Streaming - Single Pass)
// ============================================================================

/// @brief Segment character type flags
#define SEG_HAS_STAR 0x01     // Contains '*'
#define SEG_HAS_QUESTION 0x02 // Contains '?'
#define SEG_HAS_BRACKET 0x04  // Contains '['
#define SEG_HAS_BRACE 0x08    // Contains '{'
#define SEG_HAS_REGULAR 0x10  // Contains regular characters
#define SEG_HAS_ESCAPE 0x20   // Contains escaped characters

/// @brief Check if character is a glob metacharacter
static inline bool rbc_is_magic_char(char c)
{
    return c == '*' || c == '?' || c == '[' || c == '{';
}

/// @brief Get next segment from pattern (streaming, no allocation)
/// @return true if segment found, false if end of pattern
static bool rbc_next_segment(const char **pattern, rbc_segment_t *seg)
{
    const char *p = *pattern;
    if (*p == '\0')
        return false;

    const char *seg_start = p;
    seg->start = seg_start;
    seg->starts_with_dot = (*p == '.');

    // Initialize segment type as LITERAL (will upgrade if needed)
    seg->type = RBC_SEG_LITERAL;

    // Skip leading dot for pattern analysis (if present)
    if (*p == '.')
        p++;

    // Scan segment and collect character types
    const char *pattern_part = p; // Pattern analysis starts here
    unsigned char char_flags = 0;
    int in_bracket = 0;
    int in_brace = 0;

    while (*p)
    {
        if (*p == '\\' && *(p + 1))
        {
            char_flags |= SEG_HAS_ESCAPE | SEG_HAS_REGULAR;
            p += 2;
            continue;
        }

        switch (*p)
        {
        case '/':
            if (in_bracket == 0 && in_brace == 0)
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
        case '{':
            char_flags |= SEG_HAS_BRACE;
            in_brace++;
            break;
        case '}':
            if (in_brace > 0)
                in_brace--;
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

    if (char_flags & SEG_HAS_BRACE)
    {
        seg->type = RBC_SEG_BRACE;
    }
    else if (char_flags == SEG_HAS_STAR && pattern_len == 2)
    {
        // ** pattern detected (exactly 2 stars) - check if terminal or recursive
        // MRI behavior:
        // ** (no slash, no following) → same as * (RBC_SEG_ANY)
        // **/ (with slash) → recursive directories (RBC_SEG_RECURSIVE)
        // **/*.txt (with following pattern) → recursive (RBC_SEG_RECURSIVE)
        // .** (terminal) → same as .* (RBC_SEG_ANY)
        // .**/ or .**/pattern → recursive matching dot files

        if (*p == '\0')
        {
            // Terminal ** (with or without dot): treat as * or .*
            seg->type = RBC_SEG_ANY;
        }
        else
        {
            // ** with trailing slash or following pattern: recursive
            seg->type = RBC_SEG_RECURSIVE;
        }
    }
    else if (char_flags == SEG_HAS_STAR && pattern_len >= 1)
    {
        // *, ***, ****, etc. - all treated as single * wildcard
        seg->type = RBC_SEG_ANY;
    }
    else if (char_flags == SEG_HAS_REGULAR || char_flags == 0)
    {
        seg->type = RBC_SEG_LITERAL;
    }
    else
    {
        seg->type = RBC_SEG_MAGICAL;
    }

    // Count trailing slashes (パターン由来のスラッシュ総数)
    // 例: 'a/b'  → trailing_slashes=1 (分離用)
    //     'a//b' → trailing_slashes=2 (分離 + 連続スラッシュ1つ)
    seg->trailing_slashes = 0;
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
// Brace Expansion (Improved)
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

/// @brief Expand braces recursively
static void rbc_brace_expand_impl(
    const char *pattern,
    char *buf,
    size_t buf_pos,
    void (*cb)(const char *, void *),
    void *arg)
{
    const char *p = pattern;

    while (*p)
    {
        if (*p == '\\' && *(p + 1))
        {
            buf[buf_pos++] = *p++;
            buf[buf_pos++] = *p++;
            continue;
        }

        if (*p == '{')
        {
            const char *close = rbc_find_matching_brace(p);
            if (!close)
            {
                // Not a valid brace expansion
                buf[buf_pos++] = *p++;
                continue;
            }

            // Expand each option (supporting nested braces)
            const char *opt_start = p + 1;
            const char *opt_end = opt_start;
            int depth = 0;

            while (opt_end < close)
            {
                // Track nested braces to find real commas (not inside nested braces)
                if (*opt_end == '{')
                    depth++;
                else if (*opt_end == '}')
                    depth--;
                else if (depth == 0 && *opt_end == ',')
                {
                    // Found a comma at depth 0 - this ends the current option
                    size_t opt_len = opt_end - opt_start;

                    // Copy the option into a temporary buffer
                    char option[RBC_GLOB_MAX_PATH];
                    memcpy(option, opt_start, opt_len);
                    option[opt_len] = '\0';

                    // Build pattern: current_prefix + option + rest_of_pattern
                    char temp_pattern[RBC_GLOB_MAX_PATH];
                    memcpy(temp_pattern, buf, buf_pos);
                    strcpy(temp_pattern + buf_pos, option);
                    strcpy(temp_pattern + buf_pos + opt_len, close + 1);

                    // Recursively expand the complete pattern
                    // (this will handle any nested braces in the option)
                    rbc_brace_expand_impl(temp_pattern, buf, 0, cb, arg);

                    opt_start = opt_end + 1;
                }
                opt_end++;
            }

            // Handle the last option (after the last comma, or the only option)
            size_t opt_len = close - opt_start;

            // Copy the option into a temporary buffer
            char option[RBC_GLOB_MAX_PATH];
            memcpy(option, opt_start, opt_len);
            option[opt_len] = '\0';

            // Build pattern: current_prefix + option + rest_of_pattern
            char temp_pattern[RBC_GLOB_MAX_PATH];
            memcpy(temp_pattern, buf, buf_pos);
            strcpy(temp_pattern + buf_pos, option);
            strcpy(temp_pattern + buf_pos + opt_len, close + 1);

            // Recursively expand the complete pattern
            rbc_brace_expand_impl(temp_pattern, buf, 0, cb, arg);

            return;
        }

        buf[buf_pos++] = *p++;
    }

    buf[buf_pos] = '\0';
    cb(buf, arg);
}

static void rbc_brace_expand(
    const char *pattern,
    void (*cb)(const char *, void *),
    void *arg)
{
    char buf[RBC_GLOB_MAX_PATH];
    rbc_brace_expand_impl(pattern, buf, 0, cb, arg);
}

// ============================================================================
// Glob Core Logic (MRI-compatible) - Refactored for clarity
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
    case RBC_SEG_ANY:
    case RBC_SEG_MAGICAL:
        return rbc_fnmatch(pattern_buf, name, flags);
    case RBC_SEG_BRACE:
        // Should not happen (brace expansion done earlier)
        return strcmp(pattern_buf, name) == 0;
    default:
        return false;
    }
}

static void rbc_glob_recursive_helper(const char *path, size_t path_len, size_t baselen,
                                      const rbc_segment_t *recursive_seg, const char *remaining_pattern,
                                      unsigned flags, rbc_results_t *results, bool sort);

/// @brief Handle RECURSIVE (**) pattern matching
/// Separated from normal matching for clarity and to avoid pattern reconstruction
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
    // Check if this is **/ (directories only)
    bool is_dirs_only = (*remaining_pattern == '\0' && recursive_seg->trailing_slashes > 0);

    // Skip the separator after **
    const char *pattern_after_doublestar = remaining_pattern;
    if (*pattern_after_doublestar == '/')
        pattern_after_doublestar++;

    // True DFS: Add current directory first (for **/ pattern)
    if (is_dirs_only)
    {
        // **/ pattern: add current directory with trailing slash
        add_result_from_path(path, baselen, true, results);
    }

    rbc_dir_entries_t *entries = rbc_opendir_sorted((path_len > 0) ? path : ".", sort);
    if (!entries)
        return;

    char pathbuf[RBC_GLOB_MAX_PATH];

    // Set SKIPDOT for recursive calls (MRI-compatible)
    flags |= RBC_GLOB_SKIPDOT;

    const char *name;
    while ((name = rbc_readdir_sorted(entries)) != NULL)
    {
        size_t namlen = strlen(name);
        int dotfile = 0; // MRI's dotfile counter: 0=normal, 1=dotfile, 2="." entry

        // Handle "." and ".." entries (MRI dir.c L2879-2892)
        if (name[0] == '.')
        {
            ++dotfile; // All dot entries: dotfile >= 1

            if (namlen == 2 && name[1] == '.')
            {
                // Always skip ".." to prevent infinite recursion
                continue;
            }
            if (namlen == 1)
            {
                // "." entry: dotfile = 2, always skip (0-time match already processed)
                ++dotfile;
                continue;
            }
            // For dotfiles (.hidden, .subhidden0, etc.): dotfile = 1
        }

        // Filter for .**/ patterns: only descend into dot-directories
        // For **/ patterns (non-dot), this filter doesn't apply - dotfile counter handles it
        if (recursive_seg->starts_with_dot && name[0] != '.')
        {
            // .**/ pattern: skip non-dot directories
            continue;
        }

        // Build path and check if directory
        size_t new_len = rbc_build_path_with_slashes(pathbuf, sizeof(pathbuf),
                                                     path, path_len, name, 0);

        if (is_directory_path(pathbuf))
        {
            // Determine if we should descend into this directory (MRI-compatible):
            // 1. .**/: descend into dot-directories at first level, then into ALL subdirectories recursively
            //    First level: only .hidden-like directories
            //    Subsequent levels: ALL directories (both .subhidden and sub0)
            // 2. **/ with DOTMATCH: descend into all directories recursively
            // 3. **/ without DOTMATCH: descend into non-dot directories only
            bool should_descend;
            if (recursive_seg->starts_with_dot)
            {
                // .**/pattern: behavior changes based on depth
                bool is_first_level = !(flags & RBC_INTERNAL_IN_DOUBLESTAR);
                if (is_first_level)
                {
                    // First level: only descend into dot-directories (.hidden)
                    should_descend = (dotfile == 1);
                }
                else
                {
                    // Subsequent levels within dot-directories: descend into ALL subdirectories
                    // This includes both .subhidden0 and sub0
                    should_descend = (dotfile == 0 || dotfile == 1);
                }
            }
            else if (flags & RBC_FNM_DOTMATCH)
            {
                // **/pattern with DOTMATCH: descend into all directories (dot and non-dot)
                // But still skip "." (dotfile==2 is already skipped above)
                should_descend = (dotfile == 0 || dotfile == 1);
            }
            else
            {
                // **/pattern without DOTMATCH: descend into non-dot directories only
                should_descend = (dotfile == 0);
            }

            if (should_descend)
            {
                // DFS Step 1: Process 0-time match for this directory (only for **/pattern, not **/)
                if (!is_dirs_only && *pattern_after_doublestar != '\0')
                {
                    // **/pattern: check if this directory matches the pattern
                    // Parse next segment to determine match type
                    rbc_segment_t next_seg;
                    const char *next_pat = pattern_after_doublestar;
                    if (rbc_next_segment(&next_pat, &next_seg))
                    {
                        if (next_seg.type == RBC_SEG_RECURSIVE)
                        {
                            // **/**... pattern: directory always matches (add without slash)
                            add_result_from_path(pathbuf, baselen, false, results);
                        }
                        else
                        {
                            // **/*.txt or **/literal pattern: check if directory name matches
                            // Extract just the filename for matching
                            const char *filename = name;
                            char pattern_buf[RBC_GLOB_MAX_PATH];
                            memcpy(pattern_buf, next_seg.start, next_seg.len);
                            pattern_buf[next_seg.len] = '\0';

                            if (match_segment(&next_seg, filename, pattern_buf, flags))
                            {
                                add_result_from_path(pathbuf, baselen, false, results);
                            }
                        }
                    }
                }

                // DFS Step 2: Recurse immediately into this directory
                bool is_first_level_dot = recursive_seg->starts_with_dot && !(flags & RBC_INTERNAL_IN_DOUBLESTAR);

                if (is_first_level_dot)
                {
                    // First descent into dot-directory: apply remaining pattern non-recursively
                    const char *pattern_to_match = remaining_pattern;
                    if (*pattern_to_match == '/')
                        pattern_to_match++;

                    rbc_glob_match(pathbuf, new_len, baselen, pattern_to_match,
                                   flags | RBC_INTERNAL_IN_DOUBLESTAR, results, sort);
                }
                else
                {
                    // Continue recursive descent (DFS)
                    rbc_glob_recursive_helper(pathbuf, new_len, baselen,
                                              recursive_seg, remaining_pattern,
                                              flags | RBC_INTERNAL_IN_DOUBLESTAR, results, sort);
                }
            }
        }
        else
        {
            // File entry: Process 0-time match
            if (!is_dirs_only && *pattern_after_doublestar != '\0')
            {
                // Check if file matches pattern
                rbc_segment_t next_seg;
                const char *next_pat = pattern_after_doublestar;
                if (rbc_next_segment(&next_pat, &next_seg))
                {
                    if (next_seg.type == RBC_SEG_RECURSIVE)
                    {
                        // **/**... pattern: file always matches
                        add_result_from_path(pathbuf, baselen, false, results);
                    }
                    else
                    {
                        // **/*.txt or **/literal pattern: check if filename matches
                        const char *filename = name;
                        char pattern_buf[RBC_GLOB_MAX_PATH];
                        memcpy(pattern_buf, next_seg.start, next_seg.len);
                        pattern_buf[next_seg.len] = '\0';

                        if (match_segment(&next_seg, filename, pattern_buf, flags))
                        {
                            add_result_from_path(pathbuf, baselen, false, results);
                        }
                    }
                }
            }
        }
    }

    rbc_closedir_sorted(entries);
}

/// @brief Match pattern against directory entries (excludes RECURSIVE handling)
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
    if (!rbc_next_segment(&pat_ptr, &seg))
    {
        // No more segments
        // MRI behavior: empty pattern matches nothing, EXCEPT when it's a root path "/"
        if (*pattern == '\0')
        {
            // Check if we have a root path like "/"
            if (path_len > 0 && path[0] == '/' && path[1] == '\0')
            {
                // Root path "/" with empty pattern: match the root
                const char *check_path = path;
                struct stat st;
                if (stat(check_path, &st) == 0)
                {
                    add_result_from_path(path, baselen, false, results);
                }
                return;
            }
            // Otherwise, empty pattern: no match (MRI-compatible)
            return;
        }

        // Non-empty path: check if it exists
        const char *check_path = (path_len > 0) ? path : ".";
        struct stat st;
        if (stat(check_path, &st) == 0)
        {
            add_result_from_path(path, baselen, false, results);
        }
        return;
    }

    // Delegate RECURSIVE pattern to specialized handler
    if (seg.type == RBC_SEG_RECURSIVE)
    {
        rbc_glob_recursive_helper(path, path_len, baselen, &seg, pat_ptr, flags, results, sort);
        return;
    }

    // Handle normal segment (literal or wildcard)
    rbc_dir_entries_t *entries = rbc_opendir_sorted((path_len > 0) ? path : ".", sort);
    if (!entries)
        return;

    // MRI-compatible (dir.c L2815-2865): Save skipdot before updating it
    // Only MAGICAL/ANY patterns update SKIPDOT (PLAIN/LITERAL don't)
    bool skipdot = (flags & RBC_GLOB_SKIPDOT) != 0;
    if (seg.type == RBC_SEG_MAGICAL || seg.type == RBC_SEG_ANY || seg.type == RBC_SEG_RECURSIVE)
    {
        flags |= RBC_GLOB_SKIPDOT;
    }

    char pathbuf[RBC_GLOB_MAX_PATH];
    char pattern_buf[RBC_GLOB_MAX_PATH];

    // Copy segment to null-terminated buffer for fnmatch
    // For terminal **, use "*" explicitly
    if (seg.type == RBC_SEG_ANY && seg.len >= 2 && seg.start[0] == '*' && seg.start[1] == '*')
    {
        pattern_buf[0] = '*';
        pattern_buf[1] = '\0';
    }
    else
    {
        memcpy(pattern_buf, seg.start, seg.len);
        pattern_buf[seg.len] = '\0';
    }

    // flags already modified at function start (MRI-compatible)

    const char *name;
    while ((name = rbc_readdir_sorted(entries)) != NULL)
    {
        size_t namlen = strlen(name);

        // MRI behavior (dir.c L2877-2892): Handle dot entries
        if (name[0] == '.')
        {
            if (namlen == 1)
            {
                // "." entry handling (MRI dir.c L2883-2885)
                // MRI: if (recursive && !(flags & FNM_DOTMATCH)) continue;
                // MRI: if (skipdot) continue;

                // Skip "." if we're in a RECURSIVE pattern without DOTMATCH
                if ((flags & RBC_INTERNAL_IN_DOUBLESTAR) && !(flags & RBC_FNM_DOTMATCH))
                    continue;

                // Skip "." if SKIPDOT was already set (subdirectory or MAGICAL/ANY pattern)
                if (skipdot)
                    continue;

                // For patterns starting with ".*" inside a ** context, skip "." entry
                // This prevents "**/.*" from matching "." itself
                // But allows top-level ".*" to match "."
                // Exception: with DOTMATCH flag, allow "." to match
                if ((flags & RBC_INTERNAL_IN_DOUBLESTAR) && seg.starts_with_dot && seg.len > 1 && !(flags & RBC_FNM_DOTMATCH))
                    continue;

                // Top level with PLAIN pattern and not in recursive: let it through for fnmatch
            }
            else if (namlen == 2 && name[1] == '.')
            {
                // ".." entry: skip unless pattern is explicitly ".."
                // Ruby allows Dir.glob("..") to match the parent directory
                bool is_explicit_dotdot = (seg.type == RBC_SEG_LITERAL &&
                                           seg.len == 2 &&
                                           seg.start[0] == '.' &&
                                           seg.start[1] == '.');
                if (!is_explicit_dotdot)
                    continue;
            }
        }

        // Match against pattern
        if (seg.type == RBC_SEG_RECURSIVE)
        {
            // Should not reach here (handled above)
            continue;
        }

        if (!match_segment(&seg, name, pattern_buf, flags))
            continue;

        // Build path with trailing slashes preserved
        // trailing_slashes includes the separator, so for recursion we need trailing_slashes - 1 extra slashes
        // Example: "a//b" has trailing_slashes=2, we add 1 extra slash when building "a/"
        size_t extra_slashes = (seg.trailing_slashes > 1) ? (seg.trailing_slashes - 1) : 0;
        size_t new_len = rbc_build_path_with_slashes(pathbuf, sizeof(pathbuf),
                                                     path, path_len, name, extra_slashes);

        // Check if more segments remain
        if (*pat_ptr == '\0')
        {
            // Last segment - add result with trailing slashes if needed
            struct stat st;
            if (stat(pathbuf, &st) == 0)
            {
                // If segment has trailing slashes, only match directories
                if (seg.trailing_slashes > 0)
                {
                    if (!S_ISDIR(st.st_mode))
                        continue;
                    // Result already has the correct trailing slashes from rbc_build_path_with_slashes
                    // But we need to add one more for the directory separator
                    if (seg.trailing_slashes > 1)
                    {
                        // Extra slashes already added by rbc_build_path_with_slashes
                        // Add one more for directory indicator
                        char result_with_slashes[RBC_GLOB_MAX_PATH];
                        const char *result = pathbuf + baselen;
                        if (baselen > 0)
                        {
                            while (*result == '/')
                                result++;
                        }
                        if (*result == '\0')
                            result = ".";
                        size_t len = snprintf(result_with_slashes, sizeof(result_with_slashes), "%s", result);
                        // Add one more slash (directory indicator)
                        if (len < sizeof(result_with_slashes) - 1)
                        {
                            result_with_slashes[len++] = '/';
                        }
                        result_with_slashes[len] = '\0';
                        rbc_glob_results_add(results, result_with_slashes);
                    }
                    else
                    {
                        add_result_from_path(pathbuf, baselen, true, results);
                    }
                }
                else
                {
                    add_result_from_path(pathbuf, baselen, false, results);
                }
            }
        }
        else
        {
            // Intermediate segment - must be a directory
            if (is_directory_path(pathbuf))
            {
                rbc_glob_match(pathbuf, new_len, baselen, pat_ptr, flags, results, sort);
            }
        }
    }

    rbc_closedir_sorted(entries);
}

/// @brief Callback for brace expansion
static void rbc_glob_brace_cb(const char *pat, void *arg)
{
    struct
    {
        const char *base;
        size_t baselen;
        unsigned flags;
        rbc_results_t *results;
        bool sort;
    } *ctx = arg;

    // For absolute paths, base contains the full path and baselen=0
    // We need to pass base as the starting path
    size_t base_path_len = ctx->baselen > 0 ? ctx->baselen : (ctx->base[0] != '\0' ? strlen(ctx->base) : 0);

    // Remove trailing slash from base_path_len for path construction
    // Exception: don't remove if it's the root path "/"
    if (base_path_len > 1 && ctx->base[base_path_len - 1] == '/')
        base_path_len--;

    rbc_glob_match(
        ctx->base,
        base_path_len,
        ctx->baselen,
        pat,
        ctx->flags,
        ctx->results,
        ctx->sort);
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
    if (!patterns || !out || !count || npatterns == 0)
        return false;

    // Initialize results (no context needed now)
    rbc_results_t results;
    results.capacity = RBC_RESULTS_CAPACITY;
    results.items = malloc(sizeof(char *) * results.capacity);
    results.lengths = malloc(sizeof(size_t) * results.capacity);
    results.count = 0;
    results.ctx = NULL;

    if (!results.items || !results.lengths)
    {
        free(results.items);
        free(results.lengths);
        return false;
    }

    // Calculate base length and normalize
    size_t baselen = 0;
    char normalized_base[RBC_GLOB_MAX_PATH] = "";

    if (base && base[0] != '\0')
    {
        baselen = strlen(base);
        // Copy base and ensure it ends with / for consistent path building
        if (baselen > 0 && baselen < RBC_GLOB_MAX_PATH - 1)
        {
            memcpy(normalized_base, base, baselen);
            // Remove trailing slashes, we'll add one separator when needed
            while (baselen > 0 && normalized_base[baselen - 1] == '/')
            {
                baselen--;
            }
            // Add exactly one trailing slash
            if (baselen > 0)
            {
                normalized_base[baselen] = '/';
                baselen++;
            }
            normalized_base[baselen] = '\0';
        }
    }

    // Process each pattern
    for (size_t i = 0; i < npatterns; i++)
    {
        const char *pattern = patterns[i];
        const char *pattern_base = baselen > 0 ? normalized_base : "";
        size_t pattern_baselen = baselen;
        char abs_base[RBC_GLOB_MAX_PATH] = "";
        const char *relative_pattern = pattern;

        // Handle absolute paths (pattern starts with '/')
        if (pattern[0] == '/')
        {
            // Extract the base directory from the absolute pattern
            // Find the first wildcard or end of path
            const char *p = pattern + 1;
            const char *last_slash = pattern;

            while (*p && !rbc_is_magic_char(*p))
            {
                if (*p == '/')
                    last_slash = p;
                p++;
            }

            // If we found a directory part, use it as base
            if (last_slash > pattern)
            {
                size_t len = last_slash - pattern;
                if (len < RBC_GLOB_MAX_PATH - 1)
                {
                    memcpy(abs_base, pattern, len);
                    abs_base[len] = '\0'; // No trailing slash
                    pattern_base = abs_base;
                    pattern_baselen = 0;               // Don't strip prefix - return absolute paths
                    relative_pattern = last_slash + 1; // Pattern after the base
                }
            }
            else
            {
                // Root directory with wildcard immediately (e.g., /*)
                abs_base[0] = '/';
                abs_base[1] = '\0';
                pattern_base = abs_base;
                pattern_baselen = 0; // Don't strip prefix
                relative_pattern = pattern + 1;
            }
        }

        // MRI behavior: fnmatch handles "." matching based on pattern and FNM_DOTMATCH
        // No need to set SKIPDOT at this level
        struct
        {
            const char *base;
            size_t baselen;
            unsigned flags;
            rbc_results_t *results;
            bool sort;
        } cb_ctx = {
            pattern_base,
            pattern_baselen,
            flags | RBC_INTERNAL_FIRST_CALL,
            &results,
            sort};

        // MRI-compatible: Fold continuous **/ patterns before processing
        // This prevents duplicate results in patterns like **/**/ or **/**/
        char normalized_pattern[RBC_GLOB_MAX_PATH];
        rbc_normalize_pattern(relative_pattern, normalized_pattern, sizeof(normalized_pattern));

        rbc_brace_expand(normalized_pattern, rbc_glob_brace_cb, &cb_ctx);
    }

    // Note: Sorting is done at directory level (MRI-compatible)
    // No global sort needed here

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
