#include <stdio.h>
#include <stdlib.h>
#include "rbc/rbc.h"

void test_pattern(const char *pattern, unsigned flags, const char *desc)
{
    printf("\n=== %s ===\n", desc);
    printf("Pattern: %s, Flags: %u\n", pattern, flags);

    const char *patterns[] = {pattern};
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    bool ret = rbc_glob(patterns, 1, flags, NULL, true, &results, &count, &lengths);

    if (ret)
    {
        printf("Count: %zu\n", count);
        for (size_t i = 0; i < count && i < 10; i++)
        {
            printf("  %s\n", results[i]);
        }
        if (count > 10)
        {
            printf("  ... (%zu more)\n", count - 10);
        }
        rbc_glob_free(results, count, lengths);
    }
    else
    {
        printf("Error\n");
    }
}

int main()
{
    printf("Testing .** patterns after fix\n");
    printf("================================\n");

    // Test 1: .**/.*/
    test_pattern(".**/.*/", 0, ".**/.*/  (should be 4 matches)");

    // Test 2: .hidden/.**/.*
    test_pattern(".hidden/.**/.*", 0, ".hidden/.**/.*  (should be 15 matches)");

    // Test 3: Basic .** test
    test_pattern(".**", 0, ".**  (non-recursive, dot items only)");

    // Test 4: .**/ (directories starting with dot)
    test_pattern(".**/", 0, ".**/  (dot directories)");

    return 0;
}
