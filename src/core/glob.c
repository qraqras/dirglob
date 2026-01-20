#include <rbc/rbc.h>
#include "rbc/glob_hints.h"
#include "internal.h"
#include "../utils/utils.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>

/// @name Context
/// @{

/// @brief Initialize the context
/// @param ctx Context to initialize
/// @return true on success, false on failure
bool rbc_glob_ctx_init(rbc_ctx_t *ctx)
{
    if (!rbc_arena_init(&ctx->arena, 0))
        return false;
    ctx->discovery_counter = 0;
    return true;
}

/// @brief Free the context
/// @param ctx Context to free
void rbc_glob_ctx_free(rbc_ctx_t *ctx)
{
    if (!ctx)
    {
        return;
    }
    rbc_arena_destroy(&ctx->arena);
}

/// @}

/// @name Results
/// @{

/// @brief Initial capacity for results
#define RBC_RESULTS_CAPACITY 64

/// @brief Initialize results structure
/// @param results Results structure to initialize
/// @param ctx Context for arena access
/// @return true on success, false on failure
bool rbc_glob_results_init(rbc_results_t *results, rbc_ctx_t *ctx)
{
    results->capacity = RBC_RESULTS_CAPACITY;
    results->items = malloc(sizeof(char *) * results->capacity);
    results->lengths = malloc(sizeof(size_t) * results->capacity);
    results->discovery_indices = malloc(sizeof(size_t) * results->capacity);
    results->count = 0;
    results->ctx = ctx;

    if (!results->items || !results->lengths || !results->discovery_indices)
    {
        free(results->items);
        free(results->lengths);
        free(results->discovery_indices);
        results->items = NULL;
        results->lengths = NULL;
        results->discovery_indices = NULL;
        results->capacity = 0;
        return false;
    }
    return true;
}

/// @brief Clear results structure and free memory
/// @param results Results structure to clear
void rbc_glob_results_clear(rbc_results_t *results)
{
    if (!results)
    {
        return;
    }
    free(results->items);
    free(results->lengths);
    free(results->discovery_indices);
    results->items = NULL;
    results->lengths = NULL;
    results->discovery_indices = NULL;
    results->count = 0;
    results->capacity = 0;
}

/// @brief Add s1_in path to the results
/// @param results Results structure
/// @param path Path to add
/// @return true on success, false on failure
bool rbc_glob_results_add(rbc_results_t *results, const char *path)
{
    return rbc_glob_results_add_with_index(results, path, results->ctx->discovery_counter++);
}

/// @brief Add s1_in path with discovery index to the results
/// @param results Results structure
/// @param path Path to add
/// @param index Discovery index
/// @return true on success, false on failure
bool rbc_glob_results_add_with_index(rbc_results_t *results, const char *path, size_t index)
{
    if (results->count >= results->capacity)
    {
        size_t new_cap = results->capacity ? results->capacity * 2 : 16;
        char **new_items = realloc(results->items, sizeof(char *) * new_cap);
        if (!new_items)
        {
            return false;
        }
        results->items = new_items;
        size_t *new_lens = realloc(results->lengths, sizeof(size_t) * new_cap);
        if (!new_lens)
        {
            return false;
        }
        results->lengths = new_lens;
        size_t *new_indices = realloc(results->discovery_indices, sizeof(size_t) * new_cap);
        if (!new_indices)
        {
            return false;
        }
        results->discovery_indices = new_indices;
        results->capacity = new_cap;
    }
    const char *p = path ? path : ".";
    size_t len = strlen(p);
    results->items[results->count] = rbc_arena_alloc(&results->ctx->arena, len + 1);
    memcpy(results->items[results->count], p, len + 1);
    results->lengths[results->count] = len;
    results->discovery_indices[results->count] = index;
    results->count++;
    return true;
}

/// @brief Sort results lexicographically
typedef struct rbc_glob_results_sort_item_s
{
    char *path;
    size_t length;
    size_t discovery_index;
} rbc_glob_results_sort_item_t;

