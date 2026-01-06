# rbcglob C API 使用方法

このドキュメントは、`rbcglob` ライブラリの公開 C API の利用方法をまとめたものです。
Ruby 4.0 の `Dir.glob` および関連する `File` クラスの機能を C から利用できます。

---

## 主な API

### 1. Dir.glob 相当 (rbcglob_dirglob)

```c
/**
 * patterns: パターン配列
 * npatterns: パターン数
 * flags: RBCGLOB_FNM_* フラグ
 * base: 検索基準ディレクトリ (NULL で CWD)
 * sort: 1 で Ruby compatible sort を実行
 * out_list: マッチしたパス配列を受け取るポインタ
 * out_count: マッチ数を受け取るポインタ
 * out_lengths: (任意) 各パスの長さ配列を受け取るポインタ
 * 戻り値: true で成功
 */
bool rbcglob_dirglob(const char **patterns, size_t npatterns, unsigned flags,
                     const char *base, int sort,
                     char ***out_list, size_t *out_count, size_t **out_lengths);

/* 結果の解放 */
void rbcglob_free(char **list, size_t count, size_t *lengths);
```

### 2. File.fnmatch 相当 (rbcglob_fnmatch)

```c
/* 戻り値: true でマッチ、false で不一致 */
bool rbcglob_fnmatch(const char *pattern, const char *path, unsigned flags);
```

### 3. File.join 相当 (rbcglob_join)

```c
/* 複数のパス成分を Ruby 流に結合 */
char *rbcglob_join(const char **args, size_t count);
```

### 4. File.expand_path 相当 (rbcglob_expand_path)

```c
/* ~展開や相対パスの解決 */
char *rbcglob_expand_path(const char *file_name, const char *dir_string);
```

### 5. File.dirname 相当 (rbcglob_dirname)

```c
/**
 * file_name: パス
 * level: 削除する末尾コンポーネント数 (デフォルト: 1)
 * 戻り値: 新規に割り当てられた文字列 (free()で解放)
 */
char *rbcglob_dirname(const char *file_name, int level);
```

### 6. File.basename 相当 (rbcglob_basename)

```c
/**
 * file_name: パス
 * suffix: 削除する拡張子 (NULL または "" で削除なし、".*" で任意の拡張子を削除)
 * 戻り値: 新規に割り当てられた文字列 (free()で解放)
 */
char *rbcglob_basename(const char *file_name, const char *suffix);
```

### 7. File.extname 相当 (rbcglob_extname)

```c
/**
 * path: パス
 * 戻り値: 拡張子 (ドットを含む)、拡張子がない場合は空文字列
 */
char *rbcglob_extname(const char *path);
```

---

## 主要フラグ (RBCGLOB_FNM_*)

- `RBCGLOB_FNM_NOESCAPE`: `\` をエスケープ文字として扱わない
- `RBCGLOB_FNM_PATHNAME`: `*` を `/` にマッチさせない
- `RBCGLOB_FNM_DOTMATCH`: `.` で始まるファイルを含める
- `RBCGLOB_FNM_CASEFOLD`: 大文字小文字を区別しない
- `RBCGLOB_FNM_SYSCASE`: OS のデフォルト（Win/MacはFold）に従う


実装では、これらのフラグの意味を Ruby の仕様どおり再現し、`dirglob` の `flags` 引数でビットフラグとして指定できるようにします。

---

## 使用例（複数パターン）

```c
#include <stdio.h>
#include <stdlib.h>
#include "dirglob/dirglob.h"

int main(void) {
    const char *patterns[] = {"a/**/*.rb", "**/*.txt"};
    char **res = NULL;
    size_t count = 0;
    unsigned flags = DIRGLOB_F_DOTMATCH; /* 例 */

    if (!dirglob(patterns, 2, flags, NULL, 1, &res, &count)) {
        perror("dirglob");
        return 1;
    }

    for (size_t i = 0; i < count; ++i) {
        puts(res[i]);
    }

    dirglob_free(res, count);
    return 0;
}
```


---

## 出力の形式と順序
- C 実装は Ruby の `Dir.glob` と**同じ文字列表現**と**同じ順序**を返すことを目標にします（`sort` 引数で Ruby の `sort:` を再現）。
- テストでは Ruby 側で得た出力と**生のまま**（正規化や追加ソートを行わず）比較して互換性を検証します。

---

## エラー処理とメモリ
- `dirglob` がエラー時には `false` を返します。より詳細なエラー情報が必要な場合は `errno` を参照して下さい。
- 成功時（`true` 返却時）、`out` に設定される配列と各文字列はライブラリ側の `malloc` により割り当てられるため、呼び出し側は `dirglob_free` を必ず呼んで解放してください。
- エラー時（`false` 返却時）は `out` と `count` の内容は未定義です。メモリ解放は不要です。

---

## 注意点・実装ポリシー
- **完全互換**を目標とするため、プラットフォーム固有の違い（パス区切りや大文字小文字）をテストで吸収せず、まずは Ruby と同じ挙動を再現します。
- パフォーマンスは後で最適化可能なように、まずは読みやすく保守しやすい実装を優先します。

---

## 次のステップ
- `tests/` に `patterns.txt` と `options.txt` を置き、`gen_matrix` / `gen_ruby_expected` / `gen_c_tests` で自動生成されるテスト群を使って互換性を検証します。

---

ご確認ください。必要ならこのドキュメントに **サンプル出力例** や **よくあるトラブルと対処** を追記します。
