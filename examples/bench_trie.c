/**
 * @file bench_trie.c
 * @brief Benchmark: Ruby Dir.glob vs rbc_glob vs rbc_glob_trie
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <rbc/rbc.h>

#define ITERATIONS 100

static double get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void benchmark_ruby(const char *pattern, const char *label)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "ruby -e 'require \"benchmark\"; puts Benchmark.measure { %d.times { Dir.glob(\"%s\") } }'",
             ITERATIONS, pattern);

    printf("\n%s (Ruby):\n", label);
    fflush(stdout);
    system(cmd);
}

static void benchmark_ruby_multi(const char **patterns, size_t count, const char *label)
{
    char patterns_str[1024] = "";
    for (size_t i = 0; i < count; i++)
    {
        if (i > 0)
            strcat(patterns_str, ", ");
        strcat(patterns_str, "\"");
        strcat(patterns_str, patterns[i]);
        strcat(patterns_str, "\"");
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "ruby -e 'require \"benchmark\"; puts Benchmark.measure { %d.times { [%s].each { |p| Dir.glob(p) } } }'",
             ITERATIONS, patterns_str);

    printf("\n%s (Ruby - sequential):\n", label);
    fflush(stdout);
    system(cmd);
}

static void benchmark_rbc_glob(const char **patterns, size_t count, const char *label)
{
    double start = get_time_ms();

    for (int i = 0; i < ITERATIONS; i++)
    {
        char **results = NULL;
        size_t result_count = 0;

        if (rbc_glob(patterns, count, 0, NULL, false, &results, &result_count, NULL))
        {
            rbc_glob_free(results, result_count, NULL);
        }
    }

    double elapsed = get_time_ms() - start;
    printf("\n%s (rbc_glob):\n", label);
    printf("  Total: %.3f ms (%.3f ms/iter)\n", elapsed, elapsed / ITERATIONS);
}

static void benchmark_rbc_glob_trie(const char **patterns, size_t count, const char *label)
{
    double start = get_time_ms();

    for (int i = 0; i < ITERATIONS; i++)
    {
        char **results = NULL;
        size_t result_count = 0;

        if (rbc_glob_trie(patterns, count, 0, NULL, false, &results, &result_count, NULL))
        {
            rbc_glob_free(results, result_count, NULL);
        }
    }

    double elapsed = get_time_ms() - start;
    printf("\n%s (rbc_glob_trie):\n", label);
    printf("  Total: %.3f ms (%.3f ms/iter)\n", elapsed, elapsed / ITERATIONS);
}

static size_t count_results(const char **patterns, size_t count, bool use_trie)
{
    char **results = NULL;
    size_t result_count = 0;

    if (use_trie)
    {
        if (rbc_glob_trie(patterns, count, 0, NULL, false, &results, &result_count, NULL))
        {
            rbc_glob_free(results, result_count, NULL);
            return result_count;
        }
    }
    else
    {
        if (rbc_glob(patterns, count, 0, NULL, false, &results, &result_count, NULL))
        {
            rbc_glob_free(results, result_count, NULL);
            return result_count;
        }
    }

    return 0;
}

int main(void)
{
    printf("========================================\n");
    printf("Glob Benchmark: Ruby vs rbc_glob vs rbc_glob_trie\n");
    printf("========================================\n");
    printf("Iterations: %d\n", ITERATIONS);
    printf("Target: /workspaces/dirglob workspace\n");
    printf("========================================\n");

    // Change to workspace directory
    if (chdir("/workspaces/dirglob") != 0)
    {
        perror("chdir failed");
        return 1;
    }

    // Test 1: Single pattern with brace expansion
    {
        printf("\n\n=== Test 1: Brace Expansion Pattern ===\n");
        const char *patterns[] = {"{src,tests}/**/*.{c,h}"};
        size_t count = count_results(patterns, 1, true);
        printf("Results: %zu files\n", count);

        benchmark_ruby(patterns[0], "Pattern: {src,tests}/**/*.{c,h}");
        benchmark_rbc_glob(patterns, 1, "Pattern: {src,tests}/**/*.{c,h}");
        benchmark_rbc_glob_trie(patterns, 1, "Pattern: {src,tests}/**/*.{c,h}");
    }

    // Test 2: Multiple patterns with shared prefix
    {
        printf("\n\n=== Test 2: Multiple Patterns (Shared Prefix) ===\n");
        const char *patterns[] = {
            "src/**/*.c",
            "src/**/*.h"};
        size_t count = count_results(patterns, 2, true);
        printf("Results: %zu files\n", count);

        benchmark_ruby_multi(patterns, 2, "Patterns: src/**/*.c, src/**/*.h");
        benchmark_rbc_glob(patterns, 2, "Patterns: src/**/*.c, src/**/*.h");
        benchmark_rbc_glob_trie(patterns, 2, "Patterns: src/**/*.c, src/**/*.h");
    }

    // Test 3: Many patterns with different prefixes
    {
        printf("\n\n=== Test 3: Many Patterns (Different Prefixes) ===\n");
        const char *patterns[] = {
            "src/**/*.c",
            "src/**/*.h",
            "tests/**/*.c",
            "tests/**/*.h",
            "examples/**/*.c",
            "include/**/*.h"};
        size_t count = count_results(patterns, 6, true);
        printf("Results: %zu files\n", count);

        benchmark_ruby_multi(patterns, 6, "6 patterns");
        benchmark_rbc_glob(patterns, 6, "6 patterns");
        benchmark_rbc_glob_trie(patterns, 6, "6 patterns");
    }

    // Test 4: Simple single-directory patterns
    {
        printf("\n\n=== Test 4: Simple Pattern (Single Directory) ===\n");
        const char *patterns[] = {"src/core/*.c"};
        size_t count = count_results(patterns, 1, true);
        printf("Results: %zu files\n", count);

        benchmark_ruby(patterns[0], "Pattern: src/core/*.c");
        benchmark_rbc_glob(patterns, 1, "Pattern: src/core/*.c");
        benchmark_rbc_glob_trie(patterns, 1, "Pattern: src/core/*.c");
    }

    // Test 5: Multiple simple patterns with grouping potential
    {
        printf("\n\n=== Test 5: Multiple Simple Patterns (Same Dir) ===\n");
        const char *patterns[] = {
            "src/core/*.c",
            "src/core/*.h"};
        size_t count = count_results(patterns, 2, true);
        printf("Results: %zu files\n", count);

        benchmark_ruby_multi(patterns, 2, "Patterns: src/core/*.c, src/core/*.h");
        benchmark_rbc_glob(patterns, 2, "Patterns: src/core/*.c, src/core/*.h");
        benchmark_rbc_glob_trie(patterns, 2, "Patterns: src/core/*.c, src/core/*.h");
    }

    printf("\n\n========================================\n");
    printf("Benchmark Complete\n");
    printf("========================================\n");

    return 0;
}
