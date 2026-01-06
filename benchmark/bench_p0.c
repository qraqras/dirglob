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
        size_t count = 0;
        const char *patterns[] = {pattern};

        dirglob(patterns, 1, flags, NULL, 1, &results, &count);
        rbcglob_free(results, count);
    }

    clock_t end = clock();
    return ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0 / iterations;
}

int main(void)
{
    const char *patterns[] = {
        "*.md",
        "tests/*.c",
        "tests/**/*.c",
        "tests/**/*",
        "src/rbcglob/*.c",
    };
    const char *descriptions[] = {
        "Literal suffix (*.md)",
        "Literal prefix + suffix (tests/*.c)",
        "Recursive with suffix (tests/**/*.c)",
        "Recursive all (tests/**/*)",
        "Deep literal path (src/rbcglob/*.c)",
    };
    int num_patterns = 5;
    int iterations = 100;
    int flags = RBCGLOB_FNM_DOTMATCH;

    printf("=== P0 Optimization Benchmark ===\n\n");
    printf("Iterations: %d\n\n", iterations);

    for (int i = 0; i < num_patterns; i++)
    {
        const char *pattern = patterns[i];
        const char *description = descriptions[i];

        char **results = NULL;
        size_t count = 0;
        const char *p[] = {pattern};
        dirglob(p, 1, flags, NULL, 1, &results, &count);

        printf("Pattern: %s - %s\n", pattern, description);
        printf("  Matches: %zu\n", count);

        double avg_time = benchmark(pattern, flags, iterations);
        printf("  Average time: %.2fms\n\n", avg_time);

        rbcglob_free(results, count);
    }

    return 0;
}
