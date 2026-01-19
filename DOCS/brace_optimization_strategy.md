# ブレース展開の最適化戦略（詳細設計）

## 目次
1. [現状の問題点](#現状の問題点)
2. [最適化アプローチ](#最適化アプローチ)
3. [実装戦略](#実装戦略)
4. [性能予測](#性能予測)
5. [実装例](#実装例)

---

## 現状の問題点

### 問題1: 重複したファイルシステムアクセス

**現在の処理**:
```
パターン: test_{a,b,c}.txt

展開 → test_a.txt, test_b.txt, test_c.txt

各パターンで独立処理:
  1. opendir(".")          ← 3回実行
  2. readdir() ループ       ← 3回実行
  3. fnmatch("test_a.txt") ← 全ファイルに対して
  4. closedir()            ← 3回実行
```

**コスト分析**:
- `opendir()`: ~5-10 μs × N回
- `readdir()`: ~1 μs × ファイル数 × N回
- `stat()`: ~2 μs × ファイル数 × N回（必要に応じて）

100ファイルのディレクトリで `{a,b,c,d,e}` (5展開) の場合:
- 総コスト: `(10 + 1×100 + 2×100) × 5 = 1550 μs`

---

### 問題2: メモリアクセスパターンの非効率性

```c
// 現在: 各展開パターンで独立してメモリ確保
for (int i = 0; i < N; i++) {
    char **results_i = malloc(...);  // N回のmalloc
    // 処理...
    free(results_i);                  // N回のfree
}
```

**問題**:
- アロケータのオーバーヘッド
- キャッシュミス率の増加
- メモリフラグメンテーション

---

### 問題3: パターンマッチングの重複評価

```c
// 各展開で同じプレフィックス/サフィックスを評価
match("test_a.txt", "file_x.c")  // prefix "test_" を評価
match("test_b.txt", "file_x.c")  // 再度 prefix "test_" を評価 ← 無駄
match("test_c.txt", "file_x.c")  // 再度 prefix "test_" を評価 ← 無駄
```

---

## 最適化アプローチ

### アプローチ1: 共通部分式の事前計算（CSE: Common Subexpression Elimination）

```
パターン: src/test_{a,b,c}.{h,c}

分解:
  common_prefix = "src/test_"
  alternatives1 = ["a", "b", "c"]
  middle = "."
  alternatives2 = ["h", "c"]

デカルト積:
  {a,b,c} × {h,c} = 6パターン

最適化:
  1. "src/" までディレクトリ移動（1回）
  2. "test_" プレフィックスでフィルタ（1回）
  3. {a,b,c} チェック（O(1) ハッシュセット）
  4. {h,c} サフィックスチェック（O(1) ハッシュセット）
```

---

### アプローチ2: ディレクトリスキャンの集約

```c
// 【現在】N回のスキャン
for pattern in [test_a.txt, test_b.txt, test_c.txt]:
    scan_directory(".")
    filter_by(pattern)

// 【最適化】1回のスキャン
alternatives = {a, b, c}
scan_directory(".")  // 1回だけ
for file in directory:
    if prefix_match(file, "test_") and
       suffix_match(file, ".txt"):
        middle = extract_middle(file)
        if middle in alternatives:  // O(1)
            add_to_results(file)
```

**削減率**:
- ディレクトリI/O: **N回 → 1回** (N倍高速化)
- システムコール数: **O(N × F) → O(F)** (F = ファイル数)

---

### アプローチ3: 階層的ブレース展開

```c
// ネストしたブレース: {a,b}/{x,y}.c

【現在】4パターンを独立処理:
  a/x.c
  a/y.c
  b/x.c
  b/y.c

【最適化】2段階処理:
  Stage 1: {a,b} を展開 → ディレクトリ "a/", "b/" へ移動
    scan("a/")  // x.c, y.c を探す
    scan("b/")  // x.c, y.c を探す

  Stage 2: 各ディレクトリで {x,y}.c をマッチ
```

---

## 実装戦略

### Phase 1: データ構造の設計

```c
/// @brief ブレース展開の最適化情報
typedef struct rbc_brace_info_s {
    // 分解されたパターン
    char *common_prefix;      // 共通プレフィックス
    char *common_suffix;      // 共通サフィックス

    // 代替文字列
    char **alternatives;      // 代替リスト
    size_t alt_count;         // 代替の数

    // ハッシュセット（高速ルックアップ用）
    rbc_string_set_t *alt_set;

    // 最適化ヒント
    bool is_simple;           // ワイルドカードなし
    bool has_wildcard_prefix; // プレフィックスにワイルドカード
    bool has_wildcard_suffix; // サフィックスにワイルドカード
    bool has_wildcard_alt;    // 代替にワイルドカード

    // ネストした展開
    struct rbc_brace_info_s *nested;

} rbc_brace_info_t;

/// @brief 文字列集合（O(1)ルックアップ）
typedef struct {
    char **strings;
    size_t count;
    uint32_t *hashes;  // プリ計算されたハッシュ値
} rbc_string_set_t;
```

---

### Phase 2: パターン解析アルゴリズム

```c
/**
 * ブレース展開パターンを解析して最適化情報を抽出
 *
 * 例:
 *   入力: "src/test_{a,b,c}.{h,c}"
 *   出力:
 *     prefix = "src/test_"
 *     alternatives[0] = {a,b,c}
 *     middle = "."
 *     alternatives[1] = {h,c}
 */
static rbc_brace_info_t* rbc_brace_analyze(
    const char *pattern,
    rbc_arena_t *arena)
{
    rbc_brace_info_t *info = rbc_arena_alloc(arena, sizeof(*info));
    if (!info) return NULL;

    // 1. 最初のブレースを検索
    const char *brace_open = strchr(pattern, '{');
    if (!brace_open) {
        // ブレースなし → 最適化不要
        return NULL;
    }

    // 2. プレフィックス抽出
    size_t prefix_len = brace_open - pattern;
    info->common_prefix = rbc_arena_strndup(arena, pattern, prefix_len);

    // 3. 対応する閉じブレースを探す（ネスト考慮）
    const char *brace_close = find_matching_brace(brace_open);
    if (!brace_close) {
        return NULL;  // 不正なパターン
    }

    // 4. 代替文字列の抽出
    const char *alt_start = brace_open + 1;
    const char *alt_end = brace_close;
    info->alternatives = parse_comma_separated(
        alt_start, alt_end, arena, &info->alt_count);

    // 5. サフィックス抽出
    const char *suffix_start = brace_close + 1;
    info->common_suffix = rbc_arena_strdup(arena, suffix_start);

    // 6. ネストチェック
    if (strchr(suffix_start, '{')) {
        info->nested = rbc_brace_analyze(suffix_start, arena);
    } else {
        info->nested = NULL;
    }

    // 7. 最適化ヒントの生成
    analyze_optimization_hints(info);

    // 8. ハッシュセットの構築
    info->alt_set = build_string_set(
        info->alternatives, info->alt_count, arena);

    return info;
}
```

---

### Phase 3: 最適化された実行エンジン

```c
/**
 * 最適化されたブレース展開マッチング
 */
static void rbc_walk_brace_optimized(
    const rbc_brace_info_t *info,
    const char *base_path,
    rbc_walker_ctx_t *ctx)
{
    // ケース分岐
    if (info->is_simple) {
        // ケース1: 単純な展開（ワイルドカードなし）
        walk_simple_brace(info, base_path, ctx);
    }
    else if (info->has_wildcard_prefix && !info->has_wildcard_alt) {
        // ケース2: prefix*{a,b,c}
        walk_wildcard_prefix_brace(info, base_path, ctx);
    }
    else if (!info->has_wildcard_prefix && info->has_wildcard_alt) {
        // ケース3: test_{*.c,*.h}
        walk_literal_prefix_wildcard_alt(info, base_path, ctx);
    }
    else {
        // ケース4: 複雑なパターン → フォールバック
        walk_brace_fallback(info, base_path, ctx);
    }
}

/**
 * ケース1: 単純なブレース展開
 * 例: test_{a,b,c}.txt
 */
static void walk_simple_brace(
    const rbc_brace_info_t *info,
    const char *base_path,
    rbc_walker_ctx_t *ctx)
{
    // ディレクトリパスの構築
    char dir_path[PATH_MAX];
    extract_directory_from_prefix(info->common_prefix, dir_path);

    char full_dir[PATH_MAX];
    join_path(full_dir, base_path, dir_path);

    // ファイル名プレフィックスの抽出
    const char *filename_prefix = extract_filename_from_prefix(
        info->common_prefix);
    size_t prefix_len = strlen(filename_prefix);
    size_t suffix_len = strlen(info->common_suffix);

    // ディレクトリを1回だけ開く
    DIR *dir = opendir(full_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t name_len = strlen(name);

        // 早期リジェクト: 長さチェック
        if (name_len < prefix_len + suffix_len) {
            continue;
        }

        // プレフィックスマッチ（memcmp - 高速）
        if (memcmp(name, filename_prefix, prefix_len) != 0) {
            continue;
        }

        // サフィックスマッチ（memcmp - 高速）
        const char *name_suffix = name + name_len - suffix_len;
        if (memcmp(name_suffix, info->common_suffix, suffix_len) != 0) {
            continue;
        }

        // 中間部分の抽出
        size_t middle_len = name_len - prefix_len - suffix_len;
        char middle[middle_len + 1];
        memcpy(middle, name + prefix_len, middle_len);
        middle[middle_len] = '\0';

        // ハッシュセットでO(1)チェック
        if (string_set_contains(info->alt_set, middle)) {
            // マッチ！
            char result_path[PATH_MAX];
            snprintf(result_path, PATH_MAX, "%s/%s", full_dir, name);
            add_result(ctx->results, result_path);
        }
    }

    closedir(dir);
}
```

---

### Phase 4: ハッシュセットの実装

```c
/// @brief 文字列ハッシュ関数（FNV-1a）
static uint32_t string_hash(const char *str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

/// @brief 文字列セットの構築
static rbc_string_set_t* build_string_set(
    char **strings,
    size_t count,
    rbc_arena_t *arena)
{
    rbc_string_set_t *set = rbc_arena_alloc(arena, sizeof(*set));
    set->strings = strings;
    set->count = count;
    set->hashes = rbc_arena_alloc(arena, sizeof(uint32_t) * count);

    // ハッシュ値を事前計算
    for (size_t i = 0; i < count; i++) {
        set->hashes[i] = string_hash(strings[i]);
    }

    return set;
}

/// @brief 文字列セットの検索（O(1)平均）
static bool string_set_contains(
    const rbc_string_set_t *set,
    const char *str)
{
    uint32_t hash = string_hash(str);

    // 線形探索（小さい集合では十分高速）
    for (size_t i = 0; i < set->count; i++) {
        if (set->hashes[i] == hash &&
            strcmp(set->strings[i], str) == 0) {
            return true;
        }
    }
    return false;
}

// 注: 代替数が多い場合（>16）はハッシュテーブルを使用
```

---

## 性能予測

### ベンチマークシナリオ

| パターン | ファイル数 | 展開数 | 現在 (ms) | 最適化 (ms) | 高速化 |
|---------|----------|--------|-----------|-------------|--------|
| `test_{a,b,c}.txt` | 100 | 3 | 0.45 | 0.15 | **3.0x** |
| `{a,b,c,d,e}/*.c` | 100 | 5 | 0.75 | 0.15 | **5.0x** |
| `src/{lib,app}/{a,b}.h` | 200 | 4 | 1.20 | 0.30 | **4.0x** |
| `{1..10}/*.txt` | 1000 | 10 | 15.0 | 2.5 | **6.0x** |

---

### コスト分析

**現在の実装**:
```
T_current = N × (T_opendir + F × T_readdir + F × T_match + T_closedir)
          = N × (10μs + 100×1μs + 100×0.5μs + 5μs)
          = N × 165μs

N=5 の場合: 825μs
```

**最適化版**:
```
T_optimized = T_opendir + F × (T_readdir + T_prefix + T_suffix + T_hash) + T_closedir
            = 10μs + 100×(1μs + 0.1μs + 0.1μs + 0.2μs) + 5μs
            = 10μs + 100×1.4μs + 5μs
            = 155μs

高速化率: 825μs / 155μs = 5.3x
```

---

## 実装例：完全なコード

### 統合例

```c
// グローバルエントリポイント
bool rbc_glob_with_brace_optimization(
    const char **patterns,
    size_t npatterns,
    unsigned flags,
    const char *base,
    bool sort,
    char ***out,
    size_t *count,
    size_t **lengths)
{
    rbc_ctx_t *ctx = create_context();
    rbc_results_t results;
    init_results(&results, ctx);

    for (size_t i = 0; i < npatterns; i++) {
        const char *pattern = patterns[i];

        // ブレース展開を解析
        rbc_brace_info_t *brace_info = rbc_brace_analyze(
            pattern, &ctx->arena);

        if (brace_info && should_optimize(brace_info)) {
            // 最適化パスを使用
            rbc_walk_brace_optimized(brace_info, base, &ctx);
        } else {
            // 従来の方式にフォールバック
            rbc_walk_traditional(pattern, base, &ctx);
        }
    }

    // 結果のパッケージング
    package_results(&results, out, count, lengths);
    return true;
}

// 最適化すべきか判定
static bool should_optimize(const rbc_brace_info_t *info) {
    // 代替数が少ない場合は最適化の効果が薄い
    if (info->alt_count < 3) return false;

    // 単純なパターンは最適化効果が高い
    if (info->is_simple) return true;

    // ワイルドカードが複雑な場合はフォールバック
    if (info->has_wildcard_prefix && info->has_wildcard_alt) {
        return false;
    }

    return true;
}
```

---

## まとめ

### 最適化の核心

1. **ディレクトリアクセスの削減**: N回 → 1回
2. **パターンマッチングの効率化**: memcmp + ハッシュセット
3. **メモリアロケーションの削減**: アリーナアロケータの活用
4. **キャッシュ効率の向上**: 連続メモリアクセス

### 期待される効果

- **単純なブレース展開**: 3-6倍高速化
- **ネストしたブレース**: 2-4倍高速化
- **大規模ディレクトリ**: 5-10倍高速化

### 次のステップ

1. Phase 1の実装（データ構造）
2. 単純ケースの最適化
3. ベンチマークによる検証
4. 複雑ケースへの拡張
