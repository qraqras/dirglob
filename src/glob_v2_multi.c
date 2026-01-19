/**
 * @file glob_v2_multi.c
 * @brief Multi-pattern optimization for glob v2
 *
 * Optimizes multiple glob patterns by:
 * 1. Grouping patterns by base directory
 * 2. Scanning each directory once
 * 3. Testing all patterns against each entry
 *
 * Example:
 *   Before: glob("src/*.c") + glob("src/*.h")
 *           → scan src/ twice
 *   After:  glob_multi(["src/*.c", "src/*.h"])
 *           → scan src/ once
 */

#include "rbc/glob_v2.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Pattern group structure */
typedef struct
{
    const char **patterns;   /* Original patterns */
    size_t *pattern_indices; /* Indices in original array */
    size_t count;            /* Number of patterns in group */
    char base_dir[256];      /* Shared base directory */
} pattern_group_t;

/* Forward declarations */
static pattern_group_t *group_patterns_by_directory(
    const char **patterns,
    size_t count,
    size_t *num_groups);
static void free_pattern_groups(pattern_group_t *groups, size_t num_groups);
static void extract_base_directory(const char *pattern, char *base_dir, size_t size);
static bool same_base_directory(const char *pattern1, const char *pattern2);

/**
 * Execute multiple glob patterns with optimized directory scanning
 */
rbc_glob_result_t *rbc_glob_multi_v2_optimized(
    const char **patterns,
    size_t count,
    int flags)
{
    if (!patterns || count == 0)
    {
        rbc_glob_result_t *result = calloc(1, sizeof(rbc_glob_result_t));
        if (result)
        {
            result->capacity = 16;
            result->paths = malloc(result->capacity * sizeof(char *));
        }
        return result;
    }

    /* For single pattern, use normal glob */
    if (count == 1)
    {
        return rbc_glob_v2(patterns[0], flags);
    }

    /* Group patterns by base directory */
    size_t num_groups = 0;
    pattern_group_t *groups = group_patterns_by_directory(patterns, count, &num_groups);

    if (!groups)
    {
        /* Fallback: execute patterns individually */
        rbc_glob_result_t *merged = calloc(1, sizeof(rbc_glob_result_t));
        if (!merged)
            return NULL;

        merged->capacity = 256;
        merged->paths = malloc(merged->capacity * sizeof(char *));
        if (!merged->paths)
        {
            free(merged);
            return NULL;
        }
        merged->count = 0;

        for (size_t i = 0; i < count; i++)
        {
            rbc_glob_result_t *result = rbc_glob_v2(patterns[i], flags);
            if (result)
            {
                for (size_t j = 0; j < result->count; j++)
                {
                    /* Avoid duplicates */
                    bool duplicate = false;
                    for (size_t k = 0; k < merged->count; k++)
                    {
                        if (strcmp(merged->paths[k], result->paths[j]) == 0)
                        {
                            duplicate = true;
                            break;
                        }
                    }

                    if (!duplicate)
                    {
                        if (merged->count >= merged->capacity)
                        {
                            size_t new_capacity = merged->capacity * 2;
                            char **new_paths = realloc(merged->paths, new_capacity * sizeof(char *));
                            if (new_paths)
                            {
                                merged->paths = new_paths;
                                merged->capacity = new_capacity;
                            }
                        }

                        if (merged->count < merged->capacity)
                        {
                            merged->paths[merged->count++] = strdup(result->paths[j]);
                        }
                    }
                }
                rbc_glob_result_free(result);
            }
        }

        return merged;
    }

    /* Execute each group with optimization */
    rbc_glob_result_t *final_result = calloc(1, sizeof(rbc_glob_result_t));
    if (!final_result)
    {
        free_pattern_groups(groups, num_groups);
        return NULL;
    }

    final_result->capacity = 256;
    final_result->paths = malloc(final_result->capacity * sizeof(char *));
    if (!final_result->paths)
    {
        free(final_result);
        free_pattern_groups(groups, num_groups);
        return NULL;
    }
    final_result->count = 0;

    /* Process each group */
    for (size_t i = 0; i < num_groups; i++)
    {
        pattern_group_t *group = &groups[i];

        if (group->count == 1)
        {
            /* Single pattern in group - execute normally */
            rbc_glob_result_t *result = rbc_glob_v2(group->patterns[0], flags);
            if (result)
            {
                for (size_t j = 0; j < result->count; j++)
                {
                    if (final_result->count >= final_result->capacity)
                    {
                        size_t new_capacity = final_result->capacity * 2;
                        char **new_paths = realloc(final_result->paths, new_capacity * sizeof(char *));
                        if (new_paths)
                        {
                            final_result->paths = new_paths;
                            final_result->capacity = new_capacity;
                        }
                    }

                    if (final_result->count < final_result->capacity)
                    {
                        final_result->paths[final_result->count++] = strdup(result->paths[j]);
                    }
                }
                rbc_glob_result_free(result);
            }
        }
        else
        {
            /* Multiple patterns in group - optimize by executing together */
            /* For now, use v1 multi-pattern (TODO: implement optimized scan) */
            char **v1_paths = NULL;
            size_t v1_count = 0;
            size_t *lengths = NULL;

            bool success = rbc_glob(group->patterns, group->count, (unsigned)flags,
                                    NULL, true, &v1_paths, &v1_count, &lengths);

            if (success && v1_paths)
            {
                for (size_t j = 0; j < v1_count; j++)
                {
                    if (v1_paths[j])
                    {
                        if (final_result->count >= final_result->capacity)
                        {
                            size_t new_capacity = final_result->capacity * 2;
                            char **new_paths = realloc(final_result->paths, new_capacity * sizeof(char *));
                            if (new_paths)
                            {
                                final_result->paths = new_paths;
                                final_result->capacity = new_capacity;
                            }
                        }

                        if (final_result->count < final_result->capacity)
                        {
                            final_result->paths[final_result->count++] = strdup(v1_paths[j]);
                        }
                    }
                }
                rbc_glob_free(v1_paths, v1_count, lengths);
            }
        }
    }

    free_pattern_groups(groups, num_groups);
    return final_result;
}

