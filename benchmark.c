#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>
#include <rbcglob/rbcglob.h>

double get_time_sec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void bench_libc_glob(const char *pattern, int iterations)
{
    glob_t pglob;
    double start = get_time_sec();
    size_t total_found = 0;

    for (int i = 0; i < iterations; i++)
    {
        int ret = glob(pattern, 0, NULL, &pglob);
        if (ret == 0)
        {
            total_found = pglob.gl_pathc; // Store last count or sum? Sum might be huge, just track for verification
            globfree(&pglob);
        }
    }
    double end = get_time_sec();
    printf("glob(3):   %.6f sec (found %zu)\n", end - start, total_found);
}

void bench_rbcglob(const char *pattern, int iterations)
{
    char **out_paths = NULL;
    size_t count = 0;
    const char *patterns[] = {pattern};

    double start = get_time_sec();
    size_t total_found = 0;

    for (int i = 0; i < iterations; i++)
    {
        if (rbcglob_dirglob(patterns, 1, 0, NULL, true, &out_paths, &count, NULL))
        {
            total_found = count; // verification
            rbcglob_free(out_paths, count, NULL);
        }
    }
    double end = get_time_sec();
    printf("rbcglob (oneshot): %.6f sec (found %zu)\n", end - start, total_found);
}

void bench_rbcglob_compiled(const char *pattern, int iterations)
{
    char **out_paths = NULL;
    size_t count = 0;
    rbcglob_compiled_glob_t *cg = rbcglob_compile_glob(pattern, 0);
    if (!cg)
    {
        fprintf(stderr, "Failed to compile pattern\n");
        return;
    }

    double start = get_time_sec();
    size_t total_found = 0;

    for (int i = 0; i < iterations; i++)
    {
        if (rbcglob_dirglob_compiled(cg, NULL, true, &out_paths, &count, NULL))
        {
            total_found = count; // verification
            rbcglob_free(out_paths, count, NULL);
        }
    }
    double end = get_time_sec();
    printf("rbcglob (compiled): %.6f sec (found %zu)\n", end - start, total_found);
    rbcglob_compiled_glob_free(cg);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <pattern> [iterations]\n", argv[0]);
        return 1;
    }
    const char *pattern = argv[1];
    int iterations = (argc > 2) ? atoi(argv[2]) : 1000;

    printf("Benchmarking pattern: '%s' (%d iterations)\n", pattern, iterations);
    bench_libc_glob(pattern, iterations);
    bench_rbcglob(pattern, iterations);
    // bench_rbcglob_compiled(pattern, iterations);

    return 0;
}
