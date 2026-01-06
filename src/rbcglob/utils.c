#include <rbcglob/internal/utils.h>
#include <rbcglob/internal/arena.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

bool rbcglob_has_glob_pattern(const char *str)
{
    if (!str)
        return false;

    while (*str)
    {
        switch (*str)
        {
        case '*':
        case '?':
        case '[':
        case '{':
            return true;
        case '\\':
            /* Skip escaped character */
            if (str[1])
                str++;
            break;
        }
        str++;
    }
    return false;
}

char *rbcglob_path_join_arena(rbcglob_arena_t *arena, const char *base, const char *name)
{
    if (!name)
        return NULL;
    if (!base || base[0] == '\0')
        return rbcglob_arena_strdup(arena, name);

    size_t base_len = strlen(base);
    size_t name_len = strlen(name);

    /* Check if base already ends with separator */
    bool needs_sep = (base_len > 0 && base[base_len - 1] != '/' && base[base_len - 1] != '\\');

    size_t total = base_len + (needs_sep ? 1 : 0) + name_len + 1;
    char *result = rbcglob_arena_alloc(arena, total);
    if (!result)
        return NULL;

    memcpy(result, base, base_len);
    if (needs_sep)
    {
        result[base_len] = '/';
        memcpy(result + base_len + 1, name, name_len + 1);
    }
    else
    {
        memcpy(result + base_len, name, name_len + 1);
    }

    return result;
}

char *rbcglob_strdup(const char *str)
{
    if (!str)
        return NULL;

    size_t len = strlen(str);
    char *dup = malloc(len + 1);
    if (!dup)
        return NULL;

    memcpy(dup, str, len + 1);
    return dup;
}

char *rbcglob_path_join(const char *base, const char *name)
{
    if (!name)
        return NULL;
    if (!base || base[0] == '\0')
        return rbcglob_strdup(name);

    size_t base_len = strlen(base);
    size_t name_len = strlen(name);

    /* Check if base already ends with separator */
    bool needs_sep = (base_len > 0 && base[base_len - 1] != '/' && base[base_len - 1] != '\\');

    size_t total = base_len + (needs_sep ? 1 : 0) + name_len + 1;
    char *result = malloc(total);
    if (!result)
        return NULL;

    memcpy(result, base, base_len);
    if (needs_sep)
    {
        result[base_len] = '/';
        memcpy(result + base_len + 1, name, name_len + 1);
    }
    else
    {
        memcpy(result + base_len, name, name_len + 1);
    }

    return result;
}

int rbcglob_compare_paths(const char *s1_in, const char *s2_in)
{
    /* P12 Optimization: Inline fast path for common cases */
    const unsigned char *s1 = (const unsigned char *)s1_in;
    const unsigned char *s2 = (const unsigned char *)s2_in;

    /* Fast path: check first characters */
    if (*s1 != *s2)
        return (int)*s1 - (int)*s2;
    if (*s1 == '\0')
        return 0;

    /* Fall back to strcmp for rest */
    return strcmp(s1_in, s2_in);
}
