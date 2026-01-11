#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "rbc/rbc.h"

bool test_glob(const char *pattern, const char *filename, bool expected)
{
    FILE *f = fopen(filename, "w");
    if (f)
        fclose(f);

    const char *patterns[] = {pattern};
    char **results = NULL;
    size_t count = 0;

    bool ret = rbc_glob(patterns, 1, 0, NULL, true, &results, &count, NULL);

    bool found = false;
    if (ret)
    {
        for (size_t i = 0; i < count; i++)
        {
            if (strcmp(results[i], filename) == 0)
            {
                found = true;
            }
        }
    }

    remove(filename);

    if (found == expected)
    {
        printf("PASS: pattern '%s' %s '%s'\n", pattern, expected ? "matched" : "did not match", filename);
        return true;
    }
    else
    {
        printf("FAIL: pattern '%s' %s '%s' (Expected %d, Got %d)\n", pattern, expected ? "matched" : "did not match", filename, expected, found);
        return false;
    }
}

int main()
{
    int failures = 0;

    if (!test_glob("?", "あ", true))
        failures++;
    if (!test_glob("?", "a", true))
        failures++;
    if (!test_glob("?", "あい", false))
        failures++; // ? matches 1 char, not 2
    if (!test_glob("??", "あい", true))
        failures++;

    // Mix ASCII and UTF-8
    if (!test_glob("a?", "aあ", true))
        failures++;
    if (!test_glob("[あ]", "あ", true))
        failures++;
    if (!test_glob("[^a]", "あ", true))
        failures++; // Negated match

    if (failures == 0)
    {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    else
    {
        printf("%d TESTS FAILED\n", failures);
        return 1;
    }
}
