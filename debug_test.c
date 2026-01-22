#include <stdio.h>
#include <stdbool.h>

bool rbc_glob(const char **patterns, size_t npatterns, unsigned flags,
              const char *base, bool sort, char ***out, size_t *count,
              size_t **lengths);
void rbc_glob_free(char **list, size_t count, size_t *lengths);

int main()
{
    const char *pattern = "01_basic/file";
    const char *patterns[] = {pattern};
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    printf("Testing pattern: %s\n", pattern);
    printf("Base: tests/fixtures\n");

    bool ret = rbc_glob(patterns, 1, 0, "tests/fixtures", true, &results, &count, &lengths);

    printf("Result: %s\n", ret ? "success" : "failure");
    printf("Count: %zu\n", count);

    for (size_t i = 0; i < count; i++)
    {
        printf("  [%zu] %s\n", i, results[i]);
    }

    rbc_glob_free(results, count, lengths);
    return 0;
}
