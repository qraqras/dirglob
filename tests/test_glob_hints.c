/**
 * @file test_glob_v2_hints.c
 * @brief Test program for glob v2 hint generation
 */

#include "rbc/glob_hints.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

#define TEST(name) printf("\n=== Test: %s ===\n", name)
#define ASSERT_EQ(a, b) assert((a) == (b))
#define ASSERT_STR_EQ(a, b, len) assert(strncmp(a, b, len) == 0)

void test_literal_pattern(void)
{
    TEST("Literal pattern");

    rbc_glob_hints_t hints = rbc_glob_hints_generate("src/file.txt");

    ASSERT_EQ(hints.type, GLOB_HINT_LITERAL);
    ASSERT_EQ(hints.flags.has_wildcard, false);
    ASSERT_EQ(hints.flags.has_brace, false);
    ASSERT_EQ(hints.cost.estimated_dirs, 0);

    rbc_glob_hints_dump(&hints);
    printf("✓ Literal pattern recognized\n");
}

void test_simple_pattern(void)
{
    TEST("Simple pattern");

    rbc_glob_hints_t hints = rbc_glob_hints_generate("*.txt");

    ASSERT_EQ(hints.type, GLOB_HINT_SIMPLE_PATTERN);
    ASSERT_EQ(hints.flags.has_wildcard, true);
    ASSERT_EQ(hints.flags.has_brace, false);
    ASSERT_EQ(hints.segment_count, 1);
    ASSERT_EQ(hints.cost.estimated_dirs, 1);

    rbc_glob_hints_dump(&hints);
    printf("✓ Simple pattern recognized\n");
}

void test_multi_segment_pattern(void)
{
    TEST("Multi-segment pattern");

    rbc_glob_hints_t hints = rbc_glob_hints_generate("src/*.c");

    ASSERT_EQ(hints.type, GLOB_HINT_MULTI_SEGMENT);
    ASSERT_EQ(hints.segment_count, 2);
    ASSERT_EQ(hints.flags.has_wildcard, true);
    ASSERT_EQ(hints.flags.has_brace, false);

    rbc_glob_hints_dump(&hints);
    printf("✓ Multi-segment pattern recognized\n");
}

void test_brace_pattern_simple(void)
{
    TEST("Brace pattern (simple)");

    rbc_glob_hints_t hints = rbc_glob_hints_generate("test_{a,b,c}.txt");

    ASSERT_EQ(hints.type, GLOB_HINT_BRACE_SINGLE_DIR);
    ASSERT_EQ(hints.flags.has_brace, true);
    ASSERT_EQ(hints.brace_info.choice_count, 3);

    /* Check prefix */
    ASSERT_STR_EQ(hints.brace_info.prefix, "test_", 5);
    ASSERT_EQ(hints.brace_info.prefix_len, 5);

    /* Check suffix */
    ASSERT_STR_EQ(hints.brace_info.suffix, ".txt", 4);
    ASSERT_EQ(hints.brace_info.suffix_len, 4);

    /* Check choices */
    ASSERT_STR_EQ(hints.brace_info.choices[0].start, "a", 1);
    ASSERT_EQ(hints.brace_info.choices[0].len, 1);
    ASSERT_STR_EQ(hints.brace_info.choices[1].start, "b", 1);
    ASSERT_EQ(hints.brace_info.choices[1].len, 1);
    ASSERT_STR_EQ(hints.brace_info.choices[2].start, "c", 1);
    ASSERT_EQ(hints.brace_info.choices[2].len, 1);

    /* Check optimization hints */
    ASSERT_EQ(hints.brace_info.all_single_char, true);
    ASSERT_EQ(hints.cost.estimated_dirs, 1); /* Optimized to 1 scan */

    rbc_glob_hints_dump(&hints);
    printf("✓ Brace pattern recognized and parsed correctly\n");
}

void test_brace_pattern_with_path(void)
{
    TEST("Brace pattern with path");

    rbc_glob_hints_t hints = rbc_glob_hints_generate("src/{a,b,c}/*.txt");

    ASSERT_EQ(hints.type, GLOB_HINT_BRACE_SINGLE_DIR);
    ASSERT_EQ(hints.brace_info.choice_count, 3);

    /* Prefix should include "src/" */
    ASSERT_STR_EQ(hints.brace_info.prefix, "src/", 4);
    ASSERT_EQ(hints.brace_info.prefix_len, 4);

    /* Suffix should be "/*.txt" */
    ASSERT_STR_EQ(hints.brace_info.suffix, "/*.txt", 6);
    ASSERT_EQ(hints.brace_info.suffix_len, 6);

    rbc_glob_hints_dump(&hints);
    printf("✓ Brace pattern with path recognized\n");
}

void test_brace_pattern_many_choices(void)
{
    TEST("Brace pattern with many choices");

    rbc_glob_hints_t hints = rbc_glob_hints_generate("file_{a,b,c,d,e,f,g,h}.txt");

    ASSERT_EQ(hints.type, GLOB_HINT_BRACE_SINGLE_DIR);
    ASSERT_EQ(hints.brace_info.choice_count, 8);
    ASSERT_EQ(hints.brace_info.can_use_hashset, true); /* >= 4 choices */

    rbc_glob_hints_dump(&hints);
    printf("✓ Hashset optimization hint set for many choices\n");
}

void test_doublestar_pattern(void)
{
    TEST("Doublestar pattern");

    rbc_glob_hints_t hints = rbc_glob_hints_generate("**/*.c");

    ASSERT_EQ(hints.type, GLOB_HINT_RECURSIVE);
    ASSERT_EQ(hints.flags.has_doublestar, true);
    ASSERT_EQ(hints.flags.has_wildcard, true);

    rbc_glob_hints_dump(&hints);
    printf("✓ Doublestar pattern recognized\n");
}

void test_bracket_pattern(void)
{
    TEST("Bracket pattern");

    rbc_glob_hints_t hints = rbc_glob_hints_generate("test_[abc].txt");

    ASSERT_EQ(hints.flags.has_bracket, true);
    ASSERT_EQ(hints.flags.has_wildcard, false); /* [ ] is not a wildcard */

    rbc_glob_hints_dump(&hints);
    printf("✓ Bracket pattern recognized\n");
}

void test_performance(void)
{
    TEST("Performance test");

    const char *patterns[] = {
        "*.txt",
        "src/*.c",
        "src/{a,b,c}/*.txt",
        "**/*.js",
        "test_{1,2,3,4,5,6,7,8,9,10}.dat",
    };

    const int iterations = 100000;

    printf("Generating hints %d times for each pattern...\n", iterations);

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
    {
        for (int j = 0; j < iterations; j++)
        {
            rbc_glob_hints_t hints = rbc_glob_hints_generate(patterns[i]);
            (void)hints; /* Prevent optimization */
        }
        printf("  Pattern '%s': OK\n", patterns[i]);
    }

    printf("✓ Performance test completed\n");
    printf("  (should complete in < 1 second for lightweight hint generation)\n");
}

int main(void)
{
    printf("===========================================\n");
    printf("Glob v2 Hint Generation Tests\n");
    printf("===========================================\n");

    test_literal_pattern();
    test_simple_pattern();
    test_multi_segment_pattern();
    test_brace_pattern_simple();
    test_brace_pattern_with_path();
    test_brace_pattern_many_choices();
    test_doublestar_pattern();
    test_bracket_pattern();
    test_performance();

    printf("\n===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}
