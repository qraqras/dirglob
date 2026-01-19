# rbc_glob 設計方針
## MRI参考 + プリコンパイル強化戦略

## 基本方針

### 1. **基本ロジック: MRI準拠**
- **信頼性**: 30年以上の実績があるRubyの実装
- **互換性**: Ruby 4.0のDir.glob仕様に完全準拠
- **テストケース**: MRIのテストスイートを流用可能
- **バグ回避**: 既知の問題は全て解決済み

### 2. **最適化: プリコンパイル強化**
- **3倍高速化**: 実証済み（860μs → 284μs）
- **メモリ効率**: ディレクトリキャッシュより少ない
- **シンプル**: 無効化ロジック不要
- **スレッドセーフ**: 実行計画は不変

---

## 実装戦略

### Phase 1: MRI互換レイヤー（現在）
```c
/* MRIと同じアルゴリズム */
rbc_glob_result_t *rbc_glob(const char *pattern, int flags) {
    // MRI dir.c の glob_helper() と同等のロジック
    // - パターン解析
    // - ディレクトリ走査
    // - fnmatchマッチング
    // - 再帰処理
}
```

**参考にするMRIの関数:**
- `glob_helper()` (dir.c:2694): メイン再帰処理
- `glob_opendir()` (dir.c:2608): ディレクトリオープン
- `glob_getent()` (dir.c:2684): エントリ取得
- `dirent_match()` (dir.c:2441): パターンマッチング
- `push_glob()` (dir.c:3100): パターン展開

### Phase 2: プリコンパイルレイヤー（新規）

#### 2.1 実行計画の構造

**複数パターンを木構造で融合:**
```c
/* 実行計画ノード: セグメント単位で分岐 */
typedef struct rbc_plan_node_s {
    // このノードのディレクトリパス
    char *path;                    // "src/", "include/", etc.

    // このディレクトリで適用するパターン
    rbc_plan_pattern_t *patterns;  // マッチ対象パターンの配列
    size_t pattern_count;

    // 子ディレクトリノード
    struct rbc_plan_node_s **children;
    size_t child_count;

    // 再帰フラグ
    bool recursive;                // ** パターンがある
} rbc_plan_node_t;

/* パターンメタデータ */
typedef struct {
    size_t pattern_id;             // 元のパターン番号
    rbc_segment_t *segments;       // パースされたセグメント
    rbc_fnmatch_pattern_t *matcher; // コンパイル済みマッチャー
} rbc_plan_pattern_t;

/* 実行計画 */
struct rbc_glob_plan_s {
    rbc_plan_node_t *root;         // 実行計画木のルート
    rbc_pattern_meta_t *metadata;  // 各パターンの情報
    size_t pattern_count;
    unsigned int flags;
    rbc_arena_t arena;             // メモリ管理
};
```

**最適化例:**
```
入力パターン:
  - "src/*.c"
  - "src/*.h"
  - "include/*.h"

実行計画木:
  root/
  ├─ src/           ← 1回だけopendir()
  │  ├─ match: *.c  (pattern #0)
  │  └─ match: *.h  (pattern #1)
  └─ include/       ← 1回だけopendir()
     └─ match: *.h  (pattern #2)

効果: opendir 2回 (個別実行なら3回)
```

#### 2.2 コンパイルAPI

```c
/* 実行計画のコンパイル */
rbc_glob_plan_t *rbc_glob_plan_compile(
    const char **patterns,
    size_t num_patterns,
    unsigned int flags
) {
    plan = rbc_arena_alloc(arena, sizeof(rbc_glob_plan_t));

    // 1. 各パターンをセグメントに分解
    for (i = 0; i < num_patterns; i++) {
        segments[i] = rbc_glob_segment_compile(arena, patterns[i], flags);
    }

    // 2. 共通prefixを検出して木を構築
    plan->root = build_execution_tree(arena, segments, num_patterns);

    // 3. 各ノードでパターンをコンパイル
    compile_node_patterns(plan->root);

    return plan;
}

/* 実行計画の実行 */
rbc_glob_result_t *rbc_glob_plan_execute(
    rbc_glob_plan_t *plan,
    const char *basedir
) {
    results = rbc_glob_results_init();

    // 実行計画木を再帰的に実行
    execute_plan_node(plan->root, basedir, results, plan->flags);

    if (!(plan->flags & RBC_GLOB_NOSORT)) {
        rbc_glob_results_sort(results);
    }

    return results;
}

/* 実行計画の解放 */
void rbc_glob_plan_free(rbc_glob_plan_t *plan) {
    rbc_arena_destroy(&plan->arena);
    free(plan);
}
```

