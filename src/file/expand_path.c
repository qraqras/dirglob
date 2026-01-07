#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/file.h>
#include <rbcglob/internal/dir.h>
#include <rbcglob/internal/utils.h>
#include <rbcglob/internal/arena.h>
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
