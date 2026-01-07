#include <stdio.h>
#include <rbcglob/rbcglob.h>

int main() {
    char **result = NULL;
    size_t count = 0;
    size_t *lengths = NULL;
    
    const char *patterns[] = {"{x,y,z}/.*"};
    bool ok = rbcglob_dirglob(patterns, 1, 0, NULL, 1, &result, &count, &lengths);
    
    printf("Success: %d\n", ok);
    printf("Count: %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("[%zu] %s\n", i, result[i]);
    }
    
    rbcglob_free(result, count, lengths);
    return 0;
}
