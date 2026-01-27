# rbcglob v2 設計ドキュメント

## 概要

Ruby 4.0 Dir.glob/File.fnmatch互換のC99ライブラリ。
高速・軽量・シングルヘッダを最優先とする再設計。

---

## 設計方針

### 最優先事項
1. **高速** - 最小限のsyscall、最小限のメモリ割り当て
2. **軽量** - 目標800行以下（現行1700行から削減）
3. **シンプル** - 特殊ケースを減らし、一般ルールで処理

### Ruby互換性の範囲
- **完全互換**: パターンマッチングのセマンティクス
- **非互換可**: 出力パスの正規化（`//` → `/`）
- **非互換可**: 実用性のない特殊ケース

---

## 仕様決定事項

### 1. 連続スラッシュの正規化

**決定**: パターン・出力ともに正規化する

| 入力パターン | 処理 | 出力 |
|-------------|------|------|
| `dir1//file.txt` | `dir1/file.txt`として処理 | `dir1/file.txt` |
| `a///b` | `a/b`として処理 | `a/b` |

**理由**:
- OSがパスを正規化するため実用上の差異なし
- パス構築・比較がシンプルに
- メモリ効率向上

### 2. `.**` パターンの扱い

**決定**: `.*` と同等に扱う（現行維持）

| パターン | 解釈 | 挙動 |
|---------|------|------|
| `.**` | `.` + `**`(末尾) = `.*` | カレントの`.`で始まるエントリ |
| `.**/` | `.`で始まるディレクトリ | 1階層のみ（非再帰） |
| `.**/**` | `.*` + `**` | ドットエントリ配下を再帰 |

**ルール**: `**`は`/`が後続する場合のみ再帰的

### 3. 連続する `**` の畳み込み

Rubyは連続する`**`セグメントを1つに畳み込む。

| パターン | 畳み込み後 | 理由 |
|---------|-----------|------|
| `**/**` | `**` | 連続RECURSIVE |
| `**/**/**` | `**` | 連続RECURSIVE |
| `**/**/` | `**/` | 連続RECURSIVE（trailing slash維持） |
| `.**/**` | 畳み込みなし | `.**`は`.*`（WILDCARD）なので連続しない |
| `**/.**` | 畳み込みなし | `.**`はWILDCARD |
| `**/a/**` | 畳み込みなし | リテラルで分断 |

**実装**: パターン解析時に連続`SEG_RECURSIVE`をスキップ

### 4. `**` + DOTMATCH の挙動

**決定**: Ruby 4.0完全互換

| 条件 | `**`の挙動 |
|-----|-----------|
| DOTMATCHなし | 隠しディレクトリ(`.xxx`)には**降りない** |
| DOTMATCHあり | 隠しディレクトリにも**降りる** |
| 明示的パス(`.dir/**`) | `.dir`は探索するが、内部の隠しサブディレクトリにはDOTMATCH必要 |

**`.`エントリの扱い**:
- DOTMATCHあり: 結果に含める（例: `dir/.`）
- しかし`.`ディレクトリには**再帰しない**（無限ループ防止）
- `..`は常に除外（マッチも再帰もしない）

**ワイルドカード経由の`.`マッチング**:
- リテラルプレフィックス時: `dir1/*` → `dir1/.` **含まれる**
- ワイルドカード経由時: `*/*` → `dir1/.` **含まれない**

| パターン | `subdir/.` がマッチするか |
|---------|-------------------------|
| `dir1/*` | ✅ Yes（リテラルprefix） |
| `*/*` | ❌ No（ワイルドカード経由） |
| `**/*` | ❌ No（再帰経由） |
| `*/dir/*` | ❌ No（ワイルドカード経由） |

**理由**: ワイルドカードでディレクトリを列挙する際、`.`は「そのディレクトリ自身」であり、結果として同じパスの重複を避けるためと推測される。

### 5. セグメント分類

```
SEG_LITERAL   - リテラル文字列 (例: "foo", "bar.txt")
SEG_DOT       - "." (カレントディレクトリ)
SEG_DOTDOT    - ".." (親ディレクトリ)
SEG_WILDCARD  - ワイルドカード (例: "*.txt", "foo?", "[abc]", "*")
SEG_RECURSIVE - 再帰 (**/のみ)
```

**現行からの変更**:
- `RBC_SEG_ANY`, `RBC_SEG_MAGICAL` → `SEG_WILDCARD`に統合
- `RBC_SEG_ROOT` → 削除（パターン先頭の`/`は別途処理）
- `**`(末尾、`/`なし) → `SEG_WILDCARD`として扱う
- `SEG_DOT`, `SEG_DOTDOT` → **維持**（readdir時の`.`/`..`スキップ判定に必要）

---

## アーキテクチャ

### メモリ管理

