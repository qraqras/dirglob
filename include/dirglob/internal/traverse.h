#ifndef DIRGLOB_INTERNAL_TRAVERSE_H
#define DIRGLOB_INTERNAL_TRAVERSE_H

#include <stddef.h>

/**
 * @brief Result collector for glob matches
 */
typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} glob_results_t;

/**
 * @brief Initialize result collector
 */
void glob_results_init(glob_results_t *results);

/**
 * @brief Add a path to results (duplicates string)
 * @return 0 on success, -1 on error
 */
int glob_results_add(glob_results_t *results, const char *path);

/**
 * @brief Sort results alphabetically
 */
void glob_results_sort(glob_results_t *results);

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
 * @param flags FNM_* flags
 * @param results Result collector
 * @return 0 on success, -1 on error
 */
int traverse_directory(const char *pattern, const char *base,
                       unsigned flags, glob_results_t *results);

#endif /* DIRGLOB_INTERNAL_TRAVERSE_H */
