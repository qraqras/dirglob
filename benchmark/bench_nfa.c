#include <rbcglob/rbcglob.h>
#include "rbcglob/internal/pattern.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>

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
        if (rbcglob_vm_match(string, pattern, 0))
            compiled_match++;
    }
    end = get_time_ms();
    printf("  rbcglob(direct):   %8.3f ms total (%d matches)\n", end - start, compiled_match);

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
