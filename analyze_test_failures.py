#!/usr/bin/env python3
"""
globテストの失敗を分析するスクリプト
"""
import re
from collections import defaultdict, Counter

def analyze_failures(filename):
    with open(filename, 'r') as f:
        content = f.read()

    # 失敗パターンを抽出
    failures = []

    # パターン1: カウント不一致
    count_pattern = re.compile(r'Pattern: (.*?), Expected: (\d+), Got: (\d+)')
    for match in count_pattern.finditer(content):
        pattern, expected, got = match.groups()
        failures.append({
            'type': 'count_mismatch',
            'pattern': pattern,
            'expected': int(expected),
            'got': int(got),
            'diff': int(got) - int(expected)
        })

    # パターン2: 内容不一致
    content_pattern = re.compile(r"Pattern: (.*?), Index: (\d+), Expected: (.*?), Got: (.*?)(?:\n|$)")
    for match in content_pattern.finditer(content):
        pattern, index, expected, got = match.groups()
        failures.append({
            'type': 'content_mismatch',
            'pattern': pattern,
            'index': int(index),
            'expected': expected,
            'got': got
        })

    return failures

def main():
    failures = analyze_failures('/tmp/test_results.txt')

    print(f"=== テスト失敗分析 ===")
    print(f"総失敗数: {len(failures)}\n")

    # カウント不一致の分析
    count_mismatches = [f for f in failures if f['type'] == 'count_mismatch']
    print(f"## カウント不一致: {len(count_mismatches)}件")

    # パターン別集計
    pattern_stats = defaultdict(lambda: {'count': 0, 'total_diff': 0, 'cases': []})
    for f in count_mismatches:
        pattern = f['pattern']
        pattern_stats[pattern]['count'] += 1
        pattern_stats[pattern]['total_diff'] += f['diff']
        pattern_stats[pattern]['cases'].append((f['expected'], f['got']))

    print("\n### パターン別カウント不一致:")
    for pattern in sorted(pattern_stats.keys(), key=lambda p: pattern_stats[p]['count'], reverse=True)[:20]:
        stats = pattern_stats[pattern]
        avg_diff = stats['total_diff'] / stats['count']
        # サンプルを取得
        sample = stats['cases'][0]
        print(f"  {pattern:30s}: {stats['count']:4d}件 (平均差: {avg_diff:+.1f}, 例: 期待{sample[0]} 実際{sample[1]})")

    # 内容不一致の分析
    content_mismatches = [f for f in failures if f['type'] == 'content_mismatch']
    print(f"\n## 内容不一致: {len(content_mismatches)}件")

    # パターン別集計
    content_pattern_stats = defaultdict(lambda: {'count': 0, 'examples': []})
    for f in content_mismatches:
        pattern = f['pattern']
        content_pattern_stats[pattern]['count'] += 1
        if len(content_pattern_stats[pattern]['examples']) < 3:
            content_pattern_stats[pattern]['examples'].append({
                'index': f['index'],
                'expected': f['expected'],
                'got': f['got']
            })

    print("\n### パターン別内容不一致:")
    for pattern in sorted(content_pattern_stats.keys(), key=lambda p: content_pattern_stats[p]['count'], reverse=True)[:20]:
        stats = content_pattern_stats[pattern]
        print(f"  {pattern:30s}: {stats['count']:4d}件")
        for ex in stats['examples'][:2]:
            print(f"    Index {ex['index']:2d}: 期待 '{ex['expected']}' 実際 '{ex['got']}'")

    # 問題の傾向分析
    print("\n## 傾向分析:")

    # ドット関連
    dot_issues = [f for f in count_mismatches if '.' in f['pattern'] or '*' in f['pattern']]
    print(f"  ドット/アスタリスク関連: {len(dot_issues)}件")

    # ダブルスター関連
    doublestar_issues = [f for f in count_mismatches if '**' in f['pattern']]
    print(f"  ダブルスター(**): {len(doublestar_issues)}件")

    # スラッシュ終端
    trailing_slash = [f for f in count_mismatches if f['pattern'].endswith('/')]
    print(f"  スラッシュ終端: {len(trailing_slash)}件")

    # 差分の分布
    diff_distribution = Counter([f['diff'] for f in count_mismatches])
    print(f"\n  カウント差分の分布:")
    for diff in sorted(diff_distribution.keys()):
        if diff != 0:
            print(f"    差{diff:+3d}: {diff_distribution[diff]:4d}件")

    # 特徴的な失敗パターン
    print("\n## 特徴的な失敗パターン:")

    # .や..が含まれる問題
    dot_dir_issues = [f for f in content_mismatches if '..' in f['expected'] or '/.' in f['expected']]
    if dot_dir_issues:
        print(f"  . または .. ディレクトリ関連: {len(dot_dir_issues)}件")
        for ex in dot_dir_issues[:3]:
            print(f"    Pattern: {ex['pattern']}, 期待 '{ex['expected']}' 実際 '{ex['got']}'")

    # DOTMATCH関連（.*/.* パターン）
    dotmatch_issues = [f for f in count_mismatches if f['pattern'].startswith('.*')]
    if dotmatch_issues:
        print(f"\n  ドットマッチ(.*)関連: {len(dotmatch_issues)}件")
        sample = dotmatch_issues[0]
        print(f"    例: パターン '{sample['pattern']}', 期待{sample['expected']} 実際{sample['got']} (差{sample['diff']:+d})")

if __name__ == '__main__':
    main()
