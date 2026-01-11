#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <rbc/rbc.h>

#include "internal.h"
#include "utils.h"

// --- Internal String List Utility ---

typedef struct rbc_str_list_s
{
    char **items;
    size_t count;
    size_t capacity;
    rbc_arena_t *arena;
} rbc_str_list_t;

static void rbc_str_list_init(rbc_str_list_t *list, size_t initial_cap, rbc_arena_t *arena)
{
    list->arena = arena;
    list->count = 0;
    list->capacity = initial_cap;
    if (arena)
        list->items = rbc_arena_alloc(arena, initial_cap * sizeof(char *));
    else
        list->items = malloc(initial_cap * sizeof(char *));
}

static void rbc_str_list_add(rbc_str_list_t *list, const char *str)
{
    if (list->count == list->capacity)
    {
        list->capacity *= 2;
        if (list->arena)
        {
            char **new_items = rbc_arena_alloc(list->arena, list->capacity * sizeof(char *));
            if (list->count > 0)
                memcpy(new_items, list->items, list->count * sizeof(char *));
            list->items = new_items;
        }
        else
        {
            list->items = realloc(list->items, list->capacity * sizeof(char *));
        }
    }

    if (list->arena)
    {
        list->items[list->count++] = rbc_arena_strdup(list->arena, str);
    }
    else
    {
        list->items[list->count++] = rbc_strdup(str);
    }
}

static void rbc_str_list_free(rbc_str_list_t *list)
{
    if (list->arena)
        return;

    for (size_t i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
}

// --- Pattern Query Helpers ---

static bool rbc_has_brace(const char *str)
{
    bool esc = false;
    for (const char *p = str; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '{')
            return true;
    }
    return false;
}

static bool rbc_has_wildcard(const char *str)
{
    bool esc = false;
    for (const char *p = str; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '*' || *p == '?' || *p == '[')
            return true;
    }
    return false;
}

static const char *rbc_find_segment_end(const char *str)
{
    bool esc = false;
    int depth = 0;
    const char *p = str;
    while (*p)
    {
        if (esc)
        {
            esc = false;
            p++;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            p++;
            continue;
        }

        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            if (depth > 0)
                depth--;
        }
        else if (*p == '/' && depth == 0)
            return p;

        p++;
    }
    return p;
}

// --- Brace Expansion Implementation ---

static void expand_recursive(rbc_str_list_t *results, const char *prefix, const char *pattern, rbc_arena_t *arena)
{
    const char *brace_start = NULL;
    bool esc = false;

    for (const char *p = pattern; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '{')
        {
            brace_start = p;
            break;
        }
    }

    if (!brace_start)
    {
        if (arena)
        {
            char *full = rbc_arena_printf(arena, "%s%s", prefix, pattern);
            rbc_str_list_add(results, full);
        }
        else
        {
            size_t total_len = strlen(prefix) + strlen(pattern) + 1;
            char *full = malloc(total_len);
            sprintf(full, "%s%s", prefix, pattern);
            rbc_str_list_add(results, full);
            free(full);
        }
        return;
    }

    size_t pre_len = brace_start - pattern;
    char *new_prefix_base;
    if (arena)
    {
        char *pre = rbc_arena_alloc(arena, pre_len + 1);
        memcpy(pre, pattern, pre_len);
        pre[pre_len] = '\0';
        new_prefix_base = rbc_arena_printf(arena, "%s%s", prefix, pre);
    }
    else
    {
        char *pre = malloc(pre_len + 1);
        memcpy(pre, pattern, pre_len);
        pre[pre_len] = '\0';
        new_prefix_base = malloc(strlen(prefix) + pre_len + 1);
        sprintf(new_prefix_base, "%s%s", prefix, pre);
        free(pre);
    }

    const char *p = brace_start + 1;
    const char *chunk_start = p;
    int depth = 1;
    esc = false;

    rbc_str_list_t options;
    rbc_str_list_init(&options, 4, arena);

    while (*p)
    {
        if (esc)
        {
            esc = false;
            p++;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            p++;
            continue;
        }

        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            depth--;
            if (depth == 0)
            {
                size_t chunk_len = p - chunk_start;
                char *chunk;
                if (arena)
                {
                    chunk = rbc_arena_alloc(arena, chunk_len + 1);
                    memcpy(chunk, chunk_start, chunk_len);
                    chunk[chunk_len] = '\0';
                }
                else
                {
                    chunk = malloc(chunk_len + 1);
                    memcpy(chunk, chunk_start, chunk_len);
                    chunk[chunk_len] = '\0';
                }
                rbc_str_list_add(&options, chunk);
                if (!arena)
                    free(chunk);
                break;
            }
        }
        else if (*p == ',' && depth == 1)
        {
            size_t chunk_len = p - chunk_start;
            char *chunk;
            if (arena)
            {
                chunk = rbc_arena_alloc(arena, chunk_len + 1);
                memcpy(chunk, chunk_start, chunk_len);
                chunk[chunk_len] = '\0';
            }
            else
            {
                chunk = malloc(chunk_len + 1);
                memcpy(chunk, chunk_start, chunk_len);
                chunk[chunk_len] = '\0';
            }
            rbc_str_list_add(&options, chunk);
            if (!arena)
                free(chunk);
            chunk_start = p + 1;
        }
        p++;
    }

    const char *suffix = (*p == '}') ? p + 1 : p;

    for (size_t i = 0; i < options.count; i++)
    {
        if (arena)
        {
            size_t opt_len = strlen(options.items[i]);
            size_t suf_len = strlen(suffix);
            char *next_pattern = rbc_arena_alloc(arena, opt_len + suf_len + 1);
            memcpy(next_pattern, options.items[i], opt_len);
            memcpy(next_pattern + opt_len, suffix, suf_len + 1);
            expand_recursive(results, new_prefix_base, next_pattern, arena);
        }
        else
        {
            size_t opt_len = strlen(options.items[i]);
            size_t suf_len = strlen(suffix);
            char *next_pattern = malloc(opt_len + suf_len + 1);
            sprintf(next_pattern, "%s%s", options.items[i], suffix);
            expand_recursive(results, new_prefix_base, next_pattern, arena);
            free(next_pattern);
        }
    }

    rbc_str_list_free(&options);
    if (!arena)
        free(new_prefix_base);
}

