#include <stdio.h>
#include "rbc/rbc.h"

int main(void)
{
    const char *pattern = "**/.*";
    int flags = 0;
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    rbc_glob(&pattern, 1, flags, "tests/fixtures", true, &results, &count, &lengths);

    printf("Pattern: '%s', Flags: 0x%x\n", pattern, flags);
    printf("Count: %zu\n", count);
    
    int has_dot = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (i < 110) printf("[%zu] %s\n", i, results[i]);
        if (strcmp(results[i], ".") == 0) has_dot = 1;
    }
    printf("\nContains '.' entry: %s\n", has_dot ? "YES" : "NO");

    rbc_glob_free(results, count, lengths);
    return 0;
}
