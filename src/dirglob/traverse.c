#include <dirglob/internal/traverse.h>
#include <dirglob/internal/fnmatch.h>
#include <dirglob/internal/utils.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

/* Platform-specific includes */
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define INITIAL_CAPACITY 16

void glob_results_init(glob_results_t *results)
{
    results->items = NULL;
    results->count = 0;
    results->capacity = 0;
}

int glob_results_add(glob_results_t *results, const char *path)
{
    if (!results || !path)
    {
        errno = EINVAL;
        return -1;
    }

    /* Expand capacity if needed */
    if (results->count >= results->capacity)
    {
        size_t new_capacity = results->capacity == 0 ? INITIAL_CAPACITY : results->capacity * 2;
        char **new_items = realloc(results->items, new_capacity * sizeof(char *));
        if (!new_items)
        {
            errno = ENOMEM;
            return -1;
        }
        results->items = new_items;
        results->capacity = new_capacity;
    }

    /* Duplicate the path */
    char *dup = dirglob_strdup(path);
    if (!dup)
    {
        errno = ENOMEM;
        return -1;
    }

    results->items[results->count++] = dup;
    return 0;
}

static int compare_strings(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

void glob_results_sort(glob_results_t *results)
{
    if (!results || results->count == 0)
        return;
    qsort(results->items, results->count, sizeof(char *), compare_strings);
}

void glob_results_deduplicate(glob_results_t *results)
{
    if (!results || results->count <= 1)
        return;

    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < results->count; read_idx++)
    {
        if (read_idx == 0 || strcmp(results->items[read_idx], results->items[write_idx - 1]) != 0)
        {
            if (read_idx != write_idx)
            {
                results->items[write_idx] = results->items[read_idx];
            }
            write_idx++;
        }
        else
        {
            /* Duplicate found, free it */
            free(results->items[read_idx]);
        }
    }
    results->count = write_idx;
}

void glob_results_clear(glob_results_t *results)
{
    if (!results)
        return;

    for (size_t i = 0; i < results->count; i++)
    {
        free(results->items[i]);
    }
    free(results->items);

    results->items = NULL;
    results->count = 0;
    results->capacity = 0;
}

