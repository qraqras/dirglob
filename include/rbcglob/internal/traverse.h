#ifndef DIRGLOB_INTERNAL_TRAVERSE_H
#define DIRGLOB_INTERNAL_TRAVERSE_H

#include <stddef.h>
#include <stdbool.h>
#include <rbcglob/internal/arena.h>

/**
 * @brief Compare two paths using Ruby-style sorting rules
 *
 * Used internally by traverse module for sorting glob results
 * in Ruby-compatible order.
 *
 * @param s1 First path
 * @param s2 Second path
 * @return <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2
 */
int rbcglob_compare_paths(const char *s1, const char *s2);

/**
 * @brief Glob execution context
 */
typedef struct rbcglob_ctx_s
{
    rbcglob_arena_t arena;
    size_t discovery_counter;
    /* Legacy cache fields removed */
} rbcglob_ctx_t;

/**
 * @brief Result collector for glob matches
 */
typedef struct rbcglob_results_s
{
    char **items;
    size_t *lengths;
    size_t *discovery_indices;
    size_t count;
    size_t capacity;
    rbcglob_ctx_t *ctx; /* Link back to context for arena access */
} rbcglob_results_t;

/**
 * @brief Initialize glob context
 */
void rbcglob_ctx_init(rbcglob_ctx_t *ctx);

/**
 * @brief Destroy glob context and free all memory (including arena)
 */
void rbcglob_ctx_free(rbcglob_ctx_t *ctx);

/**
 * @brief Initialize result collector
 */
void rbcglob_results_init(rbcglob_results_t *results, rbcglob_ctx_t *ctx);

/**
 * @brief Reset discovery counter in context
 */
void rbcglob_results_reset_discovery_counter(rbcglob_ctx_t *ctx);

/**
 * @brief Clear directory cache (Legacy stub)
 */
void rbcglob_results_clear_cache(rbcglob_ctx_t *ctx);

/**
 * @brief Compare two paths based on cached filesystem order
 */
int rbcglob_compare_filesystem_order(rbcglob_ctx_t *ctx, const char *a, const char *b);

/**
 * @brief Add a path to results (duplicates string)
 * @return 0 on success, -1 on error
 */
int rbcglob_results_add(rbcglob_results_t *results, const char *path);

/**
 * @brief Add a path to results with a specific discovery index
 * @return 0 on success, -1 on error
 */
int rbcglob_results_add_with_index(rbcglob_results_t *results, const char *path, size_t index);

/**
 * @brief Sort results alphabetically
 */
void rbcglob_results_sort(rbcglob_results_t *results);

/**
 * @brief Remove duplicate entries
 */
void rbcglob_results_deduplicate(rbcglob_results_t *results);

/**
 * @brief Free result collector (but not the items array itself)
 */
void rbcglob_results_clear(rbcglob_results_t *results);

#endif /* DIRGLOB_INTERNAL_TRAVERSE_H */
