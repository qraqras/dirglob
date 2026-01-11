#include <unity.h>
#include <string.h>
#include "pattern.h"
#include "arena.h"

void test_compile_strategy_exact(void)
{
    rbc_arena_t arena;
    rbc_arena_init(&arena, 1024);

    rbc_segment_t *seg = rbc_compile_segments(&arena, "file.txt", 0);

    TEST_ASSERT_NOT_NULL(seg);

    // "file.txt" should be compiled as SEG_LITERAL because it has no special characters.
    if (seg->type == RBC_SEGMENT_LITERAL)
    {
        TEST_ASSERT_EQUAL_STRING("file.txt", seg->data.literal);
    }
    else
    {
        // Fallback check if optimization strategy ever handles literals as Exact Matcher
        TEST_ASSERT_EQUAL_INT(RBC_SEGMENT_WILDCARD, seg->type);
        TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_EXACT, seg->data.glob.matcher.strategy);
        TEST_ASSERT_EQUAL_STRING("file.txt", seg->data.glob.matcher.pk.literal);
    }

    rbc_arena_destroy(&arena);
}

void test_compile_strategy_suffix(void)
{
    rbc_arena_t arena;
    rbc_arena_init(&arena, 1024);

    rbc_segment_t *seg = rbc_compile_segments(&arena, "*.c", 0);

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(RBC_SEGMENT_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_SUFFIX, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_STRING(".c", seg->data.glob.matcher.pk.affix.pattern);

    rbc_arena_destroy(&arena);
}

void test_compile_strategy_prefix(void)
{
    rbc_arena_t arena;
    rbc_arena_init(&arena, 1024);

    rbc_segment_t *seg = rbc_compile_segments(&arena, "test_*", 0);

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(RBC_SEGMENT_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_PREFIX, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_STRING("test_", seg->data.glob.matcher.pk.affix.pattern);

    rbc_arena_destroy(&arena);
}

void test_compile_strategy_infix(void)
{
    rbc_arena_t arena;
    rbc_arena_init(&arena, 1024);

    rbc_segment_t *seg = rbc_compile_segments(&arena, "*foo*", 0);

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(RBC_SEGMENT_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_INFIX, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_STRING("foo", seg->data.glob.matcher.pk.affix.pattern);

    rbc_arena_destroy(&arena);
}

void test_compile_strategy_sequence(void)
{
    rbc_arena_t arena;
    rbc_arena_init(&arena, 1024);

    // "a*b" -> Sequence [a, b] (match_start=T, match_end=T)
    rbc_segment_t *seg = rbc_compile_segments(&arena, "a*b", 0);

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(RBC_SEGMENT_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_PATTERN_CHAIN, seg->data.glob.matcher.strategy);
    TEST_ASSERT_TRUE(seg->data.glob.matcher.pk.chain.match_start);
    TEST_ASSERT_TRUE(seg->data.glob.matcher.pk.chain.match_end);
    TEST_ASSERT_EQUAL_INT(2, seg->data.glob.matcher.pk.chain.count);
    TEST_ASSERT_EQUAL_STRING("a", seg->data.glob.matcher.pk.chain.parts[0]);
    TEST_ASSERT_EQUAL_STRING("b", seg->data.glob.matcher.pk.chain.parts[1]);

    rbc_arena_destroy(&arena);
}

void test_compile_strategy_nfa_fallback(void)
{
    rbc_arena_t arena;
    rbc_arena_init(&arena, 1024);

    // Use brackets to force FNMATCH (complex fallback)
    rbc_segment_t *seg = rbc_compile_segments(&arena, "[abc].c", 0);

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(RBC_SEGMENT_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_FNMATCH, seg->data.glob.matcher.strategy);

    rbc_arena_destroy(&arena);
}

void test_compile_strategy_question_chain(void)
{
    rbc_arena_t arena;
    rbc_arena_init(&arena, 1024);

    // "?" should now be optimized as PATTERN_CHAIN (count=1)
    rbc_segment_t *seg = rbc_compile_segments(&arena, "?.c", 0);

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(RBC_SEGMENT_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_PATTERN_CHAIN, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_INT(1, seg->data.glob.matcher.pk.chain.count);
    TEST_ASSERT_EQUAL_STRING("?.c", seg->data.glob.matcher.pk.chain.parts[0]);
    TEST_ASSERT_TRUE(seg->data.glob.matcher.pk.chain.match_start);
    TEST_ASSERT_TRUE(seg->data.glob.matcher.pk.chain.match_end);

    rbc_arena_destroy(&arena);
}

void test_compile_strategy_mixed_chain(void)
{
    rbc_arena_t arena;
    rbc_arena_init(&arena, 1024);

    // "a?b*c" -> Chain ["a?b", "c"]
    rbc_segment_t *seg = rbc_compile_segments(&arena, "a?b*c", 0);

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(RBC_SEGMENT_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_PATTERN_CHAIN, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_INT(2, seg->data.glob.matcher.pk.chain.count);
    TEST_ASSERT_EQUAL_STRING("a?b", seg->data.glob.matcher.pk.chain.parts[0]);
    TEST_ASSERT_EQUAL_STRING("c", seg->data.glob.matcher.pk.chain.parts[1]);

    rbc_arena_destroy(&arena);
}

void test_compile_strategy_combinations(void)
{
    rbc_arena_t arena;
    rbc_arena_init(&arena, 1024);
    rbc_segment_t *seg;

    // 1. Prefix '*' and '?' : "*foo?"
    // Should be CHAIN ["foo?"], match_start=false, match_end=true
    seg = rbc_compile_segments(&arena, "*foo?", 0);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_PATTERN_CHAIN, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_INT(1, seg->data.glob.matcher.pk.chain.count);
    TEST_ASSERT_EQUAL_STRING("foo?", seg->data.glob.matcher.pk.chain.parts[0]);
    TEST_ASSERT_FALSE(seg->data.glob.matcher.pk.chain.match_start);
    TEST_ASSERT_TRUE(seg->data.glob.matcher.pk.chain.match_end);

    // 2. Suffix '*' and '?' : "?foo*"
    // Should be CHAIN ["?foo"], match_start=true, match_end=false
    seg = rbc_compile_segments(&arena, "?foo*", 0);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_PATTERN_CHAIN, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_INT(1, seg->data.glob.matcher.pk.chain.count);
    TEST_ASSERT_EQUAL_STRING("?foo", seg->data.glob.matcher.pk.chain.parts[0]);
    TEST_ASSERT_TRUE(seg->data.glob.matcher.pk.chain.match_start);
    TEST_ASSERT_FALSE(seg->data.glob.matcher.pk.chain.match_end);

    // 3. Infix '*' and '?' : "*?foo*"
    // Should be CHAIN ["?foo"], match_start=false, match_end=false
    seg = rbc_compile_segments(&arena, "*?foo*", 0);
    TEST_ASSERT_EQUAL_INT(RBC_STRATEGY_PATTERN_CHAIN, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_INT(1, seg->data.glob.matcher.pk.chain.count);
    TEST_ASSERT_EQUAL_STRING("?foo", seg->data.glob.matcher.pk.chain.parts[0]);
    TEST_ASSERT_FALSE(seg->data.glob.matcher.pk.chain.match_start);
    TEST_ASSERT_FALSE(seg->data.glob.matcher.pk.chain.match_end);

    rbc_arena_destroy(&arena);
}
