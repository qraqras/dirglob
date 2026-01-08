#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "rbcglob/internal/graph.h"
#include "rbcglob/internal/utils.h" /* For utility functions if needed */

/* Represents a fragment of the graph with open inputs and outputs */
typedef struct
{
    rbcglob_node_t *start;
    rbcglob_node_t *tail; /* The node where the next node should be attached */
} graph_fragment_t;

typedef struct
{
    rbcglob_arena_t *arena;
    const char *ptr;
} compiler_ctx_t;

static graph_fragment_t compile_pattern(compiler_ctx_t *ctx, bool inside_brace);

/**
 * @brief Helper to connect a new node to a fragment
 */
static void fragment_append(graph_fragment_t *frag, rbcglob_node_t *node)
{
    if (!frag->start)
    {
        frag->start = node;
        frag->tail = node;
    }
    else
    {
        frag->tail->next = node; // Link matches
        frag->tail = node;
    }
}

/**
 * @brief Create a literal node from a string buffer
 */
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

/**
 * @brief Parse brace expansion {a,b}
 */
static graph_fragment_t compile_brace(compiler_ctx_t *ctx)
{
    // We assume ctx->ptr starts just after '{'

    rbcglob_node_t *merge_node = rbcglob_graph_new_node(ctx->arena, OP_JUMP);

    // The brace generates a chain of FORK nodes.
    // Fork1 -> Option A -> Jump(Merge)
    //   |
    // Fork2 -> Option B -> Jump(Merge)
    //   |
    // ...

    // We need to keep track of the start of the fork chain
    rbcglob_node_t *fork_head = NULL;
    rbcglob_node_t *fork_curr = NULL;

    while (1)
    {
        // Parse one alternative
        graph_fragment_t frag = compile_pattern(ctx, true);

        // Connect option to Merge
        // Two cases:
        // 1. Option is empty (e.g. {a,}) -> Fragment start is NULL.
        // 2. Option is non-empty.

        if (frag.start)
        {
            // Traverse to the real tail of the fragment (in case it has internal structure)
            // But our 'fragment_append' logic maintains 'tail' as the last added node.
            // Check if tail is valid.
            rbcglob_node_t *t = frag.tail;
            // If the fragment ended with something that connects to invalid, we connect it to merge.
            // Note: If frag has internal JUMPs or FORKs, `tail` should point to the node that continues execution.
            t->next = merge_node;
        }
        else
        {
            // Empty option: Direct jump to merge?
            // Actually, we need a node to represent "do nothing then jump".
            // A JUMP node is exactly that.
            rbcglob_node_t *noop = rbcglob_graph_new_node(ctx->arena, OP_JUMP);
            noop->next = merge_node;
            frag.start = noop;
        }

        // Create a FORK node for this option
        // Wait, the standard way is:
        // Node -> NEXT(This Option)
        //      -> ALT(Next Fork)

        if (!fork_head)
        {
            fork_head = rbcglob_graph_new_node(ctx->arena, OP_FORK);
            fork_curr = fork_head;
        }
        else
        {
            // Create a new fork and attach it to previous fork's ALT
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
            // Continue loop
        }
        else
        {
            // Unexpected char or EOF
            break;
        }
    }

    // The last fork's ALT is NULL (or could point to nothing/failure).
    // In glob {a,b}, we have Fork(a, Fork(b, NULL)).
    // If we run out of ALTs, we fail that path (backtrack).
    // But `b` should be executed.
    // Actually, usually the last option is just linked directly, not via Fork.
    // Fork(a, b).
    // If 3 options {a,b,c}: Fork(a, Fork(b, c)).

    // Let's optimize the last fork.
    // Currently loop creates Fork(Option, NULL).
    // It should be:
    // If this is the last option, do NOT create a new fork for it?
    // OR create Fork(Option, NULL) where NULL means "no more matches".
    // That works if NFA engine treats NULL alt as "fail/backtrack".

    graph_fragment_t result;
    result.start = fork_head;
    result.tail = merge_node;
    return result;
}

