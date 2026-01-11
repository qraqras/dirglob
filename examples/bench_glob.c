#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>
#include "rbc/rbc.h"

double get_time_sec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void bench_libc_glob(const char *pattern, int iterations)
{
    glob_t globbuf;
    double start = get_time_sec();
    size_t total_matches = 0;

    for (int i = 0; i < iterations; i++)
    {
        globbuf.gl_offs = 0;
        int ret = glob(pattern, GLOB_BRACE, NULL, &globbuf);
        if (ret == 0)
        {
            total_matches += globbuf.gl_pathc;
            globfree(&globbuf);
        }
    }
    double end = get_time_sec();
    printf("glob(3)   : %.6f sec, matches: %zu\n", (end - start), total_matches / (iterations > 0 ? iterations : 1));
}

void bench_rbcglob(const char *pattern, int iterations)
{
    double start = get_time_sec();
    size_t total_matches = 0;

    for (int i = 0; i < iterations; i++)
    {
        const char *patterns[] = {pattern};
        char **out = NULL;
        size_t count = 0;

        bool success = rbc_glob(patterns, 1, 0, NULL, true, &out, &count, NULL);
        if (success)
        {
            total_matches += count;
            rbc_glob_free(out, count, NULL);
        }
    }
    double end = get_time_sec();
    printf("rbcglob   : %.6f sec, matches: %zu\n", (end - start), total_matches / (iterations > 0 ? iterations : 1));
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <pattern> [iterations]\n", argv[0]);
        return 1;
    }

    const char *pattern = argv[1];
    int iterations = 100;
    if (argc >= 3)
    {
        iterations = atoi(argv[2]);
    }

    printf("Benchmarking pattern: '%s' over %d iterations\n", pattern, iterations);

    bench_libc_glob(pattern, iterations);
    bench_rbcglob(pattern, iterations);

    return 0;
}
