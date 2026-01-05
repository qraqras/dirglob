#include <stdio.h>
#include <dirglob/dirglob.h>
#include <dirglob/internal/fnmatch.h>

int main()
{
    const char *patterns[] = {"*/*"};
    char **result = NULL;
    size_t count = 0;

    bool ok = dirglob(patterns, 1, FNM_DOTMATCH, NULL, 1, &result, &count);

    printf("Pattern: */* with FNM_DOTMATCH\n");
    printf("Success: %d, Count: %zu\n", ok, count);
    for (size_t i = 0; i < count && i < 10; i++)
    {
        printf("  [%zu] %s\n", i, result[i]);
    }
    if (count > 10)
        printf("  ... (%zu more)\n", count - 10);

    dirglob_free(result, count);
    return 0;
}
