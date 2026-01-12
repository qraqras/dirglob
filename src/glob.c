#include <rbc/rbc.h>
#include "internal.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>

static int rbc_compare_paths(const char *s1_in, const char *s2_in)
{
    /* P12 Optimization: Inline fast path for common cases */
    const unsigned char *s1 = (const unsigned char *)s1_in;
    const unsigned char *s2 = (const unsigned char *)s2_in;

    if (!s1_in || !s2_in)
        return (s1_in == s2_in) ? 0 : (s1_in ? 1 : -1);

    /* Fast path: check first characters */
    if (*s1 != *s2)
        return (int)*s1 - (int)*s2;
    if (*s1 == '\0')
        return 0;

    /* Fall back to strcmp for rest */
    return strcmp(s1_in, s2_in);
}

void rbc_ctx_init(rbc_ctx_t *ctx)
{
    rbc_arena_init(&ctx->arena, 0); /* Use default block size (128KB) */
    ctx->discovery_counter = 0;
}

void rbc_ctx_free(rbc_ctx_t *ctx)
{
    if (!ctx)
        return;
    rbc_arena_destroy(&ctx->arena);
}

/* P1 Optimization: Initial capacity for result array */
#define INITIAL_RESULT_CAPACITY 64

void rbc_results_init(rbc_results_t *results, rbc_ctx_t *ctx)
{
    /* P1-1: Pre-allocate result array to reduce realloc() calls */
    results->capacity = INITIAL_RESULT_CAPACITY;
    results->items = malloc(sizeof(char *) * results->capacity);
    results->lengths = malloc(sizeof(size_t) * results->capacity);
    results->discovery_indices = malloc(sizeof(size_t) * results->capacity);
    results->count = 0;
    results->ctx = ctx;

    /* Handle allocation failure gracefully */
    if (!results->items || !results->lengths || !results->discovery_indices)
    {
        free(results->items);
        free(results->lengths);
        free(results->discovery_indices);
        results->items = NULL;
        results->lengths = NULL;
        results->discovery_indices = NULL;
        results->capacity = 0;
    }
}

void rbc_results_clear(rbc_results_t *results)
{
    if (!results)
        return;
    /* P13: Strings are in arena, no need to free individually */
    /* Only free the arrays themselves */
    free(results->items);
    free(results->lengths);
    free(results->discovery_indices);
    results->items = NULL;
    results->lengths = NULL;
    results->discovery_indices = NULL;
    results->count = 0;
    results->capacity = 0;
}

int rbc_results_add_with_index(rbc_results_t *results, const char *path, size_t index)
{
    if (results->count >= results->capacity)
    {
        size_t new_cap = results->capacity ? results->capacity * 2 : 16;
        char **new_items = realloc(results->items, sizeof(char *) * new_cap);
        if (!new_items)
            return -1;
        results->items = new_items;
        size_t *new_lens = realloc(results->lengths, sizeof(size_t) * new_cap);
        if (!new_lens)
            return -1;
        results->lengths = new_lens;
        size_t *new_indices = realloc(results->discovery_indices, sizeof(size_t) * new_cap);
        if (!new_indices)
            return -1;
        results->discovery_indices = new_indices;
        results->capacity = new_cap;
    }
    /* P13: Use arena for result strings */
    const char *p = path ? path : ".";
    size_t len = strlen(p);
    results->items[results->count] = rbc_arena_alloc(&results->ctx->arena, len + 1);
    memcpy(results->items[results->count], p, len + 1);
    results->lengths[results->count] = len;
    results->discovery_indices[results->count] = index;
    results->count++;
    return 0;
}

int rbc_results_add(rbc_results_t *results, const char *path)
{
    // Pass 0 as index implicitly if not tracking discovery order for sort stability
    // (though new engine might not use discovery index in the same way)
    return rbc_results_add_with_index(results, path, results->ctx->discovery_counter++);
}

/* P10 Optimization: Helper structure for qsort() */
typedef struct rbc_traverse_sort_pair_s
{
    char *path;
    size_t length;
    size_t discovery_index;
} rbc_traverse_sort_pair_t;