/// @brief Compare two paths for sorting
/// @param s1_in First path
/// @param s2_in Second path
/// @return Negative if s1 < s2, positive if s1 > s2, zero if equal
static int rbc_glob_results_path_cmp(const char *s1_in, const char *s2_in)
{
    const unsigned char *s1 = (const unsigned char *)s1_in;
    const unsigned char *s2 = (const unsigned char *)s2_in;

    if (!s1_in || !s2_in)
    {
        return (s1_in == s2_in) ? 0 : (s1_in ? 1 : -1);
    }

    while (*s1 && *s2)
    {
        if (*s1 != *s2)
        {
            // Ruby quirk: treat '/' as smaller than other characters (e.g. '.')
            if (*s1 == '/')
                return -1;
            if (*s2 == '/')
                return 1;
            return (*s1 < *s2) ? -1 : 1;
        }
        s1++;
        s2++;
    }

    if (*s1 == *s2)
        return 0;
    return (*s1 == '\0') ? -1 : 1;
}

/// @brief Comparison function for qsort
/// @param i1 First item
/// @param i2 Second item
/// @return Comparison result
static int rbc_glob_results_sort_cmp(const void *i1, const void *i2)
{
    const rbc_glob_results_sort_item_t *pi1 = (const rbc_glob_results_sort_item_t *)i1;
    const rbc_glob_results_sort_item_t *pi2 = (const rbc_glob_results_sort_item_t *)i2;
    return rbc_glob_results_path_cmp(pi1->path, pi2->path);
}

/// @brief Sort results lexicographically
/// @param results Results structure
void rbc_glob_results_sort(rbc_results_t *results)
{
    if (results->count <= 1)
    {
        return;
    }

    rbc_glob_results_sort_item_t *pairs = malloc(sizeof(rbc_glob_results_sort_item_t) * results->count);
    if (!pairs)
    {
        return;
    }

    for (size_t i = 0; i < results->count; i++)
    {
        pairs[i].path = results->items[i];
        pairs[i].length = results->lengths[i];
        pairs[i].discovery_index = results->discovery_indices[i];
    }

    qsort(pairs, results->count, sizeof(rbc_glob_results_sort_item_t), rbc_glob_results_sort_cmp);

    for (size_t i = 0; i < results->count; i++)
    {
        results->items[i] = pairs[i].path;
        results->lengths[i] = pairs[i].length;
        results->discovery_indices[i] = pairs[i].discovery_index;
    }

    free(pairs);
}

/// @brief Deduplicate results (removes duplicates while preserving order)
/// @param results Results structure

typedef struct
{
    size_t original_index;
    const char *path;
} rbc_dedup_item_t;

static int rbc_dedup_cmp(const void *a, const void *b)
{
    const rbc_dedup_item_t *ia = (const rbc_dedup_item_t *)a;
    const rbc_dedup_item_t *ib = (const rbc_dedup_item_t *)b;
    int c = strcmp(ia->path, ib->path);
    if (c != 0)
    {
        return c;
    }
    return (ia->original_index < ib->original_index) ? -1 : 1;
}

void rbc_glob_results_deduplicate(rbc_results_t *results)
{
    // MRI does not deduplicate results.
    // Duplicates from brace expansion are preserved.
    // Duplicates from walker (recursion) should be fixed in walker logic.
    return;

    // ... (Old Logic Removed or Commented Out) ...
}

/// @}

/// @name Glob Segment Functions
/// @{

