#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <rbcglob/rbcglob.h>

void print_matches(const char *desc, const char *pattern, unsigned flags)
{
    printf("%s\n", desc);
    const char *patterns[] = {pattern};
    char **result = NULL;
    size_t count = 0;

    // NULL base
    if (rbcglob_dirglob(patterns, 1, flags, NULL, true, &result, &count, NULL))
    {
        printf("Count: %zu\n", count);
        for (size_t i = 0; i < count; i++)
        {
            printf("  [%zu] %s\n", i, result[i]);
        }
        rbcglob_free(result, count, NULL);
    }
    else
    {
        printf("FAILED\n");
    }
}

int main()
{
    print_matches("--- Test: [a-z]/. ---", "[a-z]/.", 0);
    print_matches("--- Test: [a-z]/.* ---", "[a-z]/.*", 0);
    print_matches("--- Test: [a-z]/* ---", "[a-z]/*", 0);
    print_matches("--- Test: * ---", "*", 0);
    print_matches("--- Test: * (DOTMATCH) ---", "*", RBCGLOB_FNM_DOTMATCH);
    print_matches("--- Test: a/.. ---", "a/..", 0);
    print_matches("--- Test: .. ---", "..", 0);

    return 0;
}