/* P10: Comparison function for qsort() */
static int rbc_traverse_compare_sort_pairs(const void *a, const void *b)
{
    const rbc_traverse_sort_pair_t *pa = (const rbc_traverse_sort_pair_t *)a;
    const rbc_traverse_sort_pair_t *pb = (const rbc_traverse_sort_pair_t *)b;
    return rbc_compare_paths(pa->path, pb->path);
}

void rbc_results_sort(rbc_results_t *results)
{
    if (results->count <= 1)
        return;

    /* P10: Use qsort() instead of O(n²) bubble sort */
    rbc_traverse_sort_pair_t *pairs = malloc(sizeof(rbc_traverse_sort_pair_t) * results->count);
    if (!pairs)
        return; /* Fallback: keep unsorted */

    for (size_t i = 0; i < results->count; i++)
    {
        pairs[i].path = results->items[i];
        pairs[i].length = results->lengths[i];
        pairs[i].discovery_index = results->discovery_indices[i];
    }

    qsort(pairs, results->count, sizeof(rbc_traverse_sort_pair_t), rbc_traverse_compare_sort_pairs);

    for (size_t i = 0; i < results->count; i++)
    {
        results->items[i] = pairs[i].path;
        results->lengths[i] = pairs[i].length;
        results->discovery_indices[i] = pairs[i].discovery_index;
    }

    free(pairs);
}

void rbc_results_deduplicate(rbc_results_t *results)
{
    if (results->count <= 1)
        return;

    /* P11 Optimization: O(n) deduplication for sorted array
     * Only compare adjacent elements instead of O(n²) full scan */
    size_t write_idx = 1;
    for (size_t read_idx = 1; read_idx < results->count; read_idx++)
    {
        /* Compare only with previous element (array is sorted) */
        if (strcmp(results->items[read_idx], results->items[write_idx - 1]) != 0)
        {
            /* Different from previous - keep it */
            if (write_idx != read_idx)
            {
                results->items[write_idx] = results->items[read_idx];
                results->lengths[write_idx] = results->lengths[read_idx];
                results->discovery_indices[write_idx] = results->discovery_indices[read_idx];
            }
            write_idx++;
        }
    }
    results->count = write_idx;
}

/* Defines the opaque struct from types.h */

struct rbc_glob_pattern_s
{
    rbc_ctx_t *ctx;
    rbc_segment_t *segments;
    unsigned flags; /* Store flags during compilation if needed */
};

/* Result collection callback for Walker */
typedef struct
{
    rbc_results_t *results;
    const char *base_strip; /* If set, strip this prefix from results */
    size_t base_len;
} callback_ctx_t;

static void walker_match_callback(const char *path, void *user_data)
{
    callback_ctx_t *ctx = (callback_ctx_t *)user_data;

    const char *add_path = path;

    // Handle stripping base path to match Ruby's 'base:' behavior
    // If base_strip is "foo", and path is "foo/bar", result should be "bar".
    if (ctx->base_strip && ctx->base_len > 0)
    {
        if (strncmp(path, ctx->base_strip, ctx->base_len) == 0)
        {
            if (path[ctx->base_len] == '/')
            {
                add_path = path + ctx->base_len + 1;
            }
            else if (path[ctx->base_len] == '\0')
            {
                add_path = "."; // base itself
            }
        }
    }

    rbc_results_add(ctx->results, add_path);
}

rbc_glob_pattern_t *rbc_glob_compile(const char *pattern, unsigned flags);
void rbc_glob_pattern_free(rbc_glob_pattern_t *cg);

/* --- Glob Compilation (Internal) --- */

/// @brief Check if the string is a pure recursive wildcard "**"
static bool is_recursive_wildcard(const char *s)
{
    return strcmp(s, "**") == 0;
}

/// @brief Check if the string contains any unescaped wildcard characters
static bool rbc_has_wildcard(const char *str)
{
    bool esc = false;
    for (const char *p = str; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '*' || *p == '?' || *p == '[')
            return true;
    }
    return false;
}

