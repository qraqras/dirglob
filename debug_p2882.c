#include <stdio.h>
#include <stdlib.h>
#include "dirglob/dirglob.h"

int main()
{
    const char *patterns[] = {"**/{a,b}.txt"};
    char **out = NULL;
    size_t count = 0;
    if (dirglob(patterns, 1, 0, NULL, 0, &out, &count))
    {
        for (size_t i = 0; i < count; i++)
        {
            printf("%zu: %s\n", i, out[i]);
        }
        dirglob_free(out, count);
    }
    else
    {
        printf("Error or no results\n");
    }
    return 0;
}
