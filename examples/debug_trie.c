/**
 * @file debug_trie.c
 * @brief Debug trie structure
 */

#include <stdio.h>
#include <rbc/rbc.h>

static void test_pattern(const char *pattern)
{
    printf("\n=== Pattern: %s ===\n", pattern);

    char **results_glob = NULL;
    char **results_trie = NULL;
    size_t count_glob = 0;
    size_t count_trie = 0;

    // rbc_glob
    if (rbc_glob(&pattern, 1, 0, NULL, false, &results_glob, &count_glob, NULL))
    {
        printf("rbc_glob: %zu results\n", count_glob);
        for (size_t i = 0; i < count_glob && i < 5; i++)
        {
            printf("  [%zu] %s\n", i, results_glob[i]);
        }
        rbc_glob_free(results_glob, count_glob, NULL);
    }

    // rbc_glob_trie
    if (rbc_glob_trie(&pattern, 1, 0, NULL, false, &results_trie, &count_trie, NULL))
    {
        printf("rbc_glob_trie: %zu results\n", count_trie);
        for (size_t i = 0; i < count_trie && i < 5; i++)
        {
            printf("  [%zu] %s\n", i, results_trie[i]);
        }
        rbc_glob_free(results_trie, count_trie, NULL);
    }
}

int main(void)
{
    chdir("/workspaces/dirglob");

    test_pattern("{src,tests}/**/*.{c,h}");
    test_pattern("src/**/*.c");
    test_pattern("src/core/*.c");

    return 0;
}
