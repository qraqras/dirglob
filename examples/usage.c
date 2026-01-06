#include <stdio.h>
#include <stdlib.h>
#include "rbcglob/rbcglob.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <pattern>\n", argv[0]);
        return 1;
    }

    char **result = NULL;
    size_t *lengths = NULL;
    size_t count = 0;
    const char *patterns[] = {argv[1]};

    if (dirglob(patterns, 1, 0, NULL, 1, &result, &count, &lengths))
    {
        for (size_t i = 0; i < count; i++)
        {
            printf("%s (len: %zu)\n", result[i], lengths[i]);
        }
        rbcglob_free(result, count, lengths);
    }
    else
    {
        fprintf(stderr, "dirglob failed\n");
        return 1;
    }

    return 0;
}
