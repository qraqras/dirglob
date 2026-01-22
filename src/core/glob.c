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
#define RBC_INTERNAL_IN_DOUBLESTAR 0x80000000  // Currently recursing from **
#define RBC_GLOB_SKIPDOT           0x40000000  // Skip "." entry (set for all intermediate segments)

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
    else if (char_flags == SEG_HAS_STAR && pattern_len >= 2)
    {
        // ** pattern detected - check if terminal or recursive
        // MRI behavior:
        // ** (no slash, no following) → same as * (RBC_SEG_ANY)
        // **/ (with slash) → recursive directories (RBC_SEG_RECURSIVE)
        // **/*.txt (with following pattern) → recursive (RBC_SEG_RECURSIVE)

        if (*p == '\0')
        {
            // Terminal ** with NO trailing slash: treat as *
            seg->type = RBC_SEG_ANY;
        }
        else
        {
            // ** with trailing slash or following pattern: recursive
            seg->type = RBC_SEG_RECURSIVE;
        }
    }
    else if (char_flags == SEG_HAS_STAR && pattern_len == 1)
    {
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

    // Count trailing slashes
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

            // Expand each option
            const char *opt_start = p + 1;
            const char *opt_end = opt_start;

            while (opt_end < close)
            {
                if (*opt_end == ',')
                {
                    // Found an option
                    size_t opt_len = opt_end - opt_start;
                    memcpy(buf + buf_pos, opt_start, opt_len);
                    buf[buf_pos + opt_len] = '\0';

                    // Recursively expand rest
                    rbc_brace_expand_impl(close + 1, buf, buf_pos + opt_len, cb, arg);

                    opt_start = opt_end + 1;
                }
                opt_end++;
            }

            // Last option
            size_t opt_len = close - opt_start;
            memcpy(buf + buf_pos, opt_start, opt_len);
            buf[buf_pos + opt_len] = '\0';
            rbc_brace_expand_impl(close + 1, buf, buf_pos + opt_len, cb, arg);

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
// Glob Core Logic (MRI-compatible)
// ============================================================================

bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags); // External

/// @brief Check if filename should be skipped based on dot-file rules (MRI-compatible)
/// @param name Filename to check
/// @param pattern_starts_with_dot Whether the current pattern segment starts with '.'
/// @param flags Glob flags
/// @return true if the file should be skipped
///
/// MRI behavior:
/// - FNM_DOTMATCH: If set, wildcards can match leading dots
/// - Without FNM_DOTMATCH: Wildcards (* ? []) don't match leading dots
/// - Explicit dot in pattern: Always matches dot files (e.g., .* or .hidden)
static inline bool rbc_should_skip_dotfile(
    const char *name,
    bool pattern_starts_with_dot,
    unsigned flags)
{
    // Never skip if name doesn't start with dot
    if (name[0] != '.')
        return false;

    // Always skip "." and ".."
    if (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))
        return true;

    // If pattern explicitly starts with '.', don't skip
    // This allows patterns like .*, .hidden, .???, etc. to match dot files
    if (pattern_starts_with_dot)
        return false;

    // Pattern doesn't start with '.'
    // If FNM_DOTMATCH is set, wildcards can match leading dots
    if (flags & RBC_FNM_DOTMATCH)
        return false;

    // Otherwise, skip dot files (wildcards don't match leading dots)
    return true;
}

