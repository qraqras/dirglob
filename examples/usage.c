#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rbcglob/rbcglob.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <pattern> [-d] [-n]\n", argv[0]);
        fprintf(stderr, "  -d: Enable FNM_DOTMATCH (match hidden files)\n");
        fprintf(stderr, "  -n: Disable sorting\n");
        return 1;
    }

    unsigned flags = 0;
    bool sort = true;
    const char *pattern = argv[1];

    /* Parse flags */
    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0)
        {
            flags |= RBCGLOB_FNM_DOTMATCH;
        }
        else if (strcmp(argv[i], "-n") == 0)
        {
            sort = false;
        }
    }

    char **result = NULL;
    size_t *lengths = NULL;
    size_t count = 0;
    const char *patterns[] = {pattern};

    if (rbcglob_dirglob(patterns, 1, flags, NULL, sort ? 1 : 0, &result, &count, &lengths))
    {
        for (size_t i = 0; i < count; i++)
        {
            printf("%s\n", result[i]);
        }
        rbcglob_free(result, count, lengths);
    }
    else
    {
        fprintf(stderr, "rbcglob_dirglob failed\n");
        return 1;
    }

    return 0;
}
