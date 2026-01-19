/**
 * @file test_glob_v2_recursive.c
 * @brief Tests for glob v2 recursive pattern (**) optimization
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
    /* Create deep directory structure */
    system("rm -rf test_recursive");
    mkdir("test_recursive", 0755);

    /* Level 1 */
    FILE *f;
    f = fopen("test_recursive/root.c", "w");
    if (f)
    {
        fprintf(f, "root\n");
        fclose(f);
    }

    /* Level 2 */
    mkdir("test_recursive/src", 0755);
    f = fopen("test_recursive/src/main.c", "w");
    if (f)
    {
        fprintf(f, "main\n");
        fclose(f);
    }

    f = fopen("test_recursive/src/util.c", "w");
    if (f)
    {
        fprintf(f, "util\n");
        fclose(f);
    }

    /* Level 3 */
    mkdir("test_recursive/src/core", 0755);
    f = fopen("test_recursive/src/core/engine.c", "w");
    if (f)
    {
        fprintf(f, "engine\n");
        fclose(f);
    }

    f = fopen("test_recursive/src/core/parser.c", "w");
    if (f)
    {
        fprintf(f, "parser\n");
        fclose(f);
    }

    /* Another branch */
    mkdir("test_recursive/tests", 0755);
    f = fopen("test_recursive/tests/test.c", "w");
    if (f)
    {
        fprintf(f, "test\n");
        fclose(f);
    }

    mkdir("test_recursive/tests/unit", 0755);
    f = fopen("test_recursive/tests/unit/test_util.c", "w");
    if (f)
    {
        fprintf(f, "test_util\n");
        fclose(f);
    }

    /* Non-C files */
    f = fopen("test_recursive/README.md", "w");
    if (f)
    {
        fprintf(f, "readme\n");
        fclose(f);
    }

    f = fopen("test_recursive/src/Makefile", "w");
    if (f)
    {
        fprintf(f, "makefile\n");
        fclose(f);
    }
}

static void cleanup_test_files(void)
{
    system("rm -rf test_recursive");
}

/* Test: Basic recursive pattern */
static void test_basic_recursive(void)
{
    TEST("Basic recursive - **/*.c");

    rbc_glob_result_t *result = rbc_glob_v2("test_recursive/**/*.c", 0);
    ASSERT(result != NULL);

    /* Should find all .c files recursively */
    ASSERT(result->count >= 6); /* root.c, main.c, util.c, engine.c, parser.c, test.c, test_util.c */

    printf("\n  Pattern: test_recursive/**/*.c");
    printf("\n  Results: %zu matches", result->count);
    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
    }

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Recursive from root */
static void test_recursive_from_root(void)
{
    TEST("Recursive from root - **/test*.c");

    rbc_glob_result_t *result = rbc_glob_v2("test_recursive/**/test*.c", 0);
    ASSERT(result != NULL);

    /* Should find test.c and test_util.c */
    ASSERT(result->count >= 2);

    printf("\n  Pattern: test_recursive/**/test*.c");
    printf("\n  Results: %zu matches", result->count);
    for (size_t i = 0; i < result->count; i++)
    {
        printf("\n    - %s", result->paths[i]);
    }

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Recursive specific file */
static void test_recursive_specific_file(void)
{
    TEST("Recursive specific file - **/engine.c");

    rbc_glob_result_t *result = rbc_glob_v2("test_recursive/**/engine.c", 0);
    ASSERT(result != NULL);
    ASSERT(result->count == 1);
    ASSERT(strstr(result->paths[0], "engine.c") != NULL);

    printf("\n  Pattern: test_recursive/**/engine.c");
    printf("\n  Found: %s", result->paths[0]);

    rbc_glob_result_free(result);
    PASS();
}

/* Test: No recursive matches */
static void test_no_recursive_matches(void)
{
    TEST("No recursive matches - **/*.rs");

    rbc_glob_result_t *result = rbc_glob_v2("test_recursive/**/*.rs", 0);
    ASSERT(result != NULL);
    ASSERT(result->count == 0);

    printf("\n  Pattern: test_recursive/**/*.rs");
    printf("\n  Results: 0 matches (correct)");

    rbc_glob_result_free(result);
    PASS();
}

/* Test: Hint detection for recursive */
static void test_recursive_hint_detection(void)
{
    TEST("Hint detection - verify RECURSIVE hint");

    rbc_glob_hints_t hints = rbc_glob_hints_generate("**/*.c");
    ASSERT(hints.type == GLOB_HINT_RECURSIVE);

    printf("\n  Pattern: **/*.c");
    printf("\n  Hint type: GLOB_HINT_RECURSIVE ✓");

    PASS();
}

int main(void)
{
    printf("===========================================\n");
    printf("Glob v2 Recursive Pattern Tests\n");
    printf("===========================================\n");

    setup_test_files();

    test_basic_recursive();
    test_recursive_from_root();
    test_recursive_specific_file();
    test_no_recursive_matches();
    test_recursive_hint_detection();

    cleanup_test_files();

    printf("\n===========================================\n");
    printf("Results: %d/%d tests passed\n", pass_count, test_count);
    printf("===========================================\n");

    return (pass_count == test_count) ? 0 : 1;
}
