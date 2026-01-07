/**
 * @file dirname.c
 * @brief Ruby File.dirname compatible implementation
 */

#include <rbcglob/rbcglob.h>
#include <stdlib.h>
#include <string.h>

/* Platform-specific directory separator detection */
#ifdef _WIN32
#define IS_DIRSEP(c) ((c) == '/' || (c) == '\\')
#else
#define IS_DIRSEP(c) ((c) == '/')
#endif

/**
 * @brief Get directory name from path (Ruby File.dirname equivalent)
 *
 * Returns all components except the last one (after stripping trailing separators).
 * Supports both '/' and '\' on Windows.
 *
 * If level is given, removes the last `level` components, not only one.
 *
 * @param file_name Path to process
 * @param level Number of trailing components to remove (default: 1)
 * @return Newly allocated string, caller must free with free()
 */
char *rbcglob_dirname(const char *file_name, int level)
{
    if (!file_name || level < 1)
    {
        char *result = malloc(2);
        if (!result)
            return NULL;
        strcpy(result, ".");
        return result;
    }

    size_t len = strlen(file_name);
    if (len == 0)
    {
        char *result = malloc(2);
        if (!result)
            return NULL;
        strcpy(result, ".");
        return result;
    }

    /* Allocate working buffer */
    char *path = malloc(len + 1);
    if (!path)
        return NULL;
    strcpy(path, file_name);

    /* Strip trailing separators first */
    while (len > 1 && IS_DIRSEP(path[len - 1]))
    {
        path[--len] = '\0';
    }

    /* Handle root cases */
    if (len == 0 || (len == 1 && IS_DIRSEP(path[0])))
    {
        free(path);
        char *result = malloc(2);
        if (!result)
            return NULL;
        strcpy(result, "/");
        return result;
    }

#ifdef _WIN32
    /* Handle Windows drive letter: "C:" or "C:/" */
    if (len >= 2 && path[1] == ':')
    {
        if (len == 2 || (len == 3 && IS_DIRSEP(path[2])))
        {
            char *result = malloc(len + 1);
            if (!result)
            {
                free(path);
                return NULL;
            }
            strcpy(result, path);
            free(path);
            return result;
        }
    }
#endif

    /* Remove `level` components */
    for (int i = 0; i < level; i++)
    {
        /* Find the last separator */
        char *last_sep = NULL;
        for (size_t j = 0; j < len; j++)
        {
            if (IS_DIRSEP(path[j]))
            {
                last_sep = &path[j];
            }
        }

        if (!last_sep)
        {
            /* No separator found, return "." */
            free(path);
            char *result = malloc(2);
            if (!result)
                return NULL;
            strcpy(result, ".");
            return result;
        }

        /* Truncate at the separator */
        *last_sep = '\0';
        len = last_sep - path;

        /* If we're now empty, return root */
        if (len == 0)
        {
            free(path);
            char *result = malloc(2);
            if (!result)
                return NULL;
            strcpy(result, "/");
            return result;
        }

#ifdef _WIN32
        /* Handle Windows drive letter after truncation */
        if (len == 2 && path[1] == ':')
        {
            free(path);
            char *result = malloc(4); /* "C:/" */
            if (!result)
                return NULL;
            result[0] = file_name[0];
            result[1] = ':';
            result[2] = '/';
            result[3] = '\0';
            return result;
        }
#endif

        /* Strip any trailing separators after truncation */
        while (len > 1 && IS_DIRSEP(path[len - 1]))
        {
            path[--len] = '\0';
        }
    }

    /* If result is empty after all levels, return "." */
    if (len == 0)
    {
        free(path);
        char *result = malloc(2);
        if (!result)
            return NULL;
        strcpy(result, ".");
        return result;
    }

    /* Return the result */
    char *result = malloc(len + 1);
    if (!result)
    {
        free(path);
        return NULL;
    }
    strcpy(result, path);
    free(path);
    return result;
}
