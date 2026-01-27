#include <stdio.h>
#include <stdlib.h>
#include "rbc/rbc.h"

int main()
{
    printf("Testing test_t2554 pattern\n");
    printf("Pattern: 07_recursive/**\n");
    printf("Flags: PATHNAME|CASEFOLD\n");
    printf("Base: .\n\n");

    const char *patterns[] = {"07_recursive/**"};
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    int flags = RBC_FNM_PATHNAME | RBC_FNM_CASEFOLD;

    bool ret = rbc_glob(patterns, 1, flags, ".", false, &results, &count, &lengths);

    if (ret)
    {
        printf("Count: %zu\n", count);
        for (size_t i = 0; i < count && i < 20; i++)
        {
            printf("[%zu] %s\n", i, results[i]);
        }
        if (count > 20)
        {
            printf("... (%zu more)\n", count - 20);
        }
        rbc_glob_free(results, count, lengths);
        printf("\nSuccess!\n");
    }
    else
    {
        printf("Error\n");
    }

    return 0;
}
