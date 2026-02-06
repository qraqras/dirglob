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
    rbc_glob_result_t result = {0};
    rbc_glob_status_t success = rbc_glob(patterns, 1, 0, NULL, 1, &result, NULL, NULL);

    TEST_ASSERT_EQUAL(RBC_GLOB_SUCCESS, success);
    TEST_ASSERT_EQUAL_UINT(0, result.count);

    rbc_globfree(&result);
}

void test_dirglob_null_params_return_error(void)
{
    const char *patterns[] = {"*.txt"};

    /* NULL result parameter */
    TEST_ASSERT_NOT_EQUAL(RBC_GLOB_SUCCESS, rbc_glob(patterns, 1, 0, NULL, 1, NULL, NULL, NULL));
}

void test_dirglob_free_null_is_safe(void)
{
    /* Should not crash */
    rbc_globfree(NULL);
    rbc_glob_result_t empty = {0};
    rbc_globfree(&empty);
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
    rbc_glob_result_t result = {0};

    rbc_glob_status_t success = rbc_glob(patterns, 1, 0, NULL, 1, &result, NULL, NULL);
    TEST_ASSERT_EQUAL(RBC_GLOB_SUCCESS, success);

    // Check for duplicates
    for (size_t i = 0; i < result.count; i++)
    {
        for (size_t j = i + 1; j < result.count; j++)
        {
            if (strcmp(result.paths[i], result.paths[j]) == 0)
            {
                char msg[256];
                snprintf(msg, sizeof(msg), "Duplicate found: %s at indices %zu and %zu", result.paths[i], i, j);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }

    rbc_globfree(&result);
}