static rbc_str_list_t rbc_brace_expand(const char *pattern, rbc_arena_t *arena)
{
    rbc_str_list_t list;
    rbc_str_list_init(&list, 8, arena);
    expand_recursive(&list, "", pattern, arena);
    return list;
}

static void expand_recursive_visitor(const char *pattern, rbc_arena_t *arena, rbc_brace_visit_cb cb, void *arg)
{
    const char *p = pattern;
    bool in_brace = false;

    while (*p)
    {
        if (*p == '\\')
        {
            p += 2;
            continue;
        }
        if (*p == '{')
        {
            in_brace = true;
            break;
        }
        p++;
    }

    if (!in_brace)
    {
        cb(pattern, arg);
        return;
    }

    rbc_str_list_t options;
    rbc_str_list_init(&options, 4, arena);

    p = pattern;
    while (*p && *p != '{')
    {
        if (*p == '\\')
            p++;
        p++;
    }

    if (*p != '{')
    {
        cb(pattern, arg);
        rbc_str_list_free(&options);
        return;
    }

    size_t prefix_len = p - pattern;
    p++;
    int depth = 1;
    const char *chunk_start = p;
    bool valid_brace = false;

    while (*p)
    {
        if (*p == '\\')
        {
            p += 2;
            continue;
        }
        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            depth--;
            if (depth == 0)
            {
                size_t len = p - chunk_start;
                char *chunk;
                if (arena)
                {
                    chunk = rbc_arena_alloc(arena, len + 1);
                    memcpy(chunk, chunk_start, len);
                    chunk[len] = 0;
                }
                else
                {
                    chunk = malloc(len + 1);
                    memcpy(chunk, chunk_start, len);
                    chunk[len] = 0;
                }
                rbc_str_list_add(&options, chunk);
                if (!arena)
                    free(chunk);
                valid_brace = true;
                break;
            }
        }
        else if (*p == ',' && depth == 1)
        {
            size_t len = p - chunk_start;
            char *chunk;
            if (arena)
            {
                chunk = rbc_arena_alloc(arena, len + 1);
                memcpy(chunk, chunk_start, len);
                chunk[len] = 0;
            }
            else
            {
                chunk = malloc(len + 1);
                memcpy(chunk, chunk_start, len);
                chunk[len] = 0;
            }
            rbc_str_list_add(&options, chunk);
            if (!arena)
                free(chunk);
            chunk_start = p + 1;
        }
        p++;
    }

    if (!valid_brace)
    {
        cb(pattern, arg);
        rbc_str_list_free(&options);
        return;
    }

    const char *suffix = p + 1;
    for (size_t i = 0; i < options.count; i++)
    {
        size_t opt_len = strlen(options.items[i]);
        size_t suf_len = strlen(suffix);
        size_t needed = prefix_len + opt_len + suf_len + 1;

        if (needed < 4096)
        {
            char vla[needed];
            memcpy(vla, pattern, prefix_len);
            memcpy(vla + prefix_len, options.items[i], opt_len);
            memcpy(vla + prefix_len + opt_len, suffix, suf_len + 1);
            expand_recursive_visitor(vla, arena, cb, arg);
        }
        else
        {
            char *next_buf = arena ? rbc_arena_alloc(arena, needed) : malloc(needed);
            memcpy(next_buf, pattern, prefix_len);
            memcpy(next_buf + prefix_len, options.items[i], opt_len);
            memcpy(next_buf + prefix_len + opt_len, suffix, suf_len + 1);

            expand_recursive_visitor(next_buf, arena, cb, arg);
            if (!arena)
                free(next_buf);
        }
    }

    rbc_str_list_free(&options);
}

