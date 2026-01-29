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
static bool char_match(int c1, int c2, unsigned flags)
{
    if (c1 == c2)
        return true;
    if (!(flags & RBC_FNM_CASEFOLD))
        return false;

    /* Simple ASCII case folding */
    if (c1 >= 'A' && c1 <= 'Z')
        c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
        c2 += 32;
    return c1 == c2;
}

/* Match bracket expression [a-z] [!abc] */
static bool match_bracket(const char *pattern, int c, unsigned flags)
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
            if ((flags & RBC_FNM_CASEFOLD) && tolower(c) >= tolower(start) && tolower(c) <= tolower(end))
                matched = true;
            p += 2;
            continue;
        }

        /* Single character */
        if (char_match(*p, c, flags))
            matched = true;
        p++;
    }

    return invert ? !matched : matched;
}

/* Core matching algorithm - simple iterative with backtracking */
static int fnmatch_internal(const char *pattern, const char *string, unsigned flags)
{
    const char *star_pat = NULL; /* Position after last * in pattern */
    const char *star_str = NULL; /* Position in string when * was seen */
    const char *p = pattern;
    const char *s = string;

    /* Check leading period (when DOTMATCH is not set) */
    if (!(flags & RBC_FNM_DOTMATCH) && *s == '.' && *p != '.')
    {
        /* Dot must be matched explicitly, not by wildcards */
        if (*p != '.')
            return true; /* No match */
    }

    while (*s)
    {
        /* Check for ** (recursive wildcard) - only with PATHNAME flag and followed by / */
        if ((flags & RBC_FNM_PATHNAME) && p[0] == '*' && p[1] == '*' && p[2] == '/')
        {
            /* ** followed by / matches zero or more directory levels */
            const char *rest_pattern = p + 3;
            const char *try_pos = s;

            /* Try matching at current position and after each / in string */
            while (1)
            {
                /* Check for leading dot restriction (when DOTMATCH is not set) */
                if (!(flags & RBC_FNM_DOTMATCH) && *try_pos == '.' && *rest_pattern != '.')
                {
                    /* Can't match dotfile without explicit dot in pattern */
                }
                else
                {
                    /* Try matching rest of pattern from this position */
                    if (!fnmatch_internal(rest_pattern, try_pos, flags))
                        return false;
                }

                /* Find next / to try */
                while (*try_pos && *try_pos != '/')
                    try_pos++;

                if (!*try_pos)
                    break; /* No more slashes, matching failed */

                /* Skip the / and try again from next segment */
                try_pos++;
            }

            return true;
        }

        if (*p == '*')
        {
            /* Single wildcard - save position for backtracking */
            /* With PATHNAME, * doesn't match / */
            if ((flags & RBC_FNM_PATHNAME) && *s == '/')
                goto backtrack;

            /* Without DOTMATCH, with PATHNAME, * at segment start doesn't match . */
            if (!(flags & RBC_FNM_DOTMATCH) && (flags & RBC_FNM_PATHNAME) && *s == '.')
            {
                /* Check if this is at segment start (string start or after /) */
                if (s == string || s[-1] == '/')
                    goto backtrack;
            }

            /* Skip consecutive stars */
            p++;
            while (*p == '*')
                p++;

            /* Optimization: if pattern ends with *, match rest of string */
            if (*p == '\0')
            {
                if (flags & RBC_FNM_PATHNAME)
                {
                    /* With PATHNAME, * doesn't match / - check if any / remains */
                    while (*s)
                    {
                        if (*s == '/')
                            return true; /* No match - / found */
                        s++;
                    }
                }
                return false; /* Match - pattern ends with * */
            }

            star_pat = p;
            star_str = s;
            continue;
        }

        if (*p == '\\' && !(flags & RBC_FNM_NOESCAPE) && p[1])
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
            if ((flags & RBC_FNM_PATHNAME) && *s == '/')
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
            if ((flags & RBC_FNM_PATHNAME) && *star_str == '/')
                return true;

            p = star_pat;
            s = ++star_str; /* Advance string and retry */
            continue;
        }

        return true;
    }

    /* Skip trailing * in pattern */
    while (*p == '*')
        p++;

    return (*p != 0);
}

/* Main fnmatch function */
static bool fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
        return false;

    return !fnmatch_internal(pattern, string, flags);
}

/* ========================================================================
 * RBC Wrapper Functions (Ruby-compatible API)
 * ======================================================================== */

bool rbc_fnmatch(const char *pattern, const char *path, unsigned flags)
{
    if (!pattern || !path)
        return false;

    /* Note: RBC_FNM_EXTGLOB and RBC_FNM_SYSCASE not yet supported */
    return fnmatch(pattern, path, flags);
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