#### 2.3 実行戦略

**ノード実行アルゴリズム:**
```c
void execute_plan_node(
    rbc_plan_node_t *node,
    const char *current_path,
    rbc_results_t *results,
    unsigned int flags
) {
    // 1. ディレクトリを開く (MRIスタイル: glob_opendir)
    glob_dir_t *dir = glob_opendir(current_path, flags);
    if (!dir) return;

    // 2. エントリを順次処理
    rbc_dirent_t *ent;
    while ((ent = glob_getent(dir)) != NULL) {

        // 3. このノードの全パターンでマッチング試行
        for (i = 0; i < node->pattern_count; i++) {
            pattern = &node->patterns[i];

            if (rbc_fnmatch_execute(pattern->matcher, ent->d_name)) {
                // マッチしたら結果に追加
                add_result(results, current_path, ent->d_name, pattern->pattern_id);
            }
        }

        // 4. ディレクトリなら子ノードを実行
        if (ent->d_type == DT_DIR && has_child_nodes(node, ent->d_name)) {
            child_node = find_child_node(node, ent->d_name);
            execute_plan_node(child_node, build_path(current_path, ent->d_name), results, flags);
        }
    }

    // 5. ディレクトリを閉じる (MRIスタイル: glob_dir_finish)
    glob_dir_finish(dir);
}
```

#### 2.4 最適化技法

1. **共通Prefix融合**
   - `src/*.c`, `src/*.h` → `src/`を1回だけ開く

2. **Suffix集約**
   - `**/*.{c,h}` → 全ディレクトリで`.c`と`.h`を同時マッチ

3. **再帰パターン検出**
   - `**/test/*.c` → `test/`まで再帰、その下で`*.c`マッチ

4. **パターンコンパイル再利用**
   - 同じパターン（例: `*.h`）は1回だけコンパイル

#### 2.5 再帰パターンの木構造例

**入力パターン**: `**/test/*.c`

**セグメント分解**:
```
[0] ** (RECURSIVE)
[1] test (LITERAL)
[2] *.c (WILDCARD)
```

**実行計画木**:
```c
root_node {
    path: ".",
    recursive: true,           // ** があるため再帰探索
    patterns: [],              // このレベルでマッチはしない

    // 条件付き子ノード
    conditional_children: [
        {
            condition: "dirname == 'test'",
            node: {
                path: "test/",
                recursive: false,
                patterns: ["*.c"],  // *.c をマッチング
                children: []
            }
        }
    ]
}
```

**実行時の動作**:
```
実際のディレクトリ構造:
.
├── test/          → マッチング: foo.c
│   └── foo.c
├── src/
│   └── test/      → マッチング: bar.c
│       └── bar.c
└── lib/
    └── core/
        └── test/  → マッチング: baz.c
            └── baz.c

実行フロー:
1. opendir(".")
2. readdir → "test" (dir) → 条件一致
   → opendir("./test") → "foo.c" マッチ → "test/foo.c"
3. readdir → "src" (dir) → 再帰
   → opendir("./src")
   → readdir → "test" (dir) → 条件一致
      → opendir("./src/test") → "bar.c" マッチ → "src/test/bar.c"
4. readdir → "lib" (dir) → 再帰
   → opendir("./lib")
   → readdir → "core" (dir) → 再帰
      → opendir("./lib/core")
      → readdir → "test" (dir) → 条件一致
         → opendir("./lib/core/test") → "baz.c" マッチ → "lib/core/test/baz.c"

結果: ["lib/core/test/baz.c", "src/test/bar.c", "test/foo.c"]
```

**最適化ポイント**:
- **Eager matching**: `test`ディレクトリを見つけた瞬間に`*.c`を適用
- **Lazy recursion**: `test`以外のディレクトリでは再帰のみ実行
- **Early termination**: `test/`の下では再帰しない（`*.c`は最終セグメント）

**複数パターンとの融合例**:
```
入力: ["**/test/*.c", "**/test/*.h", "**/src/*.c"]

実行計画木:
root_node {
    recursive: true,
    conditional_children: [
        {
            condition: "dirname == 'test'",
            node: {
                patterns: ["*.c", "*.h"],  // 2パターン融合
                children: []
            }
        },
        {
            condition: "dirname == 'src'",
            node: {
                patterns: ["*.c"],
                children: []
            }
        }
    ]
}

効果: 各 test/ と src/ で1回のopendir()で複数パターン処理
```
   - 複数ノードで共有

