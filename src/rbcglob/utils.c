#include <rbcglob/internal/utils.h>
#include <rbcglob/internal/arena.h>
#include <rbcglob/rbcglob.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef _WIN32
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>
#endif

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

int rbcglob_compare_paths(const char *s1_in, const char *s2_in)
{
    /* P12 Optimization: Inline fast path for common cases */
    const unsigned char *s1 = (const unsigned char *)s1_in;
    const unsigned char *s2 = (const unsigned char *)s2_in;

    if (!s1_in || !s2_in)
        return (s1_in == s2_in) ? 0 : (s1_in ? 1 : -1);

    /* Fast path: check first characters */
    if (*s1 != *s2)
        return (int)*s1 - (int)*s2;
    if (*s1 == '\0')
        return 0;

    /* Fall back to strcmp for rest */
    return strcmp(s1_in, s2_in);
}
