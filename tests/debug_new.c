#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <rbcglob/rbcglob.h>

void make_dir(const char *path)
{
    mkdir(path, 0755);
}

void make_file(const char *path)
{
    FILE *fp = fopen(path, "w");
    if (fp)
        fclose(fp);
}

int main()
{
    system("rm -rf debug_fixture");
    make_dir("debug_fixture");
    chdir("debug_fixture");
    make_dir("a");
    make_dir("b");
    make_file("a/a");
    make_file("a/b");
    make_file("b/a");
    make_file("b/b");

    const char *patterns[] = {"{a,b}/{a,b}"};
    char **results = NULL;
    size_t *lengths = NULL;
    size_t count = 0;

    printf("Running dirglob for {a,b}/{a,b} with base=. and RBCGLOB_FNM_CASEFOLD\n");
    bool success = dirglob(patterns, 1, 8, ".", 1, &results, &count, &lengths);

    if (!success)
    {
        printf("dirglob failed\n");
        return 1;
    }

    printf("Found %zu matches:\n", count);
    for (size_t i = 0; i < count; i++)
    {
        printf("  %s\n", results[i]);
    }

    rbcglob_free(results, count, lengths);

    chdir("..");
    return 0;
}
