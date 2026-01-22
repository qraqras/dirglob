#include <stdio.h>
#include <string.h>
#include "rbc/rbc.h"

int main(void)
{
    const char *patterns[] = {
        "07_recursive/**/",
        "07_recursive/**/**",
        ".*",
        "./*",
        "/workspaces/dirglob/tests/fixtures/*",
        "**/.* ",
    };

    unsigned flags = RBC_FNM_DOTMATCH;

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
    {
        printf("\n=== Pattern: '%s' ===\n", patterns[i]);

        char **files = NULL;
        size_t count = 0;
        const char *pattern = patterns[i];
        bool ret = rbc_glob(&pattern, 1, flags, ".", true, &files, &count, NULL);

        if (ret)
        {
            printf("Count: %zu\n", count);
            printf("First 10 matches:\n");
            for (size_t j = 0; j < count && j < 10; j++)
            {
                printf("  [%zu] %s\n", j, files[j]);
            }
            rbc_glob_free(files, count, NULL);
        }
        else
        {
            printf("Error\n");
        }
    }

    return 0;
}
