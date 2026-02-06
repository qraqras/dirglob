#include <stdio.h>
#include <stdlib.h>
#include <rbc/rbc.h>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <pattern> [pattern... ]\n", argv[0]);
        return 2;
    }

    for (int i = 1; i < argc; ++i)
    {
        const char *pattern = argv[i];
        rbc_glob_result_t result = {0};

        printf("Pattern: '%s'\n", pattern);
        if (rbc_glob(&pattern, 1, 0, ".", false, &result, NULL, NULL) != RBC_GLOB_SUCCESS)
        {
            fprintf(stderr, "rbc_glob failed\n");
            continue;
        }

        printf("Matches: %zu\n", result.count);
        for (size_t j = 0; j < result.count; ++j)
        {
            printf("  %zu: %s\n", j, result.paths[j] ? result.paths[j] : "(null)");
        }
        rbc_globfree(&result);
        printf("---\n");
    }
    return 0;
}
