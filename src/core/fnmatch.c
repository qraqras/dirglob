/*
 * Simple iterative fnmatch implementation
 * Inspired by SQLite glob.c (Public Domain)
 * Adapted for Ruby File.fnmatch compatibility
 */

/*
 * 参照実装: https://github.com/sqlite/sqlite/blob/master/src/func.c#L728
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "rbc/rbc.h"

/* Internal flag definitions */
#define FNM_PATHNAME 0x1
#define FNM_NOESCAPE 0x2
#define FNM_PERIOD 0x4
#define FNM_CASEFOLD 0x10

#define FNM_NOMATCH 1

/* UTF-8 character reader - returns character and advances pointer */
static int utf8_next(const char **str)
{
    const unsigned char *s = (const unsigned char *)*str;
    int c;

    if (*s == 0)
        return 0;

    if (*s < 0x80)
    {
        *str += 1;
        return *s;
    }

    /* Multi-byte UTF-8 */
    if ((*s & 0xE0) == 0xC0)
    {
        c = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *str += 2;
    }
    else if ((*s & 0xF0) == 0xE0)
    {
        c = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *str += 3;
    }
    else if ((*s & 0xF8) == 0xF0)
    {
        c = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
            ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *str += 4;
    }
    else
    {
        /* Invalid UTF-8, skip one byte */
        *str += 1;
        return -1;
    }

    return c;
}

/* Case-insensitive character comparison */
static bool char_match(int c1, int c2, int flags)
{
    if (c1 == c2)
        return true;
    if (!(flags & FNM_CASEFOLD))
        return false;

    /* Simple ASCII case folding */
    if (c1 >= 'A' && c1 <= 'Z')
        c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
        c2 += 32;
    return c1 == c2;
}

/* Match bracket expression [a-z] [!abc] [:alpha:] */
static bool match_bracket(const char *pattern, int c, int flags)
{
    bool invert = false;
    bool matched = false;
    const char *p = pattern;

    if (*p == '!' || *p == '^')
    {
        invert = true;
        p++;
    }

    /* Empty bracket is literal '[' */
    if (*p == ']')
        return false;

    while (*p && *p != ']')
    {
        if (*p == '-' && p[1] != ']' && p != pattern + (invert ? 1 : 0))
        {
            /* Range [a-z] */
            int start = (unsigned char)p[-1];
            int end = (unsigned char)p[1];
            if (c >= start && c <= end)
                matched = true;
            if ((flags & FNM_CASEFOLD) &&
                tolower(c) >= tolower(start) && tolower(c) <= tolower(end))
                matched = true;
            p += 2;
            continue;
        }

        /* Character class [:alpha:] */
        if (*p == '[' && p[1] == ':')
        {
            const char *class_end = strstr(p + 2, ":]");
            if (class_end)
            {
                char classname[16];
                int len = class_end - (p + 2);
                if (len > 0 && len < 16)
                {
                    memcpy(classname, p + 2, len);
                    classname[len] = 0;

                    /* Basic POSIX character classes */
                    if (strcmp(classname, "alpha") == 0 && isalpha(c))
                        matched = true;
                    else if (strcmp(classname, "digit") == 0 && isdigit(c))
                        matched = true;
                    else if (strcmp(classname, "alnum") == 0 && isalnum(c))
                        matched = true;
                    else if (strcmp(classname, "space") == 0 && isspace(c))
                        matched = true;
                    else if (strcmp(classname, "upper") == 0 && isupper(c))
                        matched = true;
                    else if (strcmp(classname, "lower") == 0 && islower(c))
                        matched = true;
                }
                p = class_end + 2;
                continue;
            }
        }

        /* Single character */
        if (char_match(*p, c, flags))
            matched = true;
        p++;
    }

    return invert ? !matched : matched;
}

