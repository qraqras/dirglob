/**
 * @file test_path_utils.c
 * @brief Unity tests for Ruby-compatible path utility functions
 */

#include "unity.h"
#include <rbcglob/rbcglob.h>
#include <stdlib.h>
#include <string.h>

/* dirname tests */
void test_dirname_basic_path(void)
{
    char *result = rbcglob_dirname("/home/gumby/work/ruby.rb", 1);
    TEST_ASSERT_EQUAL_STRING("/home/gumby/work", result);
    free(result);
}

void test_dirname_level2(void)
{
    char *result = rbcglob_dirname("/home/gumby/work/ruby.rb", 2);
    TEST_ASSERT_EQUAL_STRING("/home/gumby", result);
    free(result);
}

void test_dirname_level4_to_root(void)
{
    char *result = rbcglob_dirname("/home/gumby/work/ruby.rb", 4);
    TEST_ASSERT_EQUAL_STRING("/", result);
    free(result);
}

void test_dirname_trailing_separator(void)
{
    char *result = rbcglob_dirname("/home/gumby/", 1);
    TEST_ASSERT_EQUAL_STRING("/home", result);
    free(result);
}

void test_dirname_root(void)
{
    char *result = rbcglob_dirname("/", 1);
    TEST_ASSERT_EQUAL_STRING("/", result);
    free(result);
}

void test_dirname_no_separator(void)
{
    char *result = rbcglob_dirname("ruby.rb", 1);
    TEST_ASSERT_EQUAL_STRING(".", result);
    free(result);
}

void test_dirname_empty_string(void)
{
    char *result = rbcglob_dirname("", 1);
    TEST_ASSERT_EQUAL_STRING(".", result);
    free(result);
}

/* basename tests */
void test_basename_basic_path(void)
{
    char *result = rbcglob_basename("/home/gumby/work/ruby.rb", NULL);
    TEST_ASSERT_EQUAL_STRING("ruby.rb", result);
    free(result);
}

void test_basename_remove_rb_suffix(void)
{
    char *result = rbcglob_basename("/home/gumby/work/ruby.rb", ".rb");
    TEST_ASSERT_EQUAL_STRING("ruby", result);
    free(result);
}

void test_basename_remove_any_extension(void)
{
    char *result = rbcglob_basename("/home/gumby/work/ruby.rb", ".*");
    TEST_ASSERT_EQUAL_STRING("ruby", result);
    free(result);
}

void test_basename_wildcard_no_extension(void)
{
    char *result = rbcglob_basename("/home/gumby/work/ruby", ".*");
    TEST_ASSERT_EQUAL_STRING("ruby", result);
    free(result);
}

void test_basename_trailing_separator(void)
{
    char *result = rbcglob_basename("/home/gumby/", NULL);
    TEST_ASSERT_EQUAL_STRING("gumby", result);
    free(result);
}

void test_basename_only_separator(void)
{
    char *result = rbcglob_basename("/", NULL);
    TEST_ASSERT_EQUAL_STRING("/", result);
    free(result);
}

void test_basename_no_separator(void)
{
    char *result = rbcglob_basename("ruby.rb", NULL);
    TEST_ASSERT_EQUAL_STRING("ruby.rb", result);
    free(result);
}

/* extname tests */
void test_extname_basic_rb(void)
{
    char *result = rbcglob_extname("test.rb");
    TEST_ASSERT_EQUAL_STRING(".rb", result);
    free(result);
}

void test_extname_path_with_rb(void)
{
    char *result = rbcglob_extname("a/b/d/test.rb");
    TEST_ASSERT_EQUAL_STRING(".rb", result);
    free(result);
}

void test_extname_dotdir_in_path(void)
{
    char *result = rbcglob_extname(".a/b/d/test.rb");
    TEST_ASSERT_EQUAL_STRING(".rb", result);
    free(result);
}

void test_extname_dotfile_no_extension(void)
{
    char *result = rbcglob_extname(".profile");
    TEST_ASSERT_EQUAL_STRING("", result);
    free(result);
}

void test_extname_dotfile_with_extension(void)
{
    char *result = rbcglob_extname(".profile.sh");
    TEST_ASSERT_EQUAL_STRING(".sh", result);
    free(result);
}

void test_extname_no_extension(void)
{
    char *result = rbcglob_extname("test");
    TEST_ASSERT_EQUAL_STRING("", result);
    free(result);
}

void test_extname_trailing_dot(void)
{
    char *result = rbcglob_extname("foo.");
#ifndef _WIN32
    TEST_ASSERT_EQUAL_STRING(".", result);
#else
    TEST_ASSERT_EQUAL_STRING("", result);
#endif
    free(result);
}

void test_extname_multiple_dots(void)
{
    char *result = rbcglob_extname("test.tar.gz");
    TEST_ASSERT_EQUAL_STRING(".gz", result);
    free(result);
}
