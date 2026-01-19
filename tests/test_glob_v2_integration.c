/**
 * @file test_glob_v2_integration.c
 * @brief Tests for glob v2 v1 integration (Fast Path)
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
    /* Create test directory */
    mkdir("test_v2_integration", 0755);

    /* Create test files */
    FILE *f;

    f = fopen("test_v2_integration/test.txt", "w");
    if (f)
    {
        fprintf(f, "test\n");
        fclose(f);
    }

    f = fopen("test_v2_integration/test.c", "w");
    if (f)
    {
        fprintf(f, "test\n");
        fclose(f);
    }

    f = fopen("test_v2_integration/main.c", "w");
    if (f)
    {
        fprintf(f, "main\n");
        fclose(f);
    }

    f = fopen("test_v2_integration/util.c", "w");
    if (f)
    {
        fprintf(f, "util\n");
        fclose(f);
    }

    /* Create subdirectory */
    mkdir("test_v2_integration/src", 0755);

    f = fopen("test_v2_integration/src/foo.c", "w");
    if (f)
    {
        fprintf(f, "foo\n");
        fclose(f);
    }

    f = fopen("test_v2_integration/src/bar.c", "w");
    if (f)
    {
        fprintf(f, "bar\n");
        fclose(f);
    }
}

static void cleanup_test_files(void)
{
    system("rm -rf test_v2_integration");
}

/* Test: Simple pattern (*.txt) */
static void test_simple_pattern(void)
{
    TEST("Simple pattern - *.txt");

    rbc_glob_result_t *result = rbc_glob_v2("test_v2_integration/*.txt", 0);
    ASSERT(result != NULL);
    ASSERT(result->count == 1);
    ASSERT(strstr(result->paths[0], "test.txt") != NULL);

    printf("\n  Pattern: test_v2_integration/*.txt");
    printf("\n  Results: %zu matches", result->count);
    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
    }

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Simple pattern with multiple matches */
static void test_simple_multiple(void)
{
    TEST("Simple pattern - *.c (multiple matches)");

    rbc_glob_result_t *result = rbc_glob_v2("test_v2_integration/*.c", 0);
    ASSERT(result != NULL);
    ASSERT(result->count == 3); /* main.c, test.c, util.c */

    printf("\n  Pattern: test_v2_integration/*.c");
    printf("\n  Results: %zu matches", result->count);
    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
    }

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Multi-segment pattern */
static void test_multi_segment(void)
{
    TEST("Multi-segment - src/*.c");

    rbc_glob_result_t *result = rbc_glob_v2("test_v2_integration/src/*.c", 0);
    ASSERT(result != NULL);
    ASSERT(result->count == 2); /* foo.c, bar.c */

    printf("\n  Pattern: test_v2_integration/src/*.c");
    printf("\n  Results: %zu matches", result->count);
    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
    }

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Literal path */
static void test_literal_path(void)
{
    TEST("Literal path - test.txt");

    rbc_glob_result_t *result = rbc_glob_v2("test_v2_integration/test.txt", 0);
    ASSERT(result != NULL);
    ASSERT(result->count == 1);
    ASSERT(strcmp(result->paths[0], "test_v2_integration/test.txt") == 0);

    printf("\n  Pattern: test_v2_integration/test.txt");
    printf("\n  Results: %zu matches", result->count);
    printf("\n    - %s", result->paths[0]);

    rbc_glob_result_free(result);
    PASS();
}

/* Test: No matches */
static void test_no_matches(void)
{
    TEST("No matches - *.rs");

    rbc_glob_result_t *result = rbc_glob_v2("test_v2_integration/*.rs", 0);
    ASSERT(result != NULL);
    ASSERT(result->count == 0);

    printf("\n  Pattern: test_v2_integration/*.rs");
    printf("\n  Results: %zu matches (correct)", result->count);

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Question mark pattern */
static void test_question_mark(void)
{
    TEST("Question mark - ????.c");

    rbc_glob_result_t *result = rbc_glob_v2("test_v2_integration/????.c", 0);
    ASSERT(result != NULL);
    ASSERT(result->count >= 2); /* main.c, test.c, util.c */

    printf("\n  Pattern: test_v2_integration/????.c");
    printf("\n  Results: %zu matches", result->count);
    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
    }

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Hint generation integration */
static void test_hint_routing(void)
{
    TEST("Hint routing - verify correct path selection");

    /* Literal path → HINT_TYPE_LITERAL */
    rbc_glob_hints_t hints1 = rbc_glob_hints_generate("test.txt");
    ASSERT(hints1.type == GLOB_HINT_LITERAL);

    /* Simple pattern → HINT_TYPE_SIMPLE_PATTERN */
    rbc_glob_hints_t hints2 = rbc_glob_hints_generate("*.txt");
    ASSERT(hints2.type == GLOB_HINT_SIMPLE_PATTERN);

    /* Multi-segment → HINT_TYPE_MULTI_SEGMENT */
    rbc_glob_hints_t hints3 = rbc_glob_hints_generate("src/*.c");
    ASSERT(hints3.type == GLOB_HINT_MULTI_SEGMENT);

    printf("\n  Literal: HINT_TYPE_LITERAL ✓");
    printf("\n  Simple: HINT_TYPE_SIMPLE_PATTERN ✓");
    printf("\n  Multi-segment: HINT_TYPE_MULTI_SEGMENT ✓");

    PASS();
}

int main(void)
{
    printf("===========================================\n");
    printf("Glob v2 v1 Integration Tests\n");
    printf("===========================================\n");

    setup_test_files();

    test_literal_path();
    test_simple_pattern();
    test_simple_multiple();
    test_multi_segment();
    test_no_matches();
    test_question_mark();
    test_hint_routing();

    cleanup_test_files();

    printf("\n===========================================\n");
    printf("Results: %d/%d tests passed\n", pass_count, test_count);
    printf("===========================================\n");

    return (pass_count == test_count) ? 0 : 1;
}
