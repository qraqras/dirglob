#include <stdio.h>
#include <stdlib.h>
#include "rbc/rbc.h"

int main()
{
    printf("=== Direct test of .* ===\n");
    const char *p1[] = {".*"};
    char **r1 = NULL;
    size_t c1 = 0;
    rbc_glob(p1, 1, 0, NULL, true, &r1, &c1, NULL);
    printf(".* Count: %zu\n", c1);
    for (size_t i = 0; i < c1 && i < 5; i++)
    {
        printf("  %s\n", r1[i]);
    }
    rbc_glob_free(r1, c1, NULL);

    printf("\n=== Test with base path ===\n");
    const char *p2[] = {".*"};
    char **r2 = NULL;
    size_t c2 = 0;
    rbc_glob(p2, 1, 0, ".", true, &r2, &c2, NULL);
    printf(".* with base='.' Count: %zu\n", c2);
    for (size_t i = 0; i < c2 && i < 5; i++)
    {
        printf("  %s\n", r2[i]);
    }
    rbc_glob_free(r2, c2, NULL);

    printf("\n=== Test with empty base ===\n");
    const char *p3[] = {".*"};
    char **r3 = NULL;
    size_t c3 = 0;
    rbc_glob(p3, 1, 0, "", true, &r3, &c3, NULL);
    printf(".* with base='' Count: %zu\n", c3);
    for (size_t i = 0; i < c3 && i < 5; i++)
    {
        printf("  %s\n", r3[i]);
    }
    rbc_glob_free(r3, c3, NULL);

    return 0;
}
