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

/* Internal flag: Skip . directory (used when parent was matched via glob) */
#define FNM_SKIP_DOT_DIR (1U << 16)

/* Global counter for discovery order (used for sort: false) */
static size_t g_global_discovery_counter = 0;

/* Directory cache for stable discovery indices */
typedef struct dir_cache_node
{
    char *path;
    char **entries;
    size_t *discovery_indices;
    size_t count;
    size_t capacity;
    struct dir_cache_node *next;
} dir_cache_node_t;

static dir_cache_node_t *g_dir_cache = NULL;

void glob_results_reset_discovery_counter(void)
{
    g_global_discovery_counter = 0;
}

void glob_results_clear_cache(void)
{
    dir_cache_node_t *current = g_dir_cache;
    while (current)
    {
        dir_cache_node_t *next = current->next;
        free(current->path);
        for (size_t i = 0; i < current->count; i++)
            free(current->entries[i]);
        free(current->entries);
        free(current->discovery_indices);
        free(current);
        current = next;
    }
    g_dir_cache = NULL;
}

static dir_cache_node_t *get_cached_dir(const char *dir_path)
{
    dir_cache_node_t *current = g_dir_cache;
    while (current)
    {
        if (strcmp(current->path, dir_path) == 0)
            return current;
        current = current->next;
    }

    /* Not found, create new cache node */
    DIR *dir = opendir(dir_path);
    if (!dir)
        return NULL;

    dir_cache_node_t *node = malloc(sizeof(dir_cache_node_t));
    if (!node)
    {
        closedir(dir);
        return NULL;
    }

    node->path = dirglob_strdup(dir_path);
    node->count = 0;
    node->capacity = INITIAL_CAPACITY;
    node->entries = malloc(node->capacity * sizeof(char *));
    node->discovery_indices = malloc(node->capacity * sizeof(size_t));
    node->next = g_dir_cache;
    g_dir_cache = node;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (node->count >= node->capacity)
        {
            node->capacity *= 2;
            node->entries = realloc(node->entries, node->capacity * sizeof(char *));
            node->discovery_indices = realloc(node->discovery_indices, node->capacity * sizeof(size_t));
        }
        node->entries[node->count] = dirglob_strdup(entry->d_name);
        node->discovery_indices[node->count] = ++g_global_discovery_counter;
        node->count++;
    }

    closedir(dir);
    return node;
}

void glob_results_init(glob_results_t *results)
{
    results->items = NULL;
    results->discovery_indices = NULL;
    results->count = 0;
    results->capacity = 0;
}

static int glob_results_add_internal(glob_results_t *results, const char *path, size_t index)
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
        size_t *new_indices = realloc(results->discovery_indices, new_capacity * sizeof(size_t));
        if (!new_indices)
        {
            /* Note: new_items is already reallocated, but we can't easily undo it.
             * We'll just update results->items to keep it consistent. */
            results->items = new_items;
            errno = ENOMEM;
            return -1;
        }
        results->items = new_items;
        results->discovery_indices = new_indices;
        results->capacity = new_capacity;
    }

    /* Duplicate the path */
    char *dup = dirglob_strdup(path);
    if (!dup)
    {
        errno = ENOMEM;
        return -1;
    }

    results->items[results->count] = dup;
    results->discovery_indices[results->count] = index;
    /* printf("DEBUG: add %s index %zu\n", dup, index); */
    results->count++;
    return 0;
}

int glob_results_add(glob_results_t *results, const char *path)
{
    return glob_results_add_internal(results, path, g_global_discovery_counter);
}

int glob_results_add_with_index(glob_results_t *results, const char *path, size_t index)
{
    return glob_results_add_internal(results, path, index);
}

static int compare_strings(const void *a, const void *b)
{
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    return dirglob_compare_paths(s1, s2);
}

void glob_results_sort(glob_results_t *results)
{
    if (!results || results->count == 0)
        return;
    /* Note: This only sorts items, discovery_indices will be out of sync.
     * But sort: true doesn't need discovery_indices. */
    qsort(results->items, results->count, sizeof(char *), compare_strings);
}

void glob_results_sort_array(char **items, size_t count)
{
    if (!items || count == 0)
        return;
    qsort(items, count, sizeof(char *), compare_strings);
}

