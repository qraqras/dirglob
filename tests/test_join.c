#include <rbcglob/rbcglob.h>
#include <unity.h>
#include <stdlib.h>
#include <string.h>

void helper_test_join(size_t count, const char **args, const char *expected)
{
    char *res = rbcglob_join(args, count);
    TEST_ASSERT_EQUAL_STRING(expected, res);
    free(res);
}

void test_rbcglob_join_compatibility(void)
{
    {
        const char *parts2[] = {"a", "b"};
        helper_test_join(2, parts2, "a/b");
    }
    {
        const char *parts2[] = {"a/", "b"};
        helper_test_join(2, parts2, "a/b");
    }
    {
        const char *parts2[] = {"a", "/b"};
        helper_test_join(2, parts2, "a/b");
    }
    {
        const char *parts2[] = {"a/", "/b"};
        helper_test_join(2, parts2, "a/b");
    }
    {
        const char *parts2[] = {"/a", "b"};
        helper_test_join(2, parts2, "/a/b");
    }
    {
        const char *parts2[] = {"", "a"};
        helper_test_join(2, parts2, "/a");
    }
    {
        const char *parts2[] = {"a", ""};
        helper_test_join(2, parts2, "a/");
    }
    {
        const char *parts2[] = {"", ""};
        helper_test_join(2, parts2, "/");
    }
    {
        const char *parts3[] = {"a", "b", "c"};
        helper_test_join(3, parts3, "a/b/c");
    }
    {
        const char *parts3[] = {"a", "b", ""};
        helper_test_join(3, parts3, "a/b/");
    }
    {
        /* Ruby style multi-slash boundary test */
        const char *parts2[] = {"a//", "//b"};
        helper_test_join(2, parts2, "a//b");
    }
}
