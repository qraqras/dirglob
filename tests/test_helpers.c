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
            TEST_FAIL_MESSAGE("Expected empty result");
        }
        return;
    }

    // 結果を比較
    assert_result_equals(c_result, c_count, (const char **)expected, expected_count);

    // 期待出力を解放
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