#ifndef _WIN32
/* POSIX implementation */
int traverse_directory(const char *pattern, const char *base,
                       unsigned flags, glob_results_t *results)
{
    DIR *dir;
    struct dirent *entry;
    const char *dir_path = base ? base : ".";

    dir = opendir(dir_path);
    if (!dir)
    {
        /* If directory doesn't exist, that's not an error for glob */
        if (errno == ENOENT || errno == ENOTDIR)
        {
            return 0;
        }
        return -1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;

        /* Skip . and .. */
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        {
            continue;
        }

        /* Skip dot files unless FNM_DOTMATCH is set */
        if (!(flags & FNM_DOTMATCH) && name[0] == '.')
        {
            continue;
        }

        /* Check if name matches pattern */
        if (dirglob_fnmatch(pattern, name, flags) == 0)
        {
            /* For base != NULL, result should be relative to base (just the name)
             * For base == NULL, result should be the full path from current directory */
            const char *result_path;
            if (base && base[0] != '\0')
            {
                /* base is specified: return relative path (name only) */
                result_path = name;
            }
            else
            {
                /* no base: return full path (which is just the name in current dir) */
                result_path = name;
            }

            int ret = glob_results_add(results, result_path);

            if (ret != 0)
            {
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);
    return 0;
}

#else
/* Windows implementation */
int traverse_directory(const char *pattern, const char *base,
                       unsigned flags, glob_results_t *results)
{
    WIN32_FIND_DATAA find_data;
    HANDLE hFind;
    char *search_path;

    /* Create search pattern: base\* */
    const char *dir_path = base ? base : ".";
    size_t len = strlen(dir_path);
    search_path = malloc(len + 3); /* +3 for \* and null terminator */
    if (!search_path)
    {
        errno = ENOMEM;
        return -1;
    }

    strcpy(search_path, dir_path);
    if (len > 0 && dir_path[len - 1] != '\\' && dir_path[len - 1] != '/')
    {
        strcat(search_path, "\\");
    }
    strcat(search_path, "*");

    hFind = FindFirstFileA(search_path, &find_data);
    free(search_path);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
        {
            return 0; /* Not an error for glob */
        }
        errno = ENOENT;
        return -1;
    }

    do
    {
        const char *name = find_data.cFileName;

        /* Skip . and .. */
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        {
            continue;
        }

        /* Skip dot files unless FNM_DOTMATCH is set */
        if (!(flags & FNM_DOTMATCH) && name[0] == '.')
        {
            continue;
        }

        /* Check if name matches pattern */
        if (dirglob_fnmatch(pattern, name, flags) == 0)
        {
            /* For base != NULL, result should be relative to base (just the name)
             * For base == NULL, result should be the full path from current directory */
            const char *result_path;
            if (base && base[0] != '\0')
            {
                /* base is specified: return relative path (name only) */
                result_path = name;
            }
            else
            {
                /* no base: return full path (which is just the name in current dir) */
                result_path = name;
            }

            int ret = glob_results_add(results, result_path);

            if (ret != 0)
            {
                FindClose(hFind);
                return -1;
            }
        }
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
    return 0;
}
#endif

/* Forward declaration for recursion */
int traverse_directory_recursive(const char *dir_pattern, const char *file_pattern,
                                 const char *base, unsigned flags, glob_results_t *results);

/* Helper to process pattern (simplified version for recursion) */
static int process_file_pattern(const char *pattern, const char *base,
                                unsigned flags, glob_results_t *results)
{
    /* Handle literal paths and simple patterns */
    if (!has_glob_pattern(pattern))
    {
        /* Literal file - construct full path and add if exists */
        char *result_path;
        if (base && base[0] != '\0')
        {
            result_path = path_join(base, pattern);
            if (!result_path)
            {
                errno = ENOMEM;
                return -1;
            }

            /* Check if file exists */
            struct stat st;
            if (stat(result_path, &st) == 0)
            {
                /* Return relative path from original base */
                free(result_path);
                return glob_results_add(results, pattern);
            }
            free(result_path);
        }
        return 0;
    }

    /* Check if pattern has directory component */
    const char *slash = strchr(pattern, '/');
    if (slash == NULL)
    {
        /* Simple pattern - traverse and match in current directory */
        return traverse_directory(pattern, base, flags, results);
    }

    /* Pattern has directory component - split and recurse */
    size_t first_len = slash - pattern;
    char *first_component = malloc(first_len + 1);
    if (!first_component)
    {
        errno = ENOMEM;
        return -1;
    }
    memcpy(first_component, pattern, first_len);
    first_component[first_len] = '\0';

    const char *rest = slash + 1;
    while (*rest == '/')
        rest++;

    int ret;
    if (has_glob_pattern(first_component))
    {
        ret = traverse_directory_recursive(first_component, rest, base, flags, results);
    }
    else
    {
        char *new_base = path_join(base, first_component);
        if (!new_base)
        {
            free(first_component);
            errno = ENOMEM;
            return -1;
        }

        struct stat st;
        if (stat(new_base, &st) == 0 && S_ISDIR(st.st_mode))
        {
            ret = process_file_pattern(rest, new_base, flags, results);
        }
        else
        {
            ret = 0;
        }
        free(new_base);
    }

    free(first_component);
    return ret;
}

/**
 * @brief Recursively traverse directories matching dir_pattern and apply file_pattern
 */
int traverse_directory_recursive(const char *dir_pattern, const char *file_pattern,
                                 const char *base, unsigned flags, glob_results_t *results)
{
#ifndef _WIN32
    DIR *dir;
    struct dirent *entry;
    const char *dir_path = base ? base : ".";

    dir = opendir(dir_path);
    if (!dir)
    {
        /* If directory doesn't exist, that's not an error for glob */
        if (errno == ENOENT || errno == ENOTDIR)
        {
            return 0;
        }
        return -1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;
        struct stat st;

        /* Skip . and .. */
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        {
            continue;
        }

        /* Skip dot files unless FNM_DOTMATCH is set */
        if (!(flags & FNM_DOTMATCH) && name[0] == '.')
        {
            continue;
        }

        /* Check if name matches directory pattern */
        if (dirglob_fnmatch(dir_pattern, name, flags) == 0)
        {
            /* Build full path to check if it's a directory */
            char *full_path = path_join(base, name);
            if (!full_path)
            {
                closedir(dir);
                errno = ENOMEM;
                return -1;
            }

            /* Check if it's a directory */
            if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
            {
                /* Process file_pattern in this directory */
                /* We need to collect results and prepend directory name */
                glob_results_t subresults;
                glob_results_init(&subresults);

                int ret = process_file_pattern(file_pattern, full_path, flags, &subresults);

                if (ret == 0)
                {
                    /* Prepend directory name to all results */
                    for (size_t i = 0; i < subresults.count; i++)
                    {
                        char *prefixed_path = path_join(name, subresults.items[i]);
                        if (prefixed_path)
                        {
                            glob_results_add(results, prefixed_path);
                            free(prefixed_path);
                        }
                        free(subresults.items[i]);
                    }
                    free(subresults.items);
                }
                else
                {
                    glob_results_clear(&subresults);
                }

                free(full_path);

                if (ret != 0)
                {
                    closedir(dir);
                    return -1;
                }
            }
            else
            {
                free(full_path);
            }
        }
    }

    closedir(dir);
    return 0;

#else
    /* Windows implementation */
    WIN32_FIND_DATAA find_data;
    HANDLE hFind;
    char *search_path;

    /* Create search pattern: base\* */
    const char *dir_path = base ? base : ".";
    size_t len = strlen(dir_path);
    search_path = malloc(len + 3);
    if (!search_path)
    {
        errno = ENOMEM;
        return -1;
    }

    strcpy(search_path, dir_path);
    if (len > 0 && dir_path[len - 1] != '\\' && dir_path[len - 1] != '/')
    {
        strcat(search_path, "\\");
    }
    strcat(search_path, "*");

    hFind = FindFirstFileA(search_path, &find_data);
    free(search_path);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
        {
            return 0;
        }
        errno = ENOENT;
        return -1;
    }

    do
    {
        const char *name = find_data.cFileName;

        /* Skip . and .. */
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        {
            continue;
        }

        /* Skip dot files unless FNM_DOTMATCH is set */
        if (!(flags & FNM_DOTMATCH) && name[0] == '.')
        {
            continue;
        }

        /* Check if name matches directory pattern */
        if (dirglob_fnmatch(dir_pattern, name, flags) == 0)
        {
            /* Check if it's a directory */
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                char *full_path = path_join(base, name);
                if (!full_path)
                {
                    FindClose(hFind);
                    errno = ENOMEM;
                    return -1;
                }

                /* Process file_pattern in this directory */
                /* Collect results and prepend directory name */
                glob_results_t subresults;
                glob_results_init(&subresults);

                int ret = process_file_pattern(file_pattern, full_path, flags, &subresults);

                if (ret == 0)
                {
                    /* Prepend directory name to all results */
                    for (size_t i = 0; i < subresults.count; i++)
                    {
                        char *prefixed_path = path_join(name, subresults.items[i]);
                        if (prefixed_path)
                        {
                            glob_results_add(results, prefixed_path);
                            free(prefixed_path);
                        }
                        free(subresults.items[i]);
                    }
                    free(subresults.items);
                }
                else
                {
                    glob_results_clear(&subresults);
                }

                free(full_path);

                if (ret != 0)
                {
                    FindClose(hFind);
                    return -1;
                }
            }
        }
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
    return 0;
#endif
}
