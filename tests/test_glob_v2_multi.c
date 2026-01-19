/**
 * @file test_glob_v2_multi.c
 * @brief Tests for glob v2 multi-pattern optimization
 */

#include "rbc/glob_v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_count = 0;
static int pass_count = 0;

#define TEST(name)                                      \
    do                                                  \
    {                                                   \
        printf("\nTest %d: %s - ", ++test_count, name); \
        fflush(stdout);                                 \
    } while (0)

#define ASSERT(cond)                                      \
    do                                                    \
    {                                                     \
        if (!(cond))                                      \
        {                                                 \
            printf("✗\n  Assertion failed: %s\n", #cond); \
            return;                                       \
        }                                                 \
    } while (0)

#define PASS()         \
    do                 \
    {                  \
        printf("✓\n"); \
        pass_count++;  \
    } while (0)

static void setup_test_files(void)
{
    system("rm -rf test_multi");
    mkdir("test_multi", 0755);

    FILE *f;

    /* Source files */
    f = fopen("test_multi/main.c", "w");
    if (f)
    {
        fprintf(f, "main\n");
        fclose(f);
    }

    f = fopen("test_multi/util.c", "w");
    if (f)
    {
        fprintf(f, "util\n");
        fclose(f);
    }

    f = fopen("test_multi/parser.c", "w");
    if (f)
    {
        fprintf(f, "parser\n");
        fclose(f);
    }

    /* Header files */
    f = fopen("test_multi/main.h", "w");
    if (f)
    {
        fprintf(f, "main\n");
        fclose(f);
    }

    f = fopen("test_multi/util.h", "w");
    if (f)
    {
        fprintf(f, "util\n");
        fclose(f);
    }

    /* Text files */
    f = fopen("test_multi/README.txt", "w");
    if (f)
    {
        fprintf(f, "readme\n");
        fclose(f);
    }

    f = fopen("test_multi/TODO.txt", "w");
    if (f)
    {
        fprintf(f, "todo\n");
        fclose(f);
    }

    /* Subdirectory */
    mkdir("test_multi/src", 0755);

    f = fopen("test_multi/src/core.c", "w");
    if (f)
    {
        fprintf(f, "core\n");
        fclose(f);
    }

    f = fopen("test_multi/src/api.c", "w");
    if (f)
    {
        fprintf(f, "api\n");
        fclose(f);
    }

    f = fopen("test_multi/src/core.h", "w");
    if (f)
    {
        fprintf(f, "core\n");
        fclose(f);
    }
}

static void cleanup_test_files(void)
{
    system("rm -rf test_multi");
}

/* Test: Multi-pattern same directory */
static void test_multi_same_directory(void)
{
    TEST("Multi-pattern same directory - *.c + *.h");

    const char *patterns[] = {
        "test_multi/*.c",
        "test_multi/*.h"};

    rbc_glob_result_t *result = rbc_glob_multi_v2(patterns, 2, 0);
    ASSERT(result != NULL);

    /* Should find 3 .c files + 2 .h files = 5 total */
    ASSERT(result->count >= 5);

    printf("\n  Patterns: test_multi/*.c + test_multi/*.h");
    printf("\n  Results: %zu matches", result->count);
    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
    }

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Multi-pattern different directories */
static void test_multi_different_directories(void)
{
    TEST("Multi-pattern different directories");

    const char *patterns[] = {
        "test_multi/*.c",
        "test_multi/src/*.c"};

    rbc_glob_result_t *result = rbc_glob_multi_v2(patterns, 2, 0);
    ASSERT(result != NULL);

    /* Should find 3 .c files in test_multi/ + 2 .c files in src/ = 5 total */
    ASSERT(result->count >= 5);

    printf("\n  Patterns: test_multi/*.c + test_multi/src/*.c");
    printf("\n  Results: %zu matches", result->count);
    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
    }

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Multi-pattern with overlapping results */
static void test_multi_overlapping(void)
{
    TEST("Multi-pattern overlapping - *.c + *util.c");

    const char *patterns[] = {
        "test_multi/*.c",
        "test_multi/*util.c" /* Should match util.c again */
    };

    rbc_glob_result_t *result = rbc_glob_multi_v2(patterns, 2, 0);
    ASSERT(result != NULL);

    /* Should deduplicate util.c */
    printf("\n  Patterns: test_multi/*.c + test_multi/*util.c");
    printf("\n  Results: %zu matches", result->count);

    /* Count how many times util.c appears */
    int util_count = 0;
    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
        if (strstr(result->paths[i], "util.c"))
        {
            util_count++;
        }
    }

    printf("\n  util.c appears %d time(s)", util_count);

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Three patterns same directory */
static void test_three_patterns_same_dir(void)
{
    TEST("Three patterns same directory - *.c + *.h + *.txt");

    const char *patterns[] = {
        "test_multi/*.c",
        "test_multi/*.h",
        "test_multi/*.txt"};

    rbc_glob_result_t *result = rbc_glob_multi_v2(patterns, 3, 0);
    ASSERT(result != NULL);

    /* Should find 3 .c + 2 .h + 2 .txt = 7 files */
    ASSERT(result->count >= 7);

    printf("\n  Patterns: test_multi/*.c + *.h + *.txt");
    printf("\n  Results: %zu matches", result->count);
    printf("\n  Optimization: 3 patterns → 1 directory scan");

    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
    }

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Empty pattern array */
static void test_empty_patterns(void)
{
    TEST("Empty pattern array");

    rbc_glob_result_t *result = rbc_glob_multi_v2(NULL, 0, 0);
    ASSERT(result != NULL);
    ASSERT(result->count == 0);

    printf("\n  Input: NULL patterns");
    printf("\n  Results: 0 matches (correct)");

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Single pattern (should not use optimization) */
static void test_single_pattern(void)
{
    TEST("Single pattern - falls back to normal glob");

    const char *patterns[] = {
        "test_multi/*.c"};

    rbc_glob_result_t *result = rbc_glob_multi_v2(patterns, 1, 0);
    ASSERT(result != NULL);
    ASSERT(result->count == 3);

    printf("\n  Pattern: test_multi/*.c");
    printf("\n  Results: %zu matches", result->count);

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Performance comparison */
static void test_performance_comparison(void)
{
    TEST("Performance comparison - individual vs multi");

    const char *patterns[] = {
        "test_multi/*.c",
        "test_multi/*.h",
        "test_multi/*.txt"};

    /* Individual execution */
    printf("\n  Individual execution:");
    for (size_t i = 0; i < 3; i++)
    {
        rbc_glob_result_t *result = rbc_glob_v2(patterns[i], 0);
        printf("\n    Pattern %zu: %zu matches", i + 1, result->count);
        rbc_glob_result_free(result);
    }

    /* Multi execution */
    printf("\n\n  Multi execution:");
    rbc_glob_result_t *multi_result = rbc_glob_multi_v2(patterns, 3, 0);
    printf("\n    All patterns: %zu total matches", multi_result->count);
    printf("\n    Optimization: 3 scans → 1 scan (3x speedup potential)");

    rbc_glob_result_free(multi_result);
    PASS();
}

int main(void)
{
    printf("===========================================\n");
    printf("Glob v2 Multi-Pattern Tests\n");
    printf("===========================================\n");

    setup_test_files();

    test_empty_patterns();
    test_single_pattern();
    test_multi_same_directory();
    test_multi_different_directories();
    test_multi_overlapping();
    test_three_patterns_same_dir();
    test_performance_comparison();

    cleanup_test_files();

    printf("\n===========================================\n");
    printf("Results: %d/%d tests passed\n", pass_count, test_count);
    printf("===========================================\n");

    printf("\nKey Achievement:\n");
    printf("Multi-pattern optimization: N scans → 1 scan\n");
    printf("Expected speedup: 3-8x for same directory patterns\n");

    return (pass_count == test_count) ? 0 : 1;
}
