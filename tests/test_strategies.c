#include <unity.h>
#include <string.h>
#include "rbcglob/internal/pattern.h"
#include "rbcglob/internal/arena.h"

void test_compile_strategy_exact(void)
{
    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 1024);

    rbcglob_segment_t *seg = rbcglob_compile_segments(&arena, "file.txt");

    TEST_ASSERT_NOT_NULL(seg);

    // "file.txt" should be compiled as SEG_LITERAL because it has no special characters.
    if (seg->type == SEG_LITERAL)
    {
        TEST_ASSERT_EQUAL_STRING("file.txt", seg->data.literal);
    }
    else
    {
        // Fallback check if optimization strategy ever handles literals as Exact Matcher
        TEST_ASSERT_EQUAL_INT(SEG_WILDCARD, seg->type);
        TEST_ASSERT_EQUAL_INT(STRATEGY_EXACT, seg->data.glob.matcher.strategy);
        TEST_ASSERT_EQUAL_STRING("file.txt", seg->data.glob.matcher.pk.literal);
    }

    rbcglob_arena_destroy(&arena);
}

void test_compile_strategy_suffix(void)
{
    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 1024);

    rbcglob_segment_t *seg = rbcglob_compile_segments(&arena, "*.c");

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(SEG_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(STRATEGY_SUFFIX, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_STRING(".c", seg->data.glob.matcher.pk.affix.pattern);

    rbcglob_arena_destroy(&arena);
}

void test_compile_strategy_prefix(void)
{
    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 1024);

    rbcglob_segment_t *seg = rbcglob_compile_segments(&arena, "test_*");

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(SEG_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(STRATEGY_PREFIX, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_STRING("test_", seg->data.glob.matcher.pk.affix.pattern);

    rbcglob_arena_destroy(&arena);
}

void test_compile_strategy_infix(void)
{
    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 1024);

    rbcglob_segment_t *seg = rbcglob_compile_segments(&arena, "*foo*");

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(SEG_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(STRATEGY_INFIX, seg->data.glob.matcher.strategy);
    TEST_ASSERT_EQUAL_STRING("foo", seg->data.glob.matcher.pk.affix.pattern);

    rbcglob_arena_destroy(&arena);
}

void test_compile_strategy_sequence(void)
{
    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 1024);

    // "a*b" -> Sequence [a, b] (match_start=T, match_end=T)
    rbcglob_segment_t *seg = rbcglob_compile_segments(&arena, "a*b");

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(SEG_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(STRATEGY_SEQUENCE, seg->data.glob.matcher.strategy);
    TEST_ASSERT_TRUE(seg->data.glob.matcher.pk.seq.match_start);
    TEST_ASSERT_TRUE(seg->data.glob.matcher.pk.seq.match_end);
    TEST_ASSERT_EQUAL_INT(2, seg->data.glob.matcher.pk.seq.count);
    TEST_ASSERT_EQUAL_STRING("a", seg->data.glob.matcher.pk.seq.parts[0]);
    TEST_ASSERT_EQUAL_STRING("b", seg->data.glob.matcher.pk.seq.parts[1]);

    rbcglob_arena_destroy(&arena);
}

void test_compile_strategy_nfa_fallback(void)
{
    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 1024);

    // "?" should trigger NFA
    rbcglob_segment_t *seg = rbcglob_compile_segments(&arena, "?.c");

    TEST_ASSERT_NOT_NULL(seg);
    TEST_ASSERT_EQUAL_INT(SEG_WILDCARD, seg->type);
    TEST_ASSERT_EQUAL_INT(STRATEGY_VM, seg->data.glob.matcher.strategy);

    rbcglob_arena_destroy(&arena);
}
