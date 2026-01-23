#include <stdio.h>
#include "rbc/rbc.h"

int main(void)
{
    printf("Test 1: * without FNM_DOTMATCH\n");
    {
        const char *pattern = "*";
        char **files = NULL;
        size_t count = 0;
        rbc_glob(&pattern, 1, 0, ".", true, &files, &count, NULL);
        printf("Count: %zu\n", count);
        int found_dot = 0;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(files[i], ".") == 0) {
                found_dot = 1;
                printf("ERROR: Found \".\" at index %zu\n", i);
            }
        }
        if (!found_dot) printf("OK: \".\" not found\n");
        rbc_glob_free(files, count, NULL);
    }
    
    printf("\nTest 2: * with FNM_DOTMATCH\n");
    {
        const char *pattern = "*";
        char **files = NULL;
        size_t count = 0;
        rbc_glob(&pattern, 1, RBC_FNM_DOTMATCH, ".", true, &files, &count, NULL);
        printf("Count: %zu\n", count);
        int found_dot = 0;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(files[i], ".") == 0) {
                found_dot = 1;
                printf("OK: Found \".\" at index %zu\n", i);
                break;
            }
        }
        if (!found_dot) printf("ERROR: \".\" not found\n");
        rbc_glob_free(files, count, NULL);
    }
    
    return 0;
}
