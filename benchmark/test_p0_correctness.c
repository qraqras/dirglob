#include <rbcglob/rbcglob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int test_pattern(const char *pattern, const char *description, size_t expected_min)
{
    char **results = NULL;
    size_t *lengths = NULL;
    size_t count = 0;
    const char *patterns[] = {pattern};

    printf("Testing: %s - %s\n", pattern, description);

    bool success = dirglob(patterns, 1, RBCGLOB_FNM_DOTMATCH, NULL, 1, &results, &count, &lengths);

    if (!success)
    {
        printf("  ❌ FAILED: dirglob returned false\n");
        return 1;
    }

    printf("  ✓ Matched %zu files", count);
    if (expected_min > 0 && count < expected_min)
    {
        printf(" (expected at least %zu) ❌\n", expected_min);
        rbcglob_free(results, count, lengths);
        return 1;
    }
    printf(" ✓\n");

    // サンプル表示
    if (count > 0)
    {
        printf("  Sample: %s\n", results[0]);
        if (count > 1)
        {
            printf("  Sample: %s\n", results[count - 1]);
        }
    }

    rbcglob_free(results, count, lengths);
    return 0;
}

int main(void)
{
    int failures = 0;

    printf("=== P0 Optimization Correctness Tests ===\n\n");

    // リテラルパターン
    failures += test_pattern("README.md", "Exact literal match", 1);
    failures += test_pattern("*.md", "Literal suffix", 1);
    failures += test_pattern("tests/*.c", "Literal prefix + suffix", 3);

    // 再帰パターン
    failures += test_pattern("tests/**/*.c", "Recursive with suffix", 5);
    failures += test_pattern("tests/**/*", "Recursive all", 100);

    // 隠しファイルチェック
    failures += test_pattern(".*", "Hidden files (with DOTMATCH)", 0);

    printf("\n=== Summary ===\n");
    if (failures == 0)
    {
        printf("✓ All tests passed!\n");
        return 0;
    }
    else
    {
        printf("❌ %d test(s) failed\n", failures);
        return 1;
    }
}
