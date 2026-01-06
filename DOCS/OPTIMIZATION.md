# Optimization Strategy for rbcglob

## 概要
このドキュメントは、Ruby Dir.glob互換性を保ちながら高速化を実現する最適化方針を示します。

## 優先度付け原則
1. **測定第一**: ベンチマークで効果を確認してから実装
2. **互換性優先**: Ruby 4.0 Dir.globの動作を完全に再現
3. **段階的実装**: 小さな最適化から積み上げ

---

## Phase 1: 基礎最適化（必須・高効果）

### 1.1 リテラルセグメント高速パス
**効果**: 30-50%高速化（パターンの60%はリテラルを含む）

```c
// 実装箇所: rbcglob_segment_t
if (segment->type == RBCGLOB_SEGMENT_LITERAL) {
    // strcmp()による直接比較（トークンマッチング不要）
    if (strcmp(segment->pattern, filename) == 0) {
        return MATCH;
    }
    return NO_MATCH;
}
```

**ベンチマーク対象**:
- `src/foo/bar.c` - リテラルのみ
- `src/*/bar.c` - 1つだけワイルドカード
- `**/*.c` - 再帰のみ

---

### 1.2 Prefix/Suffix最適化（設計済み）
**効果**: 20-40%高速化（ワイルドカードパターン）

```c
// 実装箇所: rbcglob_segment_t
typedef struct rbcglob_segment_s {
    char *prefix;      // "test_*.c" → "test_"
    size_t prefix_len;
    char *suffix;      // "test_*.c" → ".c"
    size_t suffix_len;
} rbcglob_segment_t;

// マッチング最適化:
if (prefix_len > 0 && memcmp(filename, prefix, prefix_len) != 0) {
    return NO_MATCH;  // 早期リターン
}
if (suffix_len > 0) {
    size_t flen = strlen(filename);
    if (flen < suffix_len || memcmp(filename + flen - suffix_len, suffix, suffix_len) != 0) {
        return NO_MATCH;
    }
}
// ここまで来たら中間部分のみトークンマッチング
```

**実装タスク**:
- [ ] コンパイラ: パターン解析でprefix/suffixを抽出
- [ ] マッチャー: 上記の早期リターンを実装

---

### 1.3 ディレクトリ走査の枝刈り
**効果**: 50-90%高速化（大規模プロジェクト）

```c
// 最適化1: 先頭セグメントがリテラル → 直接移動
// "src/foo/*.c" → srcディレクトリに直接移動（全体を走査しない）

// 最適化2: ** がない場合の深さ制限
// "*.c" → 深さ1のみ
// "*/*.c" → 深さ2のみ
int max_depth = count_non_recursive_segments(pattern);
if (!has_recursive_segment) {
    if (current_depth > max_depth) return;  // 枝刈り
}

// 最適化3: 隠しファイル/ディレクトリのスキップ
if (!(flags & RBCGLOB_FNM_DOTMATCH) && filename[0] == '.') {
    continue;  // .git, .vscode などをスキップ
}
```

**実装箇所**: `traverse.c`

---

## Phase 2: メモリ最適化（中効果・実装容易）

### 2.1 結果配列の事前確保
**効果**: メモリアロケーション回数を90%削減

```c
// 現在: realloc()を繰り返す → 遅い
// 改善: 初期容量を確保してから拡張

#define INITIAL_RESULT_CAPACITY 64

char **results = malloc(INITIAL_RESULT_CAPACITY * sizeof(char*));
size_t capacity = INITIAL_RESULT_CAPACITY;
size_t count = 0;

if (count >= capacity) {
    capacity *= 2;  // 指数的拡張
    results = realloc(results, capacity * sizeof(char*));
}
```

---

### 2.2 Small String Optimization (SSO)
**効果**: 短いパス名で20-30%高速化

```c
// パス名の80%は64バイト以下 → スタック確保
#define PATH_BUFFER_SIZE 256

char path_buffer[PATH_BUFFER_SIZE];
char *path = path_buffer;  // 通常はスタック
if (needed_size > PATH_BUFFER_SIZE) {
    path = malloc(needed_size);  // 大きい場合のみヒープ
}
```

---

### 2.3 文字列プール（将来拡張）
**効果**: メモリ使用量を30-50%削減

```c
// 重複するパス成分を共有
// "src/foo/a.c", "src/foo/b.c" → "src/foo/"を共有
```

**優先度**: 低（実装コストが高い）

---

## Phase 3: アルゴリズム最適化（高効果・実装難）

### 3.1 文字クラスのビットマップ化
**効果**: `[a-z]`マッチングを10倍高速化

```c
// 現在: ループで範囲チェック
for (i = 0; i < range_count; i++) {
    if (c >= ranges[i].start && c <= ranges[i].end) return true;
}

// 最適化: ビットマップ（256bit = 32byte）
typedef struct {
    uint32_t bitmap[8];  // 256 bits
} char_class_t;

// コンパイル時にビットマップ生成
for (c = 'a'; c <= 'z'; c++) {
    bitmap[c / 32] |= (1U << (c % 32));
}

// マッチング時: O(1)
return (bitmap[c / 32] & (1U << (c % 32))) != 0;
```

**実装箇所**: `compiler.c`, `fnmatch.c`

---

### 3.2 ブレース展開の重複削除
**効果**: `*.{c,c,h}`のような入力で無駄を削減

