#include <stdbool.h>
#include <string.h>
#include <unity.h>
#include <rbcglob/rbcglob.h>

void setUp(void) {}
void tearDown(void) {}

void test_dirglob_version(void)
{
    TEST_ASSERT_EQUAL_STRING(RBCGLOB_VERSION, rbcglob_version());
}

void test_dirglob_returns_empty_for_no_matches(void)
{
    const char *patterns[] = {"nonexistent_*.txt"};
    char **result = NULL;
    size_t *lengths = NULL;
    size_t count = 0;
    bool success = dirglob(patterns, 1, 0, NULL, 1, &result, &count, &lengths);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_UINT(0, count);

    rbcglob_free(result, count, lengths);
}

void test_dirglob_null_params_return_error(void)
{
    const char *patterns[] = {"*.txt"};
    char **result = NULL;
    size_t count = 0;

    /* NULL out parameter */
    TEST_ASSERT_FALSE(dirglob(patterns, 1, 0, NULL, 1, NULL, &count, NULL));

    /* NULL count parameter */
    TEST_ASSERT_FALSE(dirglob(patterns, 1, 0, NULL, 1, &result, NULL, NULL));
}

void test_dirglob_free_null_is_safe(void)
{
    /* Should not crash */
    rbcglob_free(NULL, 0, NULL);
    TEST_ASSERT_TRUE(1);
}

void test_dirglob_match_stub_returns_no_match(void)
{
    int result = rbcglob_match("*.txt", 0, "test.txt");
    TEST_ASSERT_EQUAL_INT(0, result); /* 0 = match */
}
