#include <stdio.h>
#include <stdlib.h>
#include <rbc/rbc.h>

int main()
{
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    const char *patterns[] = {"**/.*"};
    bool ret = rbc_glob(patterns, 1, RBC_FNM_DOTMATCH, NULL, true, &results, &count, &lengths);

    if (!ret)
    {
        printf("rbc_glob failed\n");
        return 1;
    }

    printf("Count: %zu\n", count);
    for (size_t i = 0; i < count; i++)
    {
        printf("%zu: %s\n", i, results[i]);
    }

    rbc_glob_free(results, count, lengths);
    return 0;
}
