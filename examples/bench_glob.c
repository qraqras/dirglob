#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/stat.h>
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
    double elapsed = end - start;
    double avg_per_iter = elapsed / iterations;
    printf("glob(3)      : %.6f sec total, %.6f sec/iter, matches: %zu, %.2f iter/sec\n",
           elapsed, avg_per_iter, total_matches / iterations, 1.0 / avg_per_iter);
}

void bench_rbcglob(const char *pattern, int iterations)
{
    double start = get_time_sec();
    size_t total_matches = 0;

    for (int i = 0; i < iterations; i++)
    {
        const char *patterns[] = {pattern};
        rbc_glob_result_t result = {0};

        rbc_glob_status_t success = rbc_glob(patterns, 1, 0, NULL, true, &result, NULL, NULL);
        if (success == RBC_GLOB_SUCCESS)
        {
            total_matches += result.count;
            rbc_globfree(&result);
        }
    }
    double end = get_time_sec();
    double elapsed = end - start;
    double avg_per_iter = elapsed / iterations;
    printf("rbc_glob     : %.6f sec total, %.6f sec/iter, matches: %zu, %.2f iter/sec\n",
           elapsed, avg_per_iter, total_matches / iterations, 1.0 / avg_per_iter);
}

// Helper to recursively collect all files for fnmatch testing
static size_t collect_files_recursive(const char *dir, char ***files, size_t *capacity, size_t count)
{
    DIR *d = opendir(dir);
    if (!d)
        return count;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char path[4096];
        if (strcmp(dir, ".") == 0)
        {
            snprintf(path, sizeof(path), "%s", entry->d_name);
        }
        else
        {
            snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        }

        // Add to list
        if (count >= *capacity)
        {
            *capacity = (*capacity == 0) ? 1024 : (*capacity * 2);
            *files = realloc(*files, *capacity * sizeof(char *));
        }
        (*files)[count++] = strdup(path);

        // Recurse if directory
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        {
            count = collect_files_recursive(path, files, capacity, count);
        }
    }
    closedir(d);
    return count;
}

void bench_libc_fnmatch(const char *pattern, int iterations)
{
    // Collect all files first
    char **files = NULL;
    size_t capacity = 0;
    size_t file_count = collect_files_recursive(".", &files, &capacity, 0);

    if (file_count == 0)
    {
        printf("fnmatch(3)   : No files to test\n");
        return;
    }

    double start = get_time_sec();
    size_t total_matches = 0;

    for (int i = 0; i < iterations; i++)
    {
        size_t matches = 0;
        for (size_t j = 0; j < file_count; j++)
        {
            if (fnmatch(pattern, files[j], 0) == 0)
            {
                matches++;
            }
        }
        total_matches += matches;
    }
    double end = get_time_sec();
    double elapsed = end - start;
    double avg_per_iter = elapsed / iterations;
    printf("fnmatch(3)   : %.6f sec total, %.6f sec/iter, matches: %zu, %.2f iter/sec (tested %zu files)\n",
           elapsed, avg_per_iter, total_matches / iterations, 1.0 / avg_per_iter, file_count);

    // Cleanup
    for (size_t i = 0; i < file_count; i++)
        free(files[i]);
    free(files);
}

void bench_rbc_fnmatch(const char *pattern, int iterations)
{
    // Collect all files first
    char **files = NULL;
    size_t capacity = 0;
    size_t file_count = collect_files_recursive(".", &files, &capacity, 0);

    if (file_count == 0)
    {
        printf("rbc_fnmatch  : No files to test\n");
        return;
    }

    double start = get_time_sec();
    size_t total_matches = 0;

    // Fair comparison: compile pattern each time, just like fnmatch(3)
    for (int i = 0; i < iterations; i++)
    {
        size_t matches = 0;
        for (size_t j = 0; j < file_count; j++)
        {
            // Use rbc_fnmatch (compiles each time) instead of rbc_xfnmatch (precompiled)
            if (rbc_fnmatch(pattern, files[j], 0))
            {
                matches++;
            }
        }
        total_matches += matches;
    }
    double end = get_time_sec();
    double elapsed = end - start;
    double avg_per_iter = elapsed / iterations;
    printf("rbc_fnmatch  : %.6f sec total, %.6f sec/iter, matches: %zu, %.2f iter/sec (tested %zu files)\n",
           elapsed, avg_per_iter, total_matches / iterations, 1.0 / avg_per_iter, file_count);

    // Cleanup
    for (size_t i = 0; i < file_count; i++)
        free(files[i]);
    free(files);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <pattern> [iterations] [--glob-only|--fnmatch-only]\n", argv[0]);
        fprintf(stderr, "       %s --suite [iterations]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--suite") == 0)
    {
        int iterations = 50000;
        if (argc >= 3)
            iterations = atoi(argv[2]);

        const char *patterns[] = {
            "*.c",
            "*.h",
            "a*c",
            "*match*",
            "src/*.c",
            "????.c",
            "*.{c,h}", // If brace expansion is supported by fnmatch? Usually not by standard fnmatch but maybe rbc_fnmatch... actually fnmatch doesn't support braces usually.
            // Let's stick to standard fnmatch patterns.
            "test_*.c",
            "*_*",
            "r*c*b*match*"};
        size_t num_patterns = sizeof(patterns) / sizeof(patterns[0]);

        for (size_t i = 0; i < num_patterns; i++)
        {
            printf("\n>>> Running Suite Pattern: %s <<<\n", patterns[i]);

            printf("--- Glob Benchmarks ---\n");
            bench_libc_glob(patterns[i], iterations);
            bench_rbcglob(patterns[i], iterations);

            printf("--- Fnmatch Benchmarks ---\n");
            bench_libc_fnmatch(patterns[i], iterations);
            bench_rbc_fnmatch(patterns[i], iterations);
        }
        return 0;
    }

    const char *pattern = argv[1];
    int iterations = 100;
    bool glob_only = false;
    bool fnmatch_only = false;

    if (argc >= 3)
    {
        iterations = atoi(argv[2]);
    }
    if (argc >= 4)
    {
        if (strcmp(argv[3], "--glob-only") == 0)
            glob_only = true;
        else if (strcmp(argv[3], "--fnmatch-only") == 0)
            fnmatch_only = true;
    }

    printf("===============================================\n");
    printf("Benchmark: '%s'\n", pattern);
    printf("Iterations: %d\n", iterations);
    printf("===============================================\n\n");

    if (!fnmatch_only)
    {
        printf("--- Glob Benchmarks ---\n");
        bench_libc_glob(pattern, iterations);
        bench_rbcglob(pattern, iterations);
        printf("\n");
    }

    if (!glob_only)
    {
        printf("--- Fnmatch Benchmarks ---\n");
        bench_libc_fnmatch(pattern, iterations);
        bench_rbc_fnmatch(pattern, iterations);
        printf("\n");
    }

    return 0;
}
