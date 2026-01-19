# glob v2 設計ドキュメント

**バージョン**: 2.0 (設計案)
**作成日**: 2026-01-19
**ステータス**: Design Proposal

---

## 目次

1. [概要](#概要)
2. [設計哲学](#設計哲学)
3. [アーキテクチャ](#アーキテクチャ)
4. [データ構造](#データ構造)
5. [処理フロー](#処理フロー)
6. [最適化戦略](#最適化戦略)
7. [API設計](#api設計)
8. [実装計画](#実装計画)
9. [性能目標](#性能目標)
10. [移行戦略](#移行戦略)

---

## 概要

### 背景

現在のglob実装（v1）は以下の特徴があります：

**長所**:
- fnmatch側で優れたパターンマッチング最適化（2-3倍高速）
- セグメント方式による直感的な構造
- `**` 再帰パターンへの対応

**短所**:
- ブレース展開で重複したディレクトリスキャン
- 複数パターン間の最適化が困難
- グローバルな最適化の余地が少ない

### 目標

glob v2は、fnmatchの最適化を前提として、**ファイルシステムI/Oの最小化**に焦点を当てた完全な再設計です。

**主要目標**:
1. ディレクトリスキャン回数の最小化（3-10倍削減）
2. fnmatchの最適化を100%活用
3. 複数パターンの統合最適化
4. 明確で保守しやすいアーキテクチャ

---

## 設計哲学

### 1. ヒント駆動型（fnmatchと同じアプローチ）

fnmatchのヒント生成アプローチを採用し、**軽量で効率的な最適化**を実現します。

```
パターン文字列 → ヒント生成 → ヒントベース実行 → 結果
             (20-100ns)   (I/O時間)
```

**従来のAST方式との比較**：
```
# AST方式（初期設計）
パターン → AST構築 → 最適化パス → 実行プラン → 実行
          (500ns)    (300ns)     (100ns)    (I/O)
合計オーバーヘッド：900-1700ns

# ヒント方式（改善設計）
パターン → ヒント生成 → 実行
          (20-100ns)   (I/O)
合計オーバーヘッド：20-100ns  ← 10-17倍削減！
```

### 2. fnmatchとの一貫性

同じ設計哲学で実装：

```c
// fnmatch: ヒント生成 → 最適化実行
rbc_match_hints_t hints = rbc_match_hints_generate(pattern, flags);
bool match = rbc_xfnmatch(&hints, string);

// glob: ヒント生成 → 最適化実行
rbc_glob_hints_t hints = rbc_glob_hints_generate(pattern);
rbc_glob_result_t *results = rbc_glob_exec_with_hints(&hints, flags);
```

**利点**：
- 統一されたアーキテクチャ
- メモリアロケーション最小限（スタック上の構造体）
- 学習コストの削減
- メンテナンス性向上

### 3. I/Oファースト

最適化の優先順位：
1. **ファイルシステムI/O削減**（最優先）
2. メモリアクセスパターンの改善
3. CPU計算量の削減

理由：I/Oは通常、CPU処理の100-1000倍遅いため。

### 4. 段階的な最適化

パターンの複雑度に応じて最適化レベルを選択：

```
シンプルなパターン → Fast Path（v1実装を直接使用）
                    オーバーヘッド：0ns

標準的なパターン   → ヒントベース実行
                    オーバーヘッド：20-100ns

複雑なパターン     → フルAST構築（必要な場合のみ）
                    オーバーヘッド：500-1000ns
                    ただし最適化効果で相殺（3-100倍高速化）
```

### 5. fnmatchへの完全委譲

パターンマッチング自体はfnmatchの最適化を信頼し、glob側は：
- ディレクトリ走査の最適化
- 複数パターンの統合
- ブレース展開の効率化

に専念します。

---

## アーキテクチャ

### 全体構成（ヒント生成アプローチ）

```
                    ┌──────────────────────────────────┐
                    │  Public API                       │
                    │  - rbc_glob_v2()                  │
                    │  - rbc_glob_multi_v2()            │
                    └──────────────┬───────────────────┘
                                   │
                    ┌──────────────▼───────────────────┐
                    │  Hint Generator (超軽量)          │
                    │  - 1パススキャン                  │
                    │  - パターン複雑度判定              │
                    │  - ブレース展開情報の抽出          │
                    │  → ヒント構造体（スタック上）      │
                    └──────────────┬───────────────────┘
                                   │
                    ┌──────────────▼───────────────────┐
                    │  Execution Router                 │
                    │  - Fast Path判定                  │
                    │  - 実行パス選択                   │
                    └──────────┬───────────┬────────────┘
                               │           │
                ┌──────────────▼──┐    ┌──▼─────────────────┐
                │  Fast Path      │    │  Optimized Path    │
                │  - v1実装使用   │    │  - ヒントベース実行 │
                │  - オーバーヘッド0│    │  - ブレース統合    │
                └──────────┬──────┘    └──┬─────────────────┘
                           │              │
                           └──────┬───────┘
                                  │
                    ┌─────────────▼────────────────────┐
                    │  Result Collection               │
                    │  - 結果マージ                    │
                    │  - ソート・重複削除              │
                    │  → char** 配列                   │
                    └──────────────────────────────────┘
```

### コンポーネント間のデータフロー

```
入力パターン
  ↓
[Hint Generator] (20-100ns)
  ↓
rbc_glob_hints_t (スタック上の構造体)
  ↓
[Execution Router]
  ↓
┌─────────────┬─────────────────┐
│             │                 │
▼             ▼                 ▼
Fast Path    Optimized Path   Full AST Path
(v1直接)     (ヒント使用)      (複雑パターン)
│             │                 │
└─────────────┴─────────────────┘
  ↓
result_set_t (結果)
  ↓
出力（char**）
```

### 複雑度に応じた処理パス

```
パターン複雑度         処理パス              オーバーヘッド
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
"file.txt"          → LITERAL          0ns (stat()のみ)
"*.txt"             → SIMPLE           0ns (v1直接)
"src/*.c"           → MODERATE         0ns (v1直接)
"{a,b,c}/*.txt"     → BRACE_SIMPLE    20-50ns (ヒント)
"src/{x,y}/**/*.js" → BRACE_COMPLEX   50-100ns (ヒント)

超複雑なパターン      → FULL_AST        500-1000ns
                                     (ただし最適化効果で相殺)
```

---

## データ構造

### ヒント構造体（コア構造）

fnmatchと同様に、軽量なヒント構造体を使用します。

#### ヒントタイプ

```c
typedef enum {
    GLOB_HINT_LITERAL,           // "src/file.txt" - ワイルドカードなし
    GLOB_HINT_SIMPLE_PATTERN,    // "*.txt" - 単一セグメント
    GLOB_HINT_MULTI_SEGMENT,     // "src/*.c" - 複数セグメント、ブレースなし
    GLOB_HINT_BRACE_SINGLE_DIR,  // "src/{a,b,c}/*.c" - 単一階層のブレース
    GLOB_HINT_BRACE_NESTED,      // "{a,b}/{x,y}/*.c" - ネストしたブレース
    GLOB_HINT_RECURSIVE,         // "**/*.c" - 再帰パターン
    GLOB_HINT_COMPLEX,           // 複雑なパターン（AST構築が必要）
} glob_hint_type_t;
```

#### ヒント構造体

```c
typedef struct {
    glob_hint_type_t type;

    // パターンの特性（ビットフラグ）
    struct {
        bool has_brace : 1;
        bool has_doublestar : 1;
        bool has_wildcard : 1;
        bool has_bracket : 1;
        bool has_escape : 1;
    } flags;

    // 基本情報
    int segment_count;        // セグメント数
    int brace_depth;          // ブレースのネスト深さ

    // ブレース展開のヒント（BRACE_SINGLE_DIR用）
    struct {
        const char *prefix;      // 共通プレフィックス
        size_t prefix_len;
        const char *suffix;      // 共通サフィックス
        size_t suffix_len;

        // 選択肢（静的解析済み）
        // 注：ポインタのみ保持、元の文字列を参照
        struct {
            const char *start;   // 選択肢の開始位置
            size_t len;          // 選択肢の長さ
        } choices[32];           // 最大32個（通常十分）
        int choice_count;

        // 最適化ヒント
        bool can_use_hashset;    // ハッシュセット最適化可能
        bool all_single_char;    // 全て1文字の選択肢
    } brace_info;

    // セグメント情報（プリ解析済み）
    struct {
        const char *segments[16]; // セグメントポインタ（最大16階層）
        size_t lengths[16];       // 各セグメントの長さ
        int count;
    } segment_info;

    // コスト推定
    struct {
        size_t estimated_dirs;    // 推定ディレクトリスキャン回数
        size_t estimated_io_cost; // 推定I/Oコスト
    } cost;

} rbc_glob_hints_t;
```

**特徴**：
- ✅ スタック上に確保可能（mallocなし）
- ✅ 元の文字列へのポインタのみ保持（コピー不要）
- ✅ 1パススキャンで生成（20-100ns）
- ✅ fnmatchと同じ設計哲学

#### ヒント生成の例

```c
// パターン: "src/{a,b,c}/*.txt"

rbc_glob_hints_t hints = {
    .type = GLOB_HINT_BRACE_SINGLE_DIR,
    .flags = { .has_brace = true, .has_wildcard = true },
    .segment_count = 3,
    .brace_depth = 1,

    .brace_info = {
        .prefix = "src/",
        .prefix_len = 4,
        .suffix = "/*.txt",
        .suffix_len = 6,

        .choices = {
            { .start = "a", .len = 1 },
            { .start = "b", .len = 1 },
            { .start = "c", .len = 1 },
        },
        .choice_count = 3,
        .can_use_hashset = true,
        .all_single_char = true,
    },

    .cost = {
        .estimated_dirs = 1,  // 最適化により1回のスキャン
        .estimated_io_cost = 1,
    }
};
```

---

### AST (抽象構文木) - 複雑なパターンのみ使用

**注意**：AST構築は `GLOB_HINT_COMPLEX` タイプの複雑なパターンでのみ使用します。
90%以上のパターンはヒント構造体のみで処理可能です。

#### ノードタイプ

```c
typedef enum {
    NODE_ROOT,          // ルートノード
    NODE_DIR,           // ディレクトリ（リテラル）
    NODE_MATCH,         // パターンマッチング
    NODE_CHOICE,        // 選択肢（ブレース展開）
    NODE_RECURSIVE,     // ** 再帰パターン
    NODE_SEQUENCE,      // 連続するノード
} node_type_t;
```

#### ノード構造

```c
typedef struct glob_node_s glob_node_t;

struct glob_node_s {
    node_type_t type;

    union {
        // NODE_DIR: リテラルディレクトリ
        struct {
            char *path;                 // ディレクトリパス
        } dir;

        // NODE_MATCH: パターンマッチング
        struct {
            rbc_fnmatch_pattern_t *compiled;  // fnmatchコンパイル済み
            char *original;                   // 元のパターン
        } match;

        // NODE_CHOICE: ブレース展開
        struct {
            glob_node_t **alternatives;  // 代替ノード配列
            size_t count;                // 代替の数

            // 最適化情報
            bool has_common_prefix;
            bool has_common_suffix;
            char *common_prefix;         // 共通プレフィックス
            char *common_suffix;         // 共通サフィックス
        } choice;

        // NODE_RECURSIVE: ** パターン
        struct {
            glob_node_t *continuation;   // ** の後に続くパターン
            int max_depth;               // 最大探索深度（オプション）
        } recursive;

        // NODE_SEQUENCE: 連続ノード
        struct {
            glob_node_t **children;      // 子ノード配列
            size_t count;                // 子ノードの数
        } sequence;
    } data;

    // メタデータ
    bool can_optimize;              // 最適化可能フラグ
    size_t estimated_results;       // 推定結果数
    char *debug_label;              // デバッグ用ラベル
};
```

#### AST例

**パターン**: `src/{a,b}/test_*.{c,h}`

```
ROOT
 └─ SEQUENCE
     ├─ DIR "src"
     ├─ CHOICE [count=2, common_prefix="", common_suffix=""]
     │   ├─ DIR "a"
     │   └─ DIR "b"
     └─ CHOICE [count=2, common_prefix="test_", common_suffix=""]
         ├─ MATCH "test_*.c" [compiled=PREFIX_SUFFIX]
         └─ MATCH "test_*.h" [compiled=PREFIX_SUFFIX]
```

### 実行プラン

#### プランタイプ

```c
typedef enum {
    PLAN_SCAN_ONCE,        // ディレクトリを1回スキャン（最適）
    PLAN_SCAN_MULTIPLE,    // 複数回スキャン（最適化不可）
    PLAN_LITERAL_LOOKUP,   // リテラルパスの直接確認
    PLAN_RECURSIVE_SCAN,   // 再帰的スキャン（**）
    PLAN_MERGED_SCAN,      // 複数ディレクトリの統合スキャン
} plan_type_t;
```

#### プラン構造

```c
typedef struct execution_plan_s execution_plan_t;

struct execution_plan_s {
    plan_type_t type;
    char *directory;              // スキャン対象ディレクトリ

    // フィルタ条件
    filter_set_t *filters;

    // 子プラン（ネストした構造用）
    execution_plan_t **subplans;
    size_t subplan_count;

    // コスト推定
    struct {
        size_t estimated_io_ops;      // 推定I/O回数
        size_t estimated_cpu_ops;     // 推定CPU演算回数
        size_t estimated_results;     // 推定結果数
        double estimated_time_ms;     // 推定実行時間
    } cost;

    // デバッグ情報
    char *description;
};
```

### フィルタ

#### フィルタタイプ

```c
typedef enum {
    FILTER_FNMATCH,        // fnmatchで判定
    FILTER_CHOICE_SET,     // 選択肢集合（ハッシュセット）
    FILTER_PREFIX_SUFFIX,  // prefix + suffix チェック
    FILTER_LITERAL_SET,    // リテラル集合
    FILTER_BLOOM,          // ブルームフィルタ（早期リジェクト）
} filter_type_t;
```

#### フィルタ構造

```c
typedef struct {
    filter_type_t type;

    union {
        // FILTER_FNMATCH
        rbc_fnmatch_pattern_t *fnmatch;

        // FILTER_CHOICE_SET
        struct {
            char *prefix;
            char *suffix;
            rbc_string_set_t *choices;  // ハッシュセット
            uint64_t bloom;             // ブルームフィルタ
        } choice_set;

        // FILTER_PREFIX_SUFFIX
        struct {
            char *prefix;
            size_t prefix_len;
            char *suffix;
            size_t suffix_len;
        } prefix_suffix;

        // FILTER_LITERAL_SET
        struct {
            rbc_string_set_t *literals;
        } literal_set;

        // FILTER_BLOOM
        struct {
            uint64_t bloom_filter;
        } bloom;
    } data;

    // 統計情報
    struct {
        uint64_t invocation_count;
        uint64_t match_count;
        uint64_t reject_count;
    } stats;

} filter_t;
```

#### フィルタセット

```c
typedef struct {
    filter_t **filters;
    size_t count;

    // 組み合わせモード
    enum {
        FILTER_MODE_OR,   // いずれか1つがマッチすればOK
        FILTER_MODE_AND,  // すべてマッチする必要がある
    } mode;

} filter_set_t;
```

---

## 処理フロー

### 1. ヒント生成（Hint Generation） - 高速パス

**すべてのパターンで最初に実行**される超軽量な解析です。

#### 入力
```
パターン文字列: "src/{a,b,c}/*.txt"
```

#### 処理

```c
rbc_glob_hints_t rbc_glob_hints_generate(const char *pattern) {
    rbc_glob_hints_t hints = {0};

    // Phase 1: 1パススキャンでパターンを解析
    const char *p = pattern;
    const char *brace_start = NULL;
    const char *segment_starts[16] = {0};
    int segment_idx = 0;

    segment_starts[0] = pattern;  // 最初のセグメント

    while (*p) {
        switch (*p) {
            case '{':
                hints.flags.has_brace = true;
                if (!brace_start) brace_start = p;
                hints.brace_depth++;
                break;

            case '}':
                hints.brace_depth--;
                break;

            case '*':
                hints.flags.has_wildcard = true;
                if (p[1] == '*') {
                    hints.flags.has_doublestar = true;
                    p++;  // ** をスキップ
                }
                break;

            case '?':
                hints.flags.has_wildcard = true;
                break;

            case '[':
                hints.flags.has_bracket = true;
                break;

            case '/':
                hints.segment_count++;
                segment_starts[++segment_idx] = p + 1;
                break;

            case '\\':
                hints.flags.has_escape = true;
                p++;  // エスケープ文字をスキップ
                break;
        }
        p++;
    }
    hints.segment_count++;  // 最後のセグメント

    // Phase 2: ヒントタイプを決定
    if (!hints.flags.has_wildcard && !hints.flags.has_brace) {
        hints.type = GLOB_HINT_LITERAL;
        return hints;  // 早期リターン
    }

    if (hints.segment_count == 1 && !hints.flags.has_brace) {
        hints.type = GLOB_HINT_SIMPLE_PATTERN;
        return hints;
    }

    if (!hints.flags.has_brace && !hints.flags.has_doublestar) {
        hints.type = GLOB_HINT_MULTI_SEGMENT;
        return hints;
    }

    if (hints.flags.has_brace && hints.brace_depth == 1) {
        // Phase 3: ブレース展開情報を抽出（単純なケース）
        extract_brace_info(pattern, &hints.brace_info);
        hints.type = GLOB_HINT_BRACE_SINGLE_DIR;
        return hints;
    }

    if (hints.flags.has_doublestar) {
        hints.type = GLOB_HINT_RECURSIVE;
        return hints;
    }

    // 複雑なパターン（AST構築が必要）
    hints.type = GLOB_HINT_COMPLEX;
    return hints;
}

// ブレース展開情報の抽出（軽量版）
static void extract_brace_info(const char *pattern,
                                struct brace_info *info) {
    // プレフィックス: '{' の前
    const char *brace_open = strchr(pattern, '{');
    info->prefix = pattern;
    info->prefix_len = brace_open - pattern;

    // サフィックス: '}' の後
    const char *brace_close = strchr(brace_open, '}');
    info->suffix = brace_close + 1;
    info->suffix_len = strlen(info->suffix);

    // 選択肢: '{' と '}' の間を ',' で分割
    const char *choice_start = brace_open + 1;
    const char *p = choice_start;
    int choice_idx = 0;

    while (*p && *p != '}') {
        if (*p == ',') {
            info->choices[choice_idx].start = choice_start;
            info->choices[choice_idx].len = p - choice_start;
            choice_idx++;
            choice_start = p + 1;
        }
        p++;
    }

    // 最後の選択肢
    info->choices[choice_idx].start = choice_start;
    info->choices[choice_idx].len = p - choice_start;
    info->choice_count = choice_idx + 1;

    // 最適化ヒント
    info->can_use_hashset = (info->choice_count >= 4);
    info->all_single_char = true;
    for (int i = 0; i < info->choice_count; i++) {
        if (info->choices[i].len != 1) {
            info->all_single_char = false;
            break;
        }
    }
}
```

#### 出力

`rbc_glob_hints_t` 構造体（スタック上）

**コスト**: 20-100ns（パターン長に比例）

---

### 2. 実行パス選択（Execution Routing）

```c
rbc_glob_result_t* rbc_glob_v2(const char *pattern, int flags) {
    // Step 1: ヒント生成（超軽量）
    rbc_glob_hints_t hints = rbc_glob_hints_generate(pattern);

    // Step 2: ヒントに基づいて実行パスを選択
    switch (hints.type) {
        case GLOB_HINT_LITERAL:
            // Fast Path: リテラルパス
            return glob_exec_literal(pattern);

        case GLOB_HINT_SIMPLE_PATTERN:
            // Fast Path: 単純パターン（v1実装）
            return glob_exec_simple(pattern, flags);

        case GLOB_HINT_MULTI_SEGMENT:
            // Fast Path: 標準パターン（v1実装）
            return glob_exec_multi_segment(pattern, flags);

        case GLOB_HINT_BRACE_SINGLE_DIR:
            // Optimized Path: ブレース最適化（ヒント使用）
            return glob_exec_brace_optimized(&hints, flags);

        case GLOB_HINT_RECURSIVE:
            // Optimized Path: 再帰パターン
            return glob_exec_recursive(pattern, flags);

        case GLOB_HINT_COMPLEX:
            // Full Path: 完全なAST構築と最適化
            return glob_exec_full_ast(pattern, flags);
    }
}
```

---

### 3. ブレース最適化実行（Hint-Based Execution）

**最も重要な最適化**：ヒント情報を直接使用

```c
static rbc_glob_result_t* glob_exec_brace_optimized(
    const rbc_glob_hints_t *hints,
    int flags
) {
    const struct brace_info *binfo = &hints->brace_info;

    // Step 1: ベースディレクトリを開く
    char base_dir[PATH_MAX];
    memcpy(base_dir, binfo->prefix, binfo->prefix_len);
    base_dir[binfo->prefix_len] = '\0';

    // '/' を探して親ディレクトリを特定
    char *last_slash = strrchr(base_dir, '/');
    if (last_slash) *last_slash = '\0';

    DIR *dir = opendir(base_dir[0] ? base_dir : ".");
    if (!dir) return create_empty_result();

    // Step 2: 選択肢のハッシュセットを構築
    string_set_t *choice_set = create_string_set(binfo->choice_count);
    for (int i = 0; i < binfo->choice_count; i++) {
        string_set_add_n(choice_set,
                         binfo->choices[i].start,
                         binfo->choices[i].len);
    }

    // Step 3: ディレクトリを1回スキャン
    rbc_glob_result_t *results = create_result_set();
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        // 選択肢にマッチするか？（O(1)）
        if (string_set_contains(choice_set, entry->d_name)) {
            // サフィックスパターンを適用
            char subpath[PATH_MAX];
            snprintf(subpath, sizeof(subpath), "%s/%s%s",
                     base_dir, entry->d_name, binfo->suffix);

            // サフィックスがパターンを含む場合は再帰的にglob
            if (strchr(binfo->suffix, '*') || strchr(binfo->suffix, '?')) {
                rbc_glob_result_t *sub = glob_exec_simple(subpath, flags);
                merge_results(results, sub);
                free_result(sub);
            } else {
                // リテラルサフィックス：直接追加
                add_result(results, subpath);
            }
        }
    }

    closedir(dir);
    string_set_free(choice_set);

    return results;
}
```

**効果**：
- ディレクトリスキャン：N回 → 1回
- コスト：O(選択肢数 × エントリ数 × 1) = O(N)（ハッシュセット）

---

### 4. 完全なAST構築（Complex Patterns Only）

**注意**: `GLOB_HINT_COMPLEX` の場合のみ実行

#### 入力

最適化されたAST

#### 処理

```c
execution_plan_t* generate_execution_plan(glob_node_t *ast) {
    execution_plan_t *plan = create_plan();

    switch (ast->type) {
    case NODE_SEQUENCE:
        plan->type = PLAN_SCAN_ONCE;
        analyze_sequence_and_create_plan(ast, plan);
        break;

    case NODE_CHOICE:
        if (ast->can_optimize) {
            plan->type = PLAN_SCAN_ONCE;
            create_optimized_choice_plan(ast, plan);
        } else {
            plan->type = PLAN_SCAN_MULTIPLE;
            create_multiple_scan_plan(ast, plan);
        }
        break;

    case NODE_DIR:
        plan->type = PLAN_LITERAL_LOOKUP;
        plan->directory = ast->data.dir.path;
        break;

    case NODE_MATCH:
        plan->type = PLAN_SCAN_ONCE;
        create_match_plan(ast, plan);
        break;

    case NODE_RECURSIVE:
        plan->type = PLAN_RECURSIVE_SCAN;
        create_recursive_plan(ast, plan);
        break;
    }

    // コスト推定
    estimate_plan_cost(plan);

    return plan;
}
```

##### 最適化された選択肢プラン

```c
void create_optimized_choice_plan(glob_node_t *choice,
                                  execution_plan_t *plan) {
    // FILTER_CHOICE_SET を使用
    filter_t *filter = create_filter(FILTER_CHOICE_SET);

    filter->data.choice_set.prefix = choice->data.choice.common_prefix;
    filter->data.choice_set.suffix = choice->data.choice.common_suffix;

    // 中間部分を抽出してハッシュセット化
    char **middles = extract_middle_parts(
        choice->data.choice.alternatives,
        choice->data.choice.count
    );

    filter->data.choice_set.choices =
        create_string_set(middles, choice->data.choice.count);

    // ブルームフィルタも構築
    filter->data.choice_set.bloom =
        build_bloom_filter(middles, choice->data.choice.count);

    add_filter(plan->filters, filter);
}
```

#### 出力

実行プラン

**例**：`src/{lib,app}/test_*.{c,h}` の実行プラン

```
PLAN: MERGED_SCAN
  Subplan 1: SCAN_ONCE "src/lib"
    Filters:
      - FILTER_CHOICE_SET
          prefix: "test_"
          suffix: ""
          choices: {".c", ".h"}
    Estimated I/O: 1 opendir + N readdir + 1 closedir
    Estimated Time: 150μs (100 files)

  Subplan 2: SCAN_ONCE "src/app"
    Filters:
      - FILTER_CHOICE_SET
          prefix: "test_"
          suffix: ""
          choices: {".c", ".h"}
    Estimated I/O: 1 opendir + N readdir + 1 closedir
    Estimated Time: 150μs (100 files)

Total Estimated I/O: 2 directory scans
Total Estimated Time: 300μs

従来方式との比較:
  従来: 4 directory scans (lib/*.c, lib/*.h, app/*.c, app/*.h)
  v2: 2 directory scans
  削減率: 50%
```

---

### 4. 実行（Execute）

#### 入力

実行プラン

#### 処理

```c
void execute_plan(execution_plan_t *plan,
                  const char *base_path,
                  result_set_t *results) {

    switch (plan->type) {
    case PLAN_SCAN_ONCE:
        execute_single_scan(plan, base_path, results);
        break;

    case PLAN_SCAN_MULTIPLE:
        for (size_t i = 0; i < plan->subplan_count; i++) {
            execute_plan(plan->subplans[i], base_path, results);
        }
        break;

    case PLAN_LITERAL_LOOKUP:
        execute_literal_lookup(plan, base_path, results);
        break;

    case PLAN_RECURSIVE_SCAN:
        execute_recursive_scan(plan, base_path, results);
        break;

    case PLAN_MERGED_SCAN:
        execute_merged_scan(plan, base_path, results);
        break;
    }
}
```

##### 単一スキャン実行（最重要）

```c
void execute_single_scan(execution_plan_t *plan,
                         const char *base_path,
                         result_set_t *results) {

    char full_path[PATH_MAX];
    build_path(full_path, base_path, plan->directory);

    // ディレクトリを1回だけ開く
    DIR *dir = opendir(full_path);
    if (!dir) return;

    struct dirent *entry;
    size_t scanned = 0, matched = 0;

    while ((entry = readdir(dir)) != NULL) {
        scanned++;
        const char *name = entry->d_name;

        // ドットファイルのスキップ
        if (should_skip_dot_file(name)) {
            continue;
        }

        // フィルタセットを適用
        if (apply_filters(plan->filters, name)) {
            matched++;
            add_result(results, full_path, name);
        }
    }

    closedir(dir);

    // 統計更新
    update_statistics(plan, scanned, matched);
}
```

##### フィルタ適用

```c
bool apply_filters(filter_set_t *filters, const char *name) {
    if (filters->mode == FILTER_MODE_OR) {
        // いずれか1つがマッチすればOK
        for (size_t i = 0; i < filters->count; i++) {
            if (apply_single_filter(filters->filters[i], name)) {
                return true;
            }
        }
        return false;
    }
    else {
        // すべてマッチする必要がある (AND)
        for (size_t i = 0; i < filters->count; i++) {
            if (!apply_single_filter(filters->filters[i], name)) {
                return false;
            }
        }
        return true;
    }
}

bool apply_single_filter(filter_t *filter, const char *name) {
    filter->stats.invocation_count++;

    switch (filter->type) {
    case FILTER_FNMATCH:
        // fnmatchの最適化を活用
        if (rbc_xfnmatch(filter->data.fnmatch, name, 0)) {
            filter->stats.match_count++;
            return true;
        }
        filter->stats.reject_count++;
        return false;

    case FILTER_CHOICE_SET: {
        size_t len = strlen(name);
        size_t prefix_len = strlen(filter->data.choice_set.prefix);
        size_t suffix_len = strlen(filter->data.choice_set.suffix);

        // 早期リジェクト: 長さチェック
        if (len < prefix_len + suffix_len) {
            filter->stats.reject_count++;
            return false;
        }

        // プレフィックスチェック（memcmp - 最速）
        if (prefix_len > 0 &&
            memcmp(name, filter->data.choice_set.prefix,
                   prefix_len) != 0) {
            filter->stats.reject_count++;
            return false;
        }

        // サフィックスチェック（memcmp - 最速）
        if (suffix_len > 0 &&
            memcmp(name + len - suffix_len,
                   filter->data.choice_set.suffix,
                   suffix_len) != 0) {
            filter->stats.reject_count++;
            return false;
        }

        // 中間部分の抽出
        size_t middle_len = len - prefix_len - suffix_len;
        char middle[middle_len + 1];
        memcpy(middle, name + prefix_len, middle_len);
        middle[middle_len] = '\0';

        // ブルームフィルタで早期リジェクト（オプション）
        if (filter->data.choice_set.bloom != 0) {
            if (!bloom_contains(filter->data.choice_set.bloom, middle)) {
                filter->stats.reject_count++;
                return false;
            }
        }

        // O(1) ハッシュルックアップ
        if (string_set_contains(filter->data.choice_set.choices, middle)) {
            filter->stats.match_count++;
            return true;
        }

        filter->stats.reject_count++;
        return false;
    }

    case FILTER_PREFIX_SUFFIX: {
        // prefix + suffix チェック
        size_t len = strlen(name);

        if (len < filter->data.prefix_suffix.prefix_len +
                   filter->data.prefix_suffix.suffix_len) {
            filter->stats.reject_count++;
            return false;
        }

        if (memcmp(name, filter->data.prefix_suffix.prefix,
                   filter->data.prefix_suffix.prefix_len) != 0) {
            filter->stats.reject_count++;
            return false;
        }

        if (memcmp(name + len - filter->data.prefix_suffix.suffix_len,
                   filter->data.prefix_suffix.suffix,
                   filter->data.prefix_suffix.suffix_len) != 0) {
            filter->stats.reject_count++;
            return false;
        }

        filter->stats.match_count++;
        return true;
    }

    case FILTER_LITERAL_SET:
        // リテラル集合でのルックアップ
        if (string_set_contains(filter->data.literal_set.literals, name)) {
            filter->stats.match_count++;
            return true;
        }
        filter->stats.reject_count++;
        return false;

    default:
        return false;
    }
}
```

#### 出力

結果セット（マッチしたパスのリスト）

---

## 最適化戦略

### レベル1: ブレース展開の統合

**問題**：
```
パターン: test_{a,b,c}.txt
現在: 3回のディレクトリスキャン
```

**解決策**：
```c
// FILTER_CHOICE_SET を使用
opendir(".")  // 1回だけ
for each entry:
    if starts_with("test_") and ends_with(".txt"):
        middle = extract_middle(entry)
        if middle in {"a", "b", "c"}:  // O(1)
            emit(entry)
closedir()
```

**効果**：
- I/O削減: N回 → 1回（N = 代替数）
- 期待される高速化: **3-10倍**

---

### レベル2: 複数パターンの統合

**問題**：
```
パターン1: src/*.c
パターン2: src/*.h
現在: 2回のディレクトリスキャン
```

**解決策**：
```c
// rbc_glob_multi_v2() API使用
merged_plan = merge_patterns([
    parse("src/*.c"),
    parse("src/*.h")
])

// 実行プラン:
opendir("src")  // 1回だけ
for each entry:
    if fnmatch("*.c", entry) or fnmatch("*.h", entry):
        emit(entry)
closedir()
```

**効果**：
- I/O削減: M回 → 1回（M = パターン数）
- 期待される高速化: **5-20倍**（パターン数に依存）

---

### レベル3: ディレクトリキャッシング

**問題**：
```
複数のglob呼び出しで同じディレクトリをスキャン
```

**解決策**：
```c
// グローバルキャッシュ
static dir_cache_t *global_cache = NULL;

dir_entry_t* get_directory_entries(const char *path) {
    // キャッシュチェック
    if (cached = find_in_cache(global_cache, path)) {
        return cached->entries;
    }

    // キャッシュミス
    DIR *dir = opendir(path);
    entries = read_all_entries(dir);
    closedir(dir);

    // キャッシュに保存
    cache_entries(global_cache, path, entries);

    return entries;
}
```

**効果**：
- 反復実行で劇的な高速化
- 期待される高速化: **10-100倍**（2回目以降）

---

### レベル4: プラットフォーム固有最適化

#### Linux: getdents64

```c
#ifdef __linux__
// 大きなバッファで一度に多くのエントリを取得
int fd = open(path, O_RDONLY | O_DIRECTORY);
char buffer[65536];  // 64KB
syscall(SYS_getdents64, fd, buffer, sizeof(buffer));
#endif
```

**効果**：
- システムコール数: 1000回 → 5-10回
- 期待される高速化: **5-8倍**（大規模ディレクトリ）

#### macOS: getattrlistbulk

```c
#ifdef __APPLE__
// エントリと属性を同時取得
struct attrlist attrs;
getattrlistbulk(dirfd, &attrs, buffer, buffer_size, 0);
#endif
```

**効果**：
- stat()呼び出しが不要に
- 期待される高速化: **3-5倍**

---

## API設計

### 基本API

```c
/**
 * @brief 単一パターンのglob
 *
 * @param pattern グロブパターン
 * @param base ベースディレクトリ（NULLの場合はカレントディレクトリ）
 * @param flags マッチングフラグ
 * @param out 結果の配列（malloc）
 * @param count 結果の数
 * @return 成功時true、失敗時false
 */
bool rbc_glob_v2(const char *pattern,
                 const char *base,
                 unsigned flags,
                 char ***out,
                 size_t *count);

/**
 * @brief 複数パターンのglob（最適化される）
 *
 * 複数のパターンを同時に処理することで、
 * ディレクトリスキャンを統合し、I/Oを削減します。
 *
 * @param patterns グロブパターンの配列
 * @param npatterns パターンの数
 * @param base ベースディレクトリ
 * @param flags マッチングフラグ
 * @param out 結果の配列
 * @param count 結果の数
 * @return 成功時true、失敗時false
 */
bool rbc_glob_multi_v2(const char **patterns,
                       size_t npatterns,
                       const char *base,
                       unsigned flags,
                       char ***out,
                       size_t *count);

/**
 * @brief glob結果の解放
 *
 * @param results 結果の配列
 * @param count 結果の数
 */
void rbc_glob_free_v2(char **results, size_t count);
```

### 高度なAPI

```c
/**
 * @brief パターンをコンパイル
 *
 * パターンを事前にコンパイルして実行プランを生成します。
 * 同じパターンを複数回実行する場合に有効です。
 *
 * @param pattern グロブパターン
 * @param flags マッチングフラグ
 * @return コンパイル済みパターン
 */
rbc_glob_pattern_v2_t* rbc_glob_compile_v2(const char *pattern,
                                           unsigned flags);

/**
 * @brief コンパイル済みパターンで実行
 *
 * @param pattern コンパイル済みパターン
 * @param base ベースディレクトリ
 * @param out 結果の配列
 * @param count 結果の数
 * @return 成功時true、失敗時false
 */
bool rbc_glob_execute_v2(rbc_glob_pattern_v2_t *pattern,
                         const char *base,
                         char ***out,
                         size_t *count);

/**
 * @brief コンパイル済みパターンの解放
 *
 * @param pattern コンパイル済みパターン
 */
void rbc_glob_pattern_free_v2(rbc_glob_pattern_v2_t *pattern);

/**
 * @brief 実行プランの取得（デバッグ用）
 *
 * パターンの実行プランを文字列として取得します。
 *
 * @param pattern コンパイル済みパターン
 * @return 実行プランの説明（malloc）
 */
char* rbc_glob_explain_v2(rbc_glob_pattern_v2_t *pattern);
```

### 使用例

```c
// 基本的な使用
char **results;
size_t count;
rbc_glob_v2("src/**/*.c", NULL, 0, &results, &count);
for (size_t i = 0; i < count; i++) {
    printf("%s\n", results[i]);
}
rbc_glob_free_v2(results, count);

// 複数パターン（最適化）
const char *patterns[] = {"*.c", "*.h", "*.cpp"};
rbc_glob_multi_v2(patterns, 3, "src", 0, &results, &count);
rbc_glob_free_v2(results, count);

// 事前コンパイル（反復実行）
rbc_glob_pattern_v2_t *pattern = rbc_glob_compile_v2("test_*.txt", 0);

// デバッグ：実行プランを確認
char *plan = rbc_glob_explain_v2(pattern);
printf("Execution Plan:\n%s\n", plan);
free(plan);

// 複数回実行
for (int i = 0; i < 100; i++) {
    rbc_glob_execute_v2(pattern, "data", &results, &count);
    process_results(results, count);
    rbc_glob_free_v2(results, count);
}

rbc_glob_pattern_free_v2(pattern);
```

---

## 実装計画

### Phase 1: 基盤構築（2-3週間）

**Week 1: データ構造**
- [ ] AST構造体の実装
- [ ] 実行プラン構造体の実装
- [ ] フィルタ構造体の実装
- [ ] ユニットテスト

**Week 2: パーサー**
- [ ] 字句解析器（Tokenizer）
- [ ] 構文解析器（Parser）
- [ ] ASTビルダー
- [ ] パーステスト

**Week 3: 基本実行エンジン**
- [ ] シンプルな実行エンジン
- [ ] フィルタエンジン
- [ ] fnmatch統合
- [ ] 統合テスト

### Phase 2: 最適化実装（3-4週間）

**Week 4: ブレース展開最適化**
- [ ] 共通部分抽出
- [ ] FILTER_CHOICE_SET実装
- [ ] ハッシュセット実装
- [ ] ベンチマーク

**Week 5-6: 最適化パス**
- [ ] 最適化パスフレームワーク
- [ ] ディレクトリ統合最適化
- [ ] リテラルパス検出
- [ ] コスト推定モデル

**Week 7: 複数パターン統合**
- [ ] パターンマージロジック
- [ ] rbc_glob_multi_v2 API
- [ ] 統合最適化
- [ ] ベンチマーク

### Phase 3: 高度な最適化（2-3週間）

**Week 8: ディレクトリキャッシング**
- [ ] キャッシュ構造
- [ ] LRU/LFU実装
- [ ] 無効化戦略
- [ ] パフォーマンステスト

**Week 9: プラットフォーム固有最適化**
- [ ] Linux: getdents64
- [ ] macOS: getattrlistbulk
- [ ] Windows: FindFirstFileEx
- [ ] プラットフォームテスト

### Phase 4: 統合とテスト（2週間）

**Week 10: 統合**
- [ ] v1との互換性レイヤー
- [ ] 移行ガイド
- [ ] ドキュメント

**Week 11: 最終テスト**
- [ ] 包括的なテストスイート
- [ ] パフォーマンスベンチマーク
- [ ] メモリリークチェック
- [ ] エッジケーステスト

---

## 性能目標

### ベンチマーク目標

| シナリオ | v1（現在） | v2（目標） | 高速化率 |
|---------|----------|-----------|---------|
| 単純パターン `*.c` | 100μs | 80μs | 1.25x |
| ブレース展開 `test_{a,b,c,d,e}.txt` | 500μs | 80μs | **6.25x** |
| 複数パターン `*.{c,h,cpp}` | 800μs | 100μs | **8.0x** |
| ネストブレース `{a,b}/{x,y}.txt` | 1200μs | 250μs | **4.8x** |
| 再帰パターン `**/*.c` | 2000μs | 1800μs | 1.1x |
| 複数パターン統合（10個） | 5000μs | 500μs | **10.0x** |

### メモリ使用量目標

| 項目 | v1 | v2 | 削減率 |
|-----|----|----|-------|
| パターン構造 | 3.8 KB | 120 bytes | **32倍削減** |
| 実行時オーバーヘッド | 50 KB | 80 KB | 1.6倍増加 |
| キャッシュ（オプション） | - | 40 KB/1000ファイル | - |

### 品質目標

- **テストカバレッジ**: 95%以上
- **Ruby互換性**: 100%（既存テストすべて合格）
- **メモリリーク**: ゼロ
- **クラッシュ**: ゼロ

---

## 移行戦略

### 段階的移行

#### Phase 1: 共存期間

```c
// v1とv2を並行提供
bool rbc_glob(...)     // v1（既存）
bool rbc_glob_v2(...)  // v2（新規）

// 環境変数での切り替え
if (getenv("RBC_GLOB_V2")) {
    use_v2_implementation();
} else {
    use_v1_implementation();
}
```

#### Phase 2: デフォルト切り替え

```c
// v2をデフォルトに（v1は明示的に有効化）
bool rbc_glob(...)  // v2実装
bool rbc_glob_v1(...) // v1（レガシー）
```

#### Phase 3: v1削除

```c
// v1コードの削除
bool rbc_glob(...)  // v2のみ
```

### 互換性保証

- **API互換性**: 既存のrbc_glob() APIを維持
- **動作互換性**: Ruby 4.0互換性を100%維持
- **パフォーマンス**: すべてのケースでv1以上の性能

### テスト戦略

```bash
# 既存テストスイートで検証
make test           # v1でテスト
make test-v2        # v2でテスト
make test-compat    # 互換性テスト（両方で同じ結果）

# パフォーマンス回帰テスト
make bench          # v1ベンチマーク
make bench-v2       # v2ベンチマーク
make bench-compare  # 比較レポート
```

---

## 付録

### A. パフォーマンス分析ツール

```c
// デバッグモードでの統計情報
typedef struct {
    size_t total_io_ops;
    size_t total_cpu_ops;
    size_t cache_hits;
    size_t cache_misses;
    uint64_t total_time_ns;

    struct {
        size_t invocations;
        size_t matches;
        size_t rejects;
        uint64_t time_ns;
    } filter_stats[FILTER_TYPE_COUNT];

} glob_statistics_t;

// 統計の取得
glob_statistics_t* rbc_glob_get_statistics_v2(void);

// 統計のリセット
void rbc_glob_reset_statistics_v2(void);

// 統計の表示
void rbc_glob_print_statistics_v2(FILE *fp);
```

### B. デバッグツール

```c
// ASTのダンプ
void rbc_glob_dump_ast(glob_node_t *ast, FILE *fp);

// 実行プランのダンプ
void rbc_glob_dump_plan(execution_plan_t *plan, FILE *fp);

// 実行トレース
void rbc_glob_enable_trace(bool enable);
void rbc_glob_set_trace_file(FILE *fp);
```

### C. プロファイリング

```bash
# プロファイル付きビルド
make profile

# プロファイル実行
./bench_glob_v2

# 結果分析
gprof ./bench_glob_v2 gmon.out > profile.txt
```

---

## まとめ

glob v2は、fnmatchの最適化を前提とした、完全なゼロからの再設計です。

**主要な改善点**:
1. **ファイルシステムI/Oの劇的な削減**（3-10倍）
2. **明確なアーキテクチャ**（パース→最適化→実行）
3. **複数パターンの統合最適化**
4. **拡張性と保守性の向上**

**実装計画**:
- Phase 1-2（6週間）で基本機能と最適化を実装
- Phase 3（3週間）で高度な最適化
- Phase 4（2週間）で統合とテスト

**期待される効果**:
- 全体で**5-10倍の高速化**
- メモリ効率の向上（一部で32倍削減）
- Ruby完全互換性の維持

この設計により、rbcglobは業界最速のglob実装を目指します。
