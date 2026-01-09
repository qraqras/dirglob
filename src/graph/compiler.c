#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "rbcglob/internal/graph.h"
#include "rbcglob/internal/utils.h"

/******************************************************************************
 * Part 0: Strategy Analysis Helper
 ******************************************************************************/

static void analyze_and_build_matcher(rbcglob_arena_t *arena, rbcglob_matcher_t *m, const char *pattern)
{
    // Check for complexity features
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

    // Force NFA if complex characters exist
    // Note: Escaped characters might trigger this simple check, which is safe (just falls back to NFA).
    if (has_qmark || has_bracket || has_brace || has_paren || has_pipe)
    {
        m->strategy = STRATEGY_NFA;
        // ... (Logic to be filled in NFA Setup)
    }
    else if (star_count == 0)
    {
        m->strategy = STRATEGY_EXACT;
        m->pk.literal = rbcglob_arena_strdup(arena, pattern);
    }
    else if (star_count == 1)
    {
        size_t len = strlen(pattern);
        if (pattern[0] == '*')
        {
            if (len == 1)
            {
                // Pattern is "*"
                m->strategy = STRATEGY_INFIX; // or SEQUENCE with empty outer logic?
                // Actually "*" matches everything (except hidden).
                // Let's treat it as INFIX with empty pattern? Or SUFFIX with empty?
                // Let's treat it as SEQUENCE with ["", ""] parts?
                // Simplest: STRATEGY_PREFIX with empty string (starts with "")
                m->strategy = STRATEGY_PREFIX; // matches anything starting with empty string
                m->pk.affix.pattern = "";
                m->pk.affix.len = 0;
            }
            else
            {
                // "*suffix"
                m->strategy = STRATEGY_SUFFIX;
                m->pk.affix.pattern = rbcglob_arena_strdup(arena, pattern + 1);
                m->pk.affix.len = len - 1;
            }
        }
        else if (pattern[len - 1] == '*')
        {
            // "prefix*"
            m->strategy = STRATEGY_PREFIX;
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
            // So "pre*suf" is SEQUENCE.
            m->strategy = STRATEGY_SEQUENCE;

            // Build 2 parts
            m->pk.seq.count = 2;
            m->pk.seq.parts = rbcglob_arena_alloc(arena, sizeof(char *) * 2);

            // Copy prefix
            char *star_pos = strchr(pattern, '*');
            size_t pre_len = star_pos - pattern;
            char *pre = rbcglob_arena_alloc(arena, pre_len + 1);
            memcpy(pre, pattern, pre_len);
            pre[pre_len] = '\0';
            m->pk.seq.parts[0] = pre;

            // Copy suffix
            m->pk.seq.parts[1] = rbcglob_arena_strdup(arena, star_pos + 1);

            m->pk.seq.match_start = true;
            m->pk.seq.match_end = true;
        }
    }
    else
    {
        // 2 or more stars
        // Check for INFIX case: "*word*"
        size_t len = strlen(pattern);
        if (pattern[0] == '*' && pattern[len - 1] == '*' && star_count == 2)
        {
            m->strategy = STRATEGY_INFIX;
            m->pk.affix.pattern = rbcglob_arena_strdup(arena, pattern + 1);
            m->pk.affix.pattern[len - 2] = '\0'; // Remove trailing star
            m->pk.affix.len = len - 2;
        }
        else
        {
            m->strategy = STRATEGY_SEQUENCE;

            // Split by '*'
            // First count parts (star_count + 1 max)
            // But consecutive stars might reduce count.
            // e.g. "a**b" -> "a*b".
            // The split logic should handle empty parts or we normalize?
            // Let's implement robust split.

            m->pk.seq.match_start = (pattern[0] != '*');
            m->pk.seq.match_end = (pattern[len - 1] != '*');

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

            m->pk.seq.parts = parts;
            m->pk.seq.count = count;
        }
    }

    if (m->strategy == STRATEGY_NFA)
    {
        // Fill NFA struct
        m->pk.nfa.root = rbcglob_compile_nfa_fragment(arena, pattern);

        // Optimization: Extract static prefix for fast-fail
        // This allows us to check "starts_with" before entering the NFA engine.
        // We stop at the first special character or escape sequence.
        const char *special_chars = "*?[{(|\\";
        size_t prefix_len = strcspn(pattern, special_chars);

        if (prefix_len > 0)
        {
            m->pk.nfa.must_start = rbcglob_arena_alloc(arena, prefix_len + 1);
            memcpy(m->pk.nfa.must_start, pattern, prefix_len);
            m->pk.nfa.must_start[prefix_len] = '\0';
            m->pk.nfa.start_len = prefix_len;
        }
        else
        {
            m->pk.nfa.must_start = NULL;
            m->pk.nfa.start_len = 0;
        }

        m->pk.nfa.must_end = NULL; // TODO: Implement suffix optimization
        m->pk.nfa.end_len = 0;
        m->pk.nfa.required_literals = NULL; // TODO: Implement infix optimization
        m->pk.nfa.req_count = 0;
    }
}

