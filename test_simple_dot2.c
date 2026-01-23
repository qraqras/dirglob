#include <stdio.h>
#include <string.h>
#include "rbc/rbc.h"

void test_pattern(const char *pattern, unsigned flags, const char *desc) {
    char **files = NULL;
    size_t count = 0;
    rbc_glob(&pattern, 1, flags, ".", true, &files, &count, NULL);
    
    int found_dot = 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(files[i], ".") == 0) {
            found_dot = 1;
            break;
        }
    }
    
    printf("%s: count=%zu, has_dot=%s\n", desc, count, found_dot ? "YES" : "NO");
    rbc_glob_free(files, count, NULL);
}

int main(void) {
    printf("MRI Simple Rule Test\n");
    printf("=====================\n\n");
    
    test_pattern("*", 0, "* (no flags)         ");
    test_pattern("*", RBC_FNM_DOTMATCH, "* (DOTMATCH)         ");
    test_pattern(".*", 0, ".* (no flags)        ");
    test_pattern(".*", RBC_FNM_DOTMATCH, ".* (DOTMATCH)        ");
    
    return 0;
}
