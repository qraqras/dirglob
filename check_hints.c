#include <stdio.h>
#include "rbc/rbc.h"

int main()
{
    const char *pattern = "{src,tests}/**/*.{c,h}";
    printf("Pattern: %s\n", pattern);

    // Try rbc_glob
    char **paths = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    bool success = rbc_glob(&pattern, 1, 0, NULL, false, &paths, &count, &lengths);
    printf("\nrbc_glob success: %d, count: %zu\n", success, count);
    if (success && count > 0)
    {
        for (size_t i = 0; i < (count < 5 ? count : 5); i++)
        {
            printf("  [%zu] %s\n", i, paths[i]);
        }
    }

    // Try rbc_glob_trie
    char **trie_paths = NULL;
    size_t trie_count = 0;
    size_t *trie_lengths = NULL;

    bool trie_success = rbc_glob_trie(&pattern, 1, 0, NULL, false, &trie_paths, &trie_count, &trie_lengths);
    printf("\nrbc_glob_trie success: %d, count: %zu\n", trie_success, trie_count);
    if (trie_success && trie_count > 0)
    {
        for (size_t i = 0; i < (trie_count < 5 ? trie_count : 5); i++)
        {
            printf("  [%zu] %s\n", i, trie_paths[i]);
        }
    }

    return 0;
}