/******************************************************************************
 * Part 1: NFA Fragment Compilation (Character-Level)
 * Used for STRATEGY_NFA internal matching.
 ******************************************************************************/

typedef struct
{
    rbcglob_node_t *start;
    rbcglob_node_t *tail;
} graph_fragment_t;

typedef struct
{
    rbcglob_arena_t *arena;
    const char *ptr;
} compiler_ctx_t;

static graph_fragment_t compile_nfa_recursive(compiler_ctx_t *ctx, bool inside_brace);

/* Create new node helper */
rbcglob_node_t *rbcglob_graph_new_node(rbcglob_arena_t *arena, rbcglob_opcode_type_t type)
{
    rbcglob_node_t *node = rbcglob_arena_alloc(arena, sizeof(rbcglob_node_t));
    memset(node, 0, sizeof(rbcglob_node_t));
    node->type = type;
    return node;
}

static void fragment_append(graph_fragment_t *frag, rbcglob_node_t *node)
{
    if (!frag->start)
    {
        frag->start = node;
        frag->tail = node;
    }
    else
    {
        frag->tail->next = node;
        frag->tail = node;
    }
}

static rbcglob_node_t *make_literal(compiler_ctx_t *ctx, const char *start, size_t len)
{
    if (len == 0)
        return NULL;

    rbcglob_node_t *node = rbcglob_graph_new_node(ctx->arena, OP_MATCH_LITERAL);
    node->data.literal = rbcglob_arena_alloc(ctx->arena, len + 1);
    memcpy(node->data.literal, start, len);
    node->data.literal[len] = '\0';
    return node;
}

static void parse_and_fill_class(rbcglob_node_t *node, compiler_ctx_t *ctx)
{
    const char *p = ctx->ptr;
    memset(node->data.char_class.map, 0, 32);
    node->data.char_class.is_negated = false;

    if (!*p)
    {
        ctx->ptr = p;
        return;
    }
    if (*p == '^' || *p == '!')
    {
        node->data.char_class.is_negated = true;
        p++;
    }
    if (*p == ']')
    {
        unsigned char c = ']';
        node->data.char_class.map[c / 8] |= (1 << (c % 8));
        p++;
    }

    while (*p && *p != ']')
    {
        unsigned char c1;
        if (*p == '\\')
        {
            if (p[1])
                p++;
            c1 = (unsigned char)*p;
            p++;
        }
        else
        {
            c1 = (unsigned char)*p;
            p++;
        }

        if (*p == '-' && p[1] && p[1] != ']')
        {
            p++;
            unsigned char c2;
            if (*p == '\\')
            {
                if (p[1])
                    p++;
                c2 = (unsigned char)*p;
                p++;
            }
            else
            {
                c2 = (unsigned char)*p;
                p++;
            }

            if (c1 <= c2)
            {
                for (int i = c1; i <= c2; i++)
                    node->data.char_class.map[i / 8] |= (1 << (i % 8));
            }
        }
        else
        {
            node->data.char_class.map[c1 / 8] |= (1 << (c1 % 8));
        }
    }
    if (*p == ']')
        p++;
    ctx->ptr = p;
}

static graph_fragment_t compile_brace(compiler_ctx_t *ctx)
{
    rbcglob_node_t *merge_node = rbcglob_graph_new_node(ctx->arena, OP_JUMP);
    rbcglob_node_t *fork_head = NULL;
    rbcglob_node_t *fork_curr = NULL;

    while (1)
    {
        graph_fragment_t frag = compile_nfa_recursive(ctx, true);
        if (frag.start)
            frag.tail->next = merge_node;
        else
        {
            rbcglob_node_t *noop = rbcglob_graph_new_node(ctx->arena, OP_JUMP);
            noop->next = merge_node;
            frag.start = noop;
        }

        if (!fork_head)
        {
            fork_head = rbcglob_graph_new_node(ctx->arena, OP_FORK);
            fork_curr = fork_head;
        }
        else
        {
            rbcglob_node_t *next_fork = rbcglob_graph_new_node(ctx->arena, OP_FORK);
            fork_curr->data.branch.alt = next_fork;
            fork_curr = next_fork;
        }
        fork_curr->data.branch.next = frag.start;

        if (*ctx->ptr == '}')
        {
            ctx->ptr++;
            break;
        }
        else if (*ctx->ptr == ',')
        {
            ctx->ptr++;
        }
        else
        {
            break;
        }
    }
    graph_fragment_t result = {fork_head, merge_node};
    return result;
}