/// @brief Check if the string contains any unescaped brace characters
static bool rbc_has_brace(const char *str)
{
    bool esc = false;
    for (const char *p = str; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '{')
            return true;
    }
    return false;
}

/// @brief Find the end of the current segment in the pattern
static const char *rbc_find_segment_end(const char *str)
{
    bool esc = false;
    int depth = 0;
    const char *p = str;
    while (*p)
    {
        if (esc)
        {
            esc = false;
            p++;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            p++;
            continue;
        }

        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            if (depth > 0)
                depth--;
        }
        else if (*p == '/' && depth == 0)
            return p;

        p++;
    }
    return p;
}

/// @brief Create a new rbc_segment_t of the specified type
static rbc_segment_t *rbc_segment_new(rbc_arena_t *arena, rbc_segment_type_t type)
{
    rbc_segment_t *seg = rbc_arena_alloc(arena, sizeof(rbc_segment_t));
    memset(seg, 0, sizeof(rbc_segment_t));
    seg->type = type;
    return seg;
}

/// @brief Compile pattern into segments
rbc_segment_t *rbc_compile_segments(rbc_arena_t *arena, const char *pattern, unsigned int flags)
{
    if (!pattern || !*pattern)
        return NULL;

    rbc_segment_t *head = NULL;
    rbc_segment_t *curr = NULL;

    const char *p = pattern;
    while (*p)
    {
        const char *end = rbc_find_segment_end(p);
        size_t len = end - p;
        if (len == 0)
        {
            if (*end == '/')
                p = end + 1;
            else
                p = end;
            continue;
        }

        char *component = rbc_arena_alloc(arena, len + 1);
        memcpy(component, p, len);
        component[len] = '\0';

        bool is_sep = (*end == '/');
        p = is_sep ? end + 1 : end;
        const char *rest = p;

        rbc_segment_t *seg = NULL;
        rbc_str_list_t expansions = rbc_brace_collect(component, arena);

        if (!rbc_has_brace(component) && is_recursive_wildcard(component))
        {
            seg = rbc_segment_new(arena, RBC_SEGMENT_RECURSIVE);
            rbc_str_list_free(&expansions);
            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            continue;
        }

        if (!rbc_has_brace(component) && !rbc_has_wildcard(component))
        {
            seg = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
            seg->data.literal = component;
            rbc_str_list_free(&expansions);
            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            continue;
        }

        bool any_slash = false;
        bool all_literals = true;
        for (size_t i = 0; i < expansions.count; i++)
        {
            if (strchr(expansions.items[i], '/'))
                any_slash = true;
            if (rbc_has_wildcard(expansions.items[i]))
                all_literals = false;
        }

        if (any_slash || all_literals || expansions.count > 1)
        {
            seg = rbc_segment_new(arena, RBC_SEGMENT_BRANCH);
            rbc_segment_t *last_alt = NULL;

            for (size_t i = 0; i < expansions.count; i++)
            {
                char *full_pattern;
                if (is_sep)
                    full_pattern = rbc_arena_printf(arena, "%s/%s", expansions.items[i], rest);
                else if (*rest)
                    full_pattern = rbc_arena_printf(arena, "%s%s", expansions.items[i], rest);
                else
                    full_pattern = rbc_arena_strdup(arena, expansions.items[i]);

                rbc_segment_t *alt_chain = NULL;
                if (all_literals && !is_sep && !*rest)
                {
                    alt_chain = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
                    alt_chain->data.literal = full_pattern;
                }
                else
                {
                    alt_chain = rbc_compile_segments(arena, full_pattern, flags);
                }

                if (!alt_chain && !*full_pattern)
                {
                    alt_chain = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
                    alt_chain->data.literal = "";
                }

                if (alt_chain)
                {
                    if (!seg->data.branch.head)
                        seg->data.branch.head = alt_chain;
                    else if (last_alt)
                        last_alt->next_alt = alt_chain;
                    last_alt = alt_chain;
                }
            }
            rbc_str_list_free(&expansions);

            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            break;
        }
        else
        {
            seg = rbc_segment_new(arena, RBC_SEGMENT_WILDCARD);
            seg->data.glob.original_pattern = rbc_arena_strdup(arena, expansions.items[0]);
            rbc_matcher_build(arena, &seg->data.glob.matcher, seg->data.glob.original_pattern, flags);
            rbc_str_list_free(&expansions);

            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
        }
    }
    return head;
}

