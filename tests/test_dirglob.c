#include <unity.h>
#include <dirglob/dirglob.h>

void setUp(void) {}
void tearDown(void) {}

void test_dirglob_version(void)
{
    TEST_ASSERT_EQUAL_STRING(DIRGLOB_VERSION, dirglob_version());
}

void test_sample(void)
{
    TEST_ASSERT_TRUE(1);
}
