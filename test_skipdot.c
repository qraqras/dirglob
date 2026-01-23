#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "rbc.h"

void test_pattern(const char *pattern, unsigned flags, const char *desc)
{
    char **results = NULL;
    size_t count = 0;

    const char *patterns[] = {pattern};
    printf("%s:\n", desc);
    printf("  Pattern: \"%s\", Flags: 0x%x\n", pattern, flags);

    if (rbc_glob(patterns, 1, flags, NULL, true, &results, &count, NULL))
    {

        bool has_dot = false;
        bool has_sub_dot = false;

        for (size_t i = 0; i < count; i++)
        {
            if (strcmp(results[i], ".") == 0)
            {
                has_dot = true;
            }
            if (strstr(results[i], "sub/.") != NULL)
            {
                has_sub_dot = true;
            }
            printf("  [%zu] %s\n", i, results[i]);
        }

        printf("  Total: %zu, Has '.': %s, Has 'sub/.': %s\n",
               count, has_dot ? "YES" : "NO", has_sub_dot ? "YES" : "NO");

        rbc_glob_free(results, count, NULL);
    }
    else
    {
        printf("%s: FAILED\n", desc);
    }
    printf("\n");
}

int main(void)
{
    // Change to test directory
    if (chdir("/tmp/skipdot_test") != 0)
    {
        perror("chdir");
        return 1;
    }

    printf("=== SKIPDOT Implementation Test ===\n\n");

    // Test 1: Top level with no DOTMATCH
    test_pattern("*", 0, "Test 1: * (no flags)");

    // Test 2: Top level with DOTMATCH
    test_pattern("*", RBC_FNM_DOTMATCH, "Test 2: * (DOTMATCH)");

    // Test 3: Explicit dot pattern
    test_pattern(".*", 0, "Test 3: .* (no flags)");

    // Test 4: Subdirectory with no DOTMATCH
    test_pattern("*/*", 0, "Test 4: */* (no flags)");

    // Test 5: Subdirectory with DOTMATCH
    test_pattern("*/*", RBC_FNM_DOTMATCH, "Test 5: */* (DOTMATCH)");

    // Test 6: Explicit dot pattern in subdirectory
    test_pattern("*/.*", 0, "Test 6: */.* (no flags)");

    // Test 7: Explicit dot pattern with DOTMATCH
    test_pattern("*/.*", RBC_FNM_DOTMATCH, "Test 7: */.* (DOTMATCH)");

    return 0;
}
