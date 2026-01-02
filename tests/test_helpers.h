/*
 * Test helper functions for dirglob
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <stddef.h>
#include <stdbool.h>

/**
 * 期待出力ファイルを読み込んで配列として返す
 * @param filepath 期待出力ファイルのパス
 * @param count 出力される行数
 * @return 行の配列（NULLで失敗）、呼び出し側でfree必要
 */
char **load_expected_output(const char *filepath, size_t *count);

/**
 * C実装の結果と期待出力を比較してアサート
 * @param c_result C実装の結果配列
 * @param c_count C実装の結果数
 * @param expected_file 期待出力ファイルのパス
 */
void assert_matches_expected(char **c_result, size_t c_count, const char *expected_file);

/**
 * 2つの文字列配列を比較してアサート
 * @param actual 実際の結果
 * @param actual_count 実際の結果数
 * @param expected 期待される結果
 * @param expected_count 期待される結果数
 */
void assert_result_equals(char **actual, size_t actual_count,
                         const char **expected, size_t expected_count);

/**
 * テストフィクスチャをセットアップ
 * テスト開始時に呼ばれる
 */
void setup_test_fixtures(void);

/**
 * テストフィクスチャをクリーンアップ
 * テスト終了時に呼ばれる
 */
void cleanup_test_fixtures(void);

/**
 * 文字列配列を解放
 * @param arr 配列
 * @param count 要素数
 */
void free_string_array(char **arr, size_t count);

#endif /* TEST_HELPERS_H */