```c
// アリーナベースの結果バッファ
typedef struct {
    char *data;        // 連続バッファ（全パスを格納）
    size_t *offsets;   // 各パスのオフセット
    size_t count;      // パス数
    size_t data_used;  // 使用済みバイト数
    size_t data_cap;   // dataの容量
    size_t off_cap;    // offsetsの容量
} rbc_results_t;
```

**利点**:
- malloc回数: N回 → 2-3回
- メモリ断片化なし
- 解放が単純（2回のfreeで完了）

### ディレクトリ走査

```c
// d_type活用でstat()削減
static inline bool is_dir_entry(struct dirent *e, const char *path) {
#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_DIR)
    if (e->d_type == DT_DIR) return true;
    if (e->d_type != DT_UNKNOWN && e->d_type != DT_LNK) return false;
#endif
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}
```

**期待効果**: Linux/BSD/macOSでstat()呼び出し大幅削減

### パス構築

```c
// snprintf不使用、直接memcpy
static size_t build_path(char *buf, const char *base, size_t base_len,
                         const char *name, size_t name_len) {
    if (base_len > 0) {
        memcpy(buf, base, base_len);
        buf[base_len] = '/';
        memcpy(buf + base_len + 1, name, name_len + 1);
        return base_len + 1 + name_len;
    }
    memcpy(buf, name, name_len + 1);
    return name_len;
}
```

### 統一Walker

```c
// スタックベース（再帰なし、スタックオーバーフロー回避）
typedef struct {
    char path[PATH_MAX];
    uint16_t path_len;
    uint16_t pattern_offset;  // パターン文字列内のオフセット
    uint8_t flags;
} walk_frame_t;

void rbc_glob_walk(const char *pattern, const char *base,
                   unsigned flags, rbc_results_t *results);
```

---

## fnmatch設計

**決定**: 現行実装を維持（再設計対象外）

現行のfnmatch.cは十分に最適化されており、高速パス検出も実装済み。
glob部分の再設計に集中する。

---

## Brace展開

**方針**: 前処理として展開、コアロジックから分離

```c
// 入力: "{a,b}/*.txt"
// 出力: ["a/*.txt", "b/*.txt"]
```

**制限**:
- ネスト深さ上限: 8
- 展開後パターン数上限: 256
- 超過時はエラーまたは切り捨て

---

## 削除する機能

1. ~~連続スラッシュ保持~~ → 正規化
2. ~~`RBC_SEG_ANY`, `RBC_SEG_MAGICAL`~~ → `SEG_WILDCARD`に統合
3. ~~`RBC_SEG_ROOT`~~ → パターン先頭の`/`は別途処理
4. ~~`trailing_slashes`カウント~~ → bool `has_trailing_slash`
5. ~~ソート済みディレクトリ読み込み~~ → 結果のみソート（オプション）
6. ~~`rbc_glob_compile`~~ → 単発glob専用（プリコンパイル不要）

---

## API設計

### 公開API

```c
// シンプルglob
bool rbc_glob(const char *pattern, unsigned flags, const char *base,
              char ***out, size_t *count);

// 複数パターン
bool rbc_glob_multi(const char **patterns, size_t npatterns,
                    unsigned flags, const char *base,
                    char ***out, size_t *count);

// 結果解放
void rbc_glob_free(char **list, size_t count);

// fnmatch
bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags);
```

### フラグ

```c
#define RBC_FNM_NOESCAPE  0x01
#define RBC_FNM_PATHNAME  0x02
#define RBC_FNM_DOTMATCH  0x04
#define RBC_FNM_CASEFOLD  0x08
```

---

## 実装フェーズ

### Phase 1: 基盤
- [ ] アリーナ結果バッファ
- [ ] パス正規化ユーティリティ
- [ ] d_type対応エントリ判定

### Phase 2: glob
- [ ] セグメント解析（簡略版）
- [ ] 統一walker
- [ ] DOTMATCH対応

### Phase 3: 仕上げ
- [ ] brace展開
- [ ] エラー処理
- [ ] テスト

**注**: fnmatchは現行維持のため、Phase対象外

---

## ベンチマーク目標

| 指標 | 現行 | 目標 |
|-----|------|------|
| コード行数 | ~1700 | ~800 |
| malloc/glob | N+ | 2-3 |
| stat()/entry | 1-2 | 0-1 |
| 1000ファイルglob | baseline | 2x faster |

---

## 参考資料

- [Ruby Dir.glob Documentation](https://docs.ruby-lang.org/en/4.0/Dir.html#method-c-glob)
- [Ruby File.fnmatch Documentation](https://docs.ruby-lang.org/en/4.0/File.html#method-c-fnmatch)
- musl libc glob実装（MIT License）
- BSD libc glob実装（BSD License）