static graph_fragment_t compile_pattern(compiler_ctx_t *ctx, bool inside_brace)
{
    graph_fragment_t current_frag = {0};
    const char *literal_start = ctx->ptr;

// Helper to flush literal
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
            // End of this pattern segment
            FLUSH_LITERAL();
            return current_frag;
        }

        if (c == '\\')
        {
            if (ctx->ptr[1])
                ctx->ptr++; // skip backslash, consume next char as literal
            ctx->ptr++;
            continue;
        }

        if (c == '/')
        {
            FLUSH_LITERAL();
            // Create explicit node for separator
            // Usually we treat '/' as just a literal, but ensuring it's a separate node
            // helps the executor break matching into directory components.
            rbcglob_node_t *sep = make_literal(ctx, "/", 1);
            fragment_append(&current_frag, sep);

            ctx->ptr++;
            literal_start = ctx->ptr;
            continue;
        }

        if (c == '*' || c == '?' || c == '[' || c == '{')
        {
            FLUSH_LITERAL();

            rbcglob_node_t *node = NULL;
            if (c == '*')
            {
                if (ctx->ptr[1] == '*')
                {
                    node = rbcglob_graph_new_node(ctx->arena, OP_MATCH_STAR2);
                    ctx->ptr++; // Skip second '*'
                    ctx->ptr++; // Move past '**'

                    // **/ should skip the '/' as it's a directory separator, not part of pattern
                    if (*ctx->ptr == '/')
                    {
                        ctx->ptr++; // Skip '/'
                    }
                    fragment_append(&current_frag, node);
                    literal_start = ctx->ptr;
                    continue;
                }
                else
                {
                    node = rbcglob_graph_new_node(ctx->arena, OP_MATCH_STAR);
                    ctx->ptr++;
                }
            }
            else if (c == '?')
            {
                node = rbcglob_graph_new_node(ctx->arena, OP_MATCH_QMARK);
                ctx->ptr++;
            }
            else if (c == '[')
            {
                // Parse class (simplified)
                while (*ctx->ptr && *ctx->ptr != ']')
                    ctx->ptr++;
                if (*ctx->ptr == ']')
                    ctx->ptr++;
                // TODO: store content
                node = rbcglob_graph_new_node(ctx->arena, OP_MATCH_CLASS);
            }
            else if (c == '{')
            {
                ctx->ptr++;
                graph_fragment_t brace_frag = compile_brace(ctx);
                if (brace_frag.start)
                {
                    fragment_append(&current_frag, brace_frag.start);
                    // The tail of brace_frag is the Merge node.
                    // We need to update current_frag.tail to be this Merge node
                    current_frag.tail = brace_frag.tail;
                    // Note: fragment_append updates tail to brace_frag.start,
                    // but brace_frag is a subgraph. We need to manually fix tail.
                    // Actually fragment_append logic: tail->next = node.
                    // brace_frag.start IS the node we appended.
                    // But we want subsequent nodes to attach to brace_frag.tail.
                    current_frag.tail = brace_frag.tail;
                }
                // compile_brace ends at }
                literal_start = ctx->ptr;
                continue; // Skip literal reset at end of loop
            }

            if (node)
                fragment_append(&current_frag, node);
            literal_start = ctx->ptr;
            continue;
        }

        ctx->ptr++;
    }

    FLUSH_LITERAL();
    return current_frag;
}

rbcglob_node_t *rbcglob_nfa_compile(rbcglob_arena_t *arena, const char *pattern)
{
    if (!pattern)
        return NULL;

    compiler_ctx_t ctx;
    ctx.arena = arena;
    ctx.ptr = pattern;

    graph_fragment_t root = compile_pattern(&ctx, false);

    rbcglob_node_t *accept = rbcglob_graph_new_node(arena, OP_ACCEPT);
    fragment_append(&root, accept);

    return root.start;
}
