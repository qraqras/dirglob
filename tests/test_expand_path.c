#include <rbcglob/rbcglob.h>
#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void helper_test_expand_path(const char *path, const char *base, const char *expected)
{
    char *res = rbcglob_expand_path(path, base);
    /* Note: expected might need to be adjusted based on CWD if NULL is passed,
       but for these tests we use absolute paths for base */
    TEST_ASSERT_EQUAL_STRING(expected, res);
    free(res);
}

void test_rbcglob_expand_path_basic(void)
{
    /* Absolute paths */
    helper_test_expand_path("/a/b", NULL, "/a/b");
    helper_test_expand_path("/a/b", "/etc", "/a/b");

    /* Normalization */
    helper_test_expand_path("/a/./b", NULL, "/a/b");
    helper_test_expand_path("/a/../b", NULL, "/b");
    helper_test_expand_path("/a/b/../c", NULL, "/a/c");
    helper_test_expand_path("/../../a", NULL, "/a");

    /* Relative with base */
    helper_test_expand_path("a", "/root", "/root/a");
    helper_test_expand_path("./a", "/root", "/root/a");
    helper_test_expand_path("../a", "/root/b", "/root/a");

    /* Multi-slashes */
    helper_test_expand_path("/a//b", NULL, "/a/b");
}

void test_rbcglob_expand_path_tilde(void)
{
    char *home = getenv("HOME");
    if (!home)
        return;

    char expected[4096];

    /* ~/ test */
    snprintf(expected, sizeof(expected), "%s/foo", home);
    helper_test_expand_path("~/foo", "/base/ignored", expected);

    /* ~ test */
    helper_test_expand_path("~", NULL, home);
}
