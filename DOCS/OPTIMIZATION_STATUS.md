# P0/P1 Optimization Implementation Report

## 実装済み最適化

### P0-1: リテラルセグメント高速パス ✅
**実装箇所**: `src/rbcglob/traverse.c:match_tokens()`
**効果**: LITERALセグメントの場合、トークンマッチングをスキップして直接strcmp

```c
if (seg->type == RBCGLOB_SEGMENT_LITERAL) {
    if (flags & RBCGLOB_FNM_CASEFOLD) {
        // Case-insensitive comparison
        ...
    } else {
        return strcmp(seg->pattern, str) == 0;
    }
}
```

### P0-2: Prefix/Suffix最適化 ✅
**実装箇所**: `src/rbcglob/traverse.c:execute_step()` (既存実装)
**効果**: ワイルドカードパターンで prefix/suffixチェックによる早期リターン

```c
if (seg->prefix && strncmp(name, seg->prefix, seg->prefix_len) != 0)
    continue;
if (seg->suffix) {
    size_t name_len = strlen(name);
    if (name_len < seg->suffix_len || strcmp(name + name_len - seg->suffix_len, seg->suffix) != 0)
        continue;
}
```

### P0-3: 隠しファイル/ディレクトリのスキップ ✅
**実装箇所**: `src/rbcglob/traverse.c:execute_step()`
**効果**: DOTMATCHフラグがなく、パターンが明示的に'.'で始まらない場合、早期スキップ

```c
if (name[0] == '.' && !(cp->flags & RBCGLOB_FNM_DOTMATCH)) {
    bool explicit_dot = false;
    if (seg->type == RBCGLOB_SEGMENT_LITERAL && seg->pattern && seg->pattern[0] == '.') {
        explicit_dot = true;
    }
    else if (seg->token_count > 0 && seg->tokens[0].token_type == RBCGLOB_TOKEN_CHAR && seg->tokens[0].c == '.') {
        explicit_dot = true;
    }
    if (!explicit_dot) {
        continue;
    }
}
```

### P1-1: 結果配列の事前確保 ✅
**実装箇所**: `src/rbcglob/traverse.c:glob_results_init()`
**効果**: 初期容量64で事前確保、realloc回数削減

```c
#define INITIAL_RESULT_CAPACITY 64

void glob_results_init(glob_results_t *results) {
    results->capacity = INITIAL_RESULT_CAPACITY;
    results->items = malloc(sizeof(char *) * results->capacity);
    results->discovery_indices = malloc(sizeof(size_t) * results->capacity);
    results->count = 0;
    ...
}
```

## ベンチマーク結果

### 実行環境
- Iterations: 100
- Flags: RBCGLOB_FNM_DOTMATCH
- Optimization: -O2

### 結果（P0+P1最適化後）

| Pattern | Description | Matches | Avg Time (ms) |
|---------|-------------|---------|---------------|
| `*.md` | Literal suffix | 1 | 0.01 |
| `tests/*.c` | Literal prefix + suffix | 4 | 0.02 |
| `tests/**/*.c` | Recursive with suffix | 10 | 3.09 |
| `tests/**/*` | Recursive all | 4919 | 134.72 |
| `src/rbcglob/*.c` | Deep literal path | 6 | 0.03 |

### 正確性テスト
✅ All tests passed
- Literal exact match
- Literal suffix match
- Prefix + suffix match
- Recursive pattern matching
- Hidden file handling

## 次の最適化候補

### 高優先度: ディレクトリ走査の枝刈り
**期待効果**: 50-90%高速化（大規模パターン）

1. **先頭リテラルセグメントの直接移動**
   - `tests/fixtures/*.c` → `tests/fixtures/`に直接移動（カレントディレクトリ全体を走査しない）

2. **深さ制限（非再帰パターン）**
   - `*.c` → 深さ1のみ
   - `*/*.c` → 深さ2のみ
   - 再帰セグメント(`**`)がない場合のみ適用

実装には`rbcglob_compiled_pattern_t`に以下を追加：
- `bool has_recursive_segment` - 再帰セグメントの有無
- `size_t leading_literal_count` - 先頭の連続リテラルセグメント数

### 中優先度: Small String Optimization (SSO)
**期待効果**: 20-30%高速化（短いパス名）

現在の`path_join()`は常にmallocを使用。
パス長が256バイト以下の場合、スタックバッファを使用。

### 低優先度: 文字クラスのビットマップ化
**期待効果**: `[a-z]`パターンで10倍高速化

現在はループで範囲チェック → ビットマップで O(1) チェック

## まとめ

現在のP0+P1最適化により、基本的なマッチング性能は確保されています。
最も効果的な次の最適化は「ディレクトリ走査の枝刈り」で、特に大規模パターン（tests/\*\*/\*）で効果が期待できます。