```c
// 展開後に重複を削除
// ["*.c", "*.c", "*.h"] → ["*.c", "*.h"]

// ハッシュセットで重複チェック
// 実装: qsort() + 隣接重複削除
```

**優先度**: 低（実用上レアケース）

---

### 3.3 複数パターンの並列マッチング（Aho-Corasick/Trie）
**効果**: パターン数が10個以上で3-5倍高速化

**条件**: ブレース展開でパターン数 >= 10

```c
// 現在: O(N*M) - N=ファイル数, M=パターン数
for (file in files) {
    for (pattern in patterns) {
        if (match(pattern, file)) add_result();
    }
}

// 最適化: O(N*L) - L=ファイル名の長さ
trie_t *trie = build_trie(patterns);
for (file in files) {
    trie_match_all(trie, file);  // 1回の走査で全パターンチェック
}
```

**実装判断**: ベンチマークで `pattern_count >= 10` が頻出する場合のみ

---

## Phase 4: システム最適化（環境依存）

### 4.1 システムコール削減
**効果**: 大規模ディレクトリで30-50%高速化

```c
// Linux: getdents64() を直接使用（libc opendir()より高速）
// macOS: getattrlistbulk() でメタデータを一括取得
// Windows: FindFirstFileEx() with FIND_FIRST_EX_LARGE_FETCH
```

---

### 4.2 SIMD文字列比較（AVX2/NEON）
**効果**: 長いリテラルで2-3倍高速化

```c
#ifdef __AVX2__
// AVX2でprefixを32バイト単位で比較
__m256i pattern_vec = _mm256_loadu_si256((__m256i*)prefix);
__m256i filename_vec = _mm256_loadu_si256((__m256i*)filename);
__m256i cmp = _mm256_cmpeq_epi8(pattern_vec, filename_vec);
int mask = _mm256_movemask_epi8(cmp);
if (mask != 0xFFFFFFFF) return NO_MATCH;
#endif
```

**実装判断**: プロファイリングで文字列比較がボトルネックの場合のみ

---

### 4.3 並列ディレクトリ走査（マルチスレッド）
**効果**: CPUコア数に応じて線形スケール

```c
// ディレクトリごとにスレッドプール
// 例: src/, tests/, docs/ を並列処理
```

**優先度**: 最低（実装複雑度が非常に高い、Ruby互換性の検証が必要）

---

## Phase 5: キャッシング（将来拡張）

### 5.1 readdir()結果のキャッシュ
**効果**: 同じディレクトリで複数パターン実行時に90%高速化

```c
// 同一ディレクトリの複数glob実行でキャッシュを再利用
// Dir.glob(["*.c", "*.h"]) → 1回のreaddir()で済む
```

**実装**: グローバルキャッシュ（TTL付き）

---

## 実装優先順位まとめ

| 最適化 | 効果 | 実装難易度 | 優先度 |
|--------|------|-----------|--------|
| リテラル高速パス | 高 | 低 | **P0** |
| Prefix/Suffix | 高 | 中 | **P0** |
| ディレクトリ枝刈り | 最高 | 中 | **P0** |
| 結果配列事前確保 | 中 | 低 | **P1** |
| SSO | 中 | 低 | **P1** |
| ビットマップ文字クラス | 高 | 中 | **P1** |
| システムコール削減 | 高 | 高 | **P2** |
| Trie/Aho-Corasick | 中 | 最高 | **P3** |
| SIMD | 中 | 高 | **P3** |
| マルチスレッド | 高 | 最高 | **P4** |

---

## ベンチマーク計画

### テストケース設計

```c
// 小規模プロジェクト（~100ファイル）
"*.c"                    // リテラル高速パス
"test_*.c"              // Prefix最適化
"**/*.c"                // 再帰走査

// 中規模プロジェクト（~1000ファイル）
"src/**/*.{c,h}"        // ブレース展開 + 再帰
"**/test_*.c"           // Prefix + 再帰

// 大規模プロジェクト（~10000ファイル）
"**/*.{js,ts,jsx,tsx}"  // 複数パターン
"src/**/lib/**/*.c"     // 多段階再帰
```

### 測定指標
- 実行時間（マイクロ秒単位）
- メモリ使用量（malloc回数、ピークメモリ）
- システムコール回数（strace/dtruss）
- キャッシュミス率（perf/Instruments）

---

## 実装ガイドライン

1. **Phase 0: ベースライン測定**
   - 最適化なしの素朴な実装でベンチマーク
   - プロファイリングでボトルネック特定

2. **Phase 1-2: 基礎最適化**
   - P0/P1の最適化を順次実装
   - 各最適化ごとにベンチマーク

3. **Phase 3以降: 選択的実装**
   - ベンチマークで効果が証明された場合のみ
   - 複雑度と効果のトレードオフを評価

4. **継続的検証**
   - Ruby 4.0 Dir.globとの互換性テスト
   - 回帰テスト（最適化によるバグ混入防止）

---

## 参考実装

- **Rust glob**: prefix/suffix最適化のみ（シンプル路線）
- **Git wildmatch**: リテラル高速パス + 枝刈り
- **ripgrep**: SIMD + 並列化（検索特化、glob以外の最適化）
- **Python glob**: 最小限の最適化（可読性優先）

**推奨アプローチ**: Rust glob + Git wildmatchのハイブリッド