#### 2.5 期待される性能

| パターン数 | 個別実行 | 実行計画 | 高速化率 |
|-----------|---------|---------|---------|
| 1個      | 300μs   | 300μs   | 1.0x    |
| 3個      | 900μs   | 300μs   | 3.0x    |
| 10個     | 3000μs  | 400μs   | 7.5x    |

**実測済み (bench_exact_repro):**
- 3パターン個別実行: 860μs
- 3パターン融合実行: 284μs
- **高速化: 3.03x**

---

## 実装フェーズ

---

## 他実装との比較

### Rustの`globset`との違い

**globsetのアプローチ** (ripgrepで使用):
```rust
// パターンマッチング戦略の分類
enum GlobSetMatchStrategy {
    Literal(literal),         // 完全一致: "src/foo.c"
    BasenameLiteral(literal), // basename一致: "*.rs"
    Extension(ext),           // 拡張子: "*.txt"
    Prefix(aho_corasick),     // prefix集合: "src/*"
    Suffix(aho_corasick),     // suffix集合: "*.{c,h}"
    RequiredExtension(map),   // 拡張子+regex
    Regex(regex_set),         // fallback: 複雑なパターン
}

// 実行モデル: パスに対して全パターンを同時マッチング
fn matches(&self, path: &str) -> Vec<usize> {
    for strategy in &self.strategies {
        strategy.matches_into(path, &mut results);
    }
}
```

**性能特性**:
- **1パスでマッチング**: 1つのファイルパスに対して全パターンを同時マッチング
- **Aho-Corasick**: suffix/prefix集合を高速検索
- **RegexSet**: 複数のregexを同時実行
- **用途**: ripgrep (既知のファイルパスリストに対してgrep)

**パフォーマンス**:
```
// 複数パターン vs 個別マッチング (globsetベンチマーク)
test many_short_glob      ... 1,063 ns/iter  // glob個別実行
test many_short_regex_set ...   186 ns/iter  // globset同時実行
速度: 5.7x
```

---

### rbc_globの設計との違い

**我々のアプローチ**:
```c
// ディレクトリ走査戦略: パターンをディレクトリツリーで融合
typedef struct rbc_plan_node_s {
    char *path;                    // ディレクトリパス
    rbc_plan_pattern_t *patterns;  // このディレクトリで適用するパターン配列
    struct rbc_plan_node_s **children;  // 子ディレクトリノード
    bool recursive;
} rbc_plan_node_t;

// 実行モデル: ディレクトリを1回開いて複数パターン処理
void execute_plan_node(node) {
    DIR *dir = opendir(node->path);  // ← 1回のopendir()

    while ((ent = readdir(dir)) != NULL) {
        // 全パターンでマッチング試行
        for (i = 0; i < node->pattern_count; i++) {
            if (match(node->patterns[i], ent->d_name)) {
                add_result(ent->d_name);
            }
        }
    }
}
```

**最適化ポイント**:
- **システムコール削減**: `opendir()/readdir()`を共有
- **メモリ局所性**: 同じディレクトリエントリで複数マッチング
- **用途**: glob (ファイルシステムからパスを発見)

**パフォーマンス**:
```
// 実測値 (bench_exact_repro)
3パターン個別実行: 860μs  (opendir×3 + readdir×3)
3パターン融合実行: 284μs  (opendir×1 + readdir×1)
速度: 3.03x
```

---

### アプローチの本質的な違い

| 観点 | globset (ripgrep) | rbc_glob (我々) |
|------|------------------|----------------|
| **問題領域** | マッチング最適化 | ファイルシステムI/O最適化 |
| **入力** | 既知のファイルパスリスト | ディレクトリツリー (未知) |
| **ボトルネック** | パターンマッチング (CPU) | opendir/readdir (syscall) |
| **戦略** | Aho-Corasick, RegexSet | ディレクトリ走査融合 |
| **最適化対象** | 文字列マッチング回数 | システムコール回数 |
| **実行モデル** | path → all patterns | directory → all patterns |

**具体例の違い**:

