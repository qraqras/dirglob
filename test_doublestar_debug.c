#include <stdio.h>
#include <stdlib.h>
#include "rbc/rbc.h"

int main()
{
    printf("=== Test 1: .* (should match dotfiles in current dir) ===\n");
    const char *p1[] = {".*"};
    char **r1 = NULL;
    size_t c1 = 0;
    rbc_glob(p1, 1, 0, NULL, true, &r1, &c1, NULL);
    printf("Count: %zu\n", c1);
    for (size_t i = 0; i < c1 && i < 5; i++)
    {
        printf("  %s\n", r1[i]);
    }
    rbc_glob_free(r1, c1, NULL);

    printf("\n=== Test 2: ** (should match current dir items) ===\n");
    const char *p2[] = {"**"};
    char **r2 = NULL;
    size_t c2 = 0;
    rbc_glob(p2, 1, 0, NULL, true, &r2, &c2, NULL);
    printf("Count: %zu\n", c2);
    for (size_t i = 0; i < c2 && i < 5; i++)
    {
        printf("  %s\n", r2[i]);
    }
    rbc_glob_free(r2, c2, NULL);

    printf("\n=== Test 3: **/.*  (should match dotfiles recursively) ===\n");
    const char *p3[] = {"**/.*"};
    char **r3 = NULL;
    size_t c3 = 0;
    rbc_glob(p3, 1, 0, NULL, true, &r3, &c3, NULL);
    printf("Count: %zu\n", c3);
    for (size_t i = 0; i < c3; i++)
    {
        printf("  %s\n", r3[i]);
    }
    rbc_glob_free(r3, c3, NULL);

    return 0;
}
