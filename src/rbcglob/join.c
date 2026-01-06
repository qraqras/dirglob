#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/utils.h>
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