/* Core matching algorithm - simple iterative with backtracking */
static int fnmatch_internal(const char *pattern, const char *string, int flags)
{
    const char *star_pat = NULL; /* Position after last * in pattern */
    const char *star_str = NULL; /* Position in string when * was seen */
    const char *p = pattern;
    const char *s = string;

    /* Check leading period */
    if ((flags & FNM_PERIOD) && *s == '.' && *p != '.')
    {
        return FNM_NOMATCH;
    }

    while (*s)
    {
        /* Check for ** (recursive wildcard) - only with PATHNAME flag and followed by / */
        if ((flags & FNM_PATHNAME) && p[0] == '*' && p[1] == '*' && p[2] == '/')
        {
            /* ** followed by / matches zero or more directory levels */
            const char *rest_pattern = p + 3;
            const char *try_pos = s;

            /* Try matching at current position and after each / in string */
            while (1)
            {
                /* Check for leading dot restriction */
                if ((flags & FNM_PERIOD) && *try_pos == '.' && *rest_pattern != '.')
                {
                    /* Can't match dotfile without explicit dot in pattern */
                }
                else
                {
                    /* Try matching rest of pattern from this position */
                    if (fnmatch_internal(rest_pattern, try_pos, flags) == 0)
                        return 0;
                }

                /* Find next / to try */
                while (*try_pos && *try_pos != '/')
                    try_pos++;

                if (!*try_pos)
                    break; /* No more slashes, matching failed */

                /* Skip the / and try again from next segment */
                try_pos++;
            }

            return FNM_NOMATCH;
        }

        if (*p == '*')
        {
            /* Single wildcard - save position for backtracking */
            /* With PATHNAME, * doesn't match / */
            if ((flags & FNM_PATHNAME) && *s == '/')
                goto backtrack;

            /* With PERIOD and PATHNAME, * at segment start doesn't match . */
            if ((flags & FNM_PERIOD) && (flags & FNM_PATHNAME) && *s == '.')
            {
                /* Check if this is at segment start (string start or after /) */
                if (s == string || s[-1] == '/')
                    goto backtrack;
            }

            star_pat = ++p;
            star_str = s;
            continue;
        }

        if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1])
        {
            /* Escaped character */
            p++;
            if (char_match(*p, *s, flags))
            {
                p++;
                s++;
                continue;
            }
        }
        else if (*p == '?')
        {
            /* Single character wildcard */
            if ((flags & FNM_PATHNAME) && *s == '/')
            {
                /* ? doesn't match / with PATHNAME flag */
                goto backtrack;
            }
            p++;
            utf8_next(&s); /* Skip one UTF-8 character */
            continue;
        }
        else if (*p == '[')
        {
            /* Bracket expression */
            const char *bracket_end = strchr(p + 1, ']');
            if (bracket_end)
            {
                int c = utf8_next(&s);
                if (match_bracket(p + 1, c, flags))
                {
                    p = bracket_end + 1;
                    continue;
                }
            }
            else
            {
                /* No closing ], treat [ as literal */
                if (char_match(*p, *s, flags))
                {
                    p++;
                    s++;
                    continue;
                }
            }
        }
        else if (char_match(*p, *s, flags))
        {
            /* Literal character match */
            p++;
            s++;
            continue;
        }

    backtrack:
        /* Mismatch - try to backtrack to last * */
        if (star_pat)
        {
            /* With PATHNAME, * cannot match across / */
            if ((flags & FNM_PATHNAME) && *star_str == '/')
                return FNM_NOMATCH;

            p = star_pat;
            s = ++star_str; /* Advance string and retry */
            continue;
        }

        return FNM_NOMATCH;
    }

    /* Skip trailing * in pattern */
    while (*p == '*')
        p++;

    return (*p == 0) ? 0 : FNM_NOMATCH;
}

/* Main fnmatch function with pathname handling */
static int fnmatch(const char *pattern, const char *string, int flags)
{
    if (!pattern || !string)
        return FNM_NOMATCH;

    if (!(flags & FNM_PATHNAME))
    {
        return fnmatch_internal(pattern, string, flags);
    }

    /* With FNM_PATHNAME, use fnmatch_internal which handles ** correctly */
    return fnmatch_internal(pattern, string, flags);
}

/* ========================================================================
 * RBC Wrapper Functions (Ruby-compatible API)
 * ======================================================================== */

/* Map RBC flags to internal flags */
static int rbc_flags_to_internal(unsigned rbc_flags)
{
    int flags = 0;

    if (rbc_flags & RBC_FNM_NOESCAPE)
        flags |= FNM_NOESCAPE;
    if (rbc_flags & RBC_FNM_PATHNAME)
        flags |= FNM_PATHNAME;
    /* [RBC CHANGE] FNM_DOTMATCH logic: Ruby's DOTMATCH means wildcards match leading dots.
     * musl's FNM_PERIOD means wildcards DON'T match leading dots.
     * So: if DOTMATCH is NOT set, we enable PERIOD behavior. */
    if (!(rbc_flags & RBC_FNM_DOTMATCH))
        flags |= FNM_PERIOD;
    if (rbc_flags & RBC_FNM_CASEFOLD)
        flags |= FNM_CASEFOLD;
    /* Note: RBC_FNM_EXTGLOB and RBC_FNM_SYSCASE not yet supported */

    return flags;
}

bool rbc_fnmatch(const char *pattern, const char *path, unsigned flags)
{
    if (!pattern || !path)
        return false;

    int internal_flags = rbc_flags_to_internal(flags);
    int result = fnmatch(pattern, path, internal_flags);
    return (result == 0); /* 0 = match, FNM_NOMATCH = no match */
}

/* Stub implementations for precompiled pattern API */
struct rbc_fnmatch_pattern_s
{
    char *pattern;
    unsigned flags;
};

rbc_fnmatch_pattern_t *rbc_fnmatch_compile(const char *pattern, unsigned flags)
{
    if (!pattern)
        return NULL;

    rbc_fnmatch_pattern_t *fp = malloc(sizeof(rbc_fnmatch_pattern_t));
    if (!fp)
        return NULL;

    fp->pattern = strdup(pattern);
    if (!fp->pattern)
    {
        free(fp);
        return NULL;
    }
    fp->flags = flags;
    return fp;
}

bool rbc_xfnmatch(const rbc_fnmatch_pattern_t *fp, const char *path, unsigned flags)
{
    if (!fp || !fp->pattern || !path)
        return false;
    /* Use flags from compiled pattern, but allow override */
    unsigned use_flags = fp->flags | flags;
    return rbc_fnmatch(fp->pattern, path, use_flags);
}

void rbc_fnmatch_pattern_free(rbc_fnmatch_pattern_t *fp)
{
    if (fp)
    {
        free(fp->pattern);
        free(fp);
    }
}
