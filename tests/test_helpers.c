/*
 * Test helper function implementations
 */

#include "test_helpers.h"
#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LENGTH 4096

void free_string_array(char **arr, size_t count)
{
    if (arr == NULL)
        return;

    for (size_t i = 0; i < count; i++)
    {
        free(arr[i]);
    }
    free(arr);
}

char **load_expected_output(const char *filepath, size_t *count)
{
    FILE *fp = fopen(filepath, "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Failed to open expected output file: %s\n", filepath);
        *count = 0;
        return NULL;
    }

    // 行数をカウント
    size_t line_count = 0;
    char buffer[MAX_LINE_LENGTH];
    while (fgets(buffer, sizeof(buffer), fp) != NULL)
    {
        line_count++;
    }

    // ファイルの先頭に戻る
    rewind(fp);

    // メモリ割り当て
    char **lines = NULL;
    if (line_count > 0)
    {
        lines = malloc(sizeof(char *) * line_count);
        if (lines == NULL)
        {
            fclose(fp);
            *count = 0;
            return NULL;
        }
    }

    // 各行を読み込み
    size_t idx = 0;
    while (fgets(buffer, sizeof(buffer), fp) != NULL && idx < line_count)
    {
        // 改行文字を除去
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n')
        {
            buffer[len - 1] = '\0';
            len--;
        }
        if (len > 0 && buffer[len - 1] == '\r')
        {
            buffer[len - 1] = '\0';
            len--;
        }

        // 空行のみのファイル（改行のみ）は空の配列として扱う
        if (line_count == 1 && len == 0)
        {
            free(lines);
            fclose(fp);
            *count = 0;
            return NULL;
        }

        // 行をコピー
        lines[idx] = strdup(buffer);
        if (lines[idx] == NULL)
        {
            // メモリ不足：既に割り当てたメモリを解放
            for (size_t i = 0; i < idx; i++)
            {
                free(lines[i]);
            }
            free(lines);
            fclose(fp);
            *count = 0;
            return NULL;
        }
        idx++;
    }

    fclose(fp);
    *count = line_count;
    return lines;
}

void assert_result_equals(char **actual, size_t actual_count,
                          const char **expected, size_t expected_count)
{
    // 件数チェック
    if (actual_count != expected_count)
    {
        fprintf(stderr, "\nResult count mismatch:\n");
        fprintf(stderr, "  Expected: %zu\n", expected_count);
        fprintf(stderr, "  Actual:   %zu\n", actual_count);

        fprintf(stderr, "\nExpected results:\n");
        for (size_t i = 0; i < expected_count; i++)
        {
            fprintf(stderr, "  [%zu] %s\n", i, expected[i]);
        }

        fprintf(stderr, "\nActual results:\n");
        for (size_t i = 0; i < actual_count; i++)
        {
            fprintf(stderr, "  [%zu] %s\n", i, actual[i]);
        }

        TEST_FAIL_MESSAGE("Result count mismatch");
    }

    // 各要素を比較
    for (size_t i = 0; i < actual_count; i++)
    {
        if (strcmp(actual[i], expected[i]) != 0)
        {
            fprintf(stderr, "\nResult mismatch at index %zu:\n", i);
            fprintf(stderr, "  Expected: \"%s\"\n", expected[i]);
            fprintf(stderr, "  Actual:   \"%s\"\n", actual[i]);
            TEST_FAIL_MESSAGE("Result content mismatch");
        }
    }
}

static int compare_strings(const void *a, const void *b)
{
    const char *sa = *(const char **)a;
    const char *sb = *(const char **)b;
    return strcmp(sa, sb);
}

