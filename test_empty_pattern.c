#include <rbc/rbc.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
    // Test 1: Empty pattern with base="."
    printf("Test 1: Empty pattern with base=\".\"\n");
    {
        const char *patterns[] = {""};
        char **results = NULL;
        size_t count = 0;
        size_t *lengths = NULL;

        bool ret = rbc_glob(patterns, 1, 0, ".", true, &results, &count, &lengths);
        printf("  Return: %d, Count: %zu\n", ret, count);
        for (size_t i = 0; i < count; i++)
        {
            printf("  [%zu] %s\n", i, results[i]);
        }
        rbc_glob_free(results, count, lengths);
    }

    // Test 2: "/" pattern
    printf("\nTest 2: \"/\" pattern\n");
    {
        const char *patterns[] = {"/"};
        char **results = NULL;
        size_t count = 0;
        size_t *lengths = NULL;

        bool ret = rbc_glob(patterns, 1, 0, NULL, true, &results, &count, &lengths);
        printf("  Return: %d, Count: %zu\n", ret, count);
        for (size_t i = 0; i < count; i++)
        {
            printf("  [%zu] %s\n", i, results[i]);
        }
        rbc_glob_free(results, count, lengths);
    }

    // Test 3: "05_braceexpansion/{dir1,}/file1.txt" pattern
    printf("\nTest 3: \"05_braceexpansion/{dir1,}/file1.txt\" pattern\n");
    {
        const char *patterns[] = {"05_braceexpansion/{dir1,}/file1.txt"};
        char **results = NULL;
        size_t count = 0;
        size_t *lengths = NULL;

        bool ret = rbc_glob(patterns, 1, 0, NULL, true, &results, &count, &lengths);
        printf("  Return: %d, Count: %zu\n", ret, count);
        for (size_t i = 0; i < count; i++)
        {
            printf("  [%zu] %s\n", i, results[i]);
        }
        rbc_glob_free(results, count, lengths);
    }

    return 0;
}