void rbc_brace_visit(const char *pattern, rbc_arena_t *arena, rbc_brace_visit_cb cb, void *arg)
{
    expand_recursive_visitor(pattern, arena, cb, arg);
}

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
                m->strategy = RBC_STRATEGY_INFIX; // or SEQUENCE with empty outer logic?
                // Actually "*" matches everything (except hidden).
                // Let's treat it as INFIX with empty pattern? Or SUFFIX with empty?
                // Let's treat it as SEQUENCE with ["", ""] parts?
                // Simplest: STRATEGY_PREFIX with empty string (starts with "")
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

static rbc_segment_t *rbc_segment_new(rbc_arena_t *arena, rbc_segment_type_t type)
{
    rbc_segment_t *seg = rbc_arena_alloc(arena, sizeof(rbc_segment_t));
    memset(seg, 0, sizeof(rbc_segment_t));
    seg->type = type;
    return seg;
}

static bool is_recursive_wildcard(const char *s)
{
    return strcmp(s, "**") == 0;
}

rbc_segment_t *rbc_compile_segments(rbc_arena_t *arena, const char *pattern, unsigned int flags)
{
    if (!pattern || !*pattern)
        return NULL;

    rbc_segment_t *head = NULL;
    rbc_segment_t *curr = NULL;

    const char *p = pattern;
    while (*p)
    {
        const char *end = rbc_find_segment_end(p);
        size_t len = end - p;
        if (len == 0)
        {
            if (*end == '/')
                p = end + 1;
            else
                p = end;
            continue;
        }

        char *component = rbc_arena_alloc(arena, len + 1);
        memcpy(component, p, len);
        component[len] = '\0';

        bool is_sep = (*end == '/');
        p = is_sep ? end + 1 : end;
        // Logic for "rest of the string" needed for branches
        const char *rest = p;

        rbc_segment_t *seg = NULL;
        // Use the compiler's arena for brace expansion.
        // This avoids malloc/free overhead for temporary strings.
        // The expanded strings will persist in the arena for the lifetime of the compiled glob, which is acceptable.
        rbc_str_list_t expansions = rbc_brace_expand(component, arena);

        // Special Case: Pure Recursive Wildcard
        if (!rbc_has_brace(component) && is_recursive_wildcard(component))
        {
            seg = rbc_segment_new(arena, RBC_SEGMENT_RECURSIVE);
            rbc_str_list_free(&expansions);
            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            continue;
        }

        // Special Case: Simple Literal (No braces, no wildcards)
        if (!rbc_has_brace(component) && !rbc_has_wildcard(component))
        {
            seg = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
            seg->data.literal = component;
            rbc_str_list_free(&expansions);
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
            if (rbc_has_wildcard(expansions.items[i]))
                all_literals = false;
        }

        if (any_slash || all_literals || expansions.count > 1)
        {
            // Case A: SEG_BRANCH (Stat Optimization, Topology Split, or Brace Branching)
            seg = rbc_segment_new(arena, RBC_SEGMENT_BRANCH);
            rbc_segment_t *last_alt = NULL;

            for (size_t i = 0; i < expansions.count; i++)
            {
                char *full_pattern;
                // If is_sep, we need to append "/" + rest.
                // If rest is empty, we just append "/"?
                // Wait, if pattern ended with /, "dir/" -> component "dir". rest "".
                // If we expand dir -> "dir". full_pattern = "dir/".

                if (is_sep)
                {
                    full_pattern = rbc_arena_printf(arena, "%s/%s", expansions.items[i], rest);
                }
                else if (*rest)
                {
                    // Should technically not happen if we parsed correctly up to separator,
                    // unless separator was implicit or missing?
                    // Just append.
                    full_pattern = rbc_arena_printf(arena, "%s%s", expansions.items[i], rest);
                }
                else
                {
                    full_pattern = rbc_arena_strdup(arena, expansions.items[i]);
                }

                rbc_segment_t *alt_chain = NULL;

                // Optimization: Short-circuit recursion for simple literal leaves (Pattern 1)
                // If we know this branch is a simple literal (no slash append, no rest, and from all_literals set),
                // we can create the node directly without re-parsing.
                if (all_literals && !is_sep && !*rest)
                {
                    alt_chain = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
                    alt_chain->data.literal = full_pattern;
                }
                else
                {
                    alt_chain = rbc_compile_segments(arena, full_pattern, flags);
                }

                // Handle empty expansion case?
                if (!alt_chain && !*full_pattern)
                {
                    alt_chain = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
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
            rbc_str_list_free(&expansions);

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
            seg = rbc_segment_new(arena, RBC_SEGMENT_WILDCARD);
            seg->data.glob.original_pattern = rbc_arena_strdup(arena, expansions.items[0]);

            // Analyze pattern and select strategy
            rbc_matcher_build(arena, &seg->data.glob.matcher, seg->data.glob.original_pattern, flags);

            rbc_str_list_free(&expansions);

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
#include "internal.h"

// VM Program creation is now handled directly by compiler
// But we might need some stubs if referenced elsewhere

// Empty implementation or removal if build system allows
