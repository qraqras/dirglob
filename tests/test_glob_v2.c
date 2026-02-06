/**
 * @file test_glob_v2.c
 * @brief Test for glob_v2 implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <rbc/rbc.h>

// Test helper
static int test_count = 0;
static int pass_count = 0;

#define TEST(name)                                    \
    do                                                \
    {                                                 \
        test_count++;                                 \
        printf("Test %d: %s ... ", test_count, name); \
    } while (0)

#define PASS()            \
    do                    \
    {                     \
        pass_count++;     \
        printf("PASS\n"); \
    } while (0)
#define FAIL(msg) printf("FAIL: %s\n", msg)

static void print_results(char **results, size_t count)
{
    printf("  Results (%zu):\n", count);
    for (size_t i = 0; i < count; i++)
    {
        printf("    [%zu] %s\n", i, results[i]);
    }
}

// ============================================================================
// Tests
// ============================================================================

static void test_simple_literal(void)
{
    TEST("Simple literal pattern");

    const char *patterns[] = {"*.txt"};
    rbc_glob_result_t result = {0};

    rbc_glob_status_t ok = rbc_glob(patterns, 1, 0, ".", true, &result, NULL, NULL);
    if (ok != RBC_GLOB_SUCCESS)
    {
        FAIL("rbc_glob returned false");
        return;
    }

    printf("found %zu files\n", result.count);
    if (result.count > 0)
    {
        print_results(result.paths, result.count);
    }

    rbc_globfree(&result);
    PASS();
}

static void test_recursive_pattern(void)
{
    TEST("Recursive ** pattern");

    const char *patterns[] = {"**/*.c"};
    rbc_glob_result_t result = {0};

    rbc_glob_status_t ok = rbc_glob(patterns, 1, 0, ".", true, &result, NULL, NULL);
    if (ok != RBC_GLOB_SUCCESS)
    {
        FAIL("rbc_glob returned false");
        return;
    }

    printf("found %zu files\n", result.count);
    if (result.count > 0 && result.count <= 10)
    {
        print_results(result.paths, result.count);
    }
    else if (result.count > 10)
    {
        printf("  (showing first 10)\n");
        for (size_t i = 0; i < 10; i++)
        {
            printf("    [%zu] %s\n", i, result.paths[i]);
        }
    }

    rbc_globfree(&result);
    PASS();
}

static void test_brace_expansion(void)
{
    TEST("Brace expansion {a,b}");

    const char *patterns[] = {"{src,include}/**/*.h"};
    rbc_glob_result_t result = {0};

    rbc_glob_status_t ok = rbc_glob(patterns, 1, 0, ".", true, &result, NULL, NULL);
    if (ok != RBC_GLOB_SUCCESS)
    {
        FAIL("rbc_glob returned false");
        return;
    }

    printf("found %zu files\n", result.count);
    if (result.count > 0 && result.count <= 10)
    {
        print_results(result.paths, result.count);
    }

    rbc_globfree(&result);
    PASS();
}

static void test_dotmatch(void)
{
    TEST("DOTMATCH flag");

    const char *patterns[] = {"*"};
    rbc_glob_result_t result_no_dot = {0};
    rbc_glob_result_t result_with_dot = {0};

    // Without DOTMATCH
    rbc_glob(patterns, 1, 0, ".", true, &result_no_dot, NULL, NULL);

    // With DOTMATCH
    rbc_glob(patterns, 1, RBC_FNM_DOTMATCH, ".", true, &result_with_dot, NULL, NULL);

    printf("without DOTMATCH: %zu, with DOTMATCH: %zu\n", result_no_dot.count, result_with_dot.count);

    if (result_with_dot.count >= result_no_dot.count)
    {
        PASS();
    }
    else
    {
        FAIL("DOTMATCH should return more or equal results");
    }

    rbc_globfree(&result_no_dot);
    rbc_globfree(&result_with_dot);
}

static void test_trailing_slash(void)
{
    TEST("Trailing slash (directories only)");

    const char *patterns[] = {"*/"};
    rbc_glob_result_t result = {0};

    rbc_glob_status_t ok = rbc_glob(patterns, 1, 0, ".", true, &result, NULL, NULL);
    if (ok != RBC_GLOB_SUCCESS)
    {
        FAIL("rbc_glob returned false");
        return;
    }

    printf("found %zu directories\n", result.count);
    if (result.count > 0)
    {
        print_results(result.paths, result.count);
    }

    // All results should end with /
    bool all_dirs = true;
    for (size_t i = 0; i < result.count; i++)
    {
        size_t len = strlen(result.paths[i]);
        if (len == 0 || result.paths[i][len - 1] != '/')
        {
            all_dirs = false;
            break;
        }
    }

    rbc_globfree(&result);

    if (all_dirs)
    {
        PASS();
    }
    else
    {
        FAIL("Not all results end with /");
    }
}

static void test_double_star_trailing(void)
{
    TEST("**/ pattern (all directories)");

    const char *patterns[] = {"**/"};
    rbc_glob_result_t result = {0};

    rbc_glob_status_t ok = rbc_glob(patterns, 1, 0, "src", true, &result, NULL, NULL);
    if (ok != RBC_GLOB_SUCCESS)
    {
        FAIL("rbc_glob returned false");
        return;
    }

    printf("found %zu directories under src/\n", result.count);
    if (result.count > 0 && result.count <= 10)
    {
        print_results(result.paths, result.count);
    }

    rbc_globfree(&result);
    PASS();
}

static void test_wildcard_ancestor_dot(void)
{
    TEST("Wildcard ancestor: */* should not include subdir/.");

    // This tests the Ruby behavior where . entries are excluded
    // when reached via wildcard ancestor
    const char *patterns[] = {"*/*"};
    rbc_glob_result_t result = {0};

    rbc_glob_status_t ok = rbc_glob(patterns, 1, RBC_FNM_DOTMATCH, ".", true, &result, NULL, NULL);
    if (ok != RBC_GLOB_SUCCESS)
    {
        FAIL("rbc_glob returned false");
        return;
    }

    // Check that no result ends with "/."
    bool has_dot = false;
    for (size_t i = 0; i < result.count; i++)
    {
        size_t len = strlen(result.paths[i]);
        if (len >= 2 && result.paths[i][len - 2] == '/' && result.paths[i][len - 1] == '.')
        {
            has_dot = true;
            printf("  Found: %s\n", result.paths[i]);
            break;
        }
        if (len == 1 && result.paths[i][0] == '.')
        {
            has_dot = true;
            printf("  Found: %s\n", result.paths[i]);
            break;
        }
    }

    rbc_globfree(&result);

    if (!has_dot)
    {
        PASS();
    }
    else
    {
        FAIL("Found /. entry via wildcard - should be excluded");
    }
}

int main(int argc, char **argv)
{
    printf("=== glob_v2 Test Suite ===\n\n");

    // Change to project root for consistent testing
    if (argc > 1)
    {
        if (chdir(argv[1]) != 0)
        {
            perror("chdir");
            return 1;
        }
    }

    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    printf("Working directory: %s\n\n", cwd);

    test_simple_literal();
    test_recursive_pattern();
    test_brace_expansion();
    test_dotmatch();
    test_trailing_slash();
    test_double_star_trailing();
    test_wildcard_ancestor_dot();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);

    return (pass_count == test_count) ? 0 : 1;
}
