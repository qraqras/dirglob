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
        char **out = NULL;
        size_t count = 0;
        size_t *lengths = NULL;

        printf("Pattern: '%s'\n", pattern);
        if (!rbc_glob(&pattern, 1, 0, ".", false, &out, &count, &lengths))
        {
            fprintf(stderr, "rbc_glob failed\n");
            continue;
        }

        printf("Matches: %zu\n", count);
        for (size_t j = 0; j < count; ++j)
        {
            printf("  %zu: %s\n", j, out[j] ? out[j] : "(null)");
        }
        rbc_globfree(out, count, lengths);
        printf("---\n");
    }
    return 0;
}
