#ifndef DIRGLOB_INTERNAL_TRAVERSE_H
#define DIRGLOB_INTERNAL_TRAVERSE_H

#include <stddef.h>
#include <stdbool.h>
#include <rbcglob/internal/compiler.h>

/**
 * @brief Result collector for glob matches
 */
typedef struct
{
    char **items;
    size_t *discovery_indices;
    size_t count;
    size_t capacity;
} glob_results_t;

/**
 * @brief Initialize result collector
 */
void glob_results_init(glob_results_t *results);

/**
 * @brief Reset global discovery counter
 */
void glob_results_reset_discovery_counter(void);

/**
 * @brief Clear directory cache
 */
void glob_results_clear_cache(void);

/**
 * @brief Compare two paths based on cached filesystem order
 */
int rbcglob_compare_filesystem_order(const char *a, const char *b);

/**
 * @brief Add a path to results (duplicates string)
 * @return 0 on success, -1 on error
 */
int glob_results_add(glob_results_t *results, const char *path);

/**
 * @brief Add a path to results with a specific discovery index
 * @return 0 on success, -1 on error
 */
int glob_results_add_with_index(glob_results_t *results, const char *path, size_t index);

/**
 * @brief Sort results alphabetically
 */
void glob_results_sort(glob_results_t *results);

/**
 * @brief Sort an array of strings alphabetically
 */
void glob_results_sort_array(char **items, size_t count);

/**
 * @brief Remove duplicate entries
 */
void glob_results_deduplicate(glob_results_t *results);

/**
 * @brief Free result collector (but not the items array itself)
 */
void glob_results_clear(glob_results_t *results);

/**
 * @brief Traverse directory and collect matches
 *
 * @param pattern Pattern to match
 * @param base Base directory (NULL for current)
 * @param flags RBCGLOB_FNM_* flags
 * @param results Result collector
 * @return 0 on success, -1 on error
 */
int traverse_directory(const char *pattern, const char *base,
                       unsigned flags, glob_results_t *results);

/**
 * @brief Recursively traverse directory with pattern
 *
 * @param dir_pattern Pattern for directory names
 * @param file_pattern Pattern for files/subdirs within matched directories
 * @param base Base directory (NULL for current)
 * @param flags RBCGLOB_FNM_* flags
 * @param results Result collector
 * @param sort_flag Whether to sort entries at each level
 * @return 0 on success, -1 on error
 */
int traverse_directory_recursive(const char *dir_pattern, const char *file_pattern,
                                 const char *base, unsigned flags, glob_results_t *results, int sort_flag);

/**
 * @brief Recursively traverse all directories for ** pattern
 *
 * @param pattern Pattern to match in each directory
 * @param base Base directory (NULL for current)
 * @param flags RBCGLOB_FNM_* flags
 * @param results Result collector
 * @param sort_flag Whether to sort entries at each level
 * @param is_initial Whether this is the initial call for **
 * @return 0 on success, -1 on error
 */
int traverse_recursive_glob(const char *pattern, const char *base,
                            unsigned flags, glob_results_t *results, int sort_flag, bool is_initial);

/**
 * @brief Execute a compiled glob pattern
 *
 * @param cp Compiled pattern
 * @param base Base directory
 * @param results Result collector
 * @return 0 on success, -1 on error
 */
int rbcglob_execute(rbcglob_compiled_pattern_t *cp, const char *base, glob_results_t *results);

#endif /* DIRGLOB_INTERNAL_TRAVERSE_H */