/// @brief Find the end of the current segment in the pattern
/// @param pattern Pattern string
/// @return Pointer to the end of the segment (either '/' or '\0')
static const char *rbc_glob_segment_find_end(const char *pattern)
{
    bool esc = false;
    int depth = 0;
    const char *p = pattern;
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

/// @brief Create a new glob segment
/// @param arena Arena to allocate from
/// @param type Segment type
/// @return Pointer to the new segment
static rbc_segment_t *rbc_glob_segment_new(rbc_arena_t *arena, rbc_segment_type_t type)
{
    rbc_segment_t *seg = rbc_arena_alloc(arena, sizeof(rbc_segment_t));
    memset(seg, 0, sizeof(rbc_segment_t));
    seg->type = type;
    return seg;
}

/// @brief Compile pattern into glob segments
/// @param arena Arena to allocate from
/// @param pattern Pattern string
/// @param flags Compilation flags
/// @return Pointer to the head segment, or NULL on failure
rbc_segment_t *rbc_glob_segment_compile(rbc_arena_t *arena, const char *pattern, unsigned int flags)
{
    if (!pattern || !*pattern)
    {
        return NULL;
    }

    rbc_segment_t *head = NULL;
    rbc_segment_t *curr = NULL;

    const char *p = pattern;
    while (*p)
    {
        const char *end = rbc_glob_segment_find_end(p);
        size_t len = end - p;

        if (len == 0 && *end == '/')
        {
            if (head == NULL)
            {
                // Leading slash
                rbc_segment_t *root_seg = rbc_glob_segment_new(arena, RBC_SEGMENT_LITERAL);
                root_seg->data.literal = "/";
                head = root_seg;
                curr = root_seg;
            }
            else if (curr->type == RBC_SEGMENT_RECURSIVE)
            {
                // Collapse slash after **
                p = end + 1;
                continue;
            }
            else
            {
                // Middle extra slash. Turn into a literal "/" segment.
                // Our buf_append will handle this to produce //
                rbc_segment_t *sep_seg = rbc_glob_segment_new(arena, RBC_SEGMENT_LITERAL);
                sep_seg->data.literal = "/";
                curr->next = sep_seg;
                curr = sep_seg;
            }
            p = end + 1;
            continue;
        }

        if (len == 0 && *end != '/')
        {
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

        if (!rbc_has_brace(component) && rbc_is_recursive_wildcard(component))
        {
            rbc_str_list_free(&expansions);

            // ** is only recursive if followed by /
            // If not followed by /, treat as regular *
            if (!is_sep)
            {
                // Treat ** as * when not followed by /
                // Re-collect with the modified pattern
                component = "*";
                expansions = rbc_brace_collect(component, arena);
                // Fall through to wildcard handling below
            }
            else
            {
                // Collapse consecutive recursive wildcards
                if (curr && curr->type == RBC_SEGMENT_RECURSIVE)
                {
                    // Skip creating a new segment - previous ** handles everything
                    // BUT i fwe are at the end, checks for empty trailing literal (trailing slash)
                    if (!*rest)
                    {
                        rbc_segment_t *trail = rbc_glob_segment_new(arena, RBC_SEGMENT_LITERAL);
                        trail->data.literal = "";
                        curr->next = trail;
                        curr = trail;
                    }
                    continue;
                }

                seg = rbc_glob_segment_new(arena, RBC_SEGMENT_RECURSIVE);
                if (!head)
                {
                    head = seg;
                }
                else
                {
                    curr->next = seg;
                }
                curr = seg;
                if (!*rest)
                {
                    rbc_segment_t *trail = rbc_glob_segment_new(arena, RBC_SEGMENT_LITERAL);
                    trail->data.literal = "";
                    curr->next = trail;
                    curr = trail;
                }
                continue;
            }
        }

        if (!rbc_has_brace(component) && !rbc_has_wildcard(component))
        {
            seg = rbc_glob_segment_new(arena, RBC_SEGMENT_LITERAL);
            seg->data.literal = component;
            rbc_str_list_free(&expansions);
            if (!head)
            {
                head = seg;
            }
            else
            {
                curr->next = seg;
            }
            curr = seg;
            if (is_sep && !*rest)
            {
                rbc_segment_t *trail = rbc_glob_segment_new(arena, RBC_SEGMENT_LITERAL);
                trail->data.literal = "";
                curr->next = trail;
                curr = trail;
            }
            continue;
        }

        bool any_slash = false;
        bool all_literals = true;
        for (size_t i = 0; i < expansions.count; i++)
        {
            if (strchr(expansions.items[i], '/'))
            {
                any_slash = true;
            }
            if (rbc_has_wildcard(expansions.items[i]))
            {
                all_literals = false;
            }
        }

        if (any_slash || all_literals || expansions.count > 1)
        {
            seg = rbc_glob_segment_new(arena, RBC_SEGMENT_BRANCH);
            rbc_segment_t *last_alt = NULL;

            for (size_t i = 0; i < expansions.count; i++)
            {
                char *full_pattern;
                if (is_sep)
                {
                    full_pattern = rbc_arena_printf(arena, "%s/%s", expansions.items[i], rest);
                }
                else if (*rest)
                {
                    full_pattern = rbc_arena_printf(arena, "%s%s", expansions.items[i], rest);
                }
                else
                {
                    full_pattern = rbc_arena_strdup(arena, expansions.items[i]);
                }

                rbc_segment_t *alt_chain = NULL;
                if (all_literals && !is_sep && !*rest)
                {
                    alt_chain = rbc_glob_segment_new(arena, RBC_SEGMENT_LITERAL);
                    alt_chain->data.literal = full_pattern;
                }
                else
                {
                    alt_chain = rbc_glob_segment_compile(arena, full_pattern, flags);
                }

                if (!alt_chain && !*full_pattern)
                {
                    alt_chain = rbc_glob_segment_new(arena, RBC_SEGMENT_LITERAL);
                    alt_chain->data.literal = "";
                }

                if (alt_chain)
                {
                    if (!seg->data.branch.head)
                    {
                        seg->data.branch.head = alt_chain;
                    }
                    else if (last_alt)
                    {
                        last_alt->next_alt = alt_chain;
                    }
                    last_alt = alt_chain;
                }
            }
            rbc_str_list_free(&expansions);

            if (!head)
            {
                head = seg;
            }
            else
            {
                curr->next = seg;
            }
            curr = seg;
            break;
        }
        else
        {
            // Single wildcard pattern - try to compile or use alternatives
            seg = rbc_glob_segment_new(arena, RBC_SEGMENT_WILDCARD);
            seg->data.glob.original_pattern = rbc_arena_strdup(arena, expansions.items[0]);

            if (!seg->data.glob.original_pattern)
            {
                rbc_str_list_free(&expansions);
                return NULL;
            }

            // Try to compile as single pattern first
            seg->data.glob.compiled = rbc_fnmatch_compile(seg->data.glob.original_pattern, flags);
            seg->data.glob.alternatives = NULL;

            // If compile failed, we'll use rbc_fnmatch at runtime

            rbc_str_list_free(&expansions);

            if (!head)
            {
                head = seg;
            }
            else
            {
                curr->next = seg;
            }
            curr = seg;
            if (is_sep && !*rest)
            {
                rbc_segment_t *trail = rbc_glob_segment_new(arena, RBC_SEGMENT_LITERAL);
                trail->data.literal = "";
                curr->next = trail;
                curr = trail;
            }
        }
    }
    return head;
}

/// @brief Match a string against a segment
/// @param seg Segment to match against
/// @param name String to match
/// @param flags Matching flags
/// @return true if match found, false otherwise
bool rbc_segment_match(const rbc_segment_t *seg, const char *name, unsigned int flags)
{
    if (!seg || !name)
    {
        return false;
    }

    if (seg->type != RBC_SEGMENT_WILDCARD)
    {
        return false;
    }

    // Check compiled pattern first
    if (seg->data.glob.compiled)
    {
        return rbc_xfnmatch(seg->data.glob.compiled, name, flags);
    }

    // Check alternatives
    if (seg->data.glob.alternatives)
    {
        return rbc_alternatives_match(seg->data.glob.alternatives, name, flags);
    }

    // Fallback to direct fnmatch
    if (seg->data.glob.original_pattern)
    {
        return rbc_fnmatch(seg->data.glob.original_pattern, name, flags);
    }

    return false;
}

/// @brief Compile brace expansion alternatives
/// @param arena Arena to allocate from
/// @param pattern Pattern with braces (not yet expanded)
/// @param flags Compilation flags
/// @return Compiled alternatives structure
rbc_alternatives_t *rbc_alternatives_compile(rbc_arena_t *arena, const char *pattern, unsigned int flags)
{
    // This is a placeholder - real implementation would parse braces
    // For now, just compile single pattern
    if (!pattern)
    {
        return NULL;
    }

    rbc_alternatives_t *result = rbc_arena_alloc(arena, sizeof(rbc_alternatives_t));
    result->count = 1;
    result->patterns = rbc_arena_alloc(arena, sizeof(rbc_fnmatch_pattern_t *));
    result->patterns[0] = rbc_fnmatch_compile(pattern, flags);

    if (!result->patterns[0])
    {
        return NULL;
    }

    return result;
}

/// @brief Free alternatives structure
/// @param alt Alternatives to free
/// @param arena Arena (unused, for compatibility)
void rbc_alternatives_free(rbc_alternatives_t *alt, rbc_arena_t *arena)
{
    (void)arena; // Allocated from arena, will be freed with arena
    if (!alt)
    {
        return;
    }

    // Free compiled patterns
    for (size_t i = 0; i < alt->count; i++)
    {
        rbc_fnmatch_pattern_free(alt->patterns[i]);
    }
}

/// @brief Match a string against alternative patterns
/// @param alt Compiled alternatives
/// @param name String to match
/// @param flags Matching flags
/// @return true if any alternative matches, false otherwise
bool rbc_alternatives_match(const rbc_alternatives_t *alt, const char *name, unsigned int flags)
{
    if (!alt || !name)
    {
        return false;
    }

    for (size_t i = 0; i < alt->count; i++)
    {
        if (rbc_xfnmatch(alt->patterns[i], name, flags))
        {
            return true;
        }
    }

    return false;
}

/// @}

/// @name Glob Functions
/// @{

/// @brief Free a compiled glob pattern
/// @param gp Compiled glob pattern to free
void rbc_glob_pattern_free(rbc_glob_pattern_t *gp)
{
    if (!gp)
    {
        return;
    }
    if (gp->ctx)
    {
        rbc_glob_ctx_free(gp->ctx);
        free(gp->ctx);
    }
    free(gp);
}

/// @brief Compile a glob pattern
/// @param pattern Pattern string
/// @param flags Compilation flags
/// @return Compiled glob pattern, or NULL on failure
rbc_glob_pattern_t *rbc_glob_compile(const char *pattern, unsigned flags)
{
    if (!pattern)
    {
        return NULL;
    }

    rbc_glob_pattern_t *cg = malloc(sizeof(rbc_glob_pattern_t));
    if (!cg)
    {
        return NULL;
    }

    cg->ctx = malloc(sizeof(rbc_ctx_t));
    if (!cg->ctx)
    {
        free(cg);
        return NULL;
    }

    if (!rbc_glob_ctx_init(cg->ctx))
    {
        free(cg->ctx);
        free(cg);
        return NULL;
    }
    cg->flags = flags;
    cg->original_pattern = rbc_arena_strdup(&cg->ctx->arena, pattern);
    cg->type = rbc_analyze_pattern(pattern);
    cg->segments = rbc_glob_segment_compile(&cg->ctx->arena, pattern, flags);

    if (!cg->segments)
    {
        rbc_glob_pattern_free(cg);
        return NULL;
    }

    return cg;
}

/// @brief Perform globbing on patterns
/// @param patterns Array of pattern strings
/// @param npatterns Number of patterns
/// @param flags Compilation and matching flags
/// @param base Base path to strip from results
/// @param sort Whether to sort results
/// @param out Output array of matched paths
/// @param count Number of matched paths
/// @param lengths Lengths of matched paths
/// @return true on success, false on failure
bool rbc_glob(const char **patterns, size_t npatterns, unsigned flags, const char *base, bool sort, char ***out, size_t *count, size_t **lengths)
{
    // Delegate to trie-based implementation (canonical implementation)
    return rbc_glob_trie(patterns, npatterns, flags, base, sort, out, count, lengths);
}

/// @brief Perform globbing using a compiled pattern
/// @param gp Compiled glob pattern
/// @param base Base path to strip from results
/// @param sort Whether to sort results
/// @param out Output array of matched paths
/// @param count Number of matched paths
/// @param lengths Lengths of matched paths
/// @return true on success, false on failure
bool rbc_xglob(const rbc_glob_pattern_t *gp, const char *base, bool sort, char ***out, size_t *count, size_t **lengths)
{
    if (!gp || !out || !count)
    {
        return false;
    }

    rbc_ctx_t *run_ctx = malloc(sizeof(rbc_ctx_t));
    if (!run_ctx)
    {
        return false;
    }
    if (!rbc_glob_ctx_init(run_ctx))
    {
        free(run_ctx);
        return false;
    }

    rbc_results_t results;
    if (!rbc_glob_results_init(&results, run_ctx))
    {
        rbc_glob_ctx_free(run_ctx);
        free(run_ctx);
        return false;
    }

    /* Context for callback */
    typedef struct
    {
        rbc_results_t *results;
        const char *base_strip;
        size_t base_len;
    } callback_ctx_t;

    callback_ctx_t cb_ctx = {
        .results = &results,
        .base_strip = base,
        .base_len = base ? strlen(base) : 0};

    /* Callback to collect results */
    void collect_result(const char *path, void *userdata)
    {
        callback_ctx_t *ctx = (callback_ctx_t *)userdata;
        if (!path || !ctx || !ctx->results)
            return;

        const char *result_path = path;
        if (ctx->base_strip && ctx->base_len > 0)
        {
            if (strncmp(path, ctx->base_strip, ctx->base_len) == 0)
            {
                result_path = path + ctx->base_len;
                if (*result_path == '/')
                    result_path++;
                if (*result_path == '\0')
                    result_path = ".";
            }
        }
        rbc_glob_results_add_with_index(ctx->results, result_path,
                                        ctx->results->ctx->discovery_counter++);
    }

    /* Execute using optimized path based on pattern type */
    if (gp->original_pattern)
    {
        rbc_glob_hints_t hints = rbc_glob_hints_generate(gp->original_pattern);
        rbc_glob_result_t *v2_result = NULL;

        switch (hints.type)
        {
        case GLOB_HINT_LITERAL:
        {
            struct stat st;
            if (stat(gp->original_pattern, &st) == 0)
            {
                rbc_glob_results_add(&results, gp->original_pattern);
            }
        }
        break;

        case GLOB_HINT_BRACE_SINGLE_DIR:
        case GLOB_HINT_BRACE_NESTED:
            v2_result = rbc_glob_exec_brace_optimized(&hints, gp->original_pattern, (int)gp->flags);
            break;

        case GLOB_HINT_RECURSIVE:
            v2_result = rbc_glob_exec_recursive_optimized(&hints, gp->original_pattern, (int)gp->flags);
            break;

        case GLOB_HINT_SIMPLE_PATTERN:
            v2_result = rbc_glob_exec_simple_optimized(&hints, gp->original_pattern, (int)gp->flags);
            break;

        case GLOB_HINT_MULTI_SEGMENT:
        case GLOB_HINT_COMPLEX:
        default:
            if (strstr(gp->original_pattern, "**"))
            {
                v2_result = rbc_glob_exec_recursive_optimized(&hints, gp->original_pattern, (int)gp->flags);
            }
            else
            {
                v2_result = rbc_glob_exec_multi_segment_optimized(&hints, gp->original_pattern, (int)gp->flags);
            }
            break;
        }

        if (v2_result && v2_result->paths)
        {
            for (size_t j = 0; j < v2_result->count; j++)
            {
                if (v2_result->paths[j])
                {
                    collect_result(v2_result->paths[j], &cb_ctx);
                }
            }
            rbc_glob_result_free(v2_result);
        }
    }

    // if (sort)
    // {
    //     rbc_glob_results_sort(&results);
    // }
    // rbc_glob_results_deduplicate(&results);

    // Packaging
    *count = results.count;
    void **package = malloc(sizeof(void *) + (results.count + 1) * sizeof(char *));
    if (!package)
    {
        rbc_glob_results_clear(&results);
        rbc_glob_ctx_free(run_ctx);
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
    {
        *lengths = results.lengths;
    }
    else if (results.lengths)
    {
        free(results.lengths);
    }

    if (results.discovery_indices)
    {
        free(results.discovery_indices);
    }

    if (results.items)
    {
        free(results.items);
    }

    return true;
}

/// @brief Free globbing results
/// @param list List of matched paths
/// @param count Number of matched paths
/// @param lengths Lengths of matched paths
void rbc_glob_free(char **list, size_t count, size_t *lengths)
{
    (void)count;
    if (!list)
    {
        return;
    }
    void **package = (void **)list - 1;
    rbc_ctx_t *ctx = (rbc_ctx_t *)package[0];
    rbc_glob_ctx_free(ctx);
    free(ctx);
    free(package);
    if (lengths)
    {
        free(lengths);
    }
}

/// @}
