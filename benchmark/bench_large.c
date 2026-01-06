#include <rbcglob/rbcglob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

double benchmark(const char *pattern, int flags, int iterations)
{
    clock_t start = clock();

    for (int i = 0; i < iterations; i++)
    {
        char **results = NULL;
        size_t *lengths = NULL;
        size_t count = 0;
        const char *patterns[] = {pattern};

        rbcglob_dirglob(patterns, 1, flags, NULL, 1, &results, &count, &lengths);
        rbcglob_free(results, count, lengths);
    }

    clock_t end = clock();
    return ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0 / iterations;
}

int main(void)
{
    const char *pattern = "tests/**/*";
    int iterations = 100;
    int flags = RBCGLOB_FNM_DOTMATCH;

    /* First run to get count */
    char **results = NULL;
    size_t *lengths = NULL;
    size_t count = 0;
    const char *p[] = {pattern};
    rbcglob_dirglob(p, 1, flags, NULL, 1, &results, &count, &lengths);

    printf("=== Large Pattern Benchmark ===\n\n");
    printf("Pattern: %s\n", pattern);
    printf("Matches: %zu\n", count);
    printf("Iterations: %d\n\n", iterations);

    rbcglob_free(results, count, lengths);

    double avg_time = benchmark(pattern, flags, iterations);
    printf("Average time: %.2fms\n", avg_time);

    return 0;
}
