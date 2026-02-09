#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include "rbc/rbc.h"
#include "../utils/utils.h"

/// @brief Check if dotfile should be skipped (DOTMATCH restriction)
/// @param pattern Current position in pattern
/// @param string Current position in string
/// @param flags Matching flags
/// @return true if dotfile should be skipped
static inline bool rbc_should_skip_dot(const char *pattern, const char *string, unsigned flags)
{
    return !(flags & RBC_FNM_DOTMATCH) && *pattern != '.' && *string == '.';
}

/// @brief Advance pattern pointer and return segment start status
/// @param pattern Pointer to pattern position
/// @param flags Matching flags
/// @return true if advanced position is at segment start (after '/')
static inline bool rbc_advance_pattern(const char **pattern, unsigned flags)
{
    return *(*pattern)++ == '/' && flags & RBC_FNM_PATHNAME;
}

/// @brief Match bracket expression [..]
/// @param pattern Bracket pattern starting after '['
/// @param c Character to match
/// @param flags Matching flags
/// @return Next position in pattern after ']' if matched, NULL if no match
/// @note Ruby's fnmatch fails entire pattern if no closing `]`
static const char *rbc_match_bracket(const char *pattern, int c, unsigned flags)
{
    const char *p = pattern;
    bool invert = false;
    bool matched = false;

    if (flags & RBC_FNM_PATHNAME && c == '/')
        return NULL;

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
            return (invert ? !matched : matched) ? p + 1 : NULL;

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

            matched |= ch >= start && ch <= end;
            p += 3;
            continue;
        }

        matched |= rbc_char_match(*p, c, flags);
        p++;
    }

    return NULL;
}

/// @brief Internal fnmatch implementation with pattern length
#define RBC_FNMATCH_MAX_RECURSION 64

/// @param pattern Pattern string
/// @param pattern_end End of pattern (exclusive)
/// @param string Target string
/// @param flags Matching flags
/// @param depth Current recursion depth
/// @return true if matched, false if no match
static int rbc_fnmatch_recursive(const char *pattern, const char *pattern_end, const char *string, unsigned flags, int depth)
{
    if (depth > RBC_FNMATCH_MAX_RECURSION)
        return false;

    const char *p = pattern;
    const char *s = string;
    const char *p_start = NULL;   // Position after last `*` in pattern
    const char *s_start = NULL;   // Position in string when `*` was seen
    bool at_segment_start = true; // Track if at start of path segment

    if (p < pattern_end && rbc_should_skip_dot(p, s, flags))
        return false;

    while (*s)
    {
        if (p >= pattern_end)
            goto backtrack;

        switch (*p)
        {
        case '*':
            // Handle `**/` (If not PATHNAME, this behaves the same as `*`)
            if (flags & RBC_FNM_PATHNAME && at_segment_start && (p + 2) < pattern_end && p[1] == '*' && p[2] == '/')
            {
                // Skip `**/` patterns consecutively
                do
                {
                    p += 3;
                } while (p + 2 < pattern_end && p[0] == '*' && p[1] == '*' && p[2] == '/');

                const char *p_rest = p;

                // If pattern ends here, match rest of string
                if (p_rest >= pattern_end)
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
                for (;;)
                {
                    // Try matching rest of pattern from here
                    if (!rbc_should_skip_dot(p_rest, s_try, flags) && rbc_fnmatch_recursive(p_rest, pattern_end, s_try, flags, depth + 1))
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
            while (p < pattern_end && *p == '*')
                p++;

            // If pattern ends with `*`, match rest of string
            if (p >= pattern_end)
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
            if (flags & RBC_FNM_PATHNAME && at_segment_start && !(flags & RBC_FNM_DOTMATCH) && *s == '.')
                goto backtrack;

            // Record positions for backtracking
            p_start = p;
            s_start = s;
            at_segment_start = false;
            continue;

        case '?':
            if (flags & RBC_FNM_PATHNAME && *s == '/')
                goto backtrack;
            at_segment_start = rbc_advance_pattern(&p, flags);
            rbc_next_codepoint(&s);
            continue;

        case '[':
            p = rbc_match_bracket(p + 1, rbc_next_codepoint(&s), flags);
            if (!p)
                return false;
            at_segment_start = (p < pattern_end && *p == '/');
            continue;

        case '\\':
            if (!(flags & RBC_FNM_NOESCAPE) && p + 1 < pattern_end)
            {
                at_segment_start = rbc_advance_pattern(&p, flags);
                if (rbc_char_match(*p, *s, flags))
                {
                    at_segment_start = rbc_advance_pattern(&p, flags);
                    s++;
                    continue;
                }
                goto backtrack;
            }
            // **** FALLTHROUGH ****

        default:
            if (rbc_char_match(*p, *s, flags))
            {
                at_segment_start = rbc_advance_pattern(&p, flags);
                s++;
                continue;
            }
            goto backtrack;
        }

    backtrack:
        if (p_start)
        {
            // If PATHNAME, `*` cannot match across `/`
            if (flags & RBC_FNM_PATHNAME && *s_start == '/')
                return false;

            p = p_start;
            rbc_next_codepoint(&s_start); // Advance by one codepoint (UTF-8 safe)
            s = s_start;
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
        while (pp + 2 < pattern_end && pp[0] == '*' && pp[1] == '*' && pp[2] == '/')
            pp += 3;
        // Match if pattern ends with `**/` and string ends with `/`
        if (pp >= pattern_end && pp > p && s > string && s[-1] == '/')
            return true;
    }

    // Skip trailing `*`
    while (p < pattern_end && *p == '*')
        p++;

    return (p >= pattern_end);
}

/* ========================================================================
 * RBC Public API (Ruby-compatible)
 * ======================================================================== */

bool rbc_fnmatch(const char *pattern, const char *path, unsigned flags)
{
    if (!pattern || !path)
        return false;

    return rbc_fnmatch_recursive(pattern, pattern + strlen(pattern), path, flags, 0);
}

bool rbc_fnmatch_len(const char *pattern, size_t pattern_len, const char *path, unsigned flags)
{
    if (!pattern || !path || (pattern_len == 0))
        return false;

    return rbc_fnmatch_recursive(pattern, pattern + pattern_len, path, flags, 0);
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
