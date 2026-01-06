/**
 * @file extname.c
 * @brief Ruby File.extname compatible implementation
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
 * @brief Get file extension from path (Ruby File.extname equivalent)
 *
 * Returns the extension (the portion of file name starting from the last period).
 *
 * Edge cases:
 * - If path is a dotfile (.profile), returns ""
 * - If path starts with a period, the starting dot is not part of the extension
 * - Empty string returned when period is the last character (on non-Windows)
 * - On Windows, trailing dots are truncated
 *
 * @param path Path to process
 * @return Newly allocated string, caller must free with free()
 */
char *rbcglob_extname(const char *path)
{
    if (!path)
    {
        char *result = malloc(1);
        if (!result)
            return NULL;
        result[0] = '\0';
        return result;
    }

    size_t len = strlen(path);
    if (len == 0)
    {
        char *result = malloc(1);
        if (!result)
            return NULL;
        result[0] = '\0';
        return result;
    }

#ifdef _WIN32
    /* On Windows, strip trailing dots first */
    while (len > 0 && path[len - 1] == '.')
    {
        len--;
    }
    if (len == 0)
    {
        char *result = malloc(1);
        if (!result)
            return NULL;
        result[0] = '\0';
        return result;
    }
#endif

    /* Find the last directory separator */
    const char *last_sep = NULL;
    for (size_t i = 0; i < len; i++)
    {
        if (IS_DIRSEP(path[i]))
        {
            last_sep = &path[i];
        }
    }

    /* Find the basename start */
    const char *basename_start = last_sep ? last_sep + 1 : path;

    /* If basename starts with a dot, it's a dotfile - no extension */
    if (*basename_start == '.')
    {
        /* Check if there's another dot after the leading dot */
        const char *dot = strchr(basename_start + 1, '.');
        if (!dot || dot >= path + len)
        {
            /* No extension (e.g., ".profile") */
            char *result = malloc(1);
            if (!result)
                return NULL;
            result[0] = '\0';
            return result;
        }
        /* Has extension (e.g., ".profile.sh" -> ".sh") */
        size_t ext_len = (path + len) - dot;
        char *result = malloc(ext_len + 1);
        if (!result)
            return NULL;
        memcpy(result, dot, ext_len);
        result[ext_len] = '\0';
        return result;
    }

    /* Find the last dot in the basename */
    const char *last_dot = NULL;
    for (const char *p = basename_start; p < path + len; p++)
    {
        if (*p == '.')
        {
            last_dot = p;
        }
    }

    /* No dot found */
    if (!last_dot)
    {
        char *result = malloc(1);
        if (!result)
            return NULL;
        result[0] = '\0';
        return result;
    }

#ifndef _WIN32
    /* On non-Windows, if dot is at the end, return "." */
    if (last_dot == path + len - 1)
    {
        char *result = malloc(2);
        if (!result)
            return NULL;
        strcpy(result, ".");
        return result;
    }
#endif

    /* Return the extension including the dot */
    size_t ext_len = (path + len) - last_dot;
    char *result = malloc(ext_len + 1);
    if (!result)
        return NULL;
    memcpy(result, last_dot, ext_len);
    result[ext_len] = '\0';
    return result;
}
