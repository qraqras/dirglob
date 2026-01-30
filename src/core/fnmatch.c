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

/// @brief Check if dotfile should be skipped (DOTMATCH restriction)
/// @param pattern Current position in pattern
/// @param string Current position in string
/// @param flags Matching flags
/// @return true if dotfile should be skipped
static inline bool should_skip_dot(const char *pattern, const char *string, unsigned flags)
{
    return !(flags & RBC_FNM_DOTMATCH) && *pattern != '.' && *string == '.';
}

/// @brief Advance pattern pointer and return segment start status
/// @param p Pointer to pattern position
/// @param flags Matching flags
/// @return true if advanced position is at segment start (after '/')
static inline bool advance_pattern(const char **p, unsigned flags)
{
    return (*(*p)++ == '/') && (flags & RBC_FNM_PATHNAME);
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
/// @return true if matched, false if no match
static int fnmatch_internal(const char *pattern, const char *string, unsigned flags)
{
    const char *p = pattern;
    const char *s = string;
    const char *p_start = NULL;   // Position after last `*` in pattern
    const char *s_start = NULL;   // Position in string when `*` was seen
    bool at_segment_start = true; // Track if at start of path segment

    if (should_skip_dot(p, s, flags))
        return false;

    while (*s)
    {
        switch (*p)
        {
        case '*':
            // Handle `**/` (If not PATHNAME, this behaves the same as `*`)
            if ((flags & RBC_FNM_PATHNAME) && at_segment_start && p[1] == '*' && p[2] == '/')
            {
                // Skip `**/` patterns consecutively
                while (p[0] == '*' && p[1] == '*' && p[2] == '/')
                    p += 3;

                const char *p_rest = p;

                // If pattern ends here, match rest of string
                if (*p_rest == '\0')
                {
                    if (*s == '\0')
                        return true;
                    const char *end = s;
                    while (*end)
                        end++;
                    return (end > s && end[-1] == '/');
                }

                const char *s_try = s;

                // Try matching at current position and after each / in string
                while (1)
                {
                    // Try matching rest of pattern from here
                    if (!should_skip_dot(p_rest, s_try, flags) && fnmatch_internal(p_rest, s_try, flags))
                        return true;

                    // Advance to next `/` in string
                    while (*s_try && *s_try != '/')
                        s_try++;

                    // No more `/` to try
                    if (!*s_try)
                        break;

                    s_try++;
                }
                return false;
            }

            // Skip consecutive `*`
            while (*p == '*')
                p++;

            // If pattern ends with `*`, match rest of string
            if (*p == '\0')
            {
                if (flags & RBC_FNM_PATHNAME)
                {
                    while (*s)
                    {
                        if (*s == '/')
                            return false;
                        s++;
                    }
                }
                return true;
            }

            // If not DOTMATCH, `*` cannot match `.` at segment start
            if ((flags & RBC_FNM_PATHNAME) && at_segment_start && !(flags & RBC_FNM_DOTMATCH) && *s == '.')
                goto backtrack;

            // Record positions for backtracking
            p_start = p;
            s_start = s;
            at_segment_start = false;
            continue;

        case '?':
            if ((flags & RBC_FNM_PATHNAME) && *s == '/')
                goto backtrack;
            at_segment_start = advance_pattern(&p, flags);
            utf8_next(&s);
            continue;

        case '[':
            p = match_bracket(p + 1, utf8_next(&s), flags);
            if (!p)
                return false;
            at_segment_start = (*p == '/');
            continue;

        case '\\':
            if (!(flags & RBC_FNM_NOESCAPE) && p[1])
            {
                at_segment_start = advance_pattern(&p, flags);
                if (char_match(*p, *s, flags))
                {
                    at_segment_start = advance_pattern(&p, flags);
                    s++;
                    continue;
                }
                goto backtrack;
            }
            // **** FALLTHROUGH ****

        default:
            if (char_match(*p, *s, flags))
            {
                at_segment_start = advance_pattern(&p, flags);
                s++;
                continue;
            }
            goto backtrack;
        }

    backtrack:
        if (p_start)
        {
            // If PATHNAME, `*` cannot match across `/`
            if ((flags & RBC_FNM_PATHNAME) && *s_start == '/')
                return false;

            p = p_start;
            s = ++s_start; // Advance string and retry
            // Check if p_start is right after '/' in pattern
            at_segment_start = (p_start > pattern && p_start[-1] == '/');
            continue;
        }
        return false;
    }

    // If PATHNAME, skip trailing `**/` at end of pattern
    if (flags & RBC_FNM_PATHNAME)
    {
        const char *pp = p;
        while (pp[0] == '*' && pp[1] == '*' && pp[2] == '/')
            pp += 3;
        // Match if pattern ends with `**/` and string ends with `/`
        if (*pp == '\0' && pp > p && s > string && s[-1] == '/')
            return true;
    }

    // Skip trailing `*`
    while (*p == '*')
        p++;

    return (*p == 0);
}

/* Main fnmatch function */
static bool fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
        return false;

    return fnmatch_internal(pattern, string, flags);
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
