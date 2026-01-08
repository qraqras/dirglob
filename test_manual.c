#include <stdio.h>
#include <stdlib.h>
#include <rbcglob/rbcglob.h>
#include <unistd.h>
#include <sys/stat.h>

void print_results(const char *label, char **results, size_t count)
{
    printf("--- %s ---\n", label);
    if (count == 0)
    {
        printf("(No matches)\n");
    }
    else
    {
        for (size_t i = 0; i < count; i++)
        {
            printf("MATCH: %s\n", results[i]);
        }
    }
    rbcglob_free(results, count, NULL);
}

int main()
{
    // Setup
    mkdir("manual_test_dir", 0755);
    FILE *f = fopen("manual_test_dir/.hidden", "w");
    if (f)
        fclose(f);

    char **results = NULL;
    size_t count = 0;
    const char *patterns[] = {"manual_test_dir/*"};

    // DOTMATCH
    if (rbcglob_dirglob(patterns, 1, RBCGLOB_FNM_DOTMATCH, ".", true, &results, &count, NULL))
    {
        print_results("Test: manual_test_dir/* with DOTMATCH", results, count);
    }
    else
    {
        printf("Error running DOTMATCH glob\n");
    }

    // NO DOTMATCH
    if (rbcglob_dirglob(patterns, 1, 0, ".", true, &results, &count, NULL))
    {
        print_results("Test: manual_test_dir/* WITHOUT DOTMATCH", results, count);
    }
    else
    {
        printf("Error running NO DOTMATCH glob\n");
    }

    return 0;
}
