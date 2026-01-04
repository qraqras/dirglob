#include <stdio.h>
#include <stdlib.h>
#include "dirglob/dirglob.h"

int main(void)
{
    char **result = NULL;
    size_t count = 0;

    printf("Test 1: *.txt\n");
    dirglob((const char*[]){"*.txt"}, 1, 0, NULL, 1, &result, &count);
    printf("  Count: %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  [%zu] %s\n", i, result[i]);
        free(result[i]);
    }
    free(result);
    result = NULL;
    count = 0;

    printf("\nTest 2: a/a.txt\n");
    dirglob((const char*[]){"a/a.txt"}, 1, 0, NULL, 1, &result, &count);
    printf("  Count: %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  [%zu] %s\n", i, result[i]);
        free(result[i]);
    }
    free(result);
    result = NULL;
    count = 0;

    printf("\nTest 3: a/*.txt\n");
    dirglob((const char*[]){"a/*.txt"}, 1, 0, NULL, 1, &result, &count);
    printf("  Count: %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  [%zu] %s\n", i, result[i]);
        free(result[i]);
    }
    free(result);

    return 0;
}
