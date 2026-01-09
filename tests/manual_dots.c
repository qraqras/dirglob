#include <stdio.h>
#include <stdlib.h>
#include <rbcglob/rbcglob.h>

int main()
{
    // Compile pattern "test_dots/{a,b}/*"
    // Should NOT match .hidden inside a or b
    const char *pattern = "test_dots/{a,b}/*";
    rbcglob_compiled_glob_t *pat = rbcglob_compile_glob(pattern, 0);
    if (!pat)
    {
        fprintf(stderr, "Compile failed\n");
        return 1;
    }

    system("rm -rf test_dots");
    system("mkdir -p test_dots/a");
    system("mkdir -p test_dots/b");
    system("touch test_dots/a/fileA");
    system("touch test_dots/a/.hiddenA");
    system("touch test_dots/b/fileB");

    size_t count = 0;
    char **results = NULL;

    bool ok = rbcglob_dirglob_compiled(pat, NULL, false, &results, &count, NULL);

    printf("Pattern: %s\n", pattern);
    printf("Count: %zu\n", count);
    for (size_t i = 0; i < count; i++)
    {
        printf("Result %zu: %s\n", i, results[i]);
    }

    rbcglob_free(results, count, NULL);
    rbcglob_compiled_glob_free(pat);
    return 0;
}