rbc_glob_pattern_t *rbc_glob_compile(const char *pattern, unsigned flags)
{
    if (!pattern)
        return NULL;

    rbc_glob_pattern_t *cg = malloc(sizeof(rbc_glob_pattern_t));
    if (!cg)
        return NULL;

    cg->ctx = malloc(sizeof(rbc_ctx_t));
    if (!cg->ctx)
    {
        free(cg);
        return NULL;
    }

    rbc_ctx_init(cg->ctx);
    cg->flags = flags;

    cg->segments = rbc_compile_segments(&cg->ctx->arena, pattern, flags);

    if (!cg->segments)
    {
        rbc_glob_pattern_free(cg);
        return NULL;
    }

    return cg;
}

void rbc_glob_pattern_free(rbc_glob_pattern_t *cg)
{
    if (!cg)
        return;
    if (cg->ctx)
    {
        rbc_ctx_free(cg->ctx);
        free(cg->ctx);
    }
    free(cg);
}

bool rbc_xglob(const rbc_glob_pattern_t *cg, const char *base, bool sort,
               char ***out, size_t *count, size_t **lengths)
{
    if (!cg || !out || !count)
        return false;

    rbc_ctx_t *run_ctx = malloc(sizeof(rbc_ctx_t));
    if (!run_ctx)
        return false;
    rbc_ctx_init(run_ctx);

    rbc_results_t results;
    rbc_results_init(&results, run_ctx);

    callback_ctx_t cb_ctx;
    cb_ctx.results = &results;

    // Always set base_strip. Even for ".", we want to strip logical prefix "./"
    // which is now produced by executor.
    cb_ctx.base_strip = base;
    cb_ctx.base_len = base ? strlen(base) : 0;

    rbc_segments_exec(cg->segments, base, cg->flags, sort, walker_match_callback, &cb_ctx);

    if (sort)
    {
        rbc_results_sort(&results);
    }
    rbc_results_deduplicate(&results);

    // Packaging
    *count = results.count;
    void **package = malloc(sizeof(void *) + (results.count + 1) * sizeof(char *));
    if (!package)
    {
        rbc_results_clear(&results);
        rbc_ctx_free(run_ctx);
        free(run_ctx);
        return false;
    }

    package[0] = run_ctx;
    char **pkg_items = (char **)&package[1];
    if (results.count > 0)
    {
        memcpy(pkg_items, results.items, results.count * sizeof(char *));
    }
    pkg_items[results.count] = NULL;

    *out = pkg_items;
    if (lengths)
        *lengths = results.lengths;
    else if (results.lengths)
        free(results.lengths);

    if (results.discovery_indices)
        free(results.discovery_indices);

    return true;
}

/* Context for fast path visitor */
typedef struct
{
    rbc_results_t *results;
    rbc_ctx_t *ctx;
    const char *base;
    unsigned flags;
    bool sort;
    callback_ctx_t *cb_ctx;
} fast_path_ctx_t;

