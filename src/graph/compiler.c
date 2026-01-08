#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "rbcglob/internal/graph.h"
#include "rbcglob/internal/utils.h"

/******************************************************************************
 * Part 1: NFA Fragment Compilation (Character-Level)
 * Used for SEG_WILDCARD internal matching.
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

static bool is_simple_literal(const char *s)
{
    bool esc = false;
    for (; *s; s++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*s == '\\')
        {
            esc = true;
            continue;
        }
        if (*s == '*' || *s == '?' || *s == '[' || *s == '{')
            return false;
    }
    return true;
}

static bool is_recursive_wildcard(const char *s)
{
    return strcmp(s, "**") == 0;
}

static void analyze_wildcard_optimization(rbcglob_arena_t *arena, rbcglob_segment_t *seg, const char *pattern)
{
    const char *p = pattern;
    const char *start = p;
    while (*p && *p != '*' && *p != '?' && *p != '[' && *p != '{' && *p != '\\')
        p++;

    if (p > start)
    {
        size_t len = p - start;
        seg->data.glob.must_start = rbcglob_arena_alloc(arena, len + 1);
        memcpy(seg->data.glob.must_start, start, len);
        seg->data.glob.must_start[len] = '\0';
        seg->data.glob.start_len = len;
    }

    size_t total = strlen(pattern);
    if (total == 0)
        return;

    const char *last_magic_end = NULL;
    const char *cursor = pattern;
    while (*cursor)
    {
        if (*cursor == '\\')
        {
            cursor++;
            if (*cursor)
                cursor++;
            continue;
        }

        if (*cursor == '*' || *cursor == '?' || *cursor == '{' || *cursor == '}')
        {
            last_magic_end = cursor + 1;
        }
        else if (*cursor == '[')
        {
            // Scan for closing ]
            const char *temp = cursor + 1;
            // Handle negative class [^...] or [!...]
            if (*temp == '^' || *temp == '!')
                temp++;
            // Handle ] at start of class []...]
            if (*temp == ']')
                temp++;

            while (*temp && *temp != ']')
            {
                if (*temp == '\\' && temp[1])
                    temp += 2;
                else
                    temp++;
            }
            if (*temp == ']')
            {
                last_magic_end = temp + 1;
                cursor = temp; // Advance main cursor to ]
            }
        }
        cursor++;
    }

    // original code had extra check for '}' using strrchr, but we handled it in loop.

    if (last_magic_end)
    {
        const char *suffix_start = last_magic_end;
        if (*suffix_start)
        {
            size_t slen = strlen(suffix_start);
            if (slen > 0)
            {
                seg->data.glob.must_end = rbcglob_arena_alloc(arena, slen + 1);
                strcpy(seg->data.glob.must_end, suffix_start);
                seg->data.glob.end_len = slen;
            }
        }
    }
}

static bool brace_contains_slash(const char *str)
{
    const char *p = str;
    int depth = 0;
    while (*p)
    {
        if (*p == '\\')
        {
            p += 2;
            continue;
        }
        if (*p == '{')
        {
            depth++;
            p++;
            while (*p && depth > 0)
            {
                if (*p == '\\')
                {
                    p += 2;
                    continue;
                }
                if (*p == '{')
                    depth++;
                if (*p == '}')
                    depth--;
                if (*p == '/' && depth > 0)
                    return true;
                if (*p)
                    p++;
            }
            if (depth == 0)
                continue;
        }
        p++;
    }
    return false;
}

static bool contains_brace(const char *str)
{
    const char *p = str;
    while (*p)
    {
        if (*p == '\\')
        {
            p += 2;
            continue;
        }
        if (*p == '{')
            return true;
        p++;
    }
    return false;
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
        const char *start = p;
        int depth = 0;
        while (*p)
        {
            if (*p == '\\')
            {
                p += 2;
                continue;
            }
            if (*p == '{')
                depth++;
            if (*p == '}')
            {
                if (depth > 0)
                    depth--;
            }
            if (*p == '/' && depth == 0)
                break;
            p++;
        }
        size_t len = p - start;
        bool is_sep = (*p == '/');
        if (is_sep)
            p++;

        if (len == 0)
            continue;

        char *component = rbcglob_arena_alloc(arena, len + 1);
        memcpy(component, start, len);
        component[len] = '\0';

        rbcglob_segment_t *seg = NULL;

        bool has_brace = contains_brace(component);
        bool has_glob = false;
        const char *tmp_scan = component;
        while (*tmp_scan)
        {
            if (*tmp_scan == '\\')
            {
                tmp_scan += 2;
                continue;
            }
            if (*tmp_scan == '*' || *tmp_scan == '?' || *tmp_scan == '[')
            {
                has_glob = true;
                break;
            }
            tmp_scan++;
        }

        if (brace_contains_slash(component) || (has_brace && !has_glob))
        {
            // fprintf(stderr, "DEBUG: SEG_BRANCH for %s\n", component);
            seg = rbcglob_segment_new(arena, SEG_BRANCH);

            char *brace_start = NULL;
            const char *scanner = component;
            while (*scanner)
            {
                if (*scanner == '{')
                {
                    brace_start = (char *)scanner;
                    break;
                }
                scanner++;
            }

            int prefix_len = brace_start - component;
            char *prefix = rbcglob_arena_alloc(arena, prefix_len + 1);
            memcpy(prefix, component, prefix_len);
            prefix[prefix_len] = '\0';

            char *brace_content = brace_start + 1;
            char *p_end = brace_content;
            int bdepth = 1;
            while (*p_end && bdepth > 0)
            {
                if (*p_end == '{')
                    bdepth++;
                if (*p_end == '}')
                    bdepth--;
                if (bdepth == 0)
                    break;
                p_end++;
            }
            char *suffix = p_end + 1;

            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;

            char *opt_start = brace_content;
            char *opt_p = opt_start;
            int b_depth_inner = 0;
            rbcglob_segment_t *last_alt = NULL;

            while (opt_p < p_end + 1)
            {
                bool end = (opt_p == p_end);
                bool comma = (*opt_p == ',' && b_depth_inner == 0);

                if (*opt_p == '{')
                    b_depth_inner++;
                if (*opt_p == '}')
                    b_depth_inner--;

                if (end || comma)
                {
                    size_t opt_len = opt_p - opt_start;
                    char *opt_str = rbcglob_arena_alloc(arena, opt_len + 1);
                    memcpy(opt_str, opt_start, opt_len);
                    opt_str[opt_len] = '\0';

                    char *full_pattern = rbcglob_arena_printf(arena, "%s%s%s", prefix, opt_str, suffix);
                    rbcglob_segment_t *alt_chain = rbcglob_compile_segments(arena, full_pattern);

                    if (!alt_chain && !*full_pattern)
                    {
                        alt_chain = rbcglob_segment_new(arena, SEG_LITERAL);
                        alt_chain->data.literal = "";
                    }

                    if (alt_chain)
                    {
                        if (!seg->data.branch.head)
                        {
                            seg->data.branch.head = alt_chain;
                        }
                        else if (last_alt)
                        {
                            last_alt->next_alt = alt_chain;
                        }
                        last_alt = alt_chain;
                    }

                    if (end)
                        break;
                    opt_start = opt_p + 1;
                }
                opt_p++;
            }
        }
        else
        {
            if (is_recursive_wildcard(component))
            {
                seg = rbcglob_segment_new(arena, SEG_RECURSIVE);
            }
            else if (is_simple_literal(component))
            {
                seg = rbcglob_segment_new(arena, SEG_LITERAL);
                seg->data.literal = component;
            }
            else
            {
                seg = rbcglob_segment_new(arena, SEG_WILDCARD);
                seg->data.glob.original_pattern = component;

                if (contains_brace(component))
                {
                    seg->data.glob.must_start = NULL;
                    seg->data.glob.must_end = NULL;
                }
                else
                {
                    analyze_wildcard_optimization(arena, seg, component);
                }

                seg->data.glob.local_nfa_root = rbcglob_compile_nfa_fragment(arena, component);
            }

            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
        }
    }
    return head;
}
