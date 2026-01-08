#include <rbcglob/internal/traverse.h>
#include <rbcglob/internal/arena.h>
#include <stdlib.h>
#include <string.h>

int rbcglob_compare_paths(const char *s1_in, const char *s2_in)
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

void rbcglob_ctx_init(rbcglob_ctx_t *ctx)
{
    rbcglob_arena_init(&ctx->arena, 0); /* Use default block size (128KB) */
    ctx->discovery_counter = 0;
    // Cache initialized to NULL implicit or unused
}

void rbcglob_results_clear_cache(rbcglob_ctx_t *ctx)
{
    // No-op in new engine
    (void)ctx;
}

void rbcglob_ctx_free(rbcglob_ctx_t *ctx)
{
    if (!ctx)
        return;
    rbcglob_arena_destroy(&ctx->arena);
}

void rbcglob_results_reset_discovery_counter(rbcglob_ctx_t *ctx) { ctx->discovery_counter = 0; }

/* P1 Optimization: Initial capacity for result array */
#define INITIAL_RESULT_CAPACITY 64

void rbcglob_results_init(rbcglob_results_t *results, rbcglob_ctx_t *ctx)
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

void rbcglob_results_clear(rbcglob_results_t *results)
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

int rbcglob_results_add_with_index(rbcglob_results_t *results, const char *path, size_t index)
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
    results->items[results->count] = rbcglob_arena_alloc(&results->ctx->arena, len + 1);
    memcpy(results->items[results->count], p, len + 1);
    results->lengths[results->count] = len;
    results->discovery_indices[results->count] = index;
    results->count++;
    return 0;
}

int rbcglob_results_add(rbcglob_results_t *results, const char *path)
{
    // Pass 0 as index implicitly if not tracking discovery order for sort stability
    // (though new engine might not use discovery index in the same way)
    return rbcglob_results_add_with_index(results, path, results->ctx->discovery_counter++);
}

/* P10 Optimization: Helper structure for qsort() */
typedef struct rbcglob_traverse_sort_pair_s
{
    char *path;
    size_t length;
    size_t discovery_index;
} rbcglob_traverse_sort_pair_t;

/* P10: Comparison function for qsort() */
static int rbcglob_traverse_compare_sort_pairs(const void *a, const void *b)
{
    const rbcglob_traverse_sort_pair_t *pa = (const rbcglob_traverse_sort_pair_t *)a;
    const rbcglob_traverse_sort_pair_t *pb = (const rbcglob_traverse_sort_pair_t *)b;
    return rbcglob_compare_paths(pa->path, pb->path);
}

void rbcglob_results_sort(rbcglob_results_t *results)
{
    if (results->count <= 1)
        return;

    /* P10: Use qsort() instead of O(n²) bubble sort */
    rbcglob_traverse_sort_pair_t *pairs = malloc(sizeof(rbcglob_traverse_sort_pair_t) * results->count);
    if (!pairs)
        return; /* Fallback: keep unsorted */

    for (size_t i = 0; i < results->count; i++)
    {
        pairs[i].path = results->items[i];
        pairs[i].length = results->lengths[i];
        pairs[i].discovery_index = results->discovery_indices[i];
    }

    qsort(pairs, results->count, sizeof(rbcglob_traverse_sort_pair_t), rbcglob_traverse_compare_sort_pairs);

    for (size_t i = 0; i < results->count; i++)
    {
        results->items[i] = pairs[i].path;
        results->lengths[i] = pairs[i].length;
        results->discovery_indices[i] = pairs[i].discovery_index;
    }

    free(pairs);
}

void rbcglob_results_deduplicate(rbcglob_results_t *results)
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

int rbcglob_compare_filesystem_order(rbcglob_ctx_t *ctx, const char *a, const char *b)
{
    (void)ctx;
    return strcmp(a, b);
}