static void fast_path_visitor(const char *p, void *arg)
{
    fast_path_ctx_t *fp_ctx = (fast_path_ctx_t *)arg;

    // Check if remaining string has wildcards (escaped braces? etc)
    if (strpbrk(p, "*?[]{}\\") == NULL)
    {
        // Pure literal
        char full_path[4096];
        int needed;
        const char *base = fp_ctx->base;

        if (base && strcmp(base, ".") != 0)
        {
            needed = snprintf(full_path, sizeof(full_path), "%s/%s", base, p);
        }
        else
        {
            needed = snprintf(full_path, sizeof(full_path), "%s", p);
        }

        if (needed < 0 || (size_t)needed >= sizeof(full_path))
        {
            // Too long, fallback to graph? Or just abort.
            // If too long for stack buffer, NFA won't help much with stat unless it handles long paths logic differently.
            return;
        }

        struct stat st;
        if (stat(full_path, &st) == 0)
        {
            size_t plen = strlen(p);
            if (plen > 0 && p[plen - 1] == '/')
            {
                if (!S_ISDIR(st.st_mode))
                    return;
            }
            rbc_results_add(fp_ctx->results, p);
        }
    }
    else
    {
        // Still has wildcards? Compile and run walker.
        rbc_segment_t *segments = rbc_compile_segments(&fp_ctx->ctx->arena, p, fp_ctx->flags);
        if (segments)
        {
            rbc_segments_exec(segments, fp_ctx->base, fp_ctx->flags, fp_ctx->sort, walker_match_callback, fp_ctx->cb_ctx);
        }
    }
}

bool rbc_glob(const char **patterns, size_t npatterns, unsigned flags,
              const char *base, bool sort, char ***out, size_t *count, size_t **lengths)
{

    if (!out || !count)
        return false;

    rbc_ctx_t *ctx = malloc(sizeof(rbc_ctx_t));
    if (!ctx)
        return false;
    rbc_ctx_init(ctx);

    rbc_results_t results;
    rbc_results_init(&results, ctx);

    callback_ctx_t cb_ctx;
    cb_ctx.results = &results;

    cb_ctx.base_strip = base;
    cb_ctx.base_len = base ? strlen(base) : 0;

    for (size_t i = 0; i < npatterns; i++)
    {
        if (!patterns[i])
            continue;

        const char *current_pattern = patterns[i];

        if (strpbrk(current_pattern, "*?[]{}\\") == NULL)
        {
            // Pure literal: Fast path
            fast_path_ctx_t fp_ctx = {&results, ctx, base, flags, sort, &cb_ctx};
            fast_path_visitor(current_pattern, &fp_ctx);
        }
        else if (strchr(current_pattern, '{') != NULL && strpbrk(current_pattern, "*?[]") == NULL)
        {
            // Pure braces: Use visitor
            fast_path_ctx_t fp_ctx = {&results, ctx, base, flags, sort, &cb_ctx};
            rbc_brace_visit(current_pattern, &ctx->arena, fast_path_visitor, &fp_ctx);
        }
        else
        {
            // Standard path or mixed wildcard-brace
            // If mixed, we pass directly to compile segments which handles braces via segments or expansion internally
            rbc_segment_t *segments = rbc_compile_segments(&ctx->arena, current_pattern, flags);
            if (segments)
            {
                rbc_segments_exec(segments, base, flags, sort, walker_match_callback, &cb_ctx);
            }
        }
    }

    if (sort)
    {
        rbc_results_sort(&results);
    }
    rbc_results_deduplicate(&results);

    // Packaging
    *count = results.count;
    void **package = malloc(sizeof(void *) + (results.count + 1) * sizeof(char *));
    if (!package)
    {
        rbc_results_clear(&results);
        rbc_ctx_free(ctx);
        free(ctx);
        return false;
    }

    package[0] = ctx;
    char **pkg_items = (char **)&package[1];
    if (results.count > 0)
    {
        memcpy(pkg_items, results.items, results.count * sizeof(char *));
    }
    pkg_items[results.count] = NULL;

    *out = pkg_items;
    if (lengths)
        *lengths = results.lengths;
    else if (results.lengths)
        free(results.lengths);

    if (results.items)
        free(results.items);
    if (results.discovery_indices)
        free(results.discovery_indices);

    return true;
}

void rbc_glob_free(char **list, size_t count, size_t *lengths)
{
    (void)count;
    if (!list)
        return;
    void **package = (void **)list - 1;
    rbc_ctx_t *ctx = (rbc_ctx_t *)package[0];
    rbc_ctx_free(ctx);
    free(ctx);
    free(package);
    if (lengths)
        free(lengths);
}
