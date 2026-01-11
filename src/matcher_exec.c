#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <rbc/rbc.h>
#include "pattern.h"
#include "utils.h"

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
            matched = (rbc_match_fixed(name, m->pk.literal, name_len, true) && strlen(m->pk.literal) == name_len);
        else
            matched = (strcmp(name, m->pk.literal) == 0);
        break;
    case RBC_STRATEGY_PREFIX:
        if (casefold)
            matched = rbc_match_fixed(name, m->pk.affix.pattern, m->pk.affix.len, true);
        else
            matched = (strncmp(name, m->pk.affix.pattern, m->pk.affix.len) == 0);

        if (matched && pathname)
        {
            if (memchr(name + m->pk.affix.len, '/', name_len - m->pk.affix.len))
                matched = false;
        }
        break;
    case RBC_STRATEGY_SUFFIX:
        if (name_len >= m->pk.affix.len)
        {
            if (casefold)
                matched = rbc_match_fixed(name + name_len - m->pk.affix.len, m->pk.affix.pattern, m->pk.affix.len, true);
            else
                matched = (strcmp(name + name_len - m->pk.affix.len, m->pk.affix.pattern) == 0);

            if (matched && pathname)
            {
                if (memchr(name, '/', name_len - m->pk.affix.len))
                    matched = false;
            }
        }
        break;
    case RBC_STRATEGY_INFIX:
        if (!pathname)
        {
            if (casefold)
                matched = (rbc_search_fixed(name, m->pk.affix.pattern, name + name_len, true) != NULL);
            else
                matched = (strstr(name, m->pk.affix.pattern) != NULL);
        }
        else
        {
            const char *p = name;
            size_t pat_len = m->pk.affix.len;
            const char *end_ptr = name + name_len;

            while (true)
            {
                if (casefold)
                    p = rbc_search_fixed(p, m->pk.affix.pattern, end_ptr, true);
                else
                    p = strstr(p, m->pk.affix.pattern);

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
    case RBC_STRATEGY_FNMATCH:
        matched = rbc_recursive_match(name, m->pk.fnmatch.pattern, flags);
        break;
    }
    return matched;
}
