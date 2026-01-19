/**
 * @file glob_v2_brace.c
 * @brief Brace expansion optimization implementation
 *
 * This is the core optimization of glob v2.
 * Reduces N directory scans to 1 scan using hashset filtering.
 */

#include "rbc/glob_v2.h"
#include "string_set.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <limits.h>

/* Forward declarations */
static void merge_results(rbc_glob_result_t *dest, rbc_glob_result_t *src);
static rbc_glob_result_t *create_result_set(void);
static void add_result(rbc_glob_result_t *result, const char *path);
static rbc_glob_result_t *create_empty_result(void);

/**
 * @brief Execute brace expansion with optimization
 *
 * Strategy:
 * 1. Extract base directory from prefix
 * 2. Build hashset of choices (O(1) lookup)
 * 3. Scan directory once
 * 4. Filter entries with hashset
 * 5. Apply suffix pattern to matches
 *
 * Example: "src/{a,b,c}/*.txt"
 * - Base dir: "src"
 * - Choices: {a, b, c}
 * - Suffix: "/*.txt"
 *
 * Traditional: 3 directory scans (src/a, src/b, src/c)
 * Optimized: 1 directory scan (src) + hashset filter
 */
rbc_glob_result_t *rbc_glob_exec_brace_optimized(
    const rbc_glob_hints_t *hints,
    const char *pattern,
    int flags)
{
    if (!hints || !pattern)
    {
        return create_empty_result();
    }

    const glob_brace_info_t *binfo = &hints->brace_info;

    if (binfo->choice_count == 0)
    {
        return create_empty_result();
    }

    /* Step 1: Determine base directory */
    char base_dir[PATH_MAX];
    char *dir_part = NULL;

    /* Extract directory from prefix */
    if (binfo->prefix_len > 0)
    {
        memcpy(base_dir, binfo->prefix, binfo->prefix_len);
        base_dir[binfo->prefix_len] = '\0';

        /* Find last '/' to get directory part */
        char *last_slash = strrchr(base_dir, '/');
        if (last_slash && last_slash != base_dir)
        {
            *last_slash = '\0';
            dir_part = last_slash + 1; /* Part after last slash */
        }
        else if (last_slash == base_dir)
        {
            /* Root directory */
            strcpy(base_dir, "/");
            dir_part = NULL;
        }
        else
        {
            /* No directory separator - scan current directory */
            /* prefix is the partial name before brace */
            strcpy(base_dir, ".");
            dir_part = NULL;
        }
    }
    else
    {
        /* No prefix, use current directory */
        strcpy(base_dir, ".");
        dir_part = NULL;
    }

    /* Step 2: Build hashset of choices */
    rbc_string_set_t *choice_set = rbc_string_set_create(binfo->choice_count);
    if (!choice_set)
    {
        return create_empty_result();
    }

    for (int i = 0; i < binfo->choice_count; i++)
    {
        rbc_string_set_add_n(choice_set,
                             binfo->choices[i].start,
                             binfo->choices[i].len);
    }

    /* Step 3: Scan directory once */
    DIR *dir = opendir(base_dir);
    if (!dir)
    {
        rbc_string_set_free(choice_set);
        return create_empty_result();
    }

    rbc_glob_result_t *results = create_result_set();
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        results->entries_checked++;

        /* For patterns like "test_{a,b,c}.txt", we need to:
         * 1. Check if entry name starts with prefix (before brace)
         * 2. Extract the choice part
         * 3. Check if it's in our choice set
         * 4. Check if it ends with suffix
         */

        /* Build full name with prefix removed for checking */
        const char *name_to_check = entry->d_name;

        /* If we have a prefix before the brace, check it */
        size_t prefix_before_brace = binfo->prefix_len;
        if (dir_part)
        {
            /* Adjust prefix length - remove directory part */
            size_t dir_len = strlen(dir_part);
            if (prefix_before_brace > dir_len)
            {
                prefix_before_brace -= dir_len;
            }
        }

        /* For simple case: "test_{a,b,c}.txt"
         * prefix = "test_", suffix = ".txt"
         * We want to match "test_a.txt", "test_b.txt", "test_c.txt"
         */

        /* Check prefix */
        if (strncmp(entry->d_name, binfo->prefix, binfo->prefix_len) != 0)
        {
            continue;
        }

        /* Check suffix */
        size_t name_len = strlen(entry->d_name);
        if (name_len < binfo->prefix_len + binfo->suffix_len)
        {
            continue;
        }

        if (strcmp(entry->d_name + name_len - binfo->suffix_len, binfo->suffix) != 0)
        {
            continue;
        }

        /* Extract middle part (the choice) */
        size_t choice_len = name_len - binfo->prefix_len - binfo->suffix_len;
        char choice[256];
        if (choice_len >= sizeof(choice))
        {
            continue;
        }
        memcpy(choice, entry->d_name + binfo->prefix_len, choice_len);
        choice[choice_len] = '\0';

        /* Step 4: Check if choice is in our set (O(1)) */
        if (rbc_string_set_contains(choice_set, choice))
        {
            /* Match! Add to results */
            if (strcmp(base_dir, ".") == 0)
            {
                add_result(results, entry->d_name);
            }
            else
            {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s/%s",
                         base_dir, entry->d_name);
                add_result(results, full_path);
            }
        }
    }

    closedir(dir);
    rbc_string_set_free(choice_set);

    results->dirs_scanned++;

    return results;
}

/* Helper functions */
static rbc_glob_result_t *create_result_set(void)
{
    rbc_glob_result_t *result = calloc(1, sizeof(rbc_glob_result_t));
    if (!result)
        return NULL;

    result->capacity = 16;
    result->paths = malloc(result->capacity * sizeof(char *));
    if (!result->paths)
    {
        free(result);
        return NULL;
    }

    return result;
}

static rbc_glob_result_t *create_empty_result(void)
{
    return create_result_set();
}

static void add_result(rbc_glob_result_t *result, const char *path)
{
    if (!result || !path)
        return;

    /* Grow array if needed */
    if (result->count >= result->capacity)
    {
        size_t new_capacity = result->capacity * 2;
        char **new_paths = realloc(result->paths, new_capacity * sizeof(char *));
        if (!new_paths)
            return;

        result->paths = new_paths;
        result->capacity = new_capacity;
    }

    /* Add path (duplicate string) */
    result->paths[result->count] = strdup(path);
    if (result->paths[result->count])
    {
        result->count++;
    }
}

static void merge_results(rbc_glob_result_t *dest, rbc_glob_result_t *src)
{
    if (!dest || !src)
        return;

    for (size_t i = 0; i < src->count; i++)
    {
        add_result(dest, src->paths[i]);
    }

    dest->dirs_scanned += src->dirs_scanned;
    dest->entries_checked += src->entries_checked;
}
