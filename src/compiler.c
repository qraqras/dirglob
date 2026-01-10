#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "pattern.h"
#include "utils.h"

void rbcglob_build_matcher(rbcglob_arena_t *arena, rbcg_matcher_t *m, const char *pattern)
{
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
        if (*p == '\\')
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
        m->strategy = RBCG_STRATEGY_FNMATCH;
        m->pk.fnmatch.pattern = rbcglob_arena_strdup(arena, pattern);
    }
    else if (star_count == 0)
    {
        // No stars. If we have '?', it's a fixed length pattern chain (count=1).
        // If no '?', it's exact match.
        if (has_qmark)
        {
            m->strategy = RBCG_STRATEGY_PATTERN_CHAIN;
            m->pk.chain.count = 1;
            m->pk.chain.parts = rbcglob_arena_alloc(arena, sizeof(char *));
            m->pk.chain.parts[0] = rbcglob_arena_strdup(arena, pattern);
            m->pk.chain.match_start = true;
            m->pk.chain.match_end = true;
        }
        else
        {
            m->strategy = RBCG_STRATEGY_EXACT;
            m->pk.literal = rbcglob_arena_strdup(arena, pattern);
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
            m->strategy = RBCG_STRATEGY_PATTERN_CHAIN;
            goto build_chain;
        }

        if (pattern[0] == '*')
        {
            if (len == 1)
            {
                // Pattern is "*"
                m->strategy = RBCG_STRATEGY_INFIX; // or SEQUENCE with empty outer logic?
                // Actually "*" matches everything (except hidden).
                // Let's treat it as INFIX with empty pattern? Or SUFFIX with empty?
                // Let's treat it as SEQUENCE with ["", ""] parts?
                // Simplest: STRATEGY_PREFIX with empty string (starts with "")
                m->strategy = RBCG_STRATEGY_PREFIX; // matches anything starting with empty string
                m->pk.affix.pattern = "";
                m->pk.affix.len = 0;
            }
            else
            {
                // "*suffix"
                m->strategy = RBCG_STRATEGY_SUFFIX;
                m->pk.affix.pattern = rbcglob_arena_strdup(arena, pattern + 1);
                m->pk.affix.len = len - 1;
            }
        }
        else if (pattern[len - 1] == '*')
        {
            // "prefix*"
            m->strategy = RBCG_STRATEGY_PREFIX;
            m->pk.affix.pattern = rbcglob_arena_strdup(arena, pattern);
            m->pk.affix.pattern[len - 1] = '\0'; // Remove star
            m->pk.affix.len = len - 1;
        }
        else
        {
            // "prefix*suffix" -> this is INFIX equivalent?
            // Wait, "pre*suf" is NOT "contains(pre) && contains(suf)".
            // It is "starts(pre) && ends(suf)".
            // INFIX is "*sub*".
            // So "pre*suf" is PATTERN_CHAIN.
            m->strategy = RBCG_STRATEGY_PATTERN_CHAIN;

            // Build 2 parts
            m->pk.chain.count = 2;
            m->pk.chain.parts = rbcglob_arena_alloc(arena, sizeof(char *) * 2);

            // Copy prefix
            char *star_pos = strchr(pattern, '*');
            size_t pre_len = star_pos - pattern;
            char *pre = rbcglob_arena_alloc(arena, pre_len + 1);
            memcpy(pre, pattern, pre_len);
            pre[pre_len] = '\0';
            m->pk.chain.parts[0] = pre;

            // Copy suffix
            m->pk.chain.parts[1] = rbcglob_arena_strdup(arena, star_pos + 1);

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
            m->strategy = RBCG_STRATEGY_INFIX;
            m->pk.affix.pattern = rbcglob_arena_strdup(arena, pattern + 1);
            m->pk.affix.pattern[len - 2] = '\0'; // Remove trailing star
            m->pk.affix.len = len - 2;
        }
        else
        {
            m->strategy = RBCG_STRATEGY_PATTERN_CHAIN;

        build_chain:
            // Split by '*'
            // First count parts (star_count + 1 max)
            // But consecutive stars might reduce count.
            // e.g. "a**b" -> "a*b".
            // The split logic should handle empty parts or we normalize?
            // Let's implement robust split.
            {
                size_t chain_len = strlen(pattern);
                m->pk.chain.match_start = (pattern[0] != '*');
                m->pk.chain.match_end = (pattern[chain_len - 1] != '*');
            }

            // Estimate max parts
            size_t max_parts = star_count + 1;
            char **parts = rbcglob_arena_alloc(arena, sizeof(char *) * max_parts);
            size_t count = 0;

            const char *curr = pattern;
            const char *next_star;

            while ((next_star = strchr(curr, '*')) != NULL)
            {
                if (next_star > curr)
                {
                    size_t plen = next_star - curr;
                    char *part = rbcglob_arena_alloc(arena, plen + 1);
                    memcpy(part, curr, plen);
                    part[plen] = '\0';
                    parts[count++] = part;
                }
                curr = next_star + 1;
            }
            if (*curr)
            {
                parts[count++] = rbcglob_arena_strdup(arena, curr);
            }

            m->pk.chain.parts = parts;
            m->pk.chain.count = count;
        }
    }

    if (m->strategy == RBCG_STRATEGY_FNMATCH)
    {
        // Fill fnmatch struct (Just copy pattern)
        m->pk.fnmatch.pattern = rbcglob_arena_strdup(arena, pattern);
    }
}

