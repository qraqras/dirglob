#include <stdio.h>
#include <stdlib.h>
#include "rbc/rbc.h"

int main(void)
{
    // Test 1: Simple brace expansion
    const char *patterns1[] = {"{a,b,c}"};
    char **results1 = NULL;
    size_t count1 = 0;

    printf("Test 1: {a,b,c} in tests/fixtures/05_braceexpansion/\n");
    if (rbc_glob(patterns1, 1, 0, "tests/fixtures/05_braceexpansion", true, &results1, &count1, NULL))
    {
        printf("  Expanded to %zu patterns:\n", count1);
        for (size_t i = 0; i < count1; i++)
        {
            printf("    - %s\n", results1[i]);
        }
        rbc_glob_free(results1, count1, NULL);
    }
    else
    {
        printf("  ERROR: Failed to expand\n");
    }

    // Test 2: Nested braces
    const char *patterns2[] = {"{a,{b,c}}"};
    char **results2 = NULL;
    size_t count2 = 0;

    printf("\nTest 2: {a,{b,c}} in tests/fixtures/05_braceexpansion/\n");
    if (rbc_glob(patterns2, 1, 0, "tests/fixtures/05_braceexpansion", true, &results2, &count2, NULL))
    {
        printf("  Expanded to %zu patterns:\n", count2);
        for (size_t i = 0; i < count2; i++)
        {
            printf("    - %s\n", results2[i]);
        }
        rbc_glob_free(results2, count2, NULL);
    }
    else
    {
        printf("  ERROR: Failed to expand\n");
    }

    // Test 3: Multiple braces
    const char *patterns3[] = {"file{0,1}.{txt}"};
    char **results3 = NULL;
    size_t count3 = 0;

    printf("\nTest 3: file{0,1}.{txt} in tests/fixtures/05_braceexpansion/\n");
    if (rbc_glob(patterns3, 1, 0, "tests/fixtures/05_braceexpansion", true, &results3, &count3, NULL))
    {
        printf("  Expanded to %zu patterns:\n", count3);
        for (size_t i = 0; i < count3; i++)
        {
            printf("    - %s\n", results3[i]);
        }
        rbc_glob_free(results3, count3, NULL);
    }
    else
    {
        printf("  ERROR: Failed to expand\n");
    }

    printf("\n✓ Brace preprocessing test completed\n");
    return 0;
}
