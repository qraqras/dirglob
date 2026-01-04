#include <stdio.h>
#include <dirglob/dirglob.h>

int main()
{
    char **result = NULL;
    size_t count = 0;

    // Test: dir/file.txt pattern
    printf("Test: dir/file.txt pattern\n");
    bool ok = dirglob((const char *[]){"dir/file.txt"}, 1, 0, NULL, 1, &result, &count);
    printf("  Success: %d, Count: %zu\n", ok, count);
    for (size_t i = 0; i < count; i++)
    {
        printf("  [%zu] %s\n", i, result[i]);
    }
    dirglob_free(result, count);

    return 0;
}
