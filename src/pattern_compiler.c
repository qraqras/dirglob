#include <string.h>
#include <rbc/rbc.h>
#include "internal.h"
#include "utils.h"

void rbc_matcher_build(rbc_arena_t *arena, rbc_matcher_t *m, const char *pattern, unsigned int flags)
{
    bool noescape = (flags & RBC_FNM_NOESCAPE);
    // Check for complexity features
    // Note: '?' is now handled by PATTERN_CHAIN, so it is not considered complex enough to force FNMATCH.
    bool has_qmark = (strchr(pattern, '?') != NULL);
    bool has_bracket = (strchr(pattern, '[') != NULL); // Basic check, ideally checking for escaped `\`
    bool has_brace = (strchr(pattern, '{') != NULL);   // Should be handled by expansion loop but double check
    bool has_paren = (strchr(pattern, '(') != NULL);   // Extended glob or pure literal
    bool has_pipe = (strchr(pattern, '|') != NULL);
    // Ignore `!` for now as it's typically start of pattern or inside bracket?
    // Actually, `!` alone might not be special unless extended glob. But let's assume complex.

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

    // Force VM if complex characters exist (excluding '?' which we can handle in chains)
    // Note: Escaped characters might trigger this simple check, which is safe (just falls back to VM).
    if (has_bracket || has_brace || has_paren || has_pipe)
    {
        m->strategy = RBC_STRATEGY_RECURSIVE;
        m->pk.recursive.pattern = rbc_arena_strdup(arena, pattern);
    }
    else if (star_count == 0)
    {
        // No stars. If we have '?', it's a fixed length pattern chain (count=1).
        // If no '?', it's exact match.
        if (has_qmark)
        {
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;
            m->pk.chain.count = 1;
            m->pk.chain.parts = rbc_arena_alloc(arena, sizeof(char *));
            m->pk.chain.parts[0] = rbc_arena_strdup(arena, pattern);
            m->pk.chain.match_start = true;
            m->pk.chain.match_end = true;
        }
        else
        {
            m->strategy = RBC_STRATEGY_EXACT;
            m->pk.str.ptr = rbc_arena_strdup(arena, pattern);
            m->pk.str.len = strlen(pattern);
        }
    }
    else if (star_count == 1)
    {
        size_t len = strlen(pattern);
        if (has_qmark)
        {
            // If it has '?', we treat it as a PATTERN_CHAIN even if it looks like prefix/suffix.
            // This simplifies logic: PREFIX/SUFFIX etc are for pure literals.
            // "abc?*" -> Chain ["abc?", ""] (if we implemented empty tail) or similar logic.
            // The existing PATTERN_CHAIN logic below splits by '*'.
            // Let's drop into the generic PATTERN_CHAIN block at the end.
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;
            goto build_chain;
        }

        if (pattern[0] == '*')
        {
            if (len == 1)
            {
                // Pattern is "*"
                m->strategy = RBC_STRATEGY_PREFIX; // matches anything starting with empty string
                m->pk.str.ptr = "";
                m->pk.str.len = 0;
            }
            else
            {
                // "*suffix"
                m->strategy = RBC_STRATEGY_SUFFIX;
                m->pk.str.ptr = rbc_arena_strdup(arena, pattern + 1);
                m->pk.str.len = len - 1;
            }
        }
        else if (pattern[len - 1] == '*')
        {
            // "prefix*"
            m->strategy = RBC_STRATEGY_PREFIX;
            m->pk.str.ptr = rbc_arena_strdup(arena, pattern);
            m->pk.str.ptr[len - 1] = '\0'; // Remove star
            m->pk.str.len = len - 1;
        }
        else
        {
            // "prefix*suffix" -> this is INFIX equivalent?
            // Wait, "pre*suf" is NOT "contains(pre) && contains(suf)".
            // It is "starts(pre) && ends(suf)".
            // INFIX is "*sub*".
            // So "pre*suf" is PATTERN_CHAIN.
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;

            // Build 2 parts
            m->pk.chain.count = 2;
            m->pk.chain.parts = rbc_arena_alloc(arena, sizeof(char *) * 2);

            // Copy prefix
            char *star_pos = strchr(pattern, '*');
            size_t pre_len = star_pos - pattern;
            char *pre = rbc_arena_alloc(arena, pre_len + 1);
            memcpy(pre, pattern, pre_len);
            pre[pre_len] = '\0';
            m->pk.chain.parts[0] = pre;

            // Copy suffix
            m->pk.chain.parts[1] = rbc_arena_strdup(arena, star_pos + 1);

            m->pk.chain.match_start = true;
            m->pk.chain.match_end = true;
        }
    }
    else
    {
        // 2 or more stars
        // Check for INFIX case: "*word*"
        size_t len = strlen(pattern);
        if (!has_qmark && pattern[0] == '*' && pattern[len - 1] == '*' && star_count == 2)
        {
            m->strategy = RBC_STRATEGY_INFIX;
            m->pk.str.ptr = rbc_arena_strdup(arena, pattern + 1);
            m->pk.str.ptr[len - 2] = '\0'; // Remove trailing star
            m->pk.str.len = len - 2;
        }
        else
        {
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;

        build_chain:
            // Split by '*' with escape handling
            {
                // Check match_start
                const char *p = pattern;
                if (!noescape && *p == '\\')
                {
                    m->pk.chain.match_start = true;
                }
                else if (*p == '*')
                {
                    m->pk.chain.match_start = false;
                }
                else
                {
                    m->pk.chain.match_start = true;
                }

                // Check match_end by robust scan
                bool last_was_star = false;
                p = pattern;
                while (*p)
                {
                    if (!noescape && *p == '\\')
                    {
                        p++;
                        if (*p)
                            p++;
                        last_was_star = false;
                        continue;
                    }
                    if (*p == '*')
                    {
                        last_was_star = true;
                        p++;
                        continue;
                    }
                    last_was_star = false;
                    p++;
                }
                m->pk.chain.match_end = !last_was_star;
            }

            // Estimate max parts
            size_t max_parts = star_count + 1;
            char **parts = rbc_arena_alloc(arena, sizeof(char *) * max_parts);
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
                        size_t plen = scan - curr;
                        char *part = rbc_arena_alloc(arena, plen + 1);
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
                size_t plen = scan - curr;
                char *part = rbc_arena_alloc(arena, plen + 1);
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
        // Fill recursive struct (Just copy pattern)
        m->pk.recursive.pattern = rbc_arena_strdup(arena, pattern);
    }
}
