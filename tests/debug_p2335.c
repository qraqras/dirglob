#include <rbcglob/rbcglob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void setup_env()
{
    mkdir("a", 0755);
    mkdir("b", 0755);
    FILE *f;
    f = fopen("a/file.txt", "w");
    fclose(f);
    f = fopen("b/file.txt", "w");
    fclose(f);
}

void cleanup_env()
{
    unlink("a/file.txt");
    rmdir("a");
    unlink("b/file.txt");
    rmdir("b");
}

int main()
{
    setup_env();

    char **result = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    printf("Calling rbcglob_dirglob with base=NULL\n");
    const char *patterns[] = {"{a,b}/file.*"};
    bool ok = rbcglob_dirglob(patterns, 1, RBCGLOB_FNM_EXTGLOB, NULL, 1, &result, &count, &lengths);

    if (!ok)
    {
        printf("rbcglob_dirglob failed\n");
        return 1;
    }

    printf("Count: %zu\n", count);
    for (size_t i = 0; i < count; i++)
    {
        printf("Result[%zu]: '%s'\n", i, result[i]);
    }

    rbcglob_free(result, count, lengths);
    cleanup_env();
    return 0;
}
