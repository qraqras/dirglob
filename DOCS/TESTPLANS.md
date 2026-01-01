# TEST PLANS — dirglob

※このドキュメントはテスト方針の現時点でのまとめと、互換性検証のために自動生成するテストの仕様、および提案する C API の草案を含みます。

---

## 目的
- Ruby の `Dir.glob` と互換性があることを、C 実装（C99）で自動検証する。
- Windows（必須）、POSIX を含むクロスプラットフォームで一致することを保証する。
- 可読性優先の実装方針を保ちながら、テストで振る舞いの差異を検出する。

## 要件（テスト面）
- 各テストケースは **パターン（glob）** と **オプション（フラグ）** の組み合わせで構成する。
- 各組み合わせについて、以下を自動で行う：
  1. Ruby の `Dir.glob(pattern, flags)` を実行して `r_result` を生成（生の出力、ソート等は行わない）。
  2. C 実装の API を呼び出して `c_result` を生成（生の出力）。
  3. **raw（一切の正規化・ソートをしない）** で `r_result` と `c_result` を比較する。
  4. 差異があればテスト失敗、詳細な差分（行単位）を出力する。
- プラットフォーム差が問題となるケース（例: 大文字小文字扱い、パス区切り）については `options` 側で明示的に制御できるようにする（`platform` 列等）。

## test data（ファイル）
- `tests/patterns.txt` — 1 行に 1 パターン（例: `a/**/*.rb`）
- `tests/options.txt` — TSV。カラム例：
  - name\t ruby_flag\t c_flag_token\t platforms\t description
  - 例： `dotmatch\tFile::FNM_DOTMATCH\tDIRGLOB_F_DOTMATCH\tall\tInclude dotfiles`

## 自動生成パイプライン（概略）
1. `gen_matrix.py`：`patterns × options` の直積を生成（テストケース一覧）
2. `gen_ruby_expected.py`：各ケースについて Ruby を実行して `build/tests/ruby_expected/<case>.txt` を作成（r_result）
3. `gen_c_tests.py`：各ケースについて Unity テストソース（`build/tests/generated/test_<case>.c`）を生成
   - 生成される C テストは、事前に定義された C API を呼び、`c_result` を作成して `r_result` と比較する
4. CMake が生成ファイルをビルドして `ctest` で実行

> 実行環境依存の要件: `git`, `python3`, `ruby` が必須（CMake 構成時にチェックし、なければ明確に失敗させる）

## 比較ポリシー（重要）
- **基本は raw 比較**（Ruby の出力と文字列レベルで一致すること）。
- 問題が発生したパターンは `options` 側で比較モードやプラットフォーム制約を設定する（例：`platforms=windows` など）。

## 情報として出力するファイル
- `build/tests/ruby_expected/<case>.txt` — Ruby の期待出力
- `build/tests/c_results/<case>.txt` — C の実行出力（トラブルシュート用、テストが失敗したときに調査）

## エラーハンドリング
- Ruby 実行が失敗した場合は generator が stderr を記録し、テスト生成は失敗として扱う（明確な原因を報告）。

---

# C API 提案（草案）

設計方針：シンプルで可読性の高い API を優先し、まずは使いやすい「配列返却型」を実装する。後でコールバック/イテレータ版も追加できるようにする。

## 型とフラグ
```c
/* flags (bitfield) */
#define DIRGLOB_F_DOTMATCH   (1u<<0)  /* include dotfiles */
#define DIRGLOB_F_PATHNAME   (1u<<1)  /* / does not match by * */
#define DIRGLOB_F_CASEFOLD   (1u<<2)  /* case-insensitive match */
/* 追加のフラグは将来追加 */
```

## 関数（草案）
```c
/* Primary API: multiple-pattern version
 * Return: newly allocated NULL-terminated char** or NULL on error. *count set to number of entries.
 * patterns: array of C strings (not NULL-terminated) with length npatterns
 */
char **dirglobv(const char **patterns, size_t npatterns, unsigned flags, const char *base, int sort, size_t *count);

/* Note: single-pattern convenience wrappers are intentionally omitted in the initial implementation. */
}

/* Free list returned by dirglob / dirglobv */
void dirglob_free(char **list, size_t count);

/* Convenience: return 0 on match, 1 on no match, negative on error */
int dirglob_match(const char *pattern, unsigned flags, const char *path);

/* version helper (already present) */
const char *dirglob_version(void);
```

### メモリの所有権
- `dirglob` は `malloc` による配列と各文字列を返す。呼び出し側は `dirglob_free` を呼んで解放する。

### エラー処理
- `dirglob` が NULL を返した場合は `errno` を参照するか、将来的に `dirglob_strerror()` のような関数を追加する。

### 将来の拡張
- コールバックベース (`dirglob_iter`) を追加すれば、大量出力でのメモリ消費を抑えられる。

---

# 次のステップ（短く）
1. `tests/options.txt` と `tests/patterns.txt` を作成する（私が最初のフラグを追加します）
2. `gen_matrix.py`, `gen_c_tests.py` を実装して、まずは 3~5 の代表ケースで生成・ビルド・比較を動かす
3. C API の最初の簡易実装（`dirglob` / `dirglob_free` / `dirglob_match`）を用意し、比較テストを走らせる

---

ご確認ください。上の C API 草案で問題なければ、私の方で `tests/options.txt` と生成スクリプト、最初の C テスト生成を実装します。どのフラグを最初に入れるか（推奨: `DIRGLOB_F_DOTMATCH`）も教えてください。
