# rbcglob 最適化戦略 - Ruby実装との比較分析

## 現状の性能

### ベンチマーク結果 (P0-P4実装後)

```
Pattern: tests/**/* - Recursive all (4919 files)
  rbcglob:     130.93ms
  Ruby:          8.93ms
  差分:        14.7x slower
```

### 既に実装済みの最適化

- ✅ **P0-1**: リテラルセグメント高速パス (strcmp直接比較)
- ✅ **P0-2**: Prefix/Suffix最適化 (既存実装の検証)
- ✅ **P0-3**: 隠しファイルスキップ (明示的なドットチェック)
- ✅ **P1-1**: 結果配列事前割り当て (INITIAL_RESULT_CAPACITY=64)
- ✅ **P2-1**: 先頭リテラルセグメント直接ナビゲーション (33%高速化)
- ✅ **P2-2**: 再帰セグメント追跡 (has_recursive_segmentフラグ)
- ✅ **P3**: stat()最小化 (d_type使用、76%高速化 for tests/**/*.c)
- ✅ **P4**: マイクロ最適化群
  - readdir事前割り当て (capacity 64, 指数的成長)
  - キャッシュローカリティ (g_last_cache_hit)
  - path_join再利用

### 性能分析

**小規模パターン** (*.md, tests/*.c)
- 結果: <0.02ms ⭐⭐⭐ (優秀)
- P0-P4の最適化が効果的

**中規模パターン** (tests/**/*.c)
- 結果: 0.66ms (P2基準3.10msから78%改善) ⭐⭐⭐
- stat()最小化が大きな効果

**大規模パターン** (tests/**/*)
- 結果: 130.93ms ❌ (Rubyの14.7倍遅い)
- マイクロ最適化では限界、アーキテクチャレベルの改善が必要

---

## 未実装の重要な最適化 (Ruby実装との比較)

### 🔴 P5: ハッシュテーブルによるディレクトリキャッシュ

**優先度**: ⭐⭐⭐⭐⭐ (最重要)

#### 現在の問題

```c
// traverse.c - 線形探索 O(n)
static ssize_t get_cached_dir_index(const char *path) {
    for (size_t i = 0; i < g_dir_cache_count; i++) {
        if (strcmp(g_dir_cache[i].path, path) == 0)
            return (ssize_t)i;
    }
    // ...
}
```

- **tests/\*\*/\*** で4919ファイル処理時、数千回のキャッシュ検索が発生
- 各検索でO(n)の線形探索 → O(n²)の複雑度
- キャッシュエントリ数が増えるほど劣化

#### Ruby実装の特徴

