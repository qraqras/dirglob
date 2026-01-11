#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>

#include "rbc/rbc.h"
#include "pattern.h"
#include "utils.h"

// Helper macros
#define IS_PATHNAME (flags & RBC_FNM_PATHNAME)
#define IS_CASEFOLD (flags & RBC_FNM_CASEFOLD)
#define IS_DOTMATCH (flags & RBC_FNM_DOTMATCH)

// 前方宣言
static bool match(const char *pat, const char *str, const char *initial_str, unsigned int flags);

static const char *bracket_match(const char *p, unsigned char c, unsigned int flags)
{
    bool negated = (*p == '!' || *p == '^');
    if (negated)
        p++;

    bool matched = false;
    const char *start = p;
    (void)start;

    if (*p == ']')
    {
        if (c == ']')
            matched = true;
        p++;
    }

    while (*p && *p != ']')
    {
        unsigned char c1;
        if (*p == '\\')
        {
            p++;
            if (*p)
                c1 = (unsigned char)*p++;
            else
                c1 = 0; // Invalid
        }
        else
        {
            c1 = (unsigned char)*p++;
        }

        if (*p == '-' && p[1] && p[1] != ']')
        {
            p++;
            unsigned char c2;
            if (*p == '\\')
            {
                p++;
                if (*p)
                    c2 = (unsigned char)*p++;
                else
                    c2 = 0;
            }
            else
            {
                c2 = (unsigned char)*p++;
            }

            if (flags & RBC_FNM_CASEFOLD)
            {
                if (tolower(c1) <= tolower(c) && tolower(c) <= tolower(c2))
                    matched = true;
            }
            else
            {
                if (c1 <= c && c <= c2)
                    matched = true;
            }
        }
        else
        {
            if (flags & RBC_FNM_CASEFOLD)
            {
                if (tolower(c1) == tolower(c))
                    matched = true;
            }
            else
            {
                if (c1 == c)
                    matched = true;
            }
        }
    }

    if (*p == ']')
        p++;

    if (negated)
        matched = !matched;
    return matched ? p : NULL;
}

static bool match(const char *p, const char *s, const char *initial_str, unsigned int flags)
{
    while (*p)
    {
        char c = *p;

        switch (c)
        {
        case '?':
        {
            if (*s == '\0')
                return false;

            if (IS_PATHNAME && *s == '/')
                return false;
            if (!IS_DOTMATCH && *s == '.' &&
                (s == initial_str || (IS_PATHNAME && s[-1] == '/')))
                return false;

            p++;
            s++;
            continue;
        }

        case '*':
        {
            // Consecutive stars are equivalent to one star
            while (*p == '*')
                p++;

            if (!IS_DOTMATCH && *s == '.' &&
                (s == initial_str || (IS_PATHNAME && s[-1] == '/')))
                return false;

            if (*p == '\0')
            {
                // Trailing star matches everything (except checks above)
                if (IS_PATHNAME)
                    return (strchr(s, '/') == NULL);
                return true;
            }

            // Optimization: Fast Forward & Literal Island
            // If the next pattern character is a literal, skip until we find it.
            // This turns O(N*M) into O(N) for cases like "*.c" or "*foo".
            char next_p = *p;
            bool is_literal = (next_p != '?' && next_p != '*' && next_p != '[' && next_p != '\\');

            // "Literal Island" Optimization (strstr-based) for run > 1
            // Significantly speeds up "*long_literal" cases by avoiding backtracking on single char hits.
            if (is_literal && !IS_CASEFOLD)
            {
                const char *lit_end = p;
                while (*lit_end && *lit_end != '?' && *lit_end != '*' && *lit_end != '[' && *lit_end != '\\')
                    lit_end++;
                size_t lit_len = lit_end - p;

                if (lit_len > 1 && lit_len < 128)
                {
                    char buf[128];
                    memcpy(buf, p, lit_len);
                    buf[lit_len] = '\0';

                    const char *search_s = s;
                    char *found;
                    while ((found = strstr(search_s, buf)) != NULL)
                    {
                        if (IS_PATHNAME)
                        {
                            // Check for slash crossing in the skipped part
                            if (memchr(search_s, '/', found - search_s))
                                return false;
                        }

                        if (match(p + lit_len, found + lit_len, initial_str, flags))
                            return true;

                        search_s = found + 1;
                    }
                    return false;
                }
            }

            // Recursive backtracking
            while (1)
            {
                // Optimization: Skip loop (Single Char)
                if (is_literal && *s != '\0')
                {
                    if (IS_CASEFOLD)
                    {
                        char next_p_lower = tolower((unsigned char)next_p);
                        while (*s)
                        {
                            if (IS_PATHNAME && *s == '/')
                                break;
                            if (tolower((unsigned char)*s) == next_p_lower)
                                break;
                            s++;
                        }
                    }
                    else
                    {
                        while (*s)
                        {
                            if (IS_PATHNAME && *s == '/')
                                break;
                            if (*s == next_p)
                                break;
                            s++;
                        }
                    }
                }

                if (match(p, s, initial_str, flags))
                    return true;
                if (*s == '\0')
                    break;
                if (IS_PATHNAME && *s == '/')
                    break;
                s++;
            }
            return false;
        }

        case '[':
        {
            if (*s == '\0')
                return false;
            if (IS_PATHNAME && *s == '/')
                return false;
            if (!IS_DOTMATCH && *s == '.' &&
                (s == initial_str || (IS_PATHNAME && s[-1] == '/')))
                return false;

            const char *next_p = bracket_match(p + 1, (unsigned char)*s, flags);
            if (next_p)
            {
                p = next_p;
                s++;
                continue;
            }
            // Fallthrough: treat '[' as literal if no matching ']' or other failure?
            // But traditionally a mismatch in bracket range fails the whole match,
            // unlike "invalid bracket syntax".
            // If bracket_match returns NULL, it means the char 'c' is not in the set.
            return false;
        }

        case '\\':
            if (p[1])
                p++;
            // Fallthrough

        default:
        {
            if (*s == '\0')
                return false;

            char c1 = c;  // from pattern
            char c2 = *s; // from string

            if (IS_CASEFOLD)
            {
                c1 = tolower((unsigned char)c1);
                c2 = tolower((unsigned char)c2);
            }

            if (c1 != c2)
                return false;

            p++;
            s++;
            continue;
        }
        }
    }

    return (*s == '\0');
}

bool rbc_recursive_match(const char *text, const char *pattern, unsigned int flags)
{
    if (!text || !pattern)
        return false;

    // Simple wrapper around recursive matcher
    return match(pattern, text, text, flags);
}

// Optimized implementation using compiler
bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
        return false;

    // Use stack memory for arena to avoid malloc overhead for most patterns
    char stack_buf[4096];
    rbc_arena_t arena;
    rbc_arena_init_static(&arena, stack_buf, sizeof(stack_buf));

    rbc_matcher_t m;

    // Note: rbc_build_matcher handles parsing strategy (CHAIN, SUFFIX etc)
    rbc_build_matcher(&arena, &m, pattern, flags);

    bool result = rbc_matcher_exec(&m, string, flags);

    rbc_arena_destroy(&arena);
    return result;
}
