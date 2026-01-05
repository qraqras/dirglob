#include <stdio.h>
#include <stdlib.h>
#include <dirglob/dirglob.h>
#include <dirglob/internal/fnmatch.h>

int main(int argc, char **argv) {
    const char *pattern = argc > 1 ? argv[1] : "*/*";
    unsigned flags = (argc > 2) ? atoi(argv[2]) : 0;
    
    char **result = NULL;
    size_t count = 0;
    
    bool ok = dirglob((const char*[]){pattern}, 1, flags, NULL, 1, &result, &count);
    
    printf("Pattern: %s (flags: %u)\n", pattern, flags);
    printf("Success: %d, Count: %zu\n", ok, count);
    for (size_t i = 0; i < count && i < 50; i++) {
        printf("%s\n", result[i]);
    }
    if (count > 50) printf("... (%zu more)\n", count - 50);
    
    dirglob_free(result, count);
    return 0;
}
