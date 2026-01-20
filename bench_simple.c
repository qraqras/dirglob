#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "rbc/rbc.h"

double get_time_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

void benchmark_ruby(const char *pattern, int iterations)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "ruby -e 'require \"benchmark\"; puts Benchmark.measure { %d.times { Dir.glob(\"%s\") } }'",
             iterations, pattern);
    system(cmd);
}

void benchmark_trie(const char *pattern, int iterations)
{
    double start = get_time_ms();

    for (int i = 0; i < iterations; i++)
    {
        char **paths = NULL;
        size_t count = 0;

        rbc_glob_trie(&pattern, 1, 0, NULL, false, &paths, &count, NULL);
        rbc_glob_free(paths, count, NULL);
    }

    double end = get_time_ms();
    double total = end - start;
    printf("  Total: %.3f ms (%.3f ms/iter)\n\n", total, total / iterations);
}

int main()
{
    int iterations = 1000;

    printf("========================================\n");
    printf("Simple Pattern Benchmark: Ruby vs Trie\n");
    printf("========================================\n");
    printf("Iterations: %d\n", iterations);
    printf("========================================\n\n");

    // Test 1: Single directory, simple wildcard
    printf("=== Test 1: src/core/*.c ===\n");
    printf("Ruby:\n");
    benchmark_ruby("src/core/*.c", iterations);
    printf("rbc_glob_trie:\n");
    benchmark_trie("src/core/*.c", iterations);

    // Test 2: Multiple wildcards
    printf("=== Test 2: src/*/*.c ===\n");
    printf("Ruby:\n");
    benchmark_ruby("src/*/*.c", iterations);
    printf("rbc_glob_trie:\n");
    benchmark_trie("src/*/*.c", iterations);

    // Test 3: Single file match
    printf("=== Test 3: src/core/glob.c ===\n");
    printf("Ruby:\n");
    benchmark_ruby("src/core/glob.c", iterations);
    printf("rbc_glob_trie:\n");
    benchmark_trie("src/core/glob.c", iterations);

    // Test 4: Multiple extensions
    printf("=== Test 4: tests/*.c ===\n");
    printf("Ruby:\n");
    benchmark_ruby("tests/*.c", iterations);
    printf("rbc_glob_trie:\n");
    benchmark_trie("tests/*.c", iterations);

    // Test 5: Complex wildcard
    printf("=== Test 5: tests/test_*.c ===\n");
    printf("Ruby:\n");
    benchmark_ruby("tests/test_*.c", iterations);
    printf("rbc_glob_trie:\n");
    benchmark_trie("tests/test_*.c", iterations);

    printf("========================================\n");
    printf("Benchmark Complete\n");
    printf("========================================\n");

    return 0;
}