void assert_result_equals_any_order(char **actual, size_t actual_count, const char **expected, size_t expected_count)
{
    // Check count first
    if (actual_count != expected_count)
    {
        fprintf(stderr, "\nResult count mismatch:\n");
        fprintf(stderr, "  Expected: %zu\n", expected_count);
        fprintf(stderr, "  Actual:   %zu\n", actual_count);

        // Print content for debugging
        fprintf(stderr, "Actual results:\n");
        for (size_t i = 0; i < actual_count; i++)
        {
            fprintf(stderr, "  [%zu] %s\n", i, actual[i]);
        }
        fprintf(stderr, "Expected results (partial):\n");
        for (size_t i = 0; i < expected_count && i < 20; i++)
        {
            fprintf(stderr, "  [%zu] %s\n", i, expected[i]);
        }
        if (expected_count > 20)
            fprintf(stderr, "  ... (%zu more)\n", expected_count - 20);

        TEST_FAIL_MESSAGE("Result count mismatch");
    }

    if (actual_count == 0)
        return;

    // Create copies for sorting
    char **actual_sorted = malloc(actual_count * sizeof(char *));
    char **expected_sorted = malloc(expected_count * sizeof(char *));

    if (!actual_sorted || !expected_sorted)
    {
        free(actual_sorted);
        free(expected_sorted);
        TEST_FAIL_MESSAGE("Memory allocation failed in test helper");
    }

    memcpy(actual_sorted, actual, actual_count * sizeof(char *));
    memcpy(expected_sorted, expected, expected_count * sizeof(char *));

    qsort(actual_sorted, actual_count, sizeof(char *), compare_strings);
    qsort(expected_sorted, expected_count, sizeof(char *), compare_strings);

    // Compare sorted arrays
    for (size_t i = 0; i < actual_count; i++)
    {
        if (strcmp(actual_sorted[i], expected_sorted[i]) != 0)
        {
            fprintf(stderr, "\nResult mismatch (any order) at sorted index %zu:\n", i);
            fprintf(stderr, "  Expected: \"%s\"\n", expected_sorted[i]);
            fprintf(stderr, "  Actual:   \"%s\"\n", actual_sorted[i]);

            fprintf(stderr, "\nFull unsorted actual:\n");
            for (size_t k = 0; k < actual_count; k++)
                fprintf(stderr, " %s\n", actual[k]);
            fprintf(stderr, "\nFull unsorted expected:\n");
            for (size_t k = 0; k < expected_count; k++)
                fprintf(stderr, " %s\n", expected[k]);

            free(actual_sorted);
            free(expected_sorted);
            TEST_FAIL_MESSAGE("Result content mismatch (even after sorting)");
        }
    }

    free(actual_sorted);
    free(expected_sorted);
}

void assert_matches_expected(char **c_result, size_t c_count, const char *expected_file)
{
    size_t expected_count = 0;
    char **expected = load_expected_output(expected_file, &expected_count);

    if (expected == NULL && expected_count == 0)
    {
        // 空の期待出力（ファイルが存在しないか空）
        if (c_count != 0)
        {
            fprintf(stderr, "\nExpected empty result, but got %zu items\n", c_count);
            fprintf(stderr, "Expected file: %s\n", expected_file);
            for (size_t i = 0; i < c_count && i < 5; i++)
            {
                fprintf(stderr, "  Result[%zu]: %s\n", i, c_result[i]);
            }
            TEST_FAIL_MESSAGE("Expected empty result");
        }
        return;
    }

    // 結果を比較
    assert_result_equals(c_result, c_count, (const char **)expected, expected_count);

    // 期待出力を解放
    free_string_array(expected, expected_count);
}

void assert_matches_expected_any_order(char **c_result, size_t c_count, const char *expected_file)
{
    size_t expected_count = 0;
    char **expected = load_expected_output(expected_file, &expected_count);

    if (expected == NULL && expected_count == 0)
    {
        if (c_count != 0)
        {
            TEST_FAIL_MESSAGE("Expected empty result");
        }
        return;
    }

    assert_result_equals_any_order(c_result, c_count, (const char **)expected, expected_count); // Use the unsorted version
    free_string_array(expected, expected_count);
}

void setup_test_fixtures(void)
{
    // 必要に応じてフィクスチャディレクトリを作成
    // 現時点では何もしない
}

void cleanup_test_fixtures(void)
{
    // 必要に応じてフィクスチャをクリーンアップ
    // 現時点では何もしない
}
