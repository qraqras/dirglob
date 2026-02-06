#include <stdbool.h>
#include <string.h>
#include <unity.h>
#include <rbc/rbc.h>

#include <unistd.h>

void setUp(void)
{
    // Ensure we are in the fixtures directory for all tests
    // The relative path depends on where the test runner is executed from.
    // If run from build/tests, it should be ../../tests/fixtures.
    // We try multiple levels just in case.
    if (chdir("../../tests/fixtures") != 0)
    {
        if (chdir("../tests/fixtures") != 0)
        {
            if (chdir("tests/fixtures") != 0)
            {
                // Try finding adjacent source directory (out-of-source build)
                if (chdir("../dirglob/tests/fixtures") != 0)
                {
                    // Fallback: assume we are in the right place or print error
                    char cwd[1024];
                    getcwd(cwd, sizeof(cwd));
                    fprintf(stderr, "WARNING: Could not change to fixtures dir. CWD: %s\n", cwd);
                }
            }
        }
    }

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
        // printf("DEBUG: CWD = %s\n", cwd);
    }
}
void tearDown(void) {}

void test_dirglob_returns_empty_for_no_matches(void)
{
    const char *patterns[] = {"nonexistent_*.txt"};
    char **result = NULL;
    size_t *lengths = NULL;
    size_t count = 0;
    bool success = rbc_glob(patterns, 1, 0, NULL, 1, &result, &count, &lengths);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_UINT(0, count);

    rbc_globfree(result, count, lengths);
}

void test_dirglob_null_params_return_error(void)
{
    const char *patterns[] = {"*.txt"};
    char **result = NULL;
    size_t count = 0;

    /* NULL out parameter */
    TEST_ASSERT_FALSE(rbc_glob(patterns, 1, 0, NULL, 1, NULL, &count, NULL));

    /* NULL count parameter */
    TEST_ASSERT_FALSE(rbc_glob(patterns, 1, 0, NULL, 1, &result, NULL, NULL));
}

void test_dirglob_free_null_is_safe(void)
{
    /* Should not crash */
    rbc_globfree(NULL, 0, NULL);
    TEST_ASSERT_TRUE(1);
}

void test_dirglob_match_stub_returns_no_match(void)
{
    bool match = rbc_fnmatch("*.txt", "test.txt", 0);
    TEST_ASSERT_TRUE(match);
}

void test_dirglob_character_class_match(void)
{
    TEST_ASSERT_TRUE(rbc_fnmatch("file[1-3].txt", "file1.txt", 0));
    TEST_ASSERT_TRUE(rbc_fnmatch("file[1-3].txt", "file2.txt", 0));
    TEST_ASSERT_TRUE(rbc_fnmatch("file[1-3].txt", "file3.txt", 0));
    TEST_ASSERT_FALSE(rbc_fnmatch("file[1-3].txt", "file4.txt", 0));
    TEST_ASSERT_FALSE(rbc_fnmatch("file[1-3].txt", "file0.txt", 0));
    TEST_ASSERT_TRUE(rbc_fnmatch("[a-c].txt", "b.txt", 0));
}

void test_recursive_glob_duplicates(void)
{
    const char *patterns[] = {"**/*.txt"};
    char **result = NULL;
    size_t count = 0;

    bool success = rbc_glob(patterns, 1, 0, NULL, 1, &result, &count, NULL);
    TEST_ASSERT_TRUE(success);

    // Check for duplicates
    for (size_t i = 0; i < count; i++)
    {
        for (size_t j = i + 1; j < count; j++)
        {
            if (strcmp(result[i], result[j]) == 0)
            {
                char msg[256];
                snprintf(msg, sizeof(msg), "Duplicate found: %s at indices %zu and %zu", result[i], i, j);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }

    rbc_globfree(result, count, NULL);
}
