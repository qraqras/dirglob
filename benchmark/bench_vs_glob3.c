#include <rbcglob/rbcglob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>

static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

void bench_rbcglob(const char *pattern, int iterations)
{
    double start = get_time_ms();
    size_t total_matches = 0;

    for (int i = 0; i < iterations; i++)
    {
        char **results = NULL;
        size_t *lengths = NULL;
        size_t count = 0;
        const char *patterns[] = {pattern};

        rbcglob_dirglob(patterns, 1, 0, NULL, 1, &results, &count, &lengths);
        total_matches = count;
        rbcglob_free(results, count, lengths);
    }

    double end = get_time_ms();
    printf("  rbcglob: %8.3f ms / iter (matches: %zu)\n", (end - start) / iterations, total_matches);
}

void bench_glob3(const char *pattern, int iterations)
{
    double start = get_time_ms();
    size_t total_matches = 0;

    for (int i = 0; i < iterations; i++)
    {
        glob_t g;
        if (glob(pattern, 0, NULL, &g) == 0)
        {
            total_matches = g.gl_pathc;
            globfree(&g);
        }
    }

    double end = get_time_ms();
    printf("  glob(3): %8.3f ms / iter (matches: %zu)\n", (end - start) / iterations, total_matches);
}

int main(void)
{
    const char *patterns[] = {
        "src/rbcglob/*.c",
        "tests/test_*.c",
        "include/rbcglob/rbcglob.h",
        "**/*.c",
    };
    int num_patterns = 4;
    int iterations = 1000;

    printf("=== Performance Benchmark: rbcglob vs libc glob(3) ===\n");
    printf("Iterations per pattern: %d\n\n", iterations);

    for (int i = 0; i < num_patterns; i++)
    {
        printf("Pattern: [%s]\n", patterns[i]);
        bench_rbcglob(patterns[i], iterations);
        bench_glob3(patterns[i], iterations);
        printf("\n");
    }

    return 0;
}
