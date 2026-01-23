#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "rbc/rbc.h"

int main(void) {
    const char *patterns[] = {"02_asterisk/*"};
    char **results = NULL;
    size_t count = 0;
    
    printf("Test 1: Without DOTMATCH\n");
    bool success = rbc_glob(patterns, 1, 0, "tests/fixtures", true, &results, &count, NULL);
    if (success) {
        printf("Count: %zu\n", count);
        for (size_t i = 0; i < count; i++) {
            printf("  %s\n", results[i]);
        }
        rbc_glob_free(results, count, NULL);
    }
    
    printf("\nTest 2: With DOTMATCH\n");
    success = rbc_glob(patterns, 1, RBC_FNM_DOTMATCH, "tests/fixtures", true, &results, &count, NULL);
    if (success) {
        printf("Count: %zu\n", count);
        bool found_dot = false;
        for (size_t i = 0; i < count; i++) {
            printf("  %s\n", results[i]);
            if (strcmp(results[i], "02_asterisk/.") == 0) {
                found_dot = true;
            }
        }
        printf("Has '02_asterisk/.': %s\n", found_dot ? "YES" : "NO");
        rbc_glob_free(results, count, NULL);
    }
    
    return 0;
}