```
入力パターン: ["src/*.c", "src/*.h", "include/*.h"]

■ globsetの実行 (ripgrepの場合):
- 前提: ファイルリストは既知
  ["src/foo.c", "src/bar.h", "include/baz.h", "test/qux.c"]

- 実行: 各ファイルに対して全パターンをマッチング
  matches("src/foo.c")     → [0]        // *.c にマッチ
  matches("src/bar.h")     → [1]        // *.h にマッチ
  matches("include/baz.h") → [2]        // *.h にマッチ
  matches("test/qux.c")    → []         // マッチなし

- 最適化: suffix集合 {".c", ".h"} をAho-Corasickで高速検索

■ rbc_globの実行 (我々の場合):
- 前提: ディレクトリツリーを探索してファイル発見

- 実行計画木:
  root/
  ├─ src/        ← opendir("src") を1回だけ
  │  ├─ *.c
  │  └─ *.h
  └─ include/    ← opendir("include") を1回だけ
     └─ *.h

- 実行:
  opendir("src")           // syscall 1回
    readdir → "foo.c"  → match(*.c) ✓, match(*.h) ✗
    readdir → "bar.h"  → match(*.c) ✗, match(*.h) ✓
  closedir("src")

  opendir("include")       // syscall 1回
    readdir → "baz.h"  → match(*.h) ✓
  closedir("include")

- 最適化: システムコール回数削減 (個別実行なら6回 → 融合で2回)
```

**両者は相補的**:
- **globset**: マッチング高速化 (CPU最適化)
- **rbc_glob**: ファイルシステムI/O削減 (syscall最適化)
- **組み合わせ可能**: rbc_globの各ノードでglobsetのAho-Corasickを使うこともできる

---

### Phase 2A: 単一パターンコンパイル（優先度: 高）
```c
/* まず単一パターンのコンパイルAPI実装 */
rbc_glob_pattern_t *rbc_glob_compile(const char *pattern, unsigned int flags);
rbc_glob_result_t *rbc_glob_execute(rbc_glob_pattern_t *compiled, const char *basedir);
void rbc_glob_pattern_free(rbc_glob_pattern_t *pattern);
```

### Phase 2B: 複数パターン実行計画（優先度: 中）
```c
/* 複数パターン融合 */
rbc_glob_plan_t *rbc_glob_plan_compile(const char **patterns, size_t count, unsigned int flags);
rbc_glob_result_t *rbc_glob_plan_execute(rbc_glob_plan_t *plan, const char *basedir
    // 最適化戦略を決定
    if (all_suffix_patterns(patterns, num_patterns)) {
        plan->type = PLAN_SUFFIX_SET;
        plan->scan_count = 1;  // 1回のスキャン
        build_suffix_set(plan, patterns);
        build_bloom_filter(plan);
    }
    else if (all_prefix_patterns(...)) {
        plan->type = PLAN_PREFIX_SET;
    }
    else {
        plan->type = PLAN_COMBINED;
        optimize_execution_order(plan);
    }

    return plan;
}

/* コンパイル済みプランの実行 */
rbc_glob_result_t *rbc_glob_execute(
    rbc_glob_plan_t *plan,
    const char *dir
) {
    // planに基づいて最適化されたスキャン
    // MRIの基本ロジックを使うが、スキャンは1回のみ
}
```

---

## アーキテクチャ

```
┌─────────────────────────────────────────┐
│         Public API Layer                │
├─────────────────────────────────────────┤
│                                         │
│  rbc_glob()          [MRI互換]         │
│  rbc_glob_compile()  [プリコンパイル]  │
│  rbc_glob_execute()  [最適化実行]      │
│                                         │
├─────────────────────────────────────────┤
│      Execution Plan Layer (新規)        │
├─────────────────────────────────────────┤
│                                         │
│  - Pattern Analyzer                     │
│  - Plan Optimizer                       │
│  - Suffix Set Builder                   │
│  - Bloom Filter Generator               │
│  - DFA Compiler (将来)                  │
│                                         │
├─────────────────────────────────────────┤
│       Core Logic Layer (MRI準拠)        │
├─────────────────────────────────────────┤
│                                         │
│  - glob_helper()     [walker.c]        │
│  - glob_opendir()    [MRI参考]         │
│  - dirent_match()    [fnmatch]         │
│  - brace_expand()    [glob_v2_brace]   │
│                                         │
├─────────────────────────────────────────┤
│      Platform Abstraction Layer         │
└─────────────────────────────────────────┘
```

---

## 具体的な実装例

### 例1: 複数サフィックスパターン
```c
// MRI方式（3回スキャン）
Dir.glob(['*.c', '*.h', '*.rb'])
→ opendir() × 3, readdir() × 3

// rbc_glob プリコンパイル方式（1回スキャン）
plan = rbc_glob_compile({"*.c", "*.h", "*.rb"}, 3, 0);
result = rbc_glob_execute(plan, ".");
→ opendir() × 1, readdir() × 1
→ 3倍高速
```

