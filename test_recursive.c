#include <stdio.h>
#include "rbc/rbc.h"

void test_pattern(const char *pattern, unsigned flags, const char *desc)
{
    printf("\n=== %s ===\n", desc);
    printf("Pattern: '%s'\n", pattern);

    char **files = NULL;
    size_t count = 0;
    bool ret = rbc_glob(&pattern, 1, flags, ".", true, &files, &count, NULL);

    if (ret)
    {
        printf("Count: %zu\n", count);
        if (count > 0)
        {
            printf("Results:\n");
            for (size_t i = 0; i < count && i < 20; i++)
            {
                printf("  [%zu] %s\n", i, files[i]);
            }
            if (count > 20)
                printf("  ... (%zu more)\n", count - 20);
        }
        rbc_glob_free(files, count, NULL);
    }
    else
    {
        printf("Error\n");
    }
}

int main(void)
{
    printf("Testing ** (doublestar) recursion\n");
    printf("====================================\n");

    // Test 1: 07_recursive/** (terminal **)
    test_pattern("07_recursive/**", RBC_FNM_DOTMATCH, "Test 1: 07_recursive/** (terminal)");

    // Test 2: 07_recursive/**/ (directories only)
    test_pattern("07_recursive/**/", RBC_FNM_DOTMATCH, "Test 2: 07_recursive/**/ (dirs only)");

    // Test 3: 07_recursive/**/** (double recursion)
    test_pattern("07_recursive/**/**", RBC_FNM_DOTMATCH, "Test 3: 07_recursive/**/** (double)");

    // Test 4: 07_recursive/**/*.txt (recursive with pattern)
    test_pattern("07_recursive/**/*.txt", RBC_FNM_DOTMATCH, "Test 4: 07_recursive/**/*.txt");

    // Test 5: Simple ** from root
    test_pattern("**/file0.txt", RBC_FNM_DOTMATCH, "Test 5: **/file0.txt");

    return 0;
}
