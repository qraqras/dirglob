#!/usr/bin/env python3
"""
Ruby期待出力生成スクリプト

test_matrix.jsonに基づいてRubyでDir.globを実行し、
期待される出力をファイルに保存する。
"""

import json
import subprocess
import sys
from pathlib import Path


def convert_flags_to_ruby(flags_str):
    """C言語のフラグ定数をRuby形式に変換"""
    if flags_str == '0':
        return '0'

    flag_map = {
        'FNM_NOESCAPE': 'File::FNM_NOESCAPE',
        'FNM_PATHNAME': 'File::FNM_PATHNAME',
        'FNM_CASEFOLD': 'File::FNM_CASEFOLD',
        'FNM_DOTMATCH': 'File::FNM_DOTMATCH',
        'FNM_EXTGLOB': 'File::FNM_EXTGLOB'
    }

    # 複数フラグの組み合わせ対応（将来用）
    flags = flags_str.split('|')
    ruby_flags = [flag_map.get(f.strip(), f.strip()) for f in flags]
    return ' | '.join(ruby_flags)


def split_patterns(pattern_str):
    """
    カンマ区切りのパターンを分割
    ブレース内のカンマは無視する
    """
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


def generate_ruby_code(test_case):
    """テストケースからRubyコードを生成"""
    pattern = test_case['pattern']
    flags = convert_flags_to_ruby(test_case['flags'])
    base = test_case['base']
    sort = test_case['sort']

    # パターン分割（複数パターン対応）
    patterns = split_patterns(pattern)

    # パターンをRuby配列またはスペース文字列として表現
    if len(patterns) == 1:
        pattern_arg = f'"{patterns[0]}"'
    else:
        pattern_list = ', '.join(f'"{p}"' for p in patterns)
        pattern_arg = f'[{pattern_list}]'

    # baseパラメータ
    # Note: 既にDir.chdir(fixtures_dir)しているため、
    # 'tests/fixtures'は'.'に変換する
    if base == 'NULL':
        base_arg = ''
    elif base == 'tests/fixtures':
        base_arg = ', base: "."'
    else:
        base_arg = f', base: "{base}"'

    # sortパラメータ
    sort_arg = f', sort: {"true" if sort == "1" else "false"}'

    # Dir.glob呼び出し
    code = f'Dir.glob({pattern_arg}, {flags}{base_arg}{sort_arg})'

    return code


def run_ruby_glob(test_case, fixtures_dir):
    """Rubyを実行してDir.globの結果を取得"""
    ruby_code = generate_ruby_code(test_case)
    full_code = f'''
require "fileutils"

begin
  # フィクスチャディレクトリに移動
  Dir.chdir("{fixtures_dir}")

  results = {ruby_code}
  puts results.join("\\n")
rescue => e
  STDERR.puts "Error: #{{e.message}}"
  exit 1
end
'''

    try:
        result = subprocess.run(
            ['ruby', '-e', full_code],
            capture_output=True,
            text=True,
            timeout=5
        )

        if result.returncode != 0:
            print(f'  Ruby error for {test_case["case_id"]}: {result.stderr}', file=sys.stderr)
            return None

        return result.stdout

    except subprocess.TimeoutExpired:
        print(f'  Timeout for {test_case["case_id"]}', file=sys.stderr)
        return None
    except FileNotFoundError:
        print('Error: Ruby not found. Please install Ruby.', file=sys.stderr)
        sys.exit(1)


def main():
    """メインエントリポイント"""
    # パス設定
    script_dir = Path(__file__).parent
    test_dir = script_dir.parent
    build_dir = test_dir.parent / 'build'
    fixtures_dir = test_dir / 'fixtures'

    # フィクスチャディレクトリ確認
    if not fixtures_dir.exists():
        print(f'Error: Fixtures directory not found: {fixtures_dir}', file=sys.stderr)
        print('Run create_fixtures.sh first.', file=sys.stderr)
        return 1

    # test_matrix.json読み込み
    matrix_file = build_dir / 'test_matrix.json'
    if not matrix_file.exists():
        print(f'Error: {matrix_file} not found. Run gen_matrix.py first.', file=sys.stderr)
        return 1

    with open(matrix_file, 'r', encoding='utf-8') as f:
        test_cases = json.load(f)

    # 出力ディレクトリ作成
    ruby_expected_dir = test_dir / 'ruby_expected'

    # プラットフォームディレクトリ
    platforms = set(tc['platform'] for tc in test_cases)
    for platform in platforms:
        platform_dir = ruby_expected_dir / platform
        platform_dir.mkdir(parents=True, exist_ok=True)

    # 各テストケースの期待出力を生成
    print(f'Generating Ruby expected outputs from {fixtures_dir}...')
    success_count = 0
    error_count = 0

    for i, test_case in enumerate(test_cases, 1):
        case_id = test_case['case_id']
        platform = test_case['platform']

        if i % 100 == 0:
            print(f'  Progress: {i}/{len(test_cases)}')

        # Ruby実行
        output = run_ruby_glob(test_case, fixtures_dir)

        if output is None:
            error_count += 1
            continue

        # ファイル保存
        output_file = ruby_expected_dir / platform / f'{case_id}.txt'
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(output)

        success_count += 1

    # 結果サマリー
    print(f'\n=== Ruby Expected Outputs Generated ===')
    print(f'Success: {success_count}/{len(test_cases)}')
    if error_count > 0:
        print(f'Errors:  {error_count}')
    print(f'Output directory: {ruby_expected_dir}')
    print(f'\nNext step: git add {ruby_expected_dir}')

    return 0 if error_count == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
