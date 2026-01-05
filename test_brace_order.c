#include <stdio.h>
#include <stdlib.h>
#include "dirglob/dirglob.h"

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <pattern> <flags>\n", argv[0]);
        return 1;
    }

    const char *pattern = argv[1];
    int flags = atoi(argv[2]);

    char **results = NULL;
    size_t count = 0;

    int ret = dirglob(pattern, flags, NULL, &results, &count);

    if (ret != 0)
    {
        fprintf(stderr, "dirglob failed: %d\n", ret);
        return 1;
    }

    printf("Count: %zu\n", count);
    for (size_t i = 0; i < count; i++)
    {
        printf("%s\n", results[i]);
    }

    dirglob_free(results, count);
    return 0;
}
