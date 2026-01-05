#include <stdio.h>
#include <dirglob/dirglob.h>

int main() {
    const char *patterns[] = {"dir/?.txt"};
    char **result = NULL;
    size_t count = 0;
    
    bool ok = dirglob(patterns, 1, 0, NULL, 1, &result, &count);
    
    printf("Pattern: dir/?.txt\n");
    printf("Success: %d, Count: %zu\n", ok, count);
    for (size_t i = 0; i < count; i++) {
        printf("  [%zu] %s\n", i, result[i]);
    }
    
    dirglob_free(result, count);
    return 0;
}