/// @brief Glob recursively with streaming pattern analysis
/// @param path Current full path for filesystem operations
/// @param path_len Length of current path
/// @param baselen Length of base directory prefix (excluded from results)
/// @param pattern Remaining pattern to match
static void rbc_glob_recursive(
    const char *path,
    size_t path_len,
    size_t baselen,
    const char *pattern,
    unsigned flags,
    rbc_results_t *results)
{
    rbc_segment_t seg;
    const char *pat_ptr = pattern;

    // Get next segment
    if (!rbc_next_segment(&pat_ptr, &seg))
    {
        // No more segments - check if path exists
        struct stat st;
        const char *check_path = (path_len > 0) ? path : ".";
        if (stat(check_path, &st) == 0)
        {
            // Add result without base prefix
            const char *result = path + baselen;
            // Skip leading slashes
            while (*result == '/')
                result++;
            if (*result == '\0')
                result = ".";
            rbc_glob_results_add(results, result);
        }
        return;
    }

    // Handle "**" (recursive wildcard)
    if (seg.type == RBC_SEG_RECURSIVE)
    {
        // Check if this is **/ (directories only) by checking if no pattern follows
        bool is_dirs_only = (*pat_ptr == '\0' && seg.trailing_slashes > 0);

        // Skip the separator after **
        if (*pat_ptr == '/')
            pat_ptr++;

        // Check if the next segment starts with '.' to determine dot file handling
        rbc_segment_t next_seg;
        const char *next_pat_ptr = pat_ptr;
        bool next_starts_with_dot = false;
        if (rbc_next_segment(&next_pat_ptr, &next_seg))
        {
            next_starts_with_dot = next_seg.starts_with_dot;
        }

        // Match current directory first with the remaining pattern (after **)
        // For **/, we need to add the current directory with trailing slash
        if (is_dirs_only)
        {
            // Add current directory with trailing slash
            const char *result = path + baselen;
            while (*result == '/')
                result++;
            if (*result == '\0')
                result = ".";

            char result_with_slash[RBC_GLOB_MAX_PATH];
            snprintf(result_with_slash, sizeof(result_with_slash), "%s/", result);
            rbc_glob_results_add(results, result_with_slash);
        }
        else
        {
            // Normal recursive match with remaining pattern
            rbc_glob_recursive(path, path_len, baselen, pat_ptr, flags, results);
        }

        // Recursively descend into subdirectories
        // MRI behavior: ** continues to match in all subdirectories
        DIR *dir = opendir((path_len > 0) ? path : ".");
        if (!dir)
            return;

        struct dirent *entry;
        char pathbuf[RBC_GLOB_MAX_PATH];

        while ((entry = readdir(dir)) != NULL)
        {
            const char *name = entry->d_name;

            // Always skip "." and ".."
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
                continue;

            // Apply dot-file filtering using the same logic as normal segments
            // Check if we should skip this dot directory based on the next pattern segment
            if (rbc_should_skip_dotfile(name, next_starts_with_dot, flags))
                continue;

            // Build path
            size_t new_len;
            if (path_len > 0)
            {
                new_len = snprintf(pathbuf, sizeof(pathbuf), "%s/%s", path, name);
            }
            else
            {
                new_len = snprintf(pathbuf, sizeof(pathbuf), "%s", name);
            }

            // Check if it's a directory
            struct stat st;
            if (stat(pathbuf, &st) == 0 && S_ISDIR(st.st_mode))
            {
                // Continue ** recursion: pass the ORIGINAL pattern (with **) to subdirectories
                // This allows ** to match multiple directory levels
                rbc_glob_recursive(pathbuf, new_len, baselen, pattern,
                                   flags | RBC_INTERNAL_IN_DOUBLESTAR, results);
            }
        }

        closedir(dir);
        return;
    }

    // Handle normal segment (literal or wildcard)
    DIR *dir = opendir((path_len > 0) ? path : ".");
    if (!dir)
        return;

    struct dirent *entry;
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

    // Check if current segment is literal "."
    bool is_literal_dot = (seg.type == RBC_SEG_LITERAL && seg.len == 1 && pattern_buf[0] == '.');

    // MRI behavior: Skip "." in subdirectories, but include it at the base level
    // At base level (path_len == baselen), don't set SKIPDOT
    // In subdirectories (path_len > baselen), set SKIPDOT
    bool skipdot = (flags & RBC_GLOB_SKIPDOT) != 0;
    unsigned local_flags = flags;
    if (path_len > baselen) {
        local_flags |= RBC_GLOB_SKIPDOT;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;

        // Always skip ".."
        if (name[0] == '.' && name[1] == '.' && name[2] == '\0')
            continue;

        // Skip "." in specific cases
        if (name[0] == '.' && name[1] == '\0')
        {
            // If we're in a ** recursion (subdirectory), skip "."
            if (flags & RBC_INTERNAL_IN_DOUBLESTAR)
                continue;

            // If RBC_GLOB_SKIPDOT was set in the incoming flags, skip "."
            // This matches MRI: skipdot controls whether "." is visible
            if (skipdot)
                continue;

            // MRI behavior: If pattern explicitly starts with '.', include "."
            // This allows patterns like .*, .???, etc. to match "."
            if (!seg.starts_with_dot)
            {
                // Pattern doesn't start with '.', skip "." unless FNM_DOTMATCH is set
                if (!(flags & RBC_FNM_DOTMATCH))
                    continue;
            }
            // If pattern starts with '.' or FNM_DOTMATCH is set, include "." (will be matched below)
        }
        else
        {
            // Apply dot-file filtering for other dot files (not "." or "..")
            if (rbc_should_skip_dotfile(name, seg.starts_with_dot, flags))
                continue;
        }

        // Match against pattern
        bool matched = false;
        switch (seg.type)
        {
        case RBC_SEG_LITERAL:
            // Exact string match
            matched = (strcmp(pattern_buf, name) == 0);
            break;
        case RBC_SEG_ANY:
        case RBC_SEG_MAGICAL:
            // Wildcard match using fnmatch (use original flags, not local_flags)
            matched = rbc_fnmatch(pattern_buf, name, flags);
            break;
        case RBC_SEG_BRACE:
            // Should not happen (brace expansion done earlier)
            // Treat as literal fallback
            matched = (strcmp(pattern_buf, name) == 0);
            break;
        case RBC_SEG_RECURSIVE:
            // Should not reach here (handled above)
            continue;
        }

        if (!matched)
            continue;

        // Build new path
        size_t new_len;
        if (path_len > 0)
        {
            new_len = snprintf(pathbuf, sizeof(pathbuf), "%s/%s", path, name);
        }
        else
        {
            new_len = snprintf(pathbuf, sizeof(pathbuf), "%s", name);
        }

        // Check if more segments remain
        if (*pat_ptr == '\0')
        {
            // Last segment - add result
            struct stat st;
            if (stat(pathbuf, &st) == 0)
            {
                // If segment has trailing slashes, only match directories
                if (seg.trailing_slashes > 0 && !S_ISDIR(st.st_mode))
                    continue;

                // Add result without base prefix
                const char *result = pathbuf + baselen;
                // Skip leading slash if present
                while (*result == '/')
                    result++;
                if (*result == '\0')
                    result = ".";

                // If segment had trailing slashes, append one to the result
                if (seg.trailing_slashes > 0)
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
        }
        else
        {
            // More segments - must be a directory
            struct stat st;
            if (stat(pathbuf, &st) == 0 && S_ISDIR(st.st_mode))
            {
                // When entering literal "." directory, clear SKIPDOT for the next level
                // This allows patterns like "02_asterisk/./*" to see "./."
                // But for wildcards matching ".", keep SKIPDOT set
                unsigned recurse_flags = local_flags;
                if (is_literal_dot && name[0] == '.' && name[1] == '\0') {
                    recurse_flags &= ~RBC_GLOB_SKIPDOT;
                }
                rbc_glob_recursive(pathbuf, new_len, baselen, pat_ptr, recurse_flags, results);
            }
        }
    }

    closedir(dir);
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
    } *ctx = arg;

    rbc_glob_recursive(
        ctx->base,
        ctx->baselen,
        ctx->baselen,
        pat,
        ctx->flags,
        ctx->results);
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
        struct
        {
            const char *base;
            size_t baselen;
            unsigned flags;
            rbc_results_t *results;
        } cb_ctx = {
            baselen > 0 ? normalized_base : "",
            baselen,
            flags,
            &results};

        rbc_brace_expand(patterns[i], rbc_glob_brace_cb, &cb_ctx);
    }

    // Sort results if requested
    if (sort)
        rbc_glob_results_sort(&results);

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