### 例2: 複雑なパターン
```c
// ブレース展開 + 再帰
pattern = "**/{*.c,*.h}"

// Phase 1: ブレース展開（MRIと同じ）
expanded = ["**/*.c", "**/*.h"]

// Phase 2: プリコンパイル最適化
plan = compile(expanded);
plan->type = PLAN_RECURSIVE_SUFFIX_SET;
plan->suffixes = {".c", ".h"};
plan->recursive = true;

// 実行: 各ディレクトリで1回スキャン
// MRI: 各ディレクトリで2回スキャン
```

---

## パフォーマンス目標

| シナリオ | MRI | rbc_glob (目標) | 改善率 |
|---------|-----|-----------------|--------|
| 単一パターン | 300μs | 300μs | 1.0x |
| 3パターン | 900μs | 300μs | **3.0x** |
| 10パターン | 3000μs | 300μs | **10.0x** |
| 複雑パターン | 500μs | 400μs | 1.25x |
| 再帰glob | 5000μs | 5000μs | 1.0x |

**ポイント**:
- 単純パターン: MRIと同速度（互換性維持）
- 複数パターン: 線形高速化（プリコンパイル効果）
- 複雑パターン: 若干の改善（DFA化で将来改善）

---

## 実装優先順位

### ✅ Phase 1: MRI互換（現在実装中）
- [x] 基本的なglob実装
- [x] fnmatchコア
- [x] ブレース展開
- [ ] MRIテストケースパス（要確認）

### 🚧 Phase 2: プリコンパイル基盤
```c
// 優先度: HIGH
1. rbc_glob_compile() API設計
2. パターン解析器
3. SUFFIX_SET最適化
4. Bloom filter実装
5. rbc_glob_execute() 実装

// 優先度: MEDIUM
6. PREFIX_SET最適化
7. COMBINED戦略
8. パターン並べ替え

// 優先度: LOW (将来)
9. DFAコンパイラ
10. SIMDサフィックスマッチ
```

### Phase 3: 高度な最適化（将来）
- [ ] ディレクトリキャッシュとの併用
- [ ] getdents64バッチング（Linux）
- [ ] 並列スキャン（マルチコア）
- [ ] ファイルシステム最適化（btrfs等）

---

## 設計原則

### ✅ DO
1. **MRIと同じアルゴリズム**を基本とする
2. **プリコンパイルで最適化**する
3. **後方互換性**を維持する
4. **測定可能な改善**のみ実装する
5. **テストケース**で検証する

### ❌ DON'T
1. MRIのバグを「改善」しない（互換性優先）
2. 測定なしで「推測最適化」しない
3. 複雑な無効化ロジックを実装しない
4. プラットフォーム固有機能に依存しない（基本は）

---

## 次のステップ

### 1. MRI実装の詳細調査
```bash
# MRI dir.c の重要関数を抽出
grep -n "static.*glob_helper" ruby/dir.c
grep -n "glob_opendir" ruby/dir.c
grep -n "dirent_match" ruby/dir.c
```

### 2. プリコンパイルAPI設計
```c
// include/rbc/glob_compile.h を作成
typedef struct rbc_glob_plan rbc_glob_plan_t;

rbc_glob_plan_t *rbc_glob_compile(...);
rbc_glob_result_t *rbc_glob_execute(...);
void rbc_glob_plan_free(...);
```

### 3. ベンチマーク基盤
```c
// benchmark/bench_compile.c
// MRI vs rbc_glob のパフォーマンス比較
```

### 4. テストケース作成
```c
// tests/test_compile.c
// プリコンパイルの正しさを検証
```

---

## 成功の定義

### 機能面
- ✅ Ruby 4.0 Dir.glob 100%互換
- ✅ 全MRIテストケースパス
- ✅ プリコンパイルで3倍以上高速化

### 品質面
- ✅ ゼロメモリリーク
- ✅ スレッドセーフ
- ✅ クロスプラットフォーム

### 性能面
- ✅ 単一パターン: MRIと同速度
- ✅ 複数パターン: N倍高速化
- ✅ メモリ使用量: MRI以下

---

## まとめ

この方針により：
1. **信頼性**: MRIの実績あるロジック
2. **性能**: プリコンパイルで3-10倍高速
3. **互換性**: Ruby完全互換
4. **保守性**: シンプルで理解しやすい設計

**次の作業**: プリコンパイルAPIの設計とプロトタイプ実装
