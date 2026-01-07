#ifndef DIRGLOB_INTERNAL_TRAVERSE_H
#define DIRGLOB_INTERNAL_TRAVERSE_H

#include <stddef.h>
#include <stdbool.h>
#include <rbcglob/internal/compiler.h>
#include <rbcglob/internal/arena.h>

#define RBCGLOB_HASH_TABLE_SIZE 1024

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
 * @brief Directory cache entry
 */
typedef struct rbcglob_dir_cache_node_s
{
    char *path;
    char **entries;
    size_t *entry_lens;     /* P18: Pre-calculate lengths to avoid strlen() */
    unsigned char *d_types; /* P3: d_type from dirent to avoid stat() */
    size_t count;
} rbcglob_dir_cache_node_t;

/**
 * @brief Hash table entry for directory cache
 */
typedef struct rbcglob_cache_hash_entry_s
{
    char *key;
    size_t cache_index;
    struct rbcglob_cache_hash_entry_s *next;
} rbcglob_cache_hash_entry_t;

/**
 * @brief Glob execution context (for thread-safety and parallelization)
 */
typedef struct rbcglob_ctx_s
{
    rbcglob_arena_t arena;
    rbcglob_dir_cache_node_t *dir_cache;
    size_t dir_cache_count;
    rbcglob_cache_hash_entry_t *cache_hash[RBCGLOB_HASH_TABLE_SIZE];
    size_t discovery_counter;
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
 * @brief Clear directory cache in context
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
 * @brief Sort an array of strings alphabetically
 */
void rbcglob_results_sort_array(char **items, size_t count);

/**
 * @brief Remove duplicate entries
 */
void rbcglob_results_deduplicate(rbcglob_results_t *results);

/**
 * @brief Free result collector (but not the items array itself)
 */
void rbcglob_results_clear(rbcglob_results_t *results);

/**
 * @brief Traverse directory and collect matches
 *
 * @param ctx Glob context
 * @param pattern Pattern to match
 * @param base Base directory (NULL for current)
 * @param flags RBCGLOB_FNM_* flags
 * @param results Result collector
 * @return 0 on success, -1 on error
 */
int rbcglob_traverse_directory(rbcglob_ctx_t *ctx, const char *pattern, const char *base,
                               unsigned flags, rbcglob_results_t *results);

/**
 * @brief Recursively traverse directory with pattern
 *
 * @param ctx Glob context
 * @param dir_pattern Pattern for directory names
 * @param file_pattern Pattern for files/subdirs within matched directories
 * @param base Base directory (NULL for current)
 * @param flags RBCGLOB_FNM_* flags
 * @param results Result collector
 * @param sort_flag Whether to sort entries at each level
 * @return 0 on success, -1 on error
 */
int rbcglob_traverse_directory_recursive(rbcglob_ctx_t *ctx, const char *dir_pattern, const char *file_pattern,
                                         const char *base, unsigned flags, rbcglob_results_t *results, int sort_flag);

/**
 * @brief Recursively traverse all directories for ** pattern
 *
 * @param ctx Glob context
 * @param pattern Pattern to match in each directory
 * @param base Base directory (NULL for current)
 * @param flags RBCGLOB_FNM_* flags
 * @param results Result collector
 * @param sort_flag Whether to sort entries at each level
 * @param is_initial Whether this is the initial call for **
 * @return 0 on success, -1 on error
 */
int rbcglob_traverse_recursive_glob(rbcglob_ctx_t *ctx, const char *pattern, const char *base,
                                    unsigned flags, rbcglob_results_t *results, int sort_flag, bool is_initial);

/**
 * @brief Execute a compiled glob pattern
 *
 * @param ctx Glob context
 * @param cp Compiled pattern
 * @param base Base directory
 * @param results Result collector
 * @return 0 on success, -1 on error
 */
int rbcglob_execute(rbcglob_ctx_t *ctx, rbcglob_compiled_pattern_t *cp, const char *base, rbcglob_results_t *results);

#endif /* DIRGLOB_INTERNAL_TRAVERSE_H */
