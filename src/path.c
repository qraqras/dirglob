/**
 * @file basename.c
 * @brief Ruby File.basename compatible implementation
 */

#include <rbcglob/rbcglob.h>
#include <stdlib.h>
#include <string.h>
#include "arena.h"

char *rbcglob_join_arena(const char **args, size_t count, rbcglob_arena_t *arena);

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
#include <rbcglob/rbcglob.h>
#include "utils.h"
#include "arena.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#define IS_DIRSEP(c) ((c) == '/' || (c) == '\\')
/* Windows absolute: Drive letter (C:/ or C:\) or UNC (//host/share or \\host\share)
 * Ruby compatible: has_drive_letter(p) && isdirsep(p[2]) or isdirsep(p[0]) && isdirsep(p[1]) */
#define IS_ABSOLUTE(p) ((p)[0] && (p)[1] && (((p)[1] == ':' && (p)[2] && IS_DIRSEP((p)[2])) || (IS_DIRSEP((p)[0]) && IS_DIRSEP((p)[1]))))
#else
#include <unistd.h>
#define IS_DIRSEP(c) ((c) == '/')
#define IS_ABSOLUTE(p) ((p)[0] == '/')
#endif

/* Forward declaration for tilde helper */
static char *expand_tilde_internal(const char *path, rbcglob_arena_t *arena);

/**
 * @brief Normalize path (resolve . and ..)
 *
 * Shared by expand_path and absolute_path.
 * Exported for use in absolute_path.c.
 */
char *rbcglob_normalize_path_arena(const char *path_to_normalize, rbcglob_arena_t *arena)
{
    char *result = rbcglob_arena_alloc(arena, strlen(path_to_normalize) + 2);
    if (!result)
        return NULL;

    char *dst = result;
    const char *src = path_to_normalize;
    size_t min_comps = 0;
    bool is_unc = false;

#ifdef _WIN32
    /* Handle drive letter or UNC prefix */
    if (src[0] && src[1] == ':' && IS_DIRSEP(src[2]))
    {
        *dst++ = src[0];
        *dst++ = ':';
        *dst++ = '/';
        src += 3;
    }
    else if (IS_DIRSEP(src[0]) && IS_DIRSEP(src[1]))
    {
        /* UNC Path Root: //host/share */
        *dst++ = '/';
        *dst++ = '/';
        src += 2;
        is_unc = true;
    }
    else if (IS_DIRSEP(src[0]))
    {
        *dst++ = '/';
        src++;
    }
#else
    if (src[0] == '/')
    {
        *dst++ = '/';
        src++;
    }
#endif

    /* Component processing */
    const char *comp_start = src;
    char **components = rbcglob_arena_alloc(arena, sizeof(char *) * (strlen(src) / 2 + 1));
    size_t ncomps = 0;

    while (*src)
    {
        while (*src && IS_DIRSEP(*src))
            src++;
        if (!*src)
            break;

        comp_start = src;
        while (*src && !IS_DIRSEP(*src))
            src++;
        size_t len = src - comp_start;

        if (len == 1 && comp_start[0] == '.')
        {
            /* Skip . */
        }
        else if (len == 2 && comp_start[0] == '.' && comp_start[1] == '.')
        {
            /* Prevent going above root (drive or UNC share) */
            if (ncomps > min_comps)
                ncomps--;
        }
        else
        {
            char *comp = rbcglob_arena_alloc(arena, len + 1);
            memcpy(comp, comp_start, len);
            comp[len] = '\0';
            components[ncomps++] = comp;

            /* UNC requires at least 2 components (host and share) to be the root */
            if (is_unc && ncomps == 2)
            {
                min_comps = 2;
            }
        }
    }

    /* Join components back */
    for (size_t i = 0; i < ncomps; i++)
    {
        size_t len = strlen(components[i]);
        memcpy(dst, components[i], len);
        dst += len;
        if (i < ncomps - 1)
            *dst++ = '/';
    }
    *dst = '\0';

    /* Ensure non-empty for root */
    if (result[0] == '\0')
    {
        result[0] = '/';
        result[1] = '\0';
    }

    return result;
}

char *rbcglob_expand_path_arena(const char *file_name, const char *dir_string, rbcglob_arena_t *arena)
{
    if (!file_name)
        return NULL;

    const char *work_path = file_name;
    const char *work_base = dir_string;

    /* 1. Tilde Expansion (File.expand_path only) */
    if (file_name[0] == '~')
    {
        char *expanded_tilde = expand_tilde_internal(file_name, arena);
        if (expanded_tilde)
        {
            work_path = expanded_tilde;
            /* If tilde was expanded, dir_string is ignored */
            work_base = NULL;
        }
    }

    /* 2. Absolute Path Handling */
    char *joined;
    if (IS_ABSOLUTE(work_path))
    {
        joined = (char *)work_path;
    }
    else
    {
        /* Not absolute, need to join with dir_string or CWD */
        if (!work_base)
        {
            char cwd[4096];
            if (getcwd(cwd, sizeof(cwd)))
            {
                joined = rbcglob_join_arena((const char *[]){cwd, work_path}, 2, arena);
            }
            else
            {
                joined = (char *)work_path;
            }
        }
        else
        {
            joined = rbcglob_join_arena((const char *[]){work_base, work_path}, 2, arena);
        }
    }

    /* 3. Normalization (shared with absolute_path) */
    return rbcglob_normalize_path_arena(joined, arena);
}

