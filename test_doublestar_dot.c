#include <stdio.h>
#include <stdlib.h>
#include "rbc/rbc.h"

int main()
{
    printf("Testing pattern: **/.* \n");
    printf("Flags: 0 (no flags)\n\n");

    const char *patterns[] = {"**/.*"};
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    bool ret = rbc_glob(patterns, 1, 0, NULL, true, &results, &count, &lengths);

    if (ret)
    {
        printf("Count: %zu\n", count);
        for (size_t i = 0; i < count && i < 20; i++)
        {
            printf("  %s\n", results[i]);
        }
        if (count > 20)
        {
            printf("  ... (%zu more)\n", count - 20);
        }
        rbc_glob_free(results, count, lengths);
    }
    else
    {
        printf("Error\n");
    }

    printf("\n\nExpected (Ruby): 41 matches\n");

    return 0;
}
