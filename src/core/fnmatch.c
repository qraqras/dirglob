#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "rbc/rbc.h"

/// @brief Internal fnmatch implementation with pattern length
#define RBC_FNMATCH_MAX_RECURSION 64

/// @brief Decode next UTF-8 codepoint
/// @param[in, out] p Pointer to string pointer (updated to next position)
/// @return Next codepoint as uint32_t
uint32_t rbc_utf8_decode(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = *s;
    // End of string
    if (c == 0)
        return 0;
    // 1 byte: 0xxxxxxx
    if (c < 0x80)
    {
        *p += 1;
        return c;
    }
    // 2 bytes: 110xxxxx 10xxxxxx
    if ((c & 0xE0) == 0xC0)
    {
        if ((s[1] & 0xC0) == 0x80)
        {
            *p += 2;
            return ((c & 0x1F) << 6) | (s[1] & 0x3F);
        }
    }
    // 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx
    else if ((c & 0xF0) == 0xE0)
    {
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
        {
            *p += 3;
            return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        }
    }
    // 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    else if ((c & 0xF8) == 0xF0)
    {
        if ((s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80)
        {
            *p += 4;
            return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        }
    }
    // Invalid UTF-8 sequence, treat as raw byte
    *p += 1;
    return c;
}

/// @brief ASCII-only tolower (locale-independent)
/// @param c Input character or codepoint
/// @return Lowercase character
static inline uint32_t rbc_ascii_tolower(uint32_t c)
{
    return ('A' <= c && c <= 'Z') ? c + 32 : c;
}

/// @brief Compare two characters with optional case folding
/// @param c1 character 1
/// @param c2 character 2
/// @param flags Matching flags (RBC_FNM_CASEFOLD)
/// @return true if characters match
static inline bool rbc_char_match(uint32_t c1, uint32_t c2, unsigned flags)
{
    if (c1 == c2)
        return true;
    if (!(flags & RBC_FNM_CASEFOLD))
        return false;
    return rbc_ascii_tolower(c1) == rbc_ascii_tolower(c2);
}

/// @brief Consume bracket expression [..] and test match
/// @param pattern Bracket pattern starting after '['
/// @param c Character (codepoint) to match
/// @param flags Matching flags
/// @return Next position in pattern after ']' if matched, NULL if no match
/// @note Ruby differs from POSIX: `]` is never a literal member.
///   `[]` and `[]]` are unclosed brackets (no match).
///   `[!]` is an empty negation (matches any character).
static const char *rbc_bracket_consume(const char *pattern, uint32_t c, unsigned flags)
{
    const char *p = pattern;

    if ((flags & RBC_FNM_PATHNAME) && c == '/')
        return NULL;
    if (*p == ']')
        return NULL; // `[]` matches nothing

    bool invert;
    if (invert = (*p == '!' || *p == '^'))
        p++; // `[!]` matches any character

    if (flags & RBC_FNM_CASEFOLD)
        c = rbc_ascii_tolower(c);

    bool matched = false;
    while (*p)
    {
        if (*p == ']')
            return (invert ? !matched : matched) ? p + 1 : NULL;
        if (*p == '\\' && !(flags & RBC_FNM_NOESCAPE) && p[1])
            p++; // skip escape

        const char *next = p;
        uint32_t pc = rbc_utf8_decode(&next);

        if (*next == '-' && next[1] && next[1] != ']')
        {
            const char *end_pos = next + 1; // skip hyphen
            if (*end_pos == '\\' && !(flags & RBC_FNM_NOESCAPE) && end_pos[1])
                end_pos++; // skip escape for range end
            uint32_t end_cp = rbc_utf8_decode(&end_pos);
            uint32_t start = pc;
            uint32_t end = end_cp;
            if (flags & RBC_FNM_CASEFOLD)
            {
                start = rbc_ascii_tolower(start);
                end = rbc_ascii_tolower(end);
            }
            // NOTE: When start > end (inverted range, e.g. [z-a]),
            // the range check is always false, so only endpoint equality applies.
            // This matches Ruby's behavior (POSIX leaves inverted ranges undefined).
            matched |= (start <= c && c <= end) || c == start || c == end;
            p = end_pos;
            continue;
        }
        matched |= rbc_char_match(pc, c, flags);
        p = next;
    }
    return NULL;
}

/// @brief Check if pattern can match a leading dot in string
/// @param pattern Current position in pattern
/// @param string Current position in string
/// @param flags Matching flags
/// @return true if matching can proceed, false if dot is hidden
static inline bool rbc_can_dotmatch(const char *pattern, const char *string, unsigned flags)
{
    return *string != '.' || *pattern == '.' || (flags & RBC_FNM_DOTMATCH);
}

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
    const char *p_start = NULL; // Position after last `*` in pattern
    const char *s_start = NULL; // Position in string when `*` was seen

    if (!rbc_can_dotmatch(p, s, flags))
        return false;

    while (*s)
    {
        if (p >= pattern_end)
            goto backtrack;

        switch (*p)
        {
        case '*':
        {
            bool seg_start = p == pattern || p[-1] == '/';

            // Handle `**/` (If not PATHNAME, this behaves the same as `*`)
            if ((flags & RBC_FNM_PATHNAME) && seg_start && p + 2 < pattern_end && p[1] == '*' && p[2] == '/')
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
                    if (rbc_can_dotmatch(p_rest, s_try, flags) && rbc_fnmatch_recursive(p_rest, pattern_end, s_try, flags, depth + 1))
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
            if ((flags & RBC_FNM_PATHNAME) && seg_start && !(flags & RBC_FNM_DOTMATCH) && *s == '.')
                goto backtrack;

            // Record positions for backtracking
            p_start = p;
            s_start = s;
            continue;
        }

        case '?':
            if ((flags & RBC_FNM_PATHNAME) && *s == '/')
                goto backtrack;
            p++;
            rbc_utf8_decode(&s);
            continue;

        case '[':
            p = rbc_bracket_consume(p + 1, rbc_utf8_decode(&s), flags);
            if (!p)
                return false;
            continue;

        case '\\':
            if (!(flags & RBC_FNM_NOESCAPE) && p + 1 < pattern_end)
            {
                p++; // skip backslash
                if (rbc_char_match(*p, *s, flags))
                {
                    p++;
                    s++;
                    continue;
                }
                goto backtrack;
            }
            // **** FALLTHROUGH ****

        default:
            if (rbc_char_match(*p, *s, flags))
            {
                p++;
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
            rbc_utf8_decode(&s_start); // Advance by one codepoint (UTF-8 safe)
            s = s_start;
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

bool rbc_fnmatch(const char *pattern, const char *path, unsigned flags)
{
    if (!pattern || !path)
        return false;
    return rbc_fnmatch_recursive(pattern, pattern + strlen(pattern), path, flags, 0);
}

bool rbc_fnmatch_len(const char *pattern, size_t pattern_len, const char *path, unsigned flags)
{
    if (!pattern || !path || pattern_len == 0)
        return false;
    return rbc_fnmatch_recursive(pattern, pattern + pattern_len, path, flags, 0);
}
