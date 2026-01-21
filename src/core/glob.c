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
typedef struct
{
    char **items;
    size_t *lengths;
    size_t count;
    size_t capacity;
    void *ctx; // Unused, for compatibility
} rbc_results_t;

/// @brief Pattern segment information (for streaming)
typedef struct
{
    const char *start;  // Segment start pointer
    size_t len;         // Segment length
    bool has_magic;     // Contains wildcards
    bool is_doublestar; // Is "**"
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

    // Skip leading slashes
    while (*p == '/')
        p++;

    if (*p == '\0')
    {
        *pattern = p;
        return false;
    }

    seg->start = p;
    seg->has_magic = false;
    seg->is_doublestar = false;

    // Scan segment until '/' or end
    const char *seg_start = p;
    while (*p && *p != '/')
    {
        if (rbc_is_magic_char(*p))
            seg->has_magic = true;
        p++;
    }

    seg->len = p - seg_start;

    // Check for "**"
    if (seg->len == 2 && seg_start[0] == '*' && seg_start[1] == '*')
    {
        seg->is_doublestar = true;
        seg->has_magic = false; // "**" is special, not ordinary magic
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
static void rbc_brace_expand_impl(const char *pattern, char *buf, size_t buf_pos,
                                  void (*cb)(const char *, void *), void *arg)
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

static void rbc_brace_expand(const char *pattern, void (*cb)(const char *, void *), void *arg)
{
    char buf[RBC_GLOB_MAX_PATH];
    rbc_brace_expand_impl(pattern, buf, 0, cb, arg);
}

// ============================================================================
// Glob Core Logic (MRI-compatible)
// ============================================================================

bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags); // External

/// @brief Check if filename should be skipped based on dot-file rules
static inline bool rbc_should_skip_dotfile(const char *name, const char *pattern,
                                           size_t pattern_len, unsigned flags)
{
    // MRI behavior: if FNM_DOTMATCH is set, match dot files
    if (flags & RBC_FNM_DOTMATCH)
        return false;

    // Skip "." and ".."
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
        return true;

    // If file starts with '.' and pattern doesn't, skip
    if (name[0] == '.' && pattern[0] != '.')
        return true;

    return false;
}

/// @brief Glob recursively with streaming pattern analysis
static void rbc_glob_recursive(const char *base, size_t base_len,
                               const char *pattern, unsigned flags,
                               rbc_results_t *results)
{
    rbc_segment_t seg;
    const char *pat_ptr = pattern;

    // Get next segment
    if (!rbc_next_segment(&pat_ptr, &seg))
    {
        // No more segments - check if path exists
        struct stat st;
        const char *check_path = (base_len > 0) ? base : ".";
        if (stat(check_path, &st) == 0)
        {
            rbc_glob_results_add(results, base_len > 0 ? base : ".");
        }
        return;
    }

    // Handle "**" (recursive wildcard)
    if (seg.is_doublestar)
    {
        // Match current directory
        rbc_glob_recursive(base, base_len, pat_ptr, flags, results);

        // Recursively descend into subdirectories
        DIR *dir = opendir((base_len > 0) ? base : ".");
        if (!dir)
            return;

        struct dirent *entry;
        char pathbuf[RBC_GLOB_MAX_PATH];

        while ((entry = readdir(dir)) != NULL)
        {
            // Skip dot entries
            const char *name = entry->d_name;
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
                continue;

            // Skip hidden files unless explicitly matched
            if (name[0] == '.' && !(flags & RBC_FNM_DOTMATCH))
                continue;

            // Build path
            size_t new_len;
            if (base_len > 0)
            {
                new_len = snprintf(pathbuf, sizeof(pathbuf), "%s/%s", base, name);
            }
            else
            {
                new_len = snprintf(pathbuf, sizeof(pathbuf), "%s", name);
            }

            // Check if it's a directory
            struct stat st;
            if (stat(pathbuf, &st) == 0 && S_ISDIR(st.st_mode))
            {
                // Recursively glob from subdirectory
                rbc_glob_recursive(pathbuf, new_len, pattern, flags, results);
            }
        }

        closedir(dir);
        return;
    }

    // Handle normal segment (literal or wildcard)
    DIR *dir = opendir((base_len > 0) ? base : ".");
    if (!dir)
        return;

    struct dirent *entry;
    char pathbuf[RBC_GLOB_MAX_PATH];
    char pattern_buf[RBC_GLOB_MAX_PATH];

    // Copy segment to null-terminated buffer for fnmatch
    memcpy(pattern_buf, seg.start, seg.len);
    pattern_buf[seg.len] = '\0';

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;

        // Skip "." and ".."
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        // Apply dot-file filtering
        if (rbc_should_skip_dotfile(name, pattern_buf, seg.len, flags))
            continue;

        // Match against pattern
        bool matched = false;
        if (seg.has_magic)
        {
            matched = rbc_fnmatch(pattern_buf, name, flags);
        }
        else
        {
            // Literal match
            matched = (strcmp(pattern_buf, name) == 0);
        }

        if (!matched)
            continue;

        // Build new path
        size_t new_len;
        if (base_len > 0)
        {
            new_len = snprintf(pathbuf, sizeof(pathbuf), "%s/%s", base, name);
        }
        else
        {
            new_len = snprintf(pathbuf, sizeof(pathbuf), "%s", name);
        }

        // Check if more segments remain
        if (*pat_ptr == '\0' || (*pat_ptr == '/' && *(pat_ptr + 1) == '\0'))
        {
            // Last segment - add result
            struct stat st;
            if (stat(pathbuf, &st) == 0)
            {
                rbc_glob_results_add(results, pathbuf);
            }
        }
        else
        {
            // More segments - must be a directory
            struct stat st;
            if (stat(pathbuf, &st) == 0 && S_ISDIR(st.st_mode))
            {
                rbc_glob_recursive(pathbuf, new_len, pat_ptr, flags, results);
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
        unsigned flags;
        rbc_results_t *results;
    } *ctx = arg;

    rbc_glob_recursive(ctx->base, ctx->base ? strlen(ctx->base) : 0,
                       pat, ctx->flags, ctx->results);
}

// ============================================================================
// Public API
// ============================================================================

bool rbc_glob(const char **patterns, size_t npatterns, unsigned flags,
              const char *base, bool sort, char ***out, size_t *count,
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

    // Process each pattern
    for (size_t i = 0; i < npatterns; i++)
    {
        struct
        {
            const char *base;
            unsigned flags;
            rbc_results_t *results;
        } cb_ctx = {
            base ? base : "",
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
    {
        if (list[i])
            free(list[i]);
    }

    // Free the arrays
    free(list);
    if (lengths)
        free(lengths);
}
