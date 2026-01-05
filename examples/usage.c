#include <stdio.h>
#include <stdlib.h>
#include "dirglob/dirglob.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <pattern>\n", argv[0]);
        return 1;
    }

    char **result = NULL;
    size_t count = 0;
    const char *patterns[] = {argv[1]};

    if (dirglob(patterns, 1, 0, NULL, 1, &result, &count))
    {
        for (size_t i = 0; i < count; i++)
        {
            printf("%s\n", result[i]);
        }
        dirglob_free(result, count);
    }
    else
    {
        fprintf(stderr, "dirglob failed\n");
        return 1;
    }

    return 0;
}
