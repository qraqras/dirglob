# ソート順不一致問題の調査結果

## 問題の概要

glob互換テストで264件のソート順不一致エラーが発生しています。

```
4515 Tests 264 Failures 0 Ignored
```

## 問題の詳細

### 1. ブレース展開パターンのソート順

**失敗例:**
- パターン: `05_braceexpansion/{dir1,}/file1.txt`
- Ruby期待値: `05_braceexpansion/dir1/file1.txt` → `05_braceexpansion//file1.txt`
- C実装結果: `05_braceexpansion//file1.txt` → `05_braceexpansion/dir1/file1.txt`

### 2. 原因分析

#### C実装の動作(現在)
```c
// 1. ブレース展開
{dir1,} → ["dir1", ""]

// 2. 各パターンでglob実行して結果を収集
results = [
    "05_braceexpansion/dir1/file1.txt",  // パターン "dir1" の結果
    "05_braceexpansion//file1.txt"        // パターン "" の結果
]

// 3. 全体をstrcmpでソート
qsort(results) → [
    "05_braceexpansion//file1.txt",       // '/' < 'd' なので先
    "05_braceexpansion/dir1/file1.txt"
]
```

#### Ruby MRIの動作(期待値)
```ruby
# 1. ブレース展開
{dir1,} → ["dir1", ""]

# 2. 各パターンでglob実行
pattern1_results = ["05_braceexpansion/dir1/file1.txt"]  # "dir1" の結果
pattern2_results = ["05_braceexpansion//file1.txt"]      # "" の結果

# 3. 各パターンの結果を個別にソート(既にソート済み)
pattern1_results.sort!  # 変化なし
pattern2_results.sort!  # 変化なし

# 4. ブレース展開の順序で連結
results = pattern1_results + pattern2_results
# => ["05_braceexpansion/dir1/file1.txt", "05_braceexpansion//file1.txt"]
```

### 3. 実験による検証

```bash
cd /workspaces/dirglob/tests/fixtures

# ブレース展開の順序によって結果が変わる
ruby -e "puts Dir.glob('05_braceexpansion/{dir1,}/file1.txt', sort: true)"
# => 05_braceexpansion/dir1/file1.txt
# => 05_braceexpansion//file1.txt

ruby -e "puts Dir.glob('05_braceexpansion/{,dir1}/file1.txt', sort: true)"
# => 05_braceexpansion//file1.txt
# => 05_braceexpansion/dir1/file1.txt

# 通常のstrcmpソート
printf '%s\n' '05_braceexpansion/dir1/file1.txt' '05_braceexpansion//file1.txt' | sort
# => 05_braceexpansion//file1.txt
# => 05_braceexpansion/dir1/file1.txt
```

## Rubyの仕様

**RubyのDir.globの`sort: true`は以下の動作をする:**

1. ブレース展開を行う（左から右の順序を保持）
2. 各展開されたパターンごとに個別にglob実行
3. 各パターンの結果を個別にソート
4. ソートされた結果をブレース展開の順序で連結

つまり、**ブレース展開の順序 > ソート順** という優先度です。

## 必要な修正

現在の実装:
```c
// glob.c:1100-1104
rbc_brace_expand(normalized_pattern, rbc_glob_brace_cb, &cb_ctx);

// Sort results if requested
if (sort)
    rbc_glob_results_sort(&results);  // ← 全体を一度にソート
```

修正案:
```c
// 各ブレース展開されたパターンごとにソートする必要がある
// 方法1: コールバック関数内でソート（各パターンの結果範囲を記録）
// 方法2: ブレース展開ごとに個別のresultsを作成して後で連結
```

### 実装方針

1. **方法A: 範囲記録方式**
   - 各`rbc_glob_brace_cb`呼び出し前に現在の`results.count`を記録
   - 呼び出し後、追加された範囲だけをソート
   - 実装が複雑になる可能性

2. **方法B: 個別results方式**（推奨）
   - 各ブレース展開パターンごとに個別の`rbc_results_t`を作成
   - 各結果を個別にソート
   - 最後に順番に連結
   - メモリ使用量は増えるが、ロジックが明確

## 影響範囲

- 影響するテスト数: 約264件
- 主に`05_braceexpansion`ディレクトリのテストと`**/.*`パターンのテスト
- コンテンツは一致しているため、ソート順のみの問題

## 次のアクション

1. [src/core/glob.c](src/core/glob.c)の`rbc_glob`関数を修正
2. ブレース展開ごとにソートする仕組みを実装
3. テストを再実行して264件の失敗が解消されることを確認
