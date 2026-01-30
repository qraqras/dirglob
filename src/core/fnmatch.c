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
        c = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
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

/// @brief Compare two characters with optional case folding
/// @param c1 character 1
/// @param c2 character 2
/// @param flags Matching flags
/// @return true if characters match
static bool char_match(int c1, int c2, unsigned flags)
{
    if (c1 == c2)
        return true;

    if (!(flags & RBC_FNM_CASEFOLD))
        return false;

    if (c1 >= 'A' && c1 <= 'Z')
        c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
        c2 += 32;

    return c1 == c2;
}

/// @brief Match bracket expression [..]
/// @param pattern Bracket pattern starting after '['
/// @param c Character to match
/// @param flags Matching flags
/// @return Next position in pattern after ']' if matched, NULL if no match
/// @note Ruby's fnmatch fails entire pattern if no closing `]`
static const char *match_bracket(const char *pattern, int c, unsigned flags)
{
    const char *p = pattern;
    bool invert = false;
    bool matched = false;

    if (*p == ']')
        return NULL;

    if (*p == '!' || *p == '^')
    {
        invert = true;
        p++;
    }

    while (*p)
    {
        if (*p == ']')
            return (invert ? !matched : matched) ? (p + 1) : NULL;

        if (*p == '\\' && !(flags & RBC_FNM_NOESCAPE) && p[1])
            p++;

        if (p[1] == '-' && p[2] && p[2] != ']')
        {
            int start = (unsigned char)*p;
            int end = (unsigned char)p[2];
            int ch = c;

            if (flags & RBC_FNM_CASEFOLD)
            {
                start = tolower(start);
                end = tolower(end);
                ch = tolower(ch);
            }

            matched |= (ch >= start && ch <= end);
            p += 3;
            continue;
        }

        matched |= char_match(*p, c, flags);
        p++;
    }

    return NULL;
}

/// @brief Internal fnmatch implementation
/// @param pattern Pattern string
/// @param string Target string
/// @param flags Matching flags
/// @return true if no match, false if matched
static int fnmatch_internal(const char *pattern, const char *string, unsigned flags)
{
    const char *p = pattern;
    const char *s = string;
    const char *p_strat = NULL;   // Position after last `*` in pattern
    const char *s_start = NULL;   // Position in string when `*` was seen
    bool at_segment_start = true; // Track if at start of path segment

    if (!(flags & RBC_FNM_DOTMATCH) && *s == '.' && *p != '.')
        return true;

    while (*s)
    {
        if (*p == '*')
        {
            // Handle `**/` (If PATHNAME is not set, this behaves the same as single `*`)
            if ((flags & RBC_FNM_PATHNAME) && at_segment_start && p[1] == '*' && p[2] == '/')
            {
                /* `**` matches zero or more directory levels */
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

            /* Skip consecutive stars (regular wildcard) */
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

            /* With PATHNAME, * doesn't match / */
            if ((flags & RBC_FNM_PATHNAME) && *s == '/')
            {
                /* Try 0-length match: skip * and continue with rest of pattern */
                at_segment_start = false;
                continue;
            }

            /* Without DOTMATCH, with PATHNAME, * at segment start doesn't match . */
            if (!(flags & RBC_FNM_DOTMATCH) && (flags & RBC_FNM_PATHNAME) && *s == '.')
            {
                /* Check if this is at segment start (string start or after /) */
                if (s == string || s[-1] == '/')
                    goto backtrack;
            }

            /* Save position for backtracking (try matching more characters) */
            p_strat = p;
            s_start = s;
            at_segment_start = false;
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
                at_segment_start = (*p == '/');
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
            at_segment_start = false;
            continue;
        }
        else if (*p == '[')
        {
            /* Bracket expression - Ruby fails entire pattern if no closing ] */
            const char *next = match_bracket(p + 1, utf8_next(&s), flags);
            if (next)
            {
                p = next;
                at_segment_start = (*p == '/');
                continue;
            }
            /* No match or invalid bracket - immediate failure (Ruby behavior) */
            return true;
        }
        else if (char_match(*p, *s, flags))
        {
            /* Literal character match */
            bool is_slash = (*p == '/' && (flags & RBC_FNM_PATHNAME));
            p++;
            s++;
            at_segment_start = is_slash;
            continue;
        }

    backtrack:
        /* Mismatch - try to backtrack to last * */
        if (p_strat)
        {
            /* With PATHNAME, * cannot match across / */
            if ((flags & RBC_FNM_PATHNAME) && *s_start == '/')
                return true;

            p = p_strat;
            s = ++s_start; /* Advance string and retry */
            at_segment_start = false;
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
