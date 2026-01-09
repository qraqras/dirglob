#include <stdio.h>
#include <rbcglob/rbcglob.h>

int main()
{
    const char *pattern = "bench_data/file[1-3].txt";
    char **out;
    size_t count;
    rbcglob_dirglob(&pattern, 1, 0, NULL, true, &out, &count, NULL);
    printf("Matches: %zu\n", count);
    for (size_t i = 0; i < count; i++)
        printf("%s\n", out[i]);
    rbcglob_free(out, count, NULL);
    return 0;
}
