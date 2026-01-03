#!/usr/bin/env python3
"""
Unityテストソース生成スクリプト

test_matrix.jsonに基づいてC言語のUnityテストコードを生成する。
"""

import json
import sys
from pathlib import Path


def convert_flags_to_c(flags_str):
    """フラグ文字列をC言語の定数に変換"""
    if flags_str == '0':
        return '0'

    # 複数フラグの組み合わせ対応（将来用）
    return flags_str


def convert_base_to_c(base_str):
    """baseパラメータをC言語表現に変換"""
    if base_str == 'NULL':
        return 'NULL'
    else:
        return f'"{base_str}"'


def split_patterns(pattern_str):
    """カンマ区切りのパターンを分割（ブレース内は無視）"""
    patterns = []
    current = []
    depth = 0

    for c in pattern_str:
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        elif c == ',' and depth == 0:
            patterns.append(''.join(current).strip())
            current = []
            continue
        current.append(c)

    if current:
        patterns.append(''.join(current).strip())

    return patterns


def escape_c_string(s):
    """C言語文字列リテラル用にエスケープ"""
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')


def generate_test_function(test_case, platform):
    """テストケースからC言語テスト関数を生成"""
    case_id = test_case['case_id']
    pattern = test_case['pattern']
    flags = convert_flags_to_c(test_case['flags'])
    base = convert_base_to_c(test_case['base'])
    sort = test_case['sort']

    # パターン分割
    patterns = split_patterns(pattern)
    npatterns = len(patterns)

    # パターン配列生成
    if npatterns == 1:
        pattern_array = f'(const char*[]){{\"{escape_c_string(patterns[0])}\"}}'
    else:
        pattern_list = ', '.join(f'"{escape_c_string(p)}"' for p in patterns)
        pattern_array = f'(const char*[]){{{pattern_list}}}'

    # 期待出力ファイルパス
    expected_file = f'tests/ruby_expected/{platform}/{case_id}.txt'

    # テスト関数生成
    code = f'''
void test_parity_{case_id}(void) {{
    char **result = NULL;
    size_t count = 0;

    // dirglob実行
    bool ok = dirglob({pattern_array}, {npatterns}, {flags}, {base}, {sort}, &result, &count);

    // 期待出力と比較
    assert_matches_expected(result, count, "{escape_c_string(expected_file)}");

    // メモリ解放
    dirglob_free(result, count);
}}
'''

    return code


def generate_test_file(test_cases, platform, output_file):
    """テストファイル全体を生成"""
    # ヘッダー
    header = '''/*
 * Auto-generated parity tests
 * DO NOT EDIT MANUALLY
 */

#include <unity.h>
#include <dirglob/dirglob.h>
#include "test_helpers.h"

'''

    # テスト関数生成
    test_functions = []
    for tc in test_cases:
        test_functions.append(generate_test_function(tc, platform))

    # ファイル書き込み
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(header)
        f.write('\n'.join(test_functions))

    print(f'  Generated: {output_file} ({len(test_cases)} tests)')


def main():
    """メインエントリポイント"""
    # パス設定
    script_dir = Path(__file__).parent
    test_dir = script_dir.parent
    build_dir = test_dir.parent / 'build'

    # test_matrix.json読み込み
    matrix_file = build_dir / 'test_matrix.json'
    if not matrix_file.exists():
        print(f'Error: {matrix_file} not found. Run gen_matrix.py first.', file=sys.stderr)
        return 1

    with open(matrix_file, 'r', encoding='utf-8') as f:
        test_cases = json.load(f)

    # プラットフォーム別にグループ化
    by_platform = {}
    for tc in test_cases:
        platform = tc['platform']
        if platform not in by_platform:
            by_platform[platform] = []
        by_platform[platform].append(tc)

    # 出力ディレクトリ
    generated_dir = build_dir / 'tests' / 'generated'
    generated_dir.mkdir(parents=True, exist_ok=True)

    # プラットフォームごとにテストファイル生成
    print('Generating C test files...')
    for platform, cases in by_platform.items():
        output_file = generated_dir / f'test_parity_{platform}.c'
        generate_test_file(cases, platform, output_file)

    print(f'\n=== C Test Files Generated ===')
    print(f'Total platforms: {len(by_platform)}')
    print(f'Total test cases: {len(test_cases)}')
    print(f'Output directory: {generated_dir}')

    return 0


if __name__ == '__main__':
    sys.exit(main())
