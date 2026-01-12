#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <rbc/rbc.h>
#include "internal.h"
#include "utils.h"

static bool rbc_match_fixed(const char *text, const char *pat, size_t len, bool casefold)
{
    if (casefold)
    {
        for (size_t i = 0; i < len; i++)
        {
            if (pat[i] != '?' && tolower((unsigned char)pat[i]) != tolower((unsigned char)text[i]))
                return false;
        }
    }
    else
    {
        for (size_t i = 0; i < len; i++)
        {
            if (pat[i] != '?' && pat[i] != text[i])
                return false;
        }
    }
    return true;
}

static const char *rbc_search_fixed(const char *text, const char *pat, const char *end_limit, bool casefold)
{
    size_t pat_len = strlen(pat);
    if (pat_len == 0)
        return text;

    for (const char *p = text; p <= end_limit; p++)
    {
        if (rbc_match_fixed(p, pat, pat_len, casefold))
            return p;
    }
    return NULL;
}

// Helper macros for recursive matcher
#define IS_PATHNAME (flags & RBC_FNM_PATHNAME)
#define IS_CASEFOLD (flags & RBC_FNM_CASEFOLD)
#define IS_DOTMATCH (flags & RBC_FNM_DOTMATCH)

static const char *bracket_match(const char *p, unsigned char c, unsigned int flags)
{
    bool negated = (*p == '!' || *p == '^');
    if (negated)
        p++;

    bool matched = false;

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

static bool match_recursive(const char *p, const char *s, const char *initial_str, unsigned int flags)
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

                        if (match_recursive(p + lit_len, found + lit_len, initial_str, flags))
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

                if (match_recursive(p, s, initial_str, flags))
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

            const char *nxt = bracket_match(p + 1, (unsigned char)*s, flags);
            if (nxt)
            {
                p = nxt;
                s++;
                continue;
            }
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

static bool rbc_match_recursive_entry(const char *text, const char *pattern, unsigned int flags)
{
    if (!text || !pattern)
        return false;
    return match_recursive(pattern, text, text, flags);
}

// Note: rbc_build_matcher handles parsing strategy (CHAIN, SUFFIX etc)

bool rbc_matcher_exec(const rbc_matcher_t *m, const char *name, unsigned int flags)
{
    size_t name_len = strlen(name);
    bool matched = false;
    bool pathname = (flags & RBC_FNM_PATHNAME);
    bool casefold = (flags & RBC_FNM_CASEFOLD);

    switch (m->strategy)
    {
    case RBC_STRATEGY_EXACT:
        if (casefold)
            matched = (rbc_match_fixed(name, m->pk.str.ptr, name_len, true) && m->pk.str.len == name_len);
        else
            matched = (m->pk.str.len == name_len && strcmp(name, m->pk.str.ptr) == 0);
        break;
    case RBC_STRATEGY_PREFIX:
        if (casefold)
            matched = rbc_match_fixed(name, m->pk.str.ptr, m->pk.str.len, true);
        else
            matched = (name_len >= m->pk.str.len && strncmp(name, m->pk.str.ptr, m->pk.str.len) == 0);

        if (matched && pathname)
        {
            if (memchr(name + m->pk.str.len, '/', name_len - m->pk.str.len))
                matched = false;
        }
        break;
    case RBC_STRATEGY_SUFFIX:
        if (name_len >= m->pk.str.len)
        {
            if (casefold)
                matched = rbc_match_fixed(name + name_len - m->pk.str.len, m->pk.str.ptr, m->pk.str.len, true);
            else
                matched = (strcmp(name + name_len - m->pk.str.len, m->pk.str.ptr) == 0);

            if (matched && pathname)
            {
                if (memchr(name, '/', name_len - m->pk.str.len))
                    matched = false;
            }
        }
        break;
    case RBC_STRATEGY_INFIX:
        if (!pathname)
        {
            if (casefold)
                matched = (rbc_search_fixed(name, m->pk.str.ptr, name + name_len, true) != NULL);
            else
                matched = (strstr(name, m->pk.str.ptr) != NULL);
        }
        else
        {
            const char *p = name;
            size_t pat_len = m->pk.str.len;
            const char *end_ptr = name + name_len;

            while (true)
            {
                if (casefold)
                    p = rbc_search_fixed(p, m->pk.str.ptr, end_ptr, true);
                else
                    p = strstr(p, m->pk.str.ptr);

                if (!p)
                    break;

                // Check prefix
                if (memchr(name, '/', p - name) == NULL)
                {
                    // Check suffix
                    if (memchr(p + pat_len, '/', end_ptr - (p + pat_len)) == NULL)
                    {
                        matched = true;
                        break;
                    }
                }

                if (memchr(name, '/', p - name))
                {
                    matched = false;
                    break;
                }
                if (memchr(p + pat_len, '/', end_ptr - (p + pat_len)))
                {
                    matched = false;
                    break;
                }
                matched = true;
                break;
            }
        }
        break;
    case RBC_STRATEGY_PATTERN_CHAIN:
    {
        const char *p = name;
        const char *end_limit = name + name_len;
        matched = true;
        size_t count = m->pk.chain.count;
        if (m->pk.chain.match_end)
        {
            char *last = m->pk.chain.parts[count - 1];
            size_t last_len = strlen(last);
            if (name_len < last_len)
                matched = false;
            else if (!rbc_match_fixed(name + name_len - last_len, last, last_len, casefold))
                matched = false;
            else
            {
                // Check suffix gap (none effectively, but we consumed last part)
                // If there was a gap BEFORE the last part, match_end doesn't cover it directly?
                // Wait, if match_end is true, the last part IS the suffix.
                // So no gap *after* it.
                // But the loop handles everything UP TO the last part.
                end_limit -= last_len;
                count--;
            }
        }
        if (matched)
        {
            // Correction from walker.c: check start constraint
            if (count == 0 && m->pk.chain.match_start)
            {
                if (p != end_limit)
                {
                    // Consumed strictly everything?
                    // if match_start=true and match_end=true and count=0 (1 part consumed by match_end)
                    // Then `name` should be empty now?
                    // Actually if `count` became 0, we had 1 part.
                    // match_end logic moved end_limit back.
                    // If match_start is true, p must equal end_limit?
                    // "abc". Pattern "abc".
                    // match_end handles "abc". end_limit at start.
                    // p at start. p == end_limit. OK.

                    // "zabc". Pattern "abc".
                    // match_end handles "abc". end_limit at z.
                    // p at start. p != end_limit. Bad.
                    // Logic holds.
                }
            }

            // Check implicit "gap" at end if we finished parts but have data left
            // and NOT match_end (so trailing *).
            // Handled by logic flow?

            // Wait, if count became 0, we don't loop.
            // But if match_start and match_end were both true, we checked strictness.
            // If match_start false (foo*), and match_end true (*bar).
            // "foobar".
            // match_end consumes bar. end_limit at foo.
            // Loop runs for "foo".
            // i=0. match_start false.
            // search "foo" in range. Found at 0.
            // p becomes 3.
            // loop done.
            // p (3) == end_limit (3).
            // What if "xfoobar"?
            // p(0) search "foo". Found at 1. Gap "x".
            // If pathname, gap "x" must be checked.

            // So we need pathname checks in the loop.

            for (size_t i = 0; i < count; i++)
            {
                char *part = m->pk.chain.parts[i];
                size_t part_len = strlen(part);
                if (i == 0 && m->pk.chain.match_start)
                {
                    if ((size_t)(end_limit - p) < part_len)
                    {
                        matched = false;
                        break;
                    }
                    if (!rbc_match_fixed(p, part, part_len, casefold))
                    {
                        matched = false;
                        break;
                    }
                    p += part_len;
                }
                else
                {
                    // Iterate to find valid match
                    const char *search_start = p;
                    bool part_found = false;
                    while (search_start <= end_limit)
                    {
                        const char *found = rbc_search_fixed(search_start, part, end_limit - part_len, casefold);

                        if (!found)
                            break;

                        if (pathname && memchr(p, '/', found - p))
                        {
                            // Invalid gap, try next
                            search_start = found + 1;
                            continue;
                        }

                        p = found + part_len;
                        part_found = true;
                        break;
                    }
                    if (!part_found)
                    {
                        matched = false;
                        break;
                    }
                }
            }
            // After loop, check trailing gap if any (p vs end_limit)
            // If match_end was true, p must equal end_limit?
            // "a*b".
            // match_end handles b. end_limit before b.
            // loop handles a. p after a.
            // Gap between a and b ?
            // The loop for `a` checked gap BEFORE `a`.
            // Any gap AFTER `a` (between a and b) is remaining in `p` ... `end_limit`.
            // Yes.
            if (matched && pathname && p < end_limit)
            {
                if (memchr(p, '/', end_limit - p))
                    matched = false;
            }
        }
    }
    break;
    case RBC_STRATEGY_RECURSIVE:
        matched = rbc_match_recursive_entry(name, m->pk.recursive.pattern, flags);
        break;
    }
    return matched;
}
