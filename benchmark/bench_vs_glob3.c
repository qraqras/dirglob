#include <rbcglob/rbcglob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>
#include <stdbool.h>

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
        // GLOB_BRACEを追加してブレース展開を有効化
        if (glob(pattern, GLOB_BRACE, NULL, &g) == 0)
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
        "benchmark/bench_many/*.txt",              // Large match (10000 files) - Alloc heavy
        "benchmark/bench_many/*nonexistent*.txt",  // Large scan (10000 checks, 0 matches) - Matcher heavy
        "benchmark/bench_many/*0*0*0*.txt",        // Complex wildcard: multiple segments (find files with three 0s)
        "benchmark/bench_many/f*i*l*e*_*0*0*.txt", // Long chain of short segments
        "benchmark/bench_many/?????_?????.txt",    // Fixed length wildcards
        "src/*.c",
        "tests/*.c",
        "include/rbcglob/*.h",
        "*.c",             // ルートディレクトリの.cファイル
        "*/*.c",           // 1階層下の.cファイル
        "test*/*.c",       // test*で始まるディレクトリの.cファイル
        "*.{c,h}",         // ルートの.cと.hファイル（ブレース展開）
        "src/*.{c,h}",     // srcの.cと.hファイル（ブレース展開）
        "tests/*.{c,h}",   // testsの.cと.hファイル（ブレース展開）
        "*/*.{c,h}",       // 1階層下の.cと.hファイル（ブレース展開）
        "{src,tests}/*.c", // srcまたはtestsの.cファイル（ブレース展開）
        // "bench_data_many/*.txt", // Large match set (2000 files)
        "*a*b*c*d*e*f*g*h*i*j*", // Pathological Case (requires long file name to be slow)
        "tests/*_???.c",         // Mix of * and ? (e.g. test_txt.c)
        "src/????.c",            // 4 chars (glob.c, path.c)
        "src/?????.c",           // 5 chars (utils.c, arena.c)
        "tests/test_????.c",     // tests/test_join.c
        "src/*mp*.c",            // Infix "mp" (compiler.c)
        "src/*a*.c",             // Infix "a" (path.c, arena.c, walker.c, glob.c, fnmatch.c) matches many
    };
    int num_patterns = 23; // Updated count
    int iterations = 100;

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