rbcglob_segment_t *rbcglob_segment_new(rbcglob_arena_t *arena, rbcg_segment_type_t type)
{
    rbcglob_segment_t *seg = rbcglob_arena_alloc(arena, sizeof(rbcglob_segment_t));
    memset(seg, 0, sizeof(rbcglob_segment_t));
    seg->type = type;
    return seg;
}

static bool is_recursive_wildcard(const char *s)
{
    return strcmp(s, "**") == 0;
}

rbcglob_segment_t *rbcglob_compile_segments(rbcglob_arena_t *arena, const char *pattern)
{
    if (!pattern || !*pattern)
        return NULL;

    rbcglob_segment_t *head = NULL;
    rbcglob_segment_t *curr = NULL;

    const char *p = pattern;
    while (*p)
    {
        const char *end = rbcglob_find_segment_end(p);
        size_t len = end - p;
        if (len == 0)
        {
            if (*end == '/')
                p = end + 1;
            else
                p = end;
            continue;
        }

        char *component = rbcglob_arena_alloc(arena, len + 1);
        memcpy(component, p, len);
        component[len] = '\0';

        bool is_sep = (*end == '/');
        p = is_sep ? end + 1 : end;
        // Logic for "rest of the string" needed for branches
        const char *rest = p;

        rbcglob_segment_t *seg = NULL;
        // Use the compiler's arena for brace expansion.
        // This avoids malloc/free overhead for temporary strings.
        // The expanded strings will persist in the arena for the lifetime of the compiled glob, which is acceptable.
        rbcglob_str_list_t expansions = rbcglob_brace_expand(component, arena);

        // Special Case: Pure Recursive Wildcard
        if (!rbcglob_has_brace(component) && is_recursive_wildcard(component))
        {
            seg = rbcglob_segment_new(arena, RBCG_SEGMENT_RECURSIVE);
            rbcglob_str_list_free(&expansions);
            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            continue;
        }

        // Special Case: Simple Literal (No braces, no wildcards)
        if (!rbcglob_has_brace(component) && !rbcglob_has_wildcard(component))
        {
            seg = rbcglob_segment_new(arena, RBCG_SEGMENT_LITERAL);
            seg->data.literal = component;
            rbcglob_str_list_free(&expansions);
            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            continue;
        }

        bool any_slash = false;
        bool all_literals = true;
        for (size_t i = 0; i < expansions.count; i++)
        {
            if (strchr(expansions.items[i], '/'))
                any_slash = true;
            if (rbcglob_has_wildcard(expansions.items[i]))
                all_literals = false;
        }

        if (any_slash || all_literals || expansions.count > 1)
        {
            // Case A: SEG_BRANCH (Stat Optimization, Topology Split, or Brace Branching)
            seg = rbcglob_segment_new(arena, RBCG_SEGMENT_BRANCH);
            rbcglob_segment_t *last_alt = NULL;

            for (size_t i = 0; i < expansions.count; i++)
            {
                char *full_pattern;
                // If is_sep, we need to append "/" + rest.
                // If rest is empty, we just append "/"?
                // Wait, if pattern ended with /, "dir/" -> component "dir". rest "".
                // If we expand dir -> "dir". full_pattern = "dir/".

                if (is_sep)
                {
                    full_pattern = rbcglob_arena_printf(arena, "%s/%s", expansions.items[i], rest);
                }
                else if (*rest)
                {
                    // Should technically not happen if we parsed correctly up to separator,
                    // unless separator was implicit or missing?
                    // Just append.
                    full_pattern = rbcglob_arena_printf(arena, "%s%s", expansions.items[i], rest);
                }
                else
                {
                    full_pattern = rbcglob_arena_strdup(arena, expansions.items[i]);
                }

                rbcglob_segment_t *alt_chain = NULL;

                // Optimization: Short-circuit recursion for simple literal leaves (Pattern 1)
                // If we know this branch is a simple literal (no slash append, no rest, and from all_literals set),
                // we can create the node directly without re-parsing.
                if (all_literals && !is_sep && !*rest)
                {
                    alt_chain = rbcglob_segment_new(arena, RBCG_SEGMENT_LITERAL);
                    alt_chain->data.literal = full_pattern;
                }
                else
                {
                    alt_chain = rbcglob_compile_segments(arena, full_pattern);
                }

                // Handle empty expansion case?
                if (!alt_chain && !*full_pattern)
                {
                    alt_chain = rbcglob_segment_new(arena, RBCG_SEGMENT_LITERAL);
                    alt_chain->data.literal = "";
                }

                if (alt_chain)
                {
                    if (!seg->data.branch.head)
                        seg->data.branch.head = alt_chain;
                    else if (last_alt)
                        last_alt->next_alt = alt_chain;
                    last_alt = alt_chain;
                }
            }
            rbcglob_str_list_free(&expansions);

            // SEG_BRANCH consumes the rest using recursion. Break loop.
            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            break;
        }
        else
        {
            // Case B: SEG_WILDCARD (Optimization Strategy)
            seg = rbcglob_segment_new(arena, RBCG_SEGMENT_WILDCARD);
            seg->data.glob.original_pattern = rbcglob_arena_strdup(arena, expansions.items[0]);

            // Analyze pattern and select strategy
            rbcglob_build_matcher(arena, &seg->data.glob.matcher, seg->data.glob.original_pattern);

            rbcglob_str_list_free(&expansions);

            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            // Do NOT break here. Continue parsing next component.
        }
    }
    return head;
}
#include <stdio.h>
#include <stddef.h>
#include "pattern.h"

// VM Program creation is now handled directly by compiler
// But we might need some stubs if referenced elsewhere

// Empty implementation or removal if build system allows
