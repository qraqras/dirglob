#include <rbcglob/rbcglob.h>
#include "rbcglob/internal/pattern.h"
#include "rbcglob/internal/utils.h"
#include <stdio.h>

int main(void)
{
    const char *pattern = "*.c";
    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 4096);

    printf("Pattern: %s\n", pattern);
    rbcglob_node_t *graph = rbcglob_compile_nfa_fragment(&arena, pattern);

    // Dump graph (implementation needed or use debugger)
    rbcglob_graph_dump(graph);

    const char *inputs[] = {"test.c", "test.h", "abc.c", "very_long_file_name_for_testing_performance.c"};
    for (int i = 0; i < 4; i++)
    {
        bool match = rbcglob_nfa_match(inputs[i], graph, 0);
        printf("Match '%s': %s\n", inputs[i], match ? "YES" : "NO");
    }

    rbcglob_arena_destroy(&arena);
    return 0;
}
