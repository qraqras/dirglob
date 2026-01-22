#include <stdio.h>
#include "rbc/rbc.h"

void test_pattern(const char *pattern, unsigned flags, const char *desc)
{
    printf("\n=== %s ===\n", desc);
    printf("Pattern: '%s', Flags: 0x%x\n", pattern, flags);

    char **files = NULL;
    size_t count = 0;
    bool ret = rbc_glob(&pattern, 1, flags, ".", true, &files, &count, NULL);

    if (ret)
    {
        printf("Count: %zu\n", count);
        for (size_t i = 0; i < count && i < 15; i++)
        {
            printf("  [%zu] %s\n", i, files[i]);
        }
        if (count > 15)
            printf("  ... (%zu more)\n", count - 15);
        rbc_glob_free(files, count, NULL);
    }
    else
    {
        printf("Error\n");
    }
}

int main(void)
{
    printf("Testing dot file/directory handling\n");
    printf("=====================================\n");

    // Test 1: .* without FNM_DOTMATCH (should match files starting with .)
    test_pattern(".*", 0, "Test 1: .* without FNM_DOTMATCH");

    // Test 2: .* with FNM_DOTMATCH (should match files starting with .)
    test_pattern(".*", RBC_FNM_DOTMATCH, "Test 2: .* with FNM_DOTMATCH");

    // Test 3: * without FNM_DOTMATCH (should NOT match dot files)
    test_pattern("*", 0, "Test 3: * without FNM_DOTMATCH");

    // Test 4: * with FNM_DOTMATCH (should match dot files)
    test_pattern("*", RBC_FNM_DOTMATCH, "Test 4: * with FNM_DOTMATCH");

    // Test 5: .hidden/* (explicit dot in pattern)
    test_pattern(".hidden/*", 0, "Test 5: .hidden/* without FNM_DOTMATCH");

    // Test 6: .hidden/* with FNM_DOTMATCH
    test_pattern(".hidden/*", RBC_FNM_DOTMATCH, "Test 6: .hidden/* with FNM_DOTMATCH");

    // Test 7: **/.* (recursive with explicit dot)
    test_pattern("**/.* ", RBC_FNM_DOTMATCH, "Test 7: **/.* with FNM_DOTMATCH");

    // Test 8: **/.*  without FNM_DOTMATCH
    test_pattern("**/.* ", 0, "Test 8: **/.* without FNM_DOTMATCH");

    return 0;
}
