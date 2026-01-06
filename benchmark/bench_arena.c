#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <rbcglob/rbcglob.h>

int main(void)
{
    const char *pattern = "tests/**/*";
    int iterations = 100;
    struct timespec start, end;

    printf("=== P13 Arena Allocator Benchmark ===\n\n");
    printf("Pattern: %s\n", pattern);
    printf("Iterations: %d\n\n", iterations);

    // Warmup
    char **results;
    size_t *lengths;
    size_t count;
    const char *p[] = {pattern};
    for (int i = 0; i < 10; i++)
    {
        rbcglob_dirglob(p, 1, 0, NULL, 1, &results, &count, &lengths);
        rbcglob_free(results, count, lengths);
    }

    // Actual benchmark
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++)
    {
        rbcglob_dirglob(p, 1, 0, NULL, 1, &results, &count, &lengths);
        rbcglob_free(results, count, lengths);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) * 1000.0 +
                     (end.tv_nsec - start.tv_nsec) / 1000000.0;
    double avg_time = elapsed / iterations;

    printf("Total matches: %zu\n", count);
    printf("Total time: %.2fms\n", elapsed);
    printf("Average time: %.2fms\n", avg_time);

    return 0;
}