/**
 * Group patterns by their base directory
 */
static pattern_group_t *group_patterns_by_directory(
    const char **patterns,
    size_t count,
    size_t *num_groups)
{
    if (!patterns || count == 0 || !num_groups)
    {
        return NULL;
    }

    /* Allocate groups (worst case: one group per pattern) */
    pattern_group_t *groups = calloc(count, sizeof(pattern_group_t));
    if (!groups)
        return NULL;

    *num_groups = 0;

    for (size_t i = 0; i < count; i++)
    {
        char base_dir[256];
        extract_base_directory(patterns[i], base_dir, sizeof(base_dir));

        /* Find existing group with same base directory */
        size_t group_idx = *num_groups;
        for (size_t j = 0; j < *num_groups; j++)
        {
            if (strcmp(groups[j].base_dir, base_dir) == 0)
            {
                group_idx = j;
                break;
            }
        }

        /* Add to existing or create new group */
        if (group_idx == *num_groups)
        {
            /* New group */
            strcpy(groups[group_idx].base_dir, base_dir);
            groups[group_idx].patterns = malloc(count * sizeof(char *));
            groups[group_idx].pattern_indices = malloc(count * sizeof(size_t));
            groups[group_idx].count = 0;
            (*num_groups)++;
        }

        /* Add pattern to group */
        size_t idx = groups[group_idx].count;
        groups[group_idx].patterns[idx] = patterns[i];
        groups[group_idx].pattern_indices[idx] = i;
        groups[group_idx].count++;
    }

    return groups;
}

/**
 * Free pattern groups
 */
static void free_pattern_groups(pattern_group_t *groups, size_t num_groups)
{
    if (!groups)
        return;

    for (size_t i = 0; i < num_groups; i++)
    {
        free((void *)groups[i].patterns);
        free(groups[i].pattern_indices);
    }

    free(groups);
}

/**
 * Extract base directory from pattern
 */
static void extract_base_directory(const char *pattern, char *base_dir, size_t size)
{
    if (!pattern || !base_dir || size == 0)
        return;

    /* Find first wildcard or brace */
    const char *p = pattern;
    const char *last_slash = NULL;

    while (*p && *p != '*' && *p != '?' && *p != '[' && *p != '{')
    {
        if (*p == '/')
        {
            last_slash = p;
        }
        p++;
    }

    if (!last_slash)
    {
        /* No directory component */
        base_dir[0] = '.';
        base_dir[1] = '\0';
    }
    else
    {
        /* Copy up to last slash */
        size_t len = last_slash - pattern;
        if (len >= size)
            len = size - 1;
        memcpy(base_dir, pattern, len);
        base_dir[len] = '\0';

        if (len == 0)
        {
            strcpy(base_dir, "/");
        }
    }
}

/**
 * Check if two patterns have the same base directory
 */
static bool same_base_directory(const char *pattern1, const char *pattern2)
{
    char base1[256], base2[256];
    extract_base_directory(pattern1, base1, sizeof(base1));
    extract_base_directory(pattern2, base2, sizeof(base2));
    return strcmp(base1, base2) == 0;
}
