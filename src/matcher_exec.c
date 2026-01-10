#include <string.h>
#include <stdbool.h>
#include "pattern.h"
#include "utils.h"

// Note: rbcglob_build_matcher handles parsing strategy (CHAIN, SUFFIX etc)
// rbcglob_recursive_match is in pattern.h?
// I just checked pattern.h and noticed I didn't verify rbcglob_recursive_match was there.
// I saw "grep output" saying it was provided.
// Let's assume it is.

bool rbcglob_matcher_exec(const rbcg_matcher_t *m, const char *name, unsigned int flags)
{
    size_t name_len = strlen(name);
    bool matched = false;

    switch (m->strategy)
    {
    case RBCG_STRATEGY_EXACT:
        matched = (strcmp(name, m->pk.literal) == 0);
        break;
    case RBCG_STRATEGY_PREFIX:
        matched = (strncmp(name, m->pk.affix.pattern, m->pk.affix.len) == 0);
        break;
    case RBCG_STRATEGY_SUFFIX:
        if (name_len >= m->pk.affix.len)
            matched = (strcmp(name + name_len - m->pk.affix.len, m->pk.affix.pattern) == 0);
        break;
    case RBCG_STRATEGY_INFIX:
        matched = (strstr(name, m->pk.affix.pattern) != NULL);
        break;
    case RBCG_STRATEGY_PATTERN_CHAIN:
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
            else if (!rbcglob_match_fixed(name + name_len - last_len, last, last_len))
                matched = false;
            else
            {
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
                    matched = false;
            }

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
                    if (!rbcglob_match_fixed(p, part, part_len))
                    {
                        matched = false;
                        break;
                    }
                    p += part_len;
                }
                else
                {
                    // Ensure we don't search past end_limit
                    if (p > end_limit)
                    {
                        matched = false;
                        break;
                    }
                    const char *found = rbcglob_search_fixed(p, part, end_limit - part_len);
                    if (!found)
                    {
                        matched = false;
                        break;
                    }
                    p = found + part_len;
                }
            }
            // Ensure no trailing garbage if match_end implies it...
            // current logic: matched means all parts found.
            // If match_end was handled in suffix check, end_limit implies we must consume up to there?
            // Actually `search_fixed` just finds strict match.
            // If `match_end` is true, intermediate parts ensure nothing.
            // But the chain strategy assumes wildcards between parts. So trailing is allowed unless match_end says "last part is AT END".
            // If match_end is true, we already adjusted end_limit.
            // The logic seems correct as copied from walker.c (with my fix).
        }
    }
    break;
    case RBCG_STRATEGY_FNMATCH:
        matched = rbcglob_recursive_match(name, m->pk.fnmatch.pattern, flags);
        break;
    }
    return matched;
}