void glob_results_deduplicate(glob_results_t *results)
{
    if (!results || results->count <= 1)
        return;

    size_t write_idx = 1;
    for (size_t read_idx = 1; read_idx < results->count; read_idx++)
    {
        bool is_duplicate = false;
        for (size_t i = 0; i < write_idx; i++)
        {
            if (strcmp(results->items[read_idx], results->items[i]) == 0)
            {
                is_duplicate = true;
                break;
            }
        }

        if (!is_duplicate)
        {
            if (read_idx != write_idx)
            {
                results->items[write_idx] = results->items[read_idx];
                results->discovery_indices[write_idx] = results->discovery_indices[read_idx];
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
    free(results->discovery_indices);

    results->items = NULL;
    results->discovery_indices = NULL;
    results->count = 0;
    results->capacity = 0;
}

#ifndef _WIN32
/* POSIX implementation */
int traverse_directory(const char *pattern, const char *base,
                       unsigned flags, glob_results_t *results)
{
    const char *dir_path = base ? base : ".";
    int pattern_starts_with_dot = (pattern && pattern[0] == '.');

    dir_cache_node_t *dir_cache = get_cached_dir(dir_path);
    if (!dir_cache)
    {
        /* If directory doesn't exist, that's not an error for glob */
        if (errno == ENOENT || errno == ENOTDIR)
        {
            return 0;
        }
        return -1;
    }

    for (size_t i = 0; i < dir_cache->count; i++)
    {
        const char *name = dir_cache->entries[i];
        size_t discovery_index = dir_cache->discovery_indices[i];

        /* Always skip .. */
        if (strcmp(name, "..") == 0)
        {
            continue;
        }

        /* Skip . if FNM_SKIP_DOT_DIR is set (parent was glob-matched) */
        if (strcmp(name, ".") == 0 && (flags & FNM_SKIP_DOT_DIR))
        {
            continue;
        }

        /* Skip . unless FNM_DOTMATCH is set or pattern starts with . */
        if (strcmp(name, ".") == 0 && !(flags & FNM_DOTMATCH) && !pattern_starts_with_dot)
        {
            continue;
        }

        /* Skip other dot files unless FNM_DOTMATCH is set or pattern starts with . */
        if (!(flags & FNM_DOTMATCH) && !pattern_starts_with_dot && name[0] == '.' && strcmp(name, ".") != 0)
        {
            continue;
        }

        /* Check if name matches pattern */
        if (dirglob_fnmatch(pattern, name, flags) == 0)
        {
            const char *result_path = name;
            int ret = glob_results_add_with_index(results, result_path, discovery_index);

            if (ret != 0)
            {
                return -1;
            }
        }
    }

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
    int pattern_starts_with_dot = (pattern && pattern[0] == '.');

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

        /* Always skip .. */
        if (strcmp(name, "..") == 0)
        {
            continue;
        }

        /* Skip . if FNM_SKIP_DOT_DIR is set (parent was glob-matched) */
        if (strcmp(name, ".") == 0 && (flags & FNM_SKIP_DOT_DIR))
        {
            continue;
        }

        /* Skip . unless FNM_DOTMATCH is set or pattern starts with . */
        if (strcmp(name, ".") == 0 && !(flags & FNM_DOTMATCH) && !pattern_starts_with_dot)
        {
            continue;
        }

        /* Skip other dot files unless FNM_DOTMATCH is set or pattern starts with . */
        if (!(flags & FNM_DOTMATCH) && !pattern_starts_with_dot && name[0] == '.' && strcmp(name, ".") != 0)
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
                                 const char *base, unsigned flags, glob_results_t *results, int sort_flag);

/* Helper to process pattern (simplified version for recursion) */
static int process_file_pattern(const char *pattern, const char *base,
                                unsigned flags, glob_results_t *results, int sort_flag)
{
    /* Handle literal paths and simple patterns */
    if (!has_glob_pattern(pattern))
    {
        /* Literal file - construct full path and add if exists */
        char *result_path = path_join(base, pattern);
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
            int ret = glob_results_add(results, pattern);
            free(result_path);
            return ret;
        }
        free(result_path);
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
        ret = traverse_directory_recursive(first_component, rest, base, flags, results, sort_flag);
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
            ret = process_file_pattern(rest, new_base, flags, results, sort_flag);
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
                                 const char *base, unsigned flags, glob_results_t *results, int sort_flag)
{
#ifndef _WIN32
    DIR *dir;
    struct dirent *entry;
    const char *dir_path = base ? base : ".";
    int pattern_starts_with_dot = (dir_pattern && dir_pattern[0] == '.');

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

    /* Collect all entries first to allow sorting */
    size_t count = 0;
    size_t capacity = 32;
    char **entries = malloc(capacity * sizeof(char *));
    if (!entries)
    {
        closedir(dir);
        return -1;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;
        g_global_discovery_counter++;

        /* Always skip .. */
        if (strcmp(name, "..") == 0)
        {
            continue;
        }

        /* Skip . unless FNM_DOTMATCH is set or pattern starts with . */
        if (strcmp(name, ".") == 0 && !(flags & FNM_DOTMATCH) && !pattern_starts_with_dot)
        {
            continue;
        }

        /* Skip other dot files unless FNM_DOTMATCH is set or pattern starts with . */
        if (!(flags & FNM_DOTMATCH) && !pattern_starts_with_dot && name[0] == '.' && strcmp(name, ".") != 0)
        {
            continue;
        }

        if (count >= capacity)
        {
            capacity *= 2;
            char **new_entries = realloc(entries, capacity * sizeof(char *));
            if (!new_entries)
            {
                for (size_t i = 0; i < count; i++)
                    free(entries[i]);
                free(entries);
                closedir(dir);
                return -1;
            }
            entries = new_entries;
        }
        entries[count++] = strdup(name);
    }
    closedir(dir);

    /* Sort entries if requested */
    if (sort_flag)
    {
        glob_results_sort_array(entries, count);
    }

    /* Process entries in order */
    int ret = 0;
    for (size_t i = 0; i < count; i++)
    {
        char *name = entries[i];
        struct stat st;

        /* Check if name matches directory pattern */
        if (dirglob_fnmatch(dir_pattern, name, flags) == 0)
        {
            /* Build full path to check if it's a directory */
            char *full_path = path_join(base, name);
            if (!full_path)
            {
                ret = -1;
                break;
            }

            /* Check if it's a directory */
            if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
            {
                /* Process file_pattern in this directory */
                /* We need to collect results and prepend directory name */
                glob_results_t subresults;
                glob_results_init(&subresults);

                /* Set FNM_SKIP_DOT_DIR to prevent matching . in subdirs */
                unsigned subflags = flags | FNM_SKIP_DOT_DIR;
                int sub_ret = process_file_pattern(file_pattern, full_path, subflags, &subresults, sort_flag);

                if (sub_ret == 0)
                {
                    /* Prepend directory name to all results */
                    for (size_t j = 0; j < subresults.count; j++)
                    {
                        char *prefixed_path = path_join(name, subresults.items[j]);
                        if (prefixed_path)
                        {
                            glob_results_add(results, prefixed_path);
                            free(prefixed_path);
                        }
                        free(subresults.items[j]);
                    }
                    free(subresults.items);
                }
                else
                {
                    glob_results_clear(&subresults);
                    ret = -1;
                }
            }
            free(full_path);
            if (ret != 0)
                break;
        }
    }

    for (size_t i = 0; i < count; i++)
        free(entries[i]);
    free(entries);
    return ret;

#else
    /* Windows implementation */
    WIN32_FIND_DATAA find_data;
    HANDLE hFind;
    char *search_path;
    int pattern_starts_with_dot = (dir_pattern && dir_pattern[0] == '.');
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

    /* Collect all entries first to allow sorting */
    size_t count = 0;
    size_t capacity = 32;
    char **entries = malloc(capacity * sizeof(char *));
    if (!entries)
    {
        FindClose(hFind);
        return -1;
    }

    do
    {
        const char *name = find_data.cFileName;

        /* Always skip .. */
        if (strcmp(name, "..") == 0)
        {
            continue;
        }

        /* Skip . unless FNM_DOTMATCH is set or pattern starts with . */
        if (strcmp(name, ".") == 0 && !(flags & FNM_DOTMATCH) && !pattern_starts_with_dot)
        {
            continue;
        }

        /* Skip other dot files unless FNM_DOTMATCH is set or pattern starts with . */
        if (!(flags & FNM_DOTMATCH) && !pattern_starts_with_dot && name[0] == '.' && strcmp(name, ".") != 0)
        {
            continue;
        }

        if (count >= capacity)
        {
            capacity *= 2;
            char **new_entries = realloc(entries, capacity * sizeof(char *));
            if (!new_entries)
            {
                for (size_t i = 0; i < count; i++)
                    free(entries[i]);
                free(entries);
                FindClose(hFind);
                return -1;
            }
            entries = new_entries;
        }
        entries[count++] = strdup(name);
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);

    /* Sort entries if requested */
    if (sort_flag)
    {
        glob_results_sort_array(entries, count);
    }

    /* Process entries in order */
    int ret = 0;
    for (size_t i = 0; i < count; i++)
    {
        char *name = entries[i];

        /* Check if name matches directory pattern */
        if (dirglob_fnmatch(dir_pattern, name, flags) == 0)
        {
            char *full_path = path_join(base, name);
            if (!full_path)
            {
                ret = -1;
                break;
            }

            /* Check if it's a directory */
            DWORD attrs = GetFileAttributesA(full_path);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                /* Process file_pattern in this directory */
                glob_results_t subresults;
                glob_results_init(&subresults);

                unsigned subflags = flags | FNM_SKIP_DOT_DIR;
                int sub_ret = process_file_pattern(file_pattern, full_path, subflags, &subresults, sort_flag);

                if (sub_ret == 0)
                {
                    for (size_t j = 0; j < subresults.count; j++)
                    {
                        char *prefixed_path = path_join(name, subresults.items[j]);
                        if (prefixed_path)
                        {
                            glob_results_add(results, prefixed_path);
                            free(prefixed_path);
                        }
                        free(subresults.items[j]);
                    }
                    free(subresults.items);
                }
                else
                {
                    glob_results_clear(&subresults);
                    ret = -1;
                }
            }
            free(full_path);
            if (ret != 0)
                break;
        }
    }

    for (size_t i = 0; i < count; i++)
        free(entries[i]);
    free(entries);
    return ret;
#endif
}

/**
 * @brief Recursively traverse all directories for ** pattern
 *
 * This implements the ** globstar pattern which matches zero or more directories.
 * For pattern "** /file.txt", it will match:
 * - file.txt (in current directory)
 * - dir/file.txt (in any subdirectory)
 * - dir/subdir/file.txt (in any nested subdirectory)
 *
 * NOTE: Results are added in natural order (interleaved) to match Ruby's behavior.
 */
int traverse_recursive_glob(const char *pattern, const char *base,
                            unsigned flags, glob_results_t *results, int sort_flag, bool is_initial)
{
#ifndef _WIN32
    /* POSIX Implementation with caching */
    const char *dir_path = base ? base : ".";

    dir_cache_node_t *dir_cache = get_cached_dir(dir_path);
    if (!dir_cache)
    {
        /* If directory doesn't exist, that's not an error for glob */
        if (errno == ENOENT || errno == ENOTDIR)
            return 0;
        return -1;
    }

    /* Iterate in sorted or unsorted order */
    size_t *iteration_order = malloc(dir_cache->count * sizeof(size_t));
    if (!iteration_order)
        return -1;

    for (size_t k = 0; k < dir_cache->count; k++)
        iteration_order[k] = k;

    if (sort_flag)
    {
        /* Simple sort */
        for (size_t i = 0; i < dir_cache->count; i++)
        {
            for (size_t j = i + 1; j < dir_cache->count; j++)
            {
                if (dirglob_compare_paths(dir_cache->entries[iteration_order[i]],
                                          dir_cache->entries[iteration_order[j]]) > 0)
                {
                    size_t tmp = iteration_order[i];
                    iteration_order[i] = iteration_order[j];
                    iteration_order[j] = tmp;
                }
            }
        }
    }

    /* Process entries in order */
    int ret = 0;

    /* 1. Zero-directory match */
    if (is_initial && pattern)
    {
        if (pattern[0] == '\0')
        {
            /* Trailing slash logic - can match . if dir */
        }
        else if (dirglob_fnmatch(pattern, ".", flags) == 0)
        {
            glob_results_add(results, ".");
        }
    }

    for (size_t k = 0; k < dir_cache->count; k++)
    {
        size_t i = iteration_order[k];
        char *name = dir_cache->entries[i];
        size_t discovery_index = dir_cache->discovery_indices[i];

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        char *full_path = path_join(base, name);
        if (!full_path)
        {
            ret = -1;
            break;
        }

        struct stat st;
        int exists = (stat(full_path, &st) == 0);
        int is_dir = (exists && S_ISDIR(st.st_mode));

        /* 2. Match pattern against current entry */
        if (pattern && pattern[0] != '\0')
        {
            const char *slash = strchr(pattern, '/');
            if (slash == NULL)
            {
                if (dirglob_fnmatch(pattern, name, flags) == 0)
                {
                    glob_results_add_with_index(results, name, discovery_index);
                }
            }
            else
            {
                size_t first_len = slash - pattern;
                char *first = malloc(first_len + 1);
                if (first)
                {
                    memcpy(first, pattern, first_len);
                    first[first_len] = '\0';

                    if (dirglob_fnmatch(first, name, flags) == 0 && is_dir)
                    {
                        const char *rest = slash + 1;
                        while (*rest == '/')
                            rest++;

                        glob_results_t match_results;
                        glob_results_init(&match_results);
                        if (process_file_pattern(rest, full_path, flags, &match_results, sort_flag) == 0)
                        {
                            for (size_t j = 0; j < match_results.count; j++)
                            {
                                char *prefixed = path_join(name, match_results.items[j]);
                                if (prefixed)
                                {
                                    glob_results_add_with_index(results, prefixed, match_results.discovery_indices[j]);
                                    free(prefixed);
                                }
                                free(match_results.items[j]);
                            }
                            free(match_results.items);
                            free(match_results.discovery_indices);
                        }
                        else
                        {
                            glob_results_clear(&match_results);
                        }
                    }
                    free(first);
                }
            }
        }
        else if (pattern && pattern[0] == '\0')
        {
            if (is_dir)
            {
                glob_results_add_with_index(results, name, discovery_index);
            }
        }

        /* 3. Recurse into subdirectories for ** */
        if (is_dir && (name[0] != '.' || (flags & FNM_DOTMATCH)))
        {
            glob_results_t subresults;
            glob_results_init(&subresults);
            if (traverse_recursive_glob(pattern, full_path, flags, &subresults, sort_flag, false) == 0)
            {
                for (size_t j = 0; j < subresults.count; j++)
                {
                    char *prefixed = path_join(name, subresults.items[j]);
                    if (prefixed)
                    {
                        glob_results_add_with_index(results, prefixed, subresults.discovery_indices[j]);
                        free(prefixed);
                    }
                    free(subresults.items[j]);
                }
                free(subresults.items);
                free(subresults.discovery_indices);
            }
            else
            {
                glob_results_clear(&subresults);
            }
        }

        free(full_path);
        if (ret != 0)
            break;
    }

    free(iteration_order);
    return ret;
#else
    /* Windows implementation */
    WIN32_FIND_DATAA find_data;
    HANDLE hFind;
    char *search_path;
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

    /* Collect all entries first to allow sorting and interleaving */
    size_t count = 0;
    size_t capacity = 32;
    char **entries = malloc(capacity * sizeof(char *));
    if (!entries)
    {
        FindClose(hFind);
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

        if (count >= capacity)
        {
            capacity *= 2;
            char **new_entries = realloc(entries, capacity * sizeof(char *));
            if (!new_entries)
            {
                for (size_t i = 0; i < count; i++)
                    free(entries[i]);
                free(entries);
                FindClose(hFind);
                return -1;
            }
            entries = new_entries;
        }
        entries[count++] = strdup(name);
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);

    /* Sort entries if requested */
    if (sort_flag)
    {
        glob_results_sort_array(entries, count);
    }

    /* Process entries in order */
    int ret = 0;

    /* 1. Zero-directory match */
    if (is_initial && pattern)
    {
        if (pattern[0] == '\0')
        {
            /* Trailing slash case */
        }
        else if (dirglob_fnmatch(pattern, ".", flags) == 0)
        {
            glob_results_add(results, ".");
        }
    }

    for (size_t i = 0; i < count; i++)
    {
        char *name = entries[i];
        char *full_path = path_join(base, name);
        if (!full_path)
        {
            ret = -1;
            break;
        }

        /* Check attributes */
        DWORD attrs = GetFileAttributesA(full_path);
        int exists = (attrs != INVALID_FILE_ATTRIBUTES);
        int is_dir = (exists && (attrs & FILE_ATTRIBUTE_DIRECTORY));

        /* 2. Recurse into subdirectories for ** */
        if (is_dir && (name[0] != '.' || (flags & FNM_DOTMATCH)))
        {
            glob_results_t subresults;
            glob_results_init(&subresults);
            if (traverse_recursive_glob(pattern, full_path, flags, &subresults, sort_flag, false) == 0)
            {
                for (size_t j = 0; j < subresults.count; j++)
                {
                    char *prefixed = path_join(name, subresults.items[j]);
                    if (prefixed)
                    {
                        glob_results_add(results, prefixed);
                        free(prefixed);
                    }
                    free(subresults.items[j]);
                }
                free(subresults.items);
            }
            else
            {
                glob_results_clear(&subresults);
                ret = -1;
            }
        }

        /* 3. Match pattern in current entry */
        if (pattern && pattern[0] != '\0')
        {
            const char *slash = strchr(pattern, '/');
            if (slash == NULL)
            {
                if (dirglob_fnmatch(pattern, name, flags) == 0)
                {
                    glob_results_add(results, name);
                }
            }
            else
            {
                size_t first_len = slash - pattern;
                char *first = malloc(first_len + 1);
                if (first)
                {
                    memcpy(first, pattern, first_len);
                    first[first_len] = '\0';

                    if (dirglob_fnmatch(first, name, flags) == 0 && is_dir)
                    {
                        const char *rest = slash + 1;
                        while (*rest == '/')
                            rest++;

                        glob_results_t match_results;
                        glob_results_init(&match_results);
                        if (process_file_pattern(rest, full_path, flags, &match_results, sort_flag) == 0)
                        {
                            for (size_t j = 0; j < match_results.count; j++)
                            {
                                char *prefixed = path_join(name, match_results.items[j]);
                                if (prefixed)
                                {
                                    glob_results_add(results, prefixed);
                                    free(prefixed);
                                }
                                free(match_results.items[j]);
                            }
                            free(match_results.items);
                        }
                        else
                        {
                            glob_results_clear(&match_results);
                        }
                    }
                    free(first);
                }
            }
        }
        else if (pattern && pattern[0] == '\0')
        {
            if (is_dir)
            {
                glob_results_add(results, name);
            }
        }

        free(full_path);
        if (ret != 0)
            break;
    }

    for (size_t i = 0; i < count; i++)
        free(entries[i]);
    free(entries);

    return ret;
#endif
}

int dirglob_compare_filesystem_order(const char *a, const char *b)
{
    char *dupA = strdup(a); // POSIX
    char *dupB = strdup(b);
    if (!dupA || !dupB) {
        free(dupA); free(dupB);
        return 0; // Error fallback
    }

    char *current_dir = strdup(".");
    if (!current_dir) {
        free(dupA); free(dupB);
        return 0;
    }

    char *pA = dupA;
    char *pB = dupB;
    int result = 0;

    /* Handle empty paths or . */
    
    while (*pA != '\0' && *pB != '\0')
    {
        /* Find next component */
        char *endA = strchr(pA, '/');
        if (endA) *endA = '\0';
        
        char *endB = strchr(pB, '/');
        if (endB) *endB = '\0';
        
        char *compA = pA;
        char *compB = pB;
        
        if (strcmp(compA, compB) == 0)
        {
            /* Check if we entered a directory */
            char *next = path_join(current_dir, compA);
            free(current_dir);
            current_dir = next;
            if (!current_dir) { result = 0; goto cleanup; } // ENOMEM
            
            /* Advance */
            if (endA) { 
                *endA = '/'; // restore
                pA = endA + 1; 
                while (*pA == '/') pA++; 
            } else {
                pA += strlen(pA);
            }
            
            if (endB) { 
                *endB = '/'; 
                pB = endB + 1; 
                while (*pB == '/') pB++; 
            } else {
                pB += strlen(pB);
            }
            continue;
        }

        /* Components differ. Compare indices in current_dir */
        dir_cache_node_t *cache = get_cached_dir(current_dir);
        if (!cache) {
             /* Cache missing? Fallback to string compare */
             result = strcmp(compA, compB);
             goto cleanup;
        }
        
        size_t idxA = (size_t)-1;
        size_t idxB = (size_t)-1;
        
        /* O(N) scan */
        for (size_t i=0; i < cache->count; i++) {
             if (strcmp(cache->entries[i], compA) == 0) idxA = i;
             if (strcmp(cache->entries[i], compB) == 0) idxB = i;
        }
        
        /* Handle . and .. if they appear */
        if (strcmp(compA, ".") == 0) idxA = 0; // Conceptual 'before'
        if (strcmp(compB, ".") == 0) idxB = 0;
        
        if (idxA == (size_t)-1 && idxB == (size_t)-1) result = strcmp(compA, compB);
        else if (idxA == (size_t)-1) result = 1; /* A not found > B found */
        else if (idxB == (size_t)-1) result = -1;
        else result = (idxA < idxB) ? -1 : 1;
        
        goto cleanup;
    }
    
    /* One ended */
    if (*pA == '\0' && *pB == '\0') result = 0;
    else if (*pA == '\0') result = -1; /* A is prefix of B, A < B */
    else result = 1;

cleanup:
    free(dupA);
    free(dupB);
    free(current_dir);
    return result;
}