**参照**: [ruby/ruby/dir.c#L2634-L2663](https://github.com/ruby/ruby/blob/main/dir.c#L2634-L2663)

```c
// Rubyはエントリをソート配列で管理、効率的なアクセス
static ruby_glob_entries_t *glob_opendir(ruby_glob_entries_t *ent,
                                          DIR *dirp, int flags,
                                          rb_encoding *enc) {
    #ifdef _WIN32
    if ((capacity = dirp->nfiles) > 0) {
        // Windows: ディレクトリサイズを事前取得
        if (!(newp = GLOB_ALLOC_N(rb_dirent_t, capacity))) {
            // ...
        }
    }
    #endif

    while ((dp = READDIR(dirp, enc)) != NULL) {
        if (count >= capacity) {
            capacity += 256;  // 固定増分で効率的
            // ...
        }
    }
}
```

#### 実装方針

**オプション1: シンプルハッシュテーブル (推奨)**

```c
#define HASH_TABLE_SIZE 1024

typedef struct cache_hash_entry {
    char *key;
    size_t cache_index;
    struct cache_hash_entry *next;  // チェイン法
} cache_hash_entry_t;

static cache_hash_entry_t *g_cache_hash[HASH_TABLE_SIZE];

// djb2 hash
static unsigned long hash_string(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash % HASH_TABLE_SIZE;
}

static ssize_t get_cached_dir_index(const char *path) {
    unsigned long h = hash_string(path);
    cache_hash_entry_t *entry = g_cache_hash[h];

    while (entry) {
        if (strcmp(entry->key, path) == 0)
            return entry->cache_index;
        entry = entry->next;
    }

    // キャッシュミス - 新規登録
    // ...
}
```

**期待効果**:
- キャッシュ検索: O(n) → O(1)
- tests/\*\*/\*: 130.93ms → **15-30ms** (5-8x高速化)
- メモリオーバーヘッド: ~16KB (ハッシュテーブル) + エントリ数 × 24B

**実装難易度**: 中 (2-3時間)

---

### 🟡 P6: ソート処理の効率化

**優先度**: ⭐⭐⭐

#### 現在の問題

```c
// 常にqsortを実行
qsort(results, result_count, sizeof(char *), compare_strings);
```

- 大量のファイル (4919個) で毎回O(n log n)のソート
- ユーザーがソート順を気にしない場合でも実行

#### Ruby実装の特徴

**参照**: [ruby/ruby/dir.c#L2608-L2634](https://github.com/ruby/ruby/blob/main/dir.c#L2608-L2634)

```c
static void glob_dir_finish(ruby_glob_entries_t *ent, int flags) {
    if (flags & FNM_GLOB_NOSORT) {
        check_closedir(ent->nosort.dirp);
        ent->nosort.dirp = NULL;  // ソートスキップ!
    }
    else if (ent->sort.entries) {
        for (size_t i = 0, count = ent->sort.count; i < count;) {
            GLOB_FREE(ent->sort.entries[i++]);
        }
        // ...
    }
}
```

#### 実装方針

```c
// dirglob.h に追加
#define RBCGLOB_FLAG_NOSORT (1 << 8)  // ソートスキップフラグ

// traverse.c
if (!(flags & RBCGLOB_FLAG_NOSORT)) {
    qsort(results, result_count, sizeof(char *), compare_strings);
}
```

**期待効果**:
- tests/\*\*/\*でソートスキップ時: **-20ms** (15%高速化)
- API互換性: フラグで制御可能

**実装難易度**: 低 (30分)

---

### 🟡 P7: ディレクトリエントリの早期スキップ

**優先度**: ⭐⭐

#### 現在の問題

```c
// すべてのエントリを処理
struct dirent *entry;
while ((entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;

    // パターンマッチング後に"."と".."を除外
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        continue;
}
```

#### Ruby実装の特徴

**参照**: [ruby/ruby/dir.c#L848-L881](https://github.com/ruby/ruby/blob/main/dir.c#L848-L881)

```c
static int to_be_skipped(const struct dirent *dp) {
    const char *name = dp->d_name;
    if (name[0] != '.') return FALSE;

    #ifdef HAVE_DIRENT_NAMLEN
    switch (NAMLEN(dp)) {
      case 2:
        if (name[1] != '.') return FALSE;
      case 1:
        return TRUE;  // "." or ".."
    }
    #else
    if (!name[1]) return TRUE;        // "."
    if (name[1] != '.') return FALSE;
    if (!name[2]) return TRUE;        // ".."
    #endif
    return FALSE;
}
```

#### 実装方針

```c
static inline bool should_skip_entry(const char *name) {
    if (name[0] != '.') return false;
    if (name[1] == '\0') return true;   // "."
    if (name[1] == '.' && name[2] == '\0') return true;  // ".."
    return false;
}

// readdir直後に適用
while ((entry = readdir(dir)) != NULL) {
    if (should_skip_entry(entry->d_name))
        continue;
    // ...
}
```

**期待効果**:
- tests/\*\*/\*: **-3-5ms** (3-4%高速化)
- 各ディレクトリで2エントリ分の処理削減

**実装難易度**: 低 (15分)

---

### 🟠 P8: メモリアロケーション戦略の改善

**優先度**: ⭐⭐

#### 現在の問題

- 個別`malloc()`/`free()`呼び出し
- 4919ファイルで数千回のアロケーション
- メモリフラグメンテーション
- エラー時のメモリリーク可能性

#### Ruby実装の特徴

**参照**: [ruby/ruby/dir.c#L1727-L1752](https://github.com/ruby/ruby/blob/main/dir.c#L1727-L1752)

```c
static void *glob_alloc_n(size_t x, size_t y) {
    size_t n = glob_alloc_size(x, y);
    if (n == 0) return NULL;
    return malloc(n);
}

#define GLOB_ALLOC(type) ((type *)malloc(sizeof(type)))
#define GLOB_ALLOC_N(type, n) ((type *)glob_alloc_n(sizeof(type), n))
#define GLOB_REALLOC_N(ptr, n) glob_realloc_n(ptr, sizeof(*(ptr)), n)
#define GLOB_FREE(ptr) free(ptr)
```

#### 実装方針

**メモリプール方式**:

```c
typedef struct {
    char *pool;
    size_t size;
    size_t used;
} string_pool_t;

static string_pool_t g_string_pool;

static char *pool_strdup(const char *str) {
    size_t len = strlen(str) + 1;
    if (g_string_pool.used + len > g_string_pool.size) {
        // プール拡張
        size_t new_size = g_string_pool.size * 2;
        char *new_pool = realloc(g_string_pool.pool, new_size);
        if (!new_pool) return NULL;
        g_string_pool.pool = new_pool;
        g_string_pool.size = new_size;
    }

    char *result = g_string_pool.pool + g_string_pool.used;
    memcpy(result, str, len);
    g_string_pool.used += len;
    return result;
}
```

**期待効果**:
- tests/\*\*/\*: **-10-15ms** (8-12%高速化)
- メモリアロケーション: 数千回 → 数十回

**実装難易度**: 中 (2-3時間)

---

### 🟠 P9: USE_OPENDIR_AT最適化

**優先度**: ⭐⭐⭐ (システム依存)

#### 現在の問題

```c
// 常にフルパス文字列を構築
char *full_path = path_join(base, entry->d_name);
stat(full_path, &st);
```

- 深い階層で長いパス文字列を何度も構築
- システムコールのパス解決オーバーヘッド

#### Ruby実装の特徴

**参照**: [ruby/ruby/dir.c#L0-L40](https://github.com/ruby/ruby/blob/main/dir.c#L0-L40)

```c
#ifndef USE_OPENDIR_AT
# if defined(HAVE_FDOPENDIR) && defined(HAVE_DIRFD) && \
    defined(HAVE_OPENAT) && defined(HAVE_FSTATAT)
#   define USE_OPENDIR_AT 1
# endif
#endif
```

**参照**: [ruby/ruby/dir.c#L1752-L1787](https://github.com/ruby/ruby/blob/main/dir.c#L1752-L1787)

```c
#if USE_OPENDIR_AT
struct fstatat_args {
    int fd;
    int flag;
    const char *path;
    struct stat *pst;
};

static void *nogvl_fstatat(void *args) {
    struct fstatat_args *p = args;
    return (void *)(VALUE)fstatat(p->fd, p->path, p->pst, p->flag);
}
#endif
```

#### 実装方針

```c
// CMakeLists.txt でチェック
check_function_exists(fdopendir HAVE_FDOPENDIR)
check_function_exists(dirfd HAVE_DIRFD)
check_function_exists(openat HAVE_OPENAT)
check_function_exists(fstatat HAVE_FSTATAT)

// traverse.c
#if defined(HAVE_FDOPENDIR) && defined(HAVE_DIRFD) && \
    defined(HAVE_OPENAT) && defined(HAVE_FSTATAT)
#define USE_OPENDIR_AT 1

static bool is_directory_at(int dirfd, const char *name) {
    struct stat st;
    if (fstatat(dirfd, name, &st, 0) != 0)
        return false;
    return S_ISDIR(st.st_mode);
}

// traverse時にdirfdを渡す
int fd = dirfd(dir);
if (is_directory_at(fd, entry->d_name)) {
    // ...
}
#endif
```

**期待効果**:
- tests/\*\*/\*: **-15-25ms** (12-20%高速化)
- パス文字列構築の削減
- システムコールパス解決の効率化

**実装難易度**: 高 (4-5時間、クロスプラットフォーム対応必要)

---

## 総合的な最適化ロードマップ

### Phase 1: 即効性の高い最適化 (1週間)

1. **P7: 早期スキップ** (15分)
   - 実装が簡単で即効性あり
   - 期待効果: -3-5ms

2. **P6: ソートオプション** (30分)
   - APIフラグ追加のみ
   - 期待効果: -20ms (NOSORT時)

3. **P5: ハッシュテーブルキャッシュ** (2-3時間) ⭐ **最重要**
   - 最大の効果が期待できる
   - 期待効果: -80-100ms (5-8x高速化)

**Phase 1合計期待効果**: 130.93ms → **25-35ms** (4-5x高速化)

### Phase 2: 構造的改善 (2週間)

4. **P8: メモリプール** (2-3時間)
   - Phase 1の効果を確認後に実装
   - 期待効果: さらに-5-10ms

5. **P9: USE_OPENDIR_AT** (4-5時間)
   - Linux/Macで効果大
   - 期待効果: さらに-10-15ms

**Phase 2合計期待効果**: 25-35ms → **10-20ms** (Ruby比1-2x)

### Phase 3: 極限最適化 (必要に応じて)

6. **並列化**: OpenMP/pthreadsでディレクトリ並列探索
7. **SIMD**: パターンマッチングの並列化
8. **JIT**: 頻出パターンのネイティブコード生成

---

## ベンチマーク目標

| パターン | 現在 | Phase 1目標 | Phase 2目標 | Ruby実装 |
|---------|------|------------|------------|----------|
| *.md | 0.01ms | - | - | 0.01ms |
| tests/*.c | 0.02ms | - | - | 0.02ms |
| tests/**/*.c | 0.66ms | 0.50ms | 0.40ms | 0.30ms |
| **tests/\*\*/\*** | **130.93ms** | **25-35ms** | **10-20ms** | **8.93ms** |

---

## 実装の注意事項

### メモリ管理
- すべての最適化でメモリリーク防止を徹底
- エラー時のクリーンアップ処理を確実に

### クロスプラットフォーム
- P9は条件付きコンパイル (#ifdef)
- Windows/Mac/Linux各環境でテスト

### 後方互換性
- P6のフラグは既存コードに影響しない
- デフォルト動作は従来通り

### テスト
- 各最適化後に正確性テスト実行
- ベンチマークで効果測定
- Ruby実装との結果一致を確認

---

## 参考資料

- [Ruby MRI dir.c](https://github.com/ruby/ruby/blob/main/dir.c)
- [Rust glob実装](https://github.com/rust-lang/glob/blob/master/src/lib.rs)
- [glibc glob実装](https://sourceware.org/git/?p=glibc.git;a=blob;f=posix/glob.c)

---

**最終更新**: 2026-01-06
**現在のステータス**: P0-P4実装完了、Phase 1準備中
