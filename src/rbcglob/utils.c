#include <rbcglob/internal/utils.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

bool has_glob_pattern(const char *str)
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

char *path_join(const char *base, const char *name)
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
    return strcmp(s1_in, s2_in);
}
