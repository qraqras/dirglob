#include <stdio.h>
#include "rbc/rbc.h"

int main(void)
{
    const char *pattern = "**/.*";
    int flags = RBC_FNM_PATHNAME | RBC_FNM_DOTMATCH;
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    rbc_glob(&pattern, 1, flags, NULL, true, &results, &count, &lengths);

    printf("Count: %zu\n", count);
    for (size_t i = 0; i < count && i < 50; i++)
    {
        printf("[%zu] %s\n", i, results[i]);
    }

    rbc_glob_free(results, count, lengths);
    return 0;
}
