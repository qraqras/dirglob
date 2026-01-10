#include <rbcglob/rbcglob.h>
#include "../src/pattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>

// Copied from walker.c for benchmarking
static bool match_fixed(const char *text, const char *pat, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (pat[i] != '?' && pat[i] != text[i])
            return false;
    }
    return true;
}

static const char *search_fixed(const char *text, const char *pat, const char *end_limit)
{
    size_t pat_len = strlen(pat);
    if (pat_len == 0)
        return text;
    for (const char *p = text; p <= end_limit; p++)
    {
        if (match_fixed(p, pat, pat_len))
            return p;
    }
    return NULL;
}

static bool run_matcher(rbcg_matcher_t *m, const char *name)
{
    size_t name_len = strlen(name);
    bool matched = false;
    switch (m->strategy)
    {
    case RBCG_STRATEGY_EXACT:
        matched = (strcmp(name, m->pk.literal) == 0);
        break;
    case RBCG_STRATEGY_PREFIX:
        matched = (strncmp(name, m->pk.affix.pattern, m->pk.affix.len) == 0);
        break;
    case RBCG_STRATEGY_SUFFIX:
        if (name_len >= m->pk.affix.len)
            matched = (strcmp(name + name_len - m->pk.affix.len, m->pk.affix.pattern) == 0);
        break;
    case RBCG_STRATEGY_INFIX:
        matched = (strstr(name, m->pk.affix.pattern) != NULL);
        break;
    case RBCG_STRATEGY_PATTERN_CHAIN:
    {
        const char *p = name;
        const char *end_limit = name + name_len;
        matched = true;
        size_t count = m->pk.chain.count;
        if (m->pk.chain.match_end)
        {
            char *last = m->pk.chain.parts[count - 1];
            size_t last_len = strlen(last);
            if (name_len < last_len)
                matched = false;
            else if (!match_fixed(name + name_len - last_len, last, last_len))
                matched = false;
            else
            {
                end_limit -= last_len;
                count--;
            }
        }
        if (matched)
        {
            for (size_t i = 0; i < count; i++)
            {
                char *part = m->pk.chain.parts[i];
                size_t part_len = strlen(part);
                if (i == 0 && m->pk.chain.match_start)
                {
                    if (p + part_len > end_limit + (m->pk.chain.match_end ? 0 : 10000))
                    {
                        matched = false;
                        break;
                    }
                    if (name_len < part_len)
                    {
                        matched = false;
                        break;
                    }
                    if (!match_fixed(p, part, part_len))
                    {
                        matched = false;
                        break;
                    }
                    p += part_len;
                }
                else
                {
                    const char *found = search_fixed(p, part, end_limit - part_len);
                    if (!found)
                    {
                        matched = false;
                        break;
                    }
                    p = found + part_len;
                }
            }
            if (matched && m->pk.chain.match_end && p > end_limit)
                matched = false;
        }
    }
    break;
    case RBCG_STRATEGY_FNMATCH:
        matched = rbcglob_recursive_match(name, m->pk.fnmatch.pattern, 0);
        break;
    }
    return matched;
}

static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

void bench_pattern(const char *name, const char *pattern, const char *string, int iterations)
{
    printf("--- %s ---\n", name);
    printf("Pattern: %s\n", pattern);
    printf("String length: %zu\n", strlen(string));

    double start, end;

    // rbcglob (Direct Match - No Compilation Step)
    start = get_time_ms();
    int compiled_match = 0;
    for (int i = 0; i < iterations; i++)
    {
        if (rbcglob_recursive_match(string, pattern, 0))
            compiled_match++;
    }
    end = get_time_ms();
    printf("  rbcglob(direct):   %8.3f ms total (%d matches)\n", end - start, compiled_match);

    // rbcglob Compiled (Optimization Enabled)
    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 0);
    rbcglob_segment_t *seg = rbcglob_compile_segments(&arena, pattern);

    // Only benchmark if it compiled to a single wildcard segment (to match fnmatch semantics)
    if (seg && seg->type == RBCG_SEGMENT_WILDCARD)
    {
        start = get_time_ms();
        int opt_match = 0;
        for (int i = 0; i < iterations; i++)
        {
            if (run_matcher(&seg->data.glob.matcher, string))
                opt_match++;
        }
        end = get_time_ms();
        printf("  rbcglob(compiled): %8.3f ms total (%d matches)\n", end - start, opt_match);
    }
    else
    {
        printf("  rbcglob(compiled): Skipped (Complex/Multi-segment pattern)\n");
    }
    rbcglob_arena_destroy(&arena);

    // rbcglob_fnmatch (Oneshot - overhead included)
    start = get_time_ms();
    int rbc_match = 0;
    for (int i = 0; i < iterations; i++)
    {
        if (rbcglob_fnmatch(pattern, string, 0))
            rbc_match++;
    }
    end = get_time_ms();
    printf("  rbcglob(oneshot):  %8.3f ms total (%d matches)\n", end - start, rbc_match);

    // fnmatch(3)
    start = get_time_ms();
    int libc_match = 0;
    for (int i = 0; i < iterations; i++)
    {
        if (fnmatch(pattern, string, 0) == 0)
            libc_match++;
    }
    end = get_time_ms();
    printf("  fnmatch(3):      %8.3f ms total (%d matches)\n", end - start, libc_match);

    printf("\n");
}

int main(void)
{
    printf("=== Match Engine Benchmark: NFA vs Backtracking ===\n\n");

    // ケース1: 単純なパターン (差が出にくい)
    bench_pattern("Simple Case", "*.c", "very_long_file_name_for_testing_performance.c", 1000000);

    // ケース2: 多数のアスタリスク (バックトラックが発生しやすい)
    // rbcglobはこれを最適化して処理できるはず
    bench_pattern("Many Asterisks",
                  "a*b*c*d*e*f*g*",
                  "aaaaabbbbbcccccdddddeeeeefffffggggg",
                  1000000);

    // ケース3: Pathological Case (意地悪なケース)
    // 実装によっては指数関数的に遅くなる
    // *******a というパターンに対して、aで終わらない長い文字列を与える
    const char *evil_pattern = "*a*b*c*d*e*f*g*h*i*j*";
    const char *long_string = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    bench_pattern("Complex Wildcards", evil_pattern, long_string, 500000);

    // ケース4: 最悪ケース再現 (ReDoS的な挙動の確認)
    // rbcglobのNFAがO(N)で処理できるか確認
    const char *redos_pattern = "*****b";
    const char *redos_string = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    bench_pattern("ReDoS Attack Case", redos_pattern, redos_string, 10000);

    // ケース5: 現実的な拡張子マッチ (文字クラスを使用)
    // Rubyのfnmatchはブレース展開しないため、文字クラスで比較するのが公平
    bench_pattern("Extension Match", "*.[choas]", "rbcglob_test_file.c", 1000000);

    // ケース6: サブディレクトリ風パターンのシミュレーション (fnmatchとしての性能)
    bench_pattern("Path Match", "src/graph/*.c", "src/graph/compiler.c", 1000000);

    // ケース7: 前方一致 (Prefix)
    bench_pattern("Prefix Match", "test_*", "test_rbcglob_basic", 1000000);

    // ケース8: 部分一致 (Infix)
    bench_pattern("Infix Match", "*glob*", "librbcglob.a", 1000000);

    return 0;
}
