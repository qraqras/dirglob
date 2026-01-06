#include <stdio.h>
#include <unistd.h>
#include <rbcglob/rbcglob.h>

int main()
{
    char **result = NULL;
    size_t count = 0;

    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    printf("Current directory: %s\n", cwd);

    // Simple test: file.txt with no base
    printf("Before rbcglob_dirglob call\n");
    fflush(stdout);
    size_t *lengths = NULL;
    bool ok = rbcglob_dirglob((const char *[]){"file.txt"}, 1, 0, NULL, 1, &result, &count, &lengths);
    printf("After rbcglob_dirglob call\n");
    fflush(stdout);

    printf("Return value: %d\n", ok);
    printf("Count: %zu\n", count);

    for (size_t i = 0; i < count; i++)
    {
        printf("Result[%zu]: %s (len: %zu)\n", i, result[i], lengths[i]);
    }

    rbcglob_free(result, count, lengths);

    return 0;
}