static graph_fragment_t compile_nfa_recursive(compiler_ctx_t *ctx, bool inside_brace)
{
    graph_fragment_t current_frag = {NULL, NULL};
    const char *literal_start = ctx->ptr;

#define FLUSH_LITERAL()                                                                       \
    do                                                                                        \
    {                                                                                         \
        if (ctx->ptr > literal_start)                                                         \
        {                                                                                     \
            rbcglob_node_t *lit = make_literal(ctx, literal_start, ctx->ptr - literal_start); \
            fragment_append(&current_frag, lit);                                              \
        }                                                                                     \
    } while (0)

    while (*ctx->ptr)
    {
        char c = *ctx->ptr;
        if (inside_brace && (c == ',' || c == '}'))
        {
            FLUSH_LITERAL();
            return current_frag;
        }
        if (c == '\\')
        {
            if (ctx->ptr[1])
                ctx->ptr++;
            ctx->ptr++;
            continue;
        }
        if (c == '*')
        {
            FLUSH_LITERAL();
            rbcglob_node_t *node;
            if (ctx->ptr[1] == '*')
            {
                node = rbcglob_graph_new_node(ctx->arena, OP_MATCH_STAR);
                ctx->ptr += 2;
            }
            else
            {
                node = rbcglob_graph_new_node(ctx->arena, OP_MATCH_STAR);
                ctx->ptr++;
            }
            fragment_append(&current_frag, node);
            literal_start = ctx->ptr;
            continue;
        }
        if (c == '?')
        {
            FLUSH_LITERAL();
            rbcglob_node_t *node = rbcglob_graph_new_node(ctx->arena, OP_MATCH_QMARK);
            ctx->ptr++;
            fragment_append(&current_frag, node);
            literal_start = ctx->ptr;
            continue;
        }
        if (c == '[')
        {
            FLUSH_LITERAL();
            ctx->ptr++;
            rbcglob_node_t *node = rbcglob_graph_new_node(ctx->arena, OP_MATCH_CLASS);
            parse_and_fill_class(node, ctx);
            fragment_append(&current_frag, node);
            literal_start = ctx->ptr;
            continue;
        }
        if (c == '{')
        {
            FLUSH_LITERAL();
            ctx->ptr++;
            graph_fragment_t brace_frag = compile_brace(ctx);
            if (brace_frag.start)
            {
                fragment_append(&current_frag, brace_frag.start);
                current_frag.tail = brace_frag.tail;
            }
            literal_start = ctx->ptr;
            continue;
        }
        ctx->ptr++;
    }
    FLUSH_LITERAL();
    return current_frag;
}

rbcglob_node_t *rbcglob_compile_nfa_fragment(rbcglob_arena_t *arena, const char *pattern)
{
    if (!pattern)
        return NULL;
    compiler_ctx_t ctx = {arena, pattern};
    graph_fragment_t root = compile_nfa_recursive(&ctx, false);
    rbcglob_node_t *accept = rbcglob_graph_new_node(arena, OP_ACCEPT);
    fragment_append(&root, accept);
    return root.start;
}

/******************************************************************************
 * Part 2: Segment Compilation (Path-Level)
 ******************************************************************************/

rbcglob_segment_t *rbcglob_segment_new(rbcglob_arena_t *arena, rbcglob_seg_type_t type)
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
            seg = rbcglob_segment_new(arena, SEG_RECURSIVE);
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
            seg = rbcglob_segment_new(arena, SEG_LITERAL);
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
            seg = rbcglob_segment_new(arena, SEG_BRANCH);
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
                    alt_chain = rbcglob_segment_new(arena, SEG_LITERAL);
                    alt_chain->data.literal = full_pattern;
                }
                else
                {
                    alt_chain = rbcglob_compile_segments(arena, full_pattern);
                }

                // Handle empty expansion case?
                if (!alt_chain && !*full_pattern)
                {
                    alt_chain = rbcglob_segment_new(arena, SEG_LITERAL);
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
            seg = rbcglob_segment_new(arena, SEG_WILDCARD);
            seg->data.glob.original_pattern = rbcglob_arena_strdup(arena, expansions.items[0]);

            // Analyze pattern and select strategy
            analyze_and_build_matcher(arena, &seg->data.glob.matcher, seg->data.glob.original_pattern);

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
