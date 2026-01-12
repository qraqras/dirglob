#include <string.h>
#include <rbc/rbc.h>
#include "internal.h"
#include "utils.h"

bool rbc_matcher_build(rbc_arena_t *arena, rbc_matcher_t *m, const char *pattern, unsigned int flags)
{
    bool noescape = (flags & RBC_FNM_NOESCAPE);
    // Check for complexity features
    bool has_qmark = (strchr(pattern, '?') != NULL);
    bool has_bracket = (strchr(pattern, '[') != NULL);
    bool has_brace = (strchr(pattern, '{') != NULL);
    bool has_paren = (strchr(pattern, '(') != NULL);
    bool has_pipe = (strchr(pattern, '|') != NULL);

    // Count stars
    int star_count = 0;
    const char *p = pattern;
    while (*p)
    {
        if (!noescape && *p == '\\')
        {
            p++;
            if (*p)
                p++;
            continue;
        }
        if (*p == '*')
            star_count++;
        p++;
    }

    // Force VM if complex characters exist
    if (has_bracket || has_brace || has_paren || has_pipe)
    {
        m->strategy = RBC_STRATEGY_RECURSIVE;
        m->pk.recursive.pattern = rbc_arena_strdup(arena, pattern);
        if (!m->pk.recursive.pattern)
            return false;
    }
    else if (star_count == 0)
    {
        if (has_qmark)
        {
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;
            m->pk.chain.count = 1;
            m->pk.chain.parts = rbc_arena_alloc(arena, sizeof(char *));
            if (!m->pk.chain.parts)
                return false;
            m->pk.chain.parts[0] = rbc_arena_strdup(arena, pattern);
            if (!m->pk.chain.parts[0])
                return false;
            m->pk.chain.match_start = true;
            m->pk.chain.match_end = true;
        }
        else
        {
            m->strategy = RBC_STRATEGY_EXACT;
            m->pk.str.ptr = rbc_arena_strdup(arena, pattern);
            if (!m->pk.str.ptr)
                return false;
            m->pk.str.len = strlen(pattern);
        }
    }
    else if (star_count == 1)
    {
        size_t len = strlen(pattern);
        if (has_qmark)
        {
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;
            goto build_chain;
        }

        if (pattern[0] == '*')
        {
            if (len == 1)
            {
                m->strategy = RBC_STRATEGY_PREFIX;
                m->pk.str.ptr = "";
                m->pk.str.len = 0;
            }
            else
            {
                m->strategy = RBC_STRATEGY_SUFFIX;
                m->pk.str.ptr = rbc_arena_strdup(arena, pattern + 1);
                if (!m->pk.str.ptr)
                    return false;
                m->pk.str.len = len - 1;
            }
        }
        else if (pattern[len - 1] == '*')
        {
            m->strategy = RBC_STRATEGY_PREFIX;
            m->pk.str.ptr = rbc_arena_strdup(arena, pattern);
            if (!m->pk.str.ptr)
                return false;
            m->pk.str.ptr[len - 1] = '\0';
            m->pk.str.len = len - 1;
        }
        else
        {
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;
            m->pk.chain.count = 2;
            m->pk.chain.parts = rbc_arena_alloc(arena, sizeof(char *) * 2);
            if (!m->pk.chain.parts)
                return false;

            char *star_pos = strchr(pattern, '*');
            size_t pre_len = (size_t)(star_pos - pattern);
            char *pre = rbc_arena_alloc(arena, pre_len + 1);
            if (!pre)
                return false;
            memcpy(pre, pattern, pre_len);
            pre[pre_len] = '\0';
            m->pk.chain.parts[0] = pre;

            m->pk.chain.parts[1] = rbc_arena_strdup(arena, star_pos + 1);
            if (!m->pk.chain.parts[1])
                return false;

            m->pk.chain.match_start = true;
            m->pk.chain.match_end = true;
        }
    }
    else
    {
        size_t len = strlen(pattern);
        if (!has_qmark && pattern[0] == '*' && pattern[len - 1] == '*' && star_count == 2)
        {
            m->strategy = RBC_STRATEGY_INFIX;
            m->pk.str.ptr = rbc_arena_strdup(arena, pattern + 1);
            if (!m->pk.str.ptr)
                return false;
            m->pk.str.ptr[len - 2] = '\0';
            m->pk.str.len = len - 2;
        }
        else
        {
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;

        build_chain:
            // ... (setup match_start/end omitted for brevity in thought, but I will include in final tool call)
            {
                const char *p_scan = pattern;
                if (!noescape && *p_scan == '\\')
                {
                    m->pk.chain.match_start = true;
                }
                else if (*p_scan == '*')
                {
                    m->pk.chain.match_start = false;
                }
                else
                {
                    m->pk.chain.match_start = true;
                }

                bool last_was_star = false;
                p_scan = pattern;
                while (*p_scan)
                {
                    if (!noescape && *p_scan == '\\')
                    {
                        p_scan++;
                        if (*p_scan)
                            p_scan++;
                        last_was_star = false;
                        continue;
                    }
                    if (*p_scan == '*')
                    {
                        last_was_star = true;
                        p_scan++;
                        continue;
                    }
                    last_was_star = false;
                    p_scan++;
                }
                m->pk.chain.match_end = !last_was_star;
            }

            size_t max_parts = (size_t)star_count + 1;
            char **parts = rbc_arena_alloc(arena, sizeof(char *) * max_parts);
            if (!parts)
                return false;
            size_t count = 0;

            const char *curr = pattern;
            const char *scan = pattern;

            while (*scan)
            {
                if (!noescape && *scan == '\\')
                {
                    scan++;
                    if (*scan)
                        scan++;
                    continue;
                }
                if (*scan == '*')
                {
                    if (scan > curr)
                    {
                        size_t plen = (size_t)(scan - curr);
                        char *part = rbc_arena_alloc(arena, plen + 1);
                        if (!part)
                            return false;
                        memcpy(part, curr, plen);
                        part[plen] = '\0';
                        parts[count++] = part;
                    }
                    curr = scan + 1;
                    scan = curr;
                    continue;
                }
                scan++;
            }
            if (scan > curr)
            {
                size_t plen = (size_t)(scan - curr);
                char *part = rbc_arena_alloc(arena, plen + 1);
                if (!part)
                    return false;
                memcpy(part, curr, plen);
                part[plen] = '\0';
                parts[count++] = part;
            }

            m->pk.chain.parts = parts;
            m->pk.chain.count = count;
        }
    }

    if (m->strategy == RBC_STRATEGY_RECURSIVE)
    {
        m->pk.recursive.pattern = rbc_arena_strdup(arena, pattern);
        if (!m->pk.recursive.pattern)
            return false;
    }
    return true;
}
