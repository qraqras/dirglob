/**
 * @file basename.c
 * @brief Ruby File.basename compatible implementation
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
 * @brief Get basename from path (Ruby File.basename equivalent)
 *
 * Returns the last component of the filename (after stripping trailing separators).
 * Both '/' and '\' are treated as separators on Windows.
 *
 * If suffix is given and present at the end of file_name, it is removed.
 * If suffix is ".*", any extension will be removed.
 *
 * @param file_name Path to process
 * @param suffix Optional suffix to remove (NULL or "" for no removal)
 * @return Newly allocated string, caller must free with free()
 */
char *rbcglob_basename(const char *file_name, const char *suffix)
{
    if (!file_name)
    {
        char *result = malloc(1);
        if (!result)
            return NULL;
        result[0] = '\0';
        return result;
    }

    size_t len = strlen(file_name);
    if (len == 0)
    {
        char *result = malloc(1);
        if (!result)
            return NULL;
        result[0] = '\0';
        return result;
    }

    /* Strip trailing separators */
    const char *end = file_name + len;
    while (end > file_name && IS_DIRSEP(*(end - 1)))
    {
        end--;
    }

    /* If only separators, return "/" */
    if (end == file_name)
    {
        char *result = malloc(2);
        if (!result)
            return NULL;
        strcpy(result, "/");
        return result;
    }

    /* Find the start of the basename (last separator before end) */
    const char *start = end;
    while (start > file_name && !IS_DIRSEP(*(start - 1)))
    {
        start--;
    }

    /* Calculate basename length */
    size_t base_len = end - start;

    /* Allocate and copy basename */
    char *base = malloc(base_len + 1);
    if (!base)
        return NULL;
    memcpy(base, start, base_len);
    base[base_len] = '\0';

    /* Handle suffix removal */
    if (suffix && suffix[0] != '\0')
    {
        size_t suffix_len = strlen(suffix);

        /* Special case: ".*" removes any extension */
        if (strcmp(suffix, ".*") == 0)
        {
            char *dot = strrchr(base, '.');
            if (dot && dot != base)
            {
                *dot = '\0';
            }
        }
        /* Remove exact suffix match */
        else if (base_len >= suffix_len)
        {
            if (strcmp(base + base_len - suffix_len, suffix) == 0)
            {
                base[base_len - suffix_len] = '\0';
            }
        }
    }

    return base;
}
