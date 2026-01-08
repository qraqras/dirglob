#include <stdio.h>
#include <stdlib.h>
#include "rbcglob/rbcglob.h"

int main()
{
    const char *pattern = "*.txt";
    char **results = NULL;
    size_t count = 0;

    printf("Test: *.txt (sort=false)\n");
    bool success = rbcglob_dirglob(&pattern, 1, 0, NULL, false, &results, &count, NULL);

    if (success)
    {
        printf("Found %zu results:\n", count);
        for (size_t i = 0; i < count; i++)
        {
            printf("%s\n", results[i]);
        }
        rbcglob_free(results, count, NULL);
    }
    else
    {
        printf("Error\n");
    }

    return 0;
}
