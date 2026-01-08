#include <stdio.h>
#include <string.h>
#include "rbcglob/internal/graph.h"

rbcglob_node_t *rbcglob_graph_new_node(rbcglob_arena_t *arena, rbcglob_opcode_type_t type)
{
    rbcglob_node_t *node = rbcglob_arena_alloc(arena, sizeof(rbcglob_node_t));
    memset(node, 0, sizeof(rbcglob_node_t));
    node->type = type;
    return node;
}

void rbcglob_graph_dump(const rbcglob_node_t *node)
{
    // Basic recursion check or loop to prevent infinite loop on cycles is needed for robust dump
    // For now, simpler implementation
    const rbcglob_node_t *curr = node;

    // Limits to prevent infinite loops (simple safeguard)
    int max_steps = 1000;
    int steps = 0;

    while (curr && steps < max_steps)
    {
        steps++;
        printf("[%p] ", (void *)curr);
        switch (curr->type)
        {
        case OP_MATCH_LITERAL:
            printf("LITERAL \"%s\"", curr->data.literal ? curr->data.literal : "(null)");
            break;
        case OP_MATCH_STAR:
            printf("STAR *");
            break;
        case OP_MATCH_STAR2:
            printf("STAR2 **");
            break;
        case OP_MATCH_QMARK:
            printf("QMARK ?");
            break;
        case OP_MATCH_CLASS:
            printf("CLASS [%s]%s", curr->data.char_class.negated ? "^" : "", curr->data.char_class.chars);
            break;
        case OP_FORK:
            printf("FORK -> %p | %p", (void *)curr->data.branch.next, (void *)curr->data.branch.alt);
            // Dumping branches is complex in a linear loop, would need recursive dumper
            break;
        case OP_JUMP:
            printf("JUMP");
            break;
        case OP_ACCEPT:
            printf("ACCEPT");
            break;
        case OP_EOS:
            printf("EOS");
            break;
        default:
            printf("UNKNOWN(%d)", curr->type);
        }
        printf("\n");

        if (curr->type == OP_FORK)
        {
            // For FORK, we stop linear dump to avoid confusion,
            // separate visualization is needed for full graph
            printf("  (Full graph dump requires more complex logic)\n");
            break;
        }

        curr = curr->next;
    }
}
