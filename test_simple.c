#include <stdio.h>
#include <stdbool.h>
#include "include/rbc/rbc.h"

int main(int argc, char *argv[])
{
    const char *pattern = (argc > 1) ? argv[1] : "*.md";
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    printf("Testing pattern: %s\n", pattern);

    bool success = rbc_glob(&pattern, 1, 0, NULL, true, &results, &count, &lengths);

    printf("Success: %d\n", success);
    printf("Count: %zu\n", count);

    if (success && results)
    {
        for (size_t i = 0; i < count; i++)
        {
            printf("  [%zu] %s\n", i, results[i]);
        }
        rbc_glob_free(results, count, lengths);
    }

    return 0;
}
