#include <stdio.h>
#include <stdlib.h>
#include "include/dirglob/dirglob.h"

int main()
{
    rbcglob_pattern_t *pat = rbcglob_pattern_compile("file.txt", 0, NULL);
    size_t count = 0;
    char **results = rbcglob_dirglob(pat, ".", 0, &count);
    printf("Count: %zu\n", count);
    if (count > 0)
        printf("Result 0: %s\n", results[0]);
    return 0;
}