char *rbcglob_expand_path(const char *file_name, const char *dir_string)
{
    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 0);

    char *result_arena = rbcglob_expand_path_arena(file_name, dir_string, &arena);
    char *result = rbcglob_strdup(result_arena);

    rbcglob_arena_destroy(&arena);
    return result;
}

static char *expand_tilde_internal(const char *path, rbcglob_arena_t *arena)
{
    if (!path || path[0] != '~')
        return (char *)path;

    const char *sep = strpbrk(path, "/\\");
    size_t user_len = sep ? (size_t)(sep - path - 1) : strlen(path + 1);
    char *home = NULL;

    if (user_len == 0)
    {
        /* Use internal rbcglob_home_dir for ~ expansion */
        home = rbcglob_home_dir(NULL);
    }
    else
    {
        /* Use internal rbcglob_home_dir for ~user expansion */
        char user[256];
        if (user_len < sizeof(user))
        {
            memcpy(user, path + 1, user_len);
            user[user_len] = '\0';
            home = rbcglob_home_dir(user);
        }
    }

    if (!home)
        return NULL;

    /* Join home directory with remaining path */
    char *result = rbcglob_join_arena((const char *[]){home, sep ? (sep + 1) : ""}, 2, arena);
    free(home);
    return result;
}
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
#include <rbcglob/rbcglob.h>
#include "utils.h"
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#define IS_DIRSEP(c) ((c) == '/' || (c) == '\\')
#else
#define IS_DIRSEP(c) ((c) == '/')
#endif

char *rbcglob_join(const char **args, size_t count)
{
    if (count == 0)
        return rbcglob_strdup("");

    size_t total_len = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (args[i])
            total_len += strlen(args[i]) + 1;
    }

    char *result = malloc(total_len + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < count; i++)
    {
        const char *arg = args[i];
        if (!arg)
            continue;

        if (*arg == '\0')
        {
            if (p == result)
                *p++ = '/';
            else if (!IS_DIRSEP(*(p - 1)))
                *p++ = '/';
            continue;
        }

        if (p != result)
        {
            bool prev_sep = IS_DIRSEP(*(p - 1));
            bool next_sep = IS_DIRSEP(*arg);

            if (prev_sep && next_sep)
            {
                while (IS_DIRSEP(*arg))
                    arg++;
            }
            else if (!prev_sep && !next_sep)
            {
                *p++ = '/';
            }
        }

        size_t len = strlen(arg);
        if (len > 0)
        {
            memcpy(p, arg, len);
            p += len;
        }
    }
    *p = '\0';

    return result;
}

char *rbcglob_join_arena(const char **args, size_t count, rbcglob_arena_t *arena)
{
    if (count == 0 || !arena)
        return (char *)rbcglob_arena_alloc(arena, 1);

    size_t total_len = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (args[i])
            total_len += strlen(args[i]) + 1;
    }

    char *result = (char *)rbcglob_arena_alloc(arena, total_len + 1);
    if (!result)
        return NULL;

    char *p = result;
    for (size_t i = 0; i < count; i++)
    {
        const char *arg = args[i];
        if (!arg)
            continue;

        if (*arg == '\0')
        {
            if (p == result)
                *p++ = '/';
            else if (!IS_DIRSEP(*(p - 1)))
                *p++ = '/';
            continue;
        }

        if (p != result)
        {
            bool prev_sep = IS_DIRSEP(*(p - 1));
            bool next_sep = IS_DIRSEP(*arg);

            if (prev_sep && next_sep)
            {
                while (IS_DIRSEP(*arg))
                    arg++;
            }
            else if (!prev_sep && !next_sep)
            {
                *p++ = '/';
            }
        }

        size_t len = strlen(arg);
        if (len > 0)
        {
            memcpy(p, arg, len);
            p += len;
        }
    }
    *p = '\0';

    return result;
}
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#endif

char *rbcglob_home_dir(const char *user)
{
    /* Get home directory for specified user, or current user if NULL */
    if (!user || !*user)
    {
        /* Current user */
#ifdef _WIN32
        /* Windows: Try USERPROFILE first, then HOMEPATH */
        char *home = getenv("USERPROFILE");
        if (!home)
        {
            home = getenv("HOMEPATH");
            if (home)
            {
                /* HOMEPATH may need HOMEDRIVE prepended */
                char *drive = getenv("HOMEDRIVE");
                if (drive)
                {
                    size_t drive_len = strlen(drive);
                    size_t home_len = strlen(home);
                    char *full_path = malloc(drive_len + home_len + 1);
                    if (full_path)
                    {
                        memcpy(full_path, drive, drive_len);
                        memcpy(full_path + drive_len, home, home_len + 1);
                        return full_path;
                    }
                }
            }
        }
        return home ? rbcglob_strdup(home) : NULL;
#else
        /* Unix: Try HOME environment variable first */
        char *home = getenv("HOME");
        if (home && *home)
        {
            return rbcglob_strdup(home);
        }

        /* Fallback to getpwuid() */
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_dir)
        {
            return rbcglob_strdup(pw->pw_dir);
        }

        return NULL;
#endif
    }
    else
    {
        /* Specified user */
#ifdef _WIN32
        /* Windows: Getting another user's home directory is not straightforward */
        /* Would require NetUserGetInfo or similar Win32 APIs */
        return NULL;
#else
        /* Unix: Use getpwnam() */
        struct passwd *pw = getpwnam(user);
        if (pw && pw->pw_dir)
        {
            return rbcglob_strdup(pw->pw_dir);
        }
        return NULL;
#endif
    }
}
