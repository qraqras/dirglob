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

#define RBC_FNM_NOESCAPE 0x01
#define RBC_FNM_PATHNAME 0x02
#define RBC_FNM_DOTMATCH 0x04
#define RBC_FNM_CASEFOLD 0x08

// External API
bool rbc_glob_v2(const char **patterns, size_t npatterns, unsigned flags,
                 const char *base, bool sort,
                 char ***out, size_t *count, size_t **lengths);
void rbc_glob_free_v2(char **list, size_t count, size_t *lengths);

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
    char **results = NULL;
    size_t count = 0;

    bool ok = rbc_glob_v2(patterns, 1, 0, ".", true, &results, &count, NULL);
    if (!ok)
    {
        FAIL("rbc_glob_v2 returned false");
        return;
    }

    printf("found %zu files\n", count);
    if (count > 0)
    {
        print_results(results, count);
    }

    rbc_glob_free_v2(results, count, NULL);
    PASS();
}

static void test_recursive_pattern(void)
{
    TEST("Recursive ** pattern");

    const char *patterns[] = {"**/*.c"};
    char **results = NULL;
    size_t count = 0;

    bool ok = rbc_glob_v2(patterns, 1, 0, ".", true, &results, &count, NULL);
    if (!ok)
    {
        FAIL("rbc_glob_v2 returned false");
        return;
    }

    printf("found %zu files\n", count);
    if (count > 0 && count <= 10)
    {
        print_results(results, count);
    }
    else if (count > 10)
    {
        printf("  (showing first 10)\n");
        for (size_t i = 0; i < 10; i++)
        {
            printf("    [%zu] %s\n", i, results[i]);
        }
    }

    rbc_glob_free_v2(results, count, NULL);
    PASS();
}

static void test_brace_expansion(void)
{
    TEST("Brace expansion {a,b}");

    const char *patterns[] = {"{src,include}/**/*.h"};
    char **results = NULL;
    size_t count = 0;

    bool ok = rbc_glob_v2(patterns, 1, 0, ".", true, &results, &count, NULL);
    if (!ok)
    {
        FAIL("rbc_glob_v2 returned false");
        return;
    }

    printf("found %zu files\n", count);
    if (count > 0 && count <= 10)
    {
        print_results(results, count);
    }

    rbc_glob_free_v2(results, count, NULL);
    PASS();
}

static void test_dotmatch(void)
{
    TEST("DOTMATCH flag");

    const char *patterns[] = {"*"};
    char **results_no_dot = NULL;
    char **results_with_dot = NULL;
    size_t count_no_dot = 0;
    size_t count_with_dot = 0;

    // Without DOTMATCH
    rbc_glob_v2(patterns, 1, 0, ".", true, &results_no_dot, &count_no_dot, NULL);

    // With DOTMATCH
    rbc_glob_v2(patterns, 1, RBC_FNM_DOTMATCH, ".", true, &results_with_dot, &count_with_dot, NULL);

    printf("without DOTMATCH: %zu, with DOTMATCH: %zu\n", count_no_dot, count_with_dot);

    if (count_with_dot >= count_no_dot)
    {
        PASS();
    }
    else
    {
        FAIL("DOTMATCH should return more or equal results");
    }

    rbc_glob_free_v2(results_no_dot, count_no_dot, NULL);
    rbc_glob_free_v2(results_with_dot, count_with_dot, NULL);
}

static void test_trailing_slash(void)
{
    TEST("Trailing slash (directories only)");

    const char *patterns[] = {"*/"};
    char **results = NULL;
    size_t count = 0;

    bool ok = rbc_glob_v2(patterns, 1, 0, ".", true, &results, &count, NULL);
    if (!ok)
    {
        FAIL("rbc_glob_v2 returned false");
        return;
    }

    printf("found %zu directories\n", count);
    if (count > 0)
    {
        print_results(results, count);
    }

    // All results should end with /
    bool all_dirs = true;
    for (size_t i = 0; i < count; i++)
    {
        size_t len = strlen(results[i]);
        if (len == 0 || results[i][len - 1] != '/')
        {
            all_dirs = false;
            break;
        }
    }

    rbc_glob_free_v2(results, count, NULL);

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
    char **results = NULL;
    size_t count = 0;

    bool ok = rbc_glob_v2(patterns, 1, 0, "src", true, &results, &count, NULL);
    if (!ok)
    {
        FAIL("rbc_glob_v2 returned false");
        return;
    }

    printf("found %zu directories under src/\n", count);
    if (count > 0 && count <= 10)
    {
        print_results(results, count);
    }

    rbc_glob_free_v2(results, count, NULL);
    PASS();
}

static void test_wildcard_ancestor_dot(void)
{
    TEST("Wildcard ancestor: */* should not include subdir/.");

    // This tests the Ruby behavior where . entries are excluded
    // when reached via wildcard ancestor
    const char *patterns[] = {"*/*"};
    char **results = NULL;
    size_t count = 0;

    bool ok = rbc_glob_v2(patterns, 1, RBC_FNM_DOTMATCH, ".", true, &results, &count, NULL);
    if (!ok)
    {
        FAIL("rbc_glob_v2 returned false");
        return;
    }

    // Check that no result ends with "/."
    bool has_dot = false;
    for (size_t i = 0; i < count; i++)
    {
        size_t len = strlen(results[i]);
        if (len >= 2 && results[i][len - 2] == '/' && results[i][len - 1] == '.')
        {
            has_dot = true;
            printf("  Found: %s\n", results[i]);
            break;
        }
        if (len == 1 && results[i][0] == '.')
        {
            has_dot = true;
            printf("  Found: %s\n", results[i]);
            break;
        }
    }

    rbc_glob_free_v2(results, count, NULL);

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
