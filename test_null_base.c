#include <stdio.h>
#include <unistd.h>
#include <rbcglob/rbcglob.h>

int main() {
    // Change to fixtures directory
    chdir("tests/fixtures");
    
    printf("Pattern: file.txt, Base: NULL (cwd)\n");
    
    const char *patterns[] = {"file.txt"};
    char **results = NULL;
    size_t count = 0;
    
    bool success = rbcglob_dirglob(patterns, 1, 0, NULL, true, &results, &count, NULL);
    
    if (!success) {
        printf("Error running glob\n");
        return 1;
    }
    
    printf("Results: %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  [%zu] %s\n", i, results[i]);
    }
    
    rbcglob_free(results, count, NULL);
    return 0;
}
