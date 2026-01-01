# dirglob C API 使用方法（概要）

このドキュメントは、`dirglob` ライブラリの公開 C API の利用方法を簡潔にまとめたものです。
目的は **Ruby の `Dir.glob` と互換のある機能を C から利用する** ための手順と注意点を示すことです。

---

## 主な API（要点）

- 複数パターン版（推奨）

```c
/* patterns: 配列（長さは npatterns）
 * flags: ビットフラグ（下記参照）
 * base: 相対検索の基準ディレクトリ（NULL でカレント）
 * sort: `1` = Ruby の sort=true 相当（既定の振る舞いを再現）、`0` = ソートしない
 * count: 出力要素数（返却配列の長さ）
 * 戻り値: NULL終端ではなく size を使うための char**（NULL はエラー）
 */
char **dirglob(const char **patterns, size_t npatterns, unsigned flags, const char *base, int sort, size_t *count);

/* 結果の解放 */
void dirglob_free(char **list, size_t count);

/* パターンと単一パスのマッチ判定（0 = match, 1 = no match, 負値 = error） */
int dirglob_match(const char *pattern, unsigned flags, const char *path);

/* ライブラリバージョン */
const char *dirglob_version(void);
```


---

## フラグ（サポート）

このライブラリは **Ruby の `File::FNM_*` で定義されるフラグ群をすべてサポートします**。
以下は主要フラグと C 側の定数名の対応例です（完全な互換を目指して実装します）。

- DIRGLOB_F_NOESCAPE : `File::FNM_NOESCAPE` 相当（バックスラッシュによるエスケープを無効にする）
- DIRGLOB_F_PATHNAME : `File::FNM_PATHNAME` 相当（`/` がワイルドカードにマッチしない）
- DIRGLOB_F_DOTMATCH  : `File::FNM_DOTMATCH` 相当（先頭 `.` のファイルを含める）
- DIRGLOB_F_CASEFOLD  : `File::FNM_CASEFOLD` 相当（大文字小文字を無視）
- DIRGLOB_F_EXTGLOB   : `File::FNM_EXTGLOB` 相当（拡張グロブをサポート）
- DIRGLOB_F_LEADING_DIR : `File::FNM_LEADING_DIR` 相当（途中のディレクトリ名でマッチを許す）

（上記以外の `File::FNM_*` フラグも Ruby と同様にサポートされます。）

実装では、これらのフラグの意味を Ruby の仕様どおり再現し、`dirglob` の `flags` 引数でビットフラグとして指定できるようにします。

---

## 使用例（複数パターン）

```c
#include <stdio.h>
#include <stdlib.h>
#include "dirglob/dirglob.h"

int main(void) {
    const char *patterns[] = {"a/**/*.rb", "**/*.txt"};
    size_t count = 0;
    unsigned flags = DIRGLOB_F_DOTMATCH; /* 例 */

    char **res = dirglob(patterns, 2, flags, NULL, 1, &count);
    if (!res) {
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
- `dirglob` がエラー時には NULL を返します。より詳細なエラー情報が必要な場合は `errno` を参照して下さい。
- 返却される配列と各文字列はライブラリ側の `malloc` により割り当てられるため、呼び出し側は `dirglob_free` を必ず呼んで解放してください。

---

## 注意点・実装ポリシー
- **完全互換**を目標とするため、プラットフォーム固有の違い（パス区切りや大文字小文字）をテストで吸収せず、まずは Ruby と同じ挙動を再現します。
- パフォーマンスは後で最適化可能なように、まずは読みやすく保守しやすい実装を優先します。

---

## 次のステップ
- `tests/` に `patterns.txt` と `options.txt` を置き、`gen_matrix` / `gen_ruby_expected` / `gen_c_tests` で自動生成されるテスト群を使って互換性を検証します。

---

ご確認ください。必要ならこのドキュメントに **サンプル出力例** や **よくあるトラブルと対処** を追記します。
