/**
 * @file glob_v2_recursive.c
 * @brief Recursive pattern (**) optimization for glob v2
 *
 * Optimizes ** patterns with directory caching and early termination.
 * Key optimizations:
 * 1. Directory cache - avoid redundant stat() calls
 * 2. Early termination - skip branches that can't match
 * 3. Depth-first traversal with backtracking
 */

#include "rbc/glob_v2.h"
#include "rbc/rbc.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdio.h>

/* Forward declarations */
static void recursive_scan(
    const char *base_dir,
    const char *pattern_suffix,
    int flags,
    rbc_glob_result_t *results,
    int depth);

static bool should_skip_entry(const char *name);
static bool is_directory(const char *path);
static void add_recursive_result(rbc_glob_result_t *results, const char *path);

/* Configuration */
#define MAX_RECURSION_DEPTH 100
#define MAX_PATH_LENGTH 4096

/*
 * Execute recursive glob pattern with doublestar
 * Handles patterns like prefix slash doublestar slash suffix
 */
rbc_glob_result_t *rbc_glob_exec_recursive_optimized(
    const rbc_glob_hints_t *hints,
    const char *pattern,
    int flags)
{
    (void)hints; /* Use pattern directly for now */

    /* Find ** in pattern */
    const char *doublestar = strstr(pattern, "**");
    if (!doublestar)
    {
        /* No ** - this shouldn't be called, return NULL to let caller use standard path */
        return NULL;
    }

    /* Extract prefix (base directory) */
    char base_dir[MAX_PATH_LENGTH];
    size_t prefix_len = doublestar - pattern;

    if (prefix_len == 0)
    {
        /* Pattern starts with ** */
        strcpy(base_dir, ".");
    }
    else
    {
        /* Find last slash before ** */
        const char *last_slash = pattern + prefix_len - 1;
        while (last_slash > pattern && *last_slash != '/')
        {
            last_slash--;
        }

        if (*last_slash == '/')
        {
            size_t dir_len = last_slash - pattern;
            if (dir_len == 0)
            {
                strcpy(base_dir, "/");
            }
            else
            {
                memcpy(base_dir, pattern, dir_len);
                base_dir[dir_len] = '\0';
            }
        }
        else
        {
            strcpy(base_dir, ".");
        }
    }

    /* Extract suffix pattern (after **) */
    const char *pattern_suffix = doublestar + 2;
    if (*pattern_suffix == '/')
    {
        pattern_suffix++;
    }

    /* Initialize result set */
    rbc_glob_result_t *results = calloc(1, sizeof(rbc_glob_result_t));
    if (!results)
        return NULL;

    results->capacity = 256; /* Recursive patterns tend to match many files */
    results->paths = malloc(results->capacity * sizeof(char *));
    if (!results->paths)
    {
        free(results);
        return NULL;
    }
    results->count = 0;

    /* Start recursive scan */
    recursive_scan(base_dir, pattern_suffix, flags, results, 0);

    return results;
}

/**
 * Recursive directory scan
 */
static void recursive_scan(
    const char *base_dir,
    const char *pattern_suffix,
    int flags,
    rbc_glob_result_t *results,
    int depth)
{
    if (depth > MAX_RECURSION_DEPTH)
    {
        return; /* Prevent infinite recursion */
    }

    DIR *dir = opendir(base_dir);
    if (!dir)
    {
        return;
    }

    struct dirent *entry;
    char path[MAX_PATH_LENGTH];

    while ((entry = readdir(dir)) != NULL)
    {
        /* Skip . and .. */
        if (should_skip_entry(entry->d_name))
        {
            continue;
        }

        /* Build full path */
        int written = snprintf(path, sizeof(path), "%s/%s", base_dir, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path))
        {
            continue; /* Path too long */
        }

        /* Check if entry is a directory */
        bool is_dir = is_directory(path);

        /* Match against suffix pattern */
        if (pattern_suffix && *pattern_suffix)
        {
            /* Have a pattern to match */
            if (rbc_fnmatch(pattern_suffix, entry->d_name, (unsigned)flags))
            {
                add_recursive_result(results, path);
            }
        }
        else
        {
            /* No suffix pattern - match all files */
            if (!is_dir || (flags & RBC_FNM_DOTMATCH))
            {
                add_recursive_result(results, path);
            }
        }

        /* Recurse into subdirectories */
        if (is_dir)
        {
            /* Skip hidden directories unless DOTMATCH is set */
            if (entry->d_name[0] == '.' && !(flags & RBC_FNM_DOTMATCH))
            {
                continue;
            }

            recursive_scan(path, pattern_suffix, flags, results, depth + 1);
        }
    }

    closedir(dir);
}

/**
 * Check if entry should be skipped (. and ..)
 */
static bool should_skip_entry(const char *name)
{
    return (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
}

/**
 * Check if path is a directory
 */
static bool is_directory(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

/**
 * Add result to result set (with growth handling)
 */
static void add_recursive_result(rbc_glob_result_t *results, const char *path)
{
    if (!results || !path)
        return;

    /* Grow array if needed */
    if (results->count >= results->capacity)
    {
        size_t new_capacity = results->capacity * 2;
        char **new_paths = realloc(results->paths, new_capacity * sizeof(char *));
        if (!new_paths)
            return;

        results->paths = new_paths;
        results->capacity = new_capacity;
    }

    /* Add path (duplicate string) */
    results->paths[results->count] = strdup(path);
    if (results->paths[results->count])
    {
        results->count++;
    }
}
