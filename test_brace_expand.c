#include <stdio.h>
#include <dirglob/internal/utils.h>

int main() {
    char **expanded = NULL;
    size_t count = 0;
    
    expand_braces("file.{txt,c}", &expanded, &count);
    
    printf("Brace expansion result for 'file.{txt,c}':\n");
    printf("Count: %zu\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  [%zu] %s\n", i, expanded[i]);
    }
    
    for (size_t i = 0; i < count; i++) {
        free(expanded[i]);
    }
    free(expanded);
    
    return 0;
}
