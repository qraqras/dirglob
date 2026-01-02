#!/usr/bin/env python3
"""
テストマトリックス生成スクリプト

directories.txt × files.txt × options.txt の3次元マトリックスから
テストケース一覧を生成する。
"""

import csv
import json
import platform
import sys
from pathlib import Path


def get_platform():
    """プラットフォーム識別と区切り文字を返す"""
    system = platform.system()
    if system == 'Windows':
        return 'windows', '\\'
    else:
        return 'linux', '/'


def load_tsv(filepath):
    """TSVファイルを辞書のリストとして読み込み"""
    with open(filepath, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f, delimiter='\t')
        return list(reader)


def combine(part1, part2, separator):
    """2つのパス要素を結合"""
    if part1 == '':
        return part2
    else:
        return part1 + separator + part2


def generate_option_name(flags, base, sort):
    """オプション組み合わせから識別名を生成"""
    flag_name = flags if flags != '0' else 'none'
    base_name = base if base != 'NULL' else 'null'
    sort_name = 'sorted' if sort == '1' else 'unsorted'
    return f'{flag_name}_{base_name}_{sort_name}'


def generate_matrix(test_dir):
    """3次元マトリックス生成"""
    platform_name, sep = get_platform()

    # TSVファイル読み込み
    directories = load_tsv(test_dir / 'directories.txt')
    files = load_tsv(test_dir / 'files.txt')
    options = load_tsv(test_dir / 'options.txt')

    test_cases = []
    case_id = 1

    # 1. ファイル単体パターン (dir.pattern == '')
    empty_dir = next(d for d in directories if d['pattern'] == '')
    for file in files:
        for opt in options:
            test_cases.append({
                'case_id': f'p{case_id:04d}',
                'platform': 'common',
                'pattern': file['pattern'],
                'dir_id': empty_dir['id'],
                'file_id': file['id'],
                'file_pattern': file['pattern'],
                'option_id': opt['id'],
                'option_name': generate_option_name(opt['flags'], opt['base'], opt['sort']),
                'flags': opt['flags'],
                'base': opt['base'],
                'sort': opt['sort'],
                'combination_type': 'file_only'
            })
            case_id += 1

    # 2. dir×file組み合わせ (dir.pattern != '', can_nest=1)
    for directory in directories:
        if directory['pattern'] == '':
            continue
        for file in files:
            pattern = combine(directory['pattern'], file['pattern'], sep)
            for opt in options:
                test_cases.append({
                    'case_id': f'p{case_id:04d}',
                    'platform': platform_name,
                    'pattern': pattern,
                    'dir_id': directory['id'],
                    'dir_pattern': directory['pattern'],
                    'dir_type': directory['type'],
                    'file_id': file['id'],
                    'file_pattern': file['pattern'],
                    'file_type': file['type'],
                    'option_id': opt['id'],
                    'option_name': generate_option_name(opt['flags'], opt['base'], opt['sort']),
                    'flags': opt['flags'],
                    'base': opt['base'],
                    'sort': opt['sort'],
                    'combination_type': 'dir_file'
                })
                case_id += 1

    # 3. dir×dir組み合わせ (can_nest=1同士)
    nestable_dirs = [d for d in directories
                     if d['pattern'] != '' and d.get('can_nest') == '1']
    for dir1 in nestable_dirs:
        for dir2 in nestable_dirs:
            pattern = combine(dir1['pattern'], dir2['pattern'], sep)
            for opt in options:
                test_cases.append({
                    'case_id': f'p{case_id:04d}',
                    'platform': platform_name,
                    'pattern': pattern,
                    'dir1_id': dir1['id'],
                    'dir1_pattern': dir1['pattern'],
                    'dir1_type': dir1['type'],
                    'dir2_id': dir2['id'],
                    'dir2_pattern': dir2['pattern'],
                    'dir2_type': dir2['type'],
                    'option_id': opt['id'],
                    'option_name': generate_option_name(opt['flags'], opt['base'], opt['sort']),
                    'flags': opt['flags'],
                    'base': opt['base'],
                    'sort': opt['sort'],
                    'combination_type': 'dir_dir'
                })
                case_id += 1

    return test_cases, platform_name


def main():
    """メインエントリポイント"""
    # パス設定
    script_dir = Path(__file__).parent
    test_dir = script_dir.parent
    build_dir = test_dir.parent / 'build'
    build_dir.mkdir(exist_ok=True)

    # マトリックス生成
    print('Generating test matrix...')
    test_cases, platform_name = generate_matrix(test_dir)

    # JSON出力
    output_file = build_dir / 'test_matrix.json'
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(test_cases, f, indent=2, ensure_ascii=False)

    # 統計情報
    by_type = {}
    for case in test_cases:
        ctype = case['combination_type']
        by_type[ctype] = by_type.get(ctype, 0) + 1

    print(f'\n=== Test Matrix Generated ===')
    print(f'Platform: {platform_name}')
    print(f'Total test cases: {len(test_cases)}')
    print(f'\nBreakdown by combination type:')
    for ctype, count in sorted(by_type.items()):
        print(f'  {ctype:12s}: {count:5d} cases')
    print(f'\nOutput: {output_file}')

    return 0


if __name__ == '__main__':
    sys.exit(main())
