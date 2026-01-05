#include <stdio.h>
#include <dirglob/dirglob.h>

int main()
{
    const char *patterns[] = {"file.{txt,c}"};
    char **result = NULL;
    size_t count = 0;

    printf("Testing: file.{txt,c} with base='.'\n");
    bool ok = dirglob(patterns, 1, 0, ".", 1, &result, &count);

    printf("Success: %d\n", ok);
    printf("Count: %zu\n", count);
    for (size_t i = 0; i < count; i++)
    {
        printf("  [%zu] %s\n", i, result[i]);
    }

    dirglob_free(result, count);
    return 0;
}
