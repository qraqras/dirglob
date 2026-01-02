# TEST PLANS — dirglob

このドキュメントは dirglob ライブラリの包括的なテスト戦略を定義します。

---

## テスト戦略の概要

### 目的
- Ruby の `Dir.glob` と完全互換の C 実装（C99）を提供する
- Windows（必須）と POSIX を含むクロスプラットフォームで一貫した動作を保証する
- TDD アプローチで段階的に機能を実装し、リグレッションを防止する

### テスト3層構造

#### Layer 1: ユニットテスト（Unit Tests）
**対象**: API 契約、境界条件、エラーハンドリング
**ファイル**: `tests/test_dirglob.c`
**目的**: 個別関数の正常系・異常系を検証

**テスト例**:
- NULL パラメータでエラーを返すか
- 空パターン配列の処理
- メモリ解放の安全性（NULL フリー等）
- `dirglob_match` の個別パターンマッチング
- フラグの組み合わせ

**実行**: すべてのビルドで自動実行（高速）

#### Layer 2: 統合テスト（Integration Tests）
**対象**: 実ファイルシステムでの動作確認
**ファイル**: `tests/test_dirglob_integration.c`（予定）
**目的**: フィクスチャを使った実際のグロブ動作検証

**テスト例**:
- `tests/fixtures/` に配置したファイルを glob
- 期待するファイル名とパスの一覧を検証
- ディレクトリ階層を含むパターン
- 隠しファイルの扱い

**フィクスチャ管理**:
- `tests/fixtures/` ディレクトリを Git 管理
- 決定性を保証（実行順序に依存しない構成）
- README.md でフィクスチャ構造を文書化

**実行**: すべてのビルドで自動実行

#### Layer 3: 互換性テスト（Parity Tests）
**対象**: Ruby `Dir.glob` との完全互換性
**ファイル**: 自動生成（`build/tests/generated/test_parity_*.c`）
**目的**: パターン × フラグの全組み合わせで Ruby と同一出力を保証

**詳細は後述の「互換性テスト詳細」セクション参照**

---

## 互換性テスト詳細

### 基本方針
- 各テストケースは **パターン** と **フラグ** の組み合わせで構成
- Ruby の実行結果を「正解」として、C 実装と **raw 比較**（正規化なし）
- プラットフォーム差異は明示的に管理（後述）

### テストフロー（改善版）

#### 開発時（Ruby 必須）
```
1. パターン・オプション定義（tests/patterns.txt, tests/options.txt）
2. make update-ruby-expected
   → Ruby で期待出力生成
   → tests/ruby_expected/<platform>/<case>.txt に保存
   → Git にコミット
3. C 実装を追加・修正
4. make test
   → C 実装実行
   → コミット済み期待出力と比較
```

#### CI/通常ビルド時（Ruby 任意）
```
1. C 実装をビルド
2. tests/ruby_expected/ の既存ファイルと比較
   → Ruby インストール不要
   → オフライン環境でも動作
3. （Ruby がある場合）期待出力を再生成して差分チェック（optional）
```

### プラットフォーム対応

#### ディレクトリ構造
```
tests/ruby_expected/
  linux/          # Linux 固有の期待出力
  windows/        # Windows 固有の期待出力
  common/         # プラットフォーム共通
```

#### プラットフォーム判定
- `tests/options.txt` の `platforms` 列で指定
  - `all`: すべてのプラットフォーム
  - `linux`: Linux のみ
  - `windows`: Windows のみ
  - `posix`: POSIX 互換環境のみ

#### 差異の例
- パス区切り: `/` vs `\`
- ソート順序: ロケール依存の文字列比較
- 大文字小文字: ファイルシステムの挙動差異

### テストデータファイル

#### patterns.txt
TSV 形式。カラム定義:

**テスト方針**:
1. ディレクトリパターンとファイル名パターンを組み合わせて1つの完全なパスパターンを生成する
2. ディレクトリパターン同士を組み合わせてネストパスパターンを生成する
3. パス区切り文字はプラットフォームに応じて変更する（Windows: `\`, POSIX: `/`）

これにより、以下を体系的にテストする：
- プラットフォーム固有のパス区切り文字の処理
- `FNM_PATHNAME` フラグの影響（ワイルドカードが区切り文字をまたぐか）
- ディレクトリトラバースとファイル名マッチングの独立性
- ディレクトリのネスト構造とパターンの再帰的なマッチング

##### ディレクトリパターン (6種類)
```
種類            バリエーション                        説明
1. リテラル      dir, .dir, dir/sub, .dir/sub         固定ディレクトリ名、ドット付き、ネスト
2. *             *, .*, */sub, .*/sub, dir/*          ワイルドカード、ドット付き
3. ?             ?, .?, ??/sub, ?ir                   単一文字マッチ、ドット付き
4. []            [abc], .[abc], [a-z], [!abc]         文字クラス、範囲、否定、ドット付き
5. {}            {a,b}, .{a,b}, {a,b}/sub             ブレース展開、ドット付き、ネスト
6. **            **, **/dir, dir/**, **/sub/dir       Globstar、再帰ディレクトリ
```

##### ファイル名パターン (5種類)
```
種類            バリエーション                        説明
1. リテラル      file.txt, .file.txt, .hidden         固定ファイル名、ドットファイル
2. *             *.txt, .*.txt, file.*, *             ワイルドカード、ドット付き
3. ?             ?.txt, file.???, ???.txt             単一文字マッチ
4. []            [abc].txt, [a-z]*.txt, [!.]*.txt     文字クラス、範囲、否定
5. {}            {a,b}.txt, file.{txt,md}             ブレース展開、拡張子選択
```

##### 組み合わせマトリックス

**基本組み合わせ**: 6種類(ディレクトリ) × 5種類(ファイル名) = **30パターン**

**各セルの代表例**:
```
              リテラル      *           ?           []          {}
リテラル      dir/file.txt  dir/*.txt   dir/?.txt   dir/[a].txt dir/{a,b}.txt
*             */file.txt    */*.txt     */?.txt     */[a].txt   */{a,b}.txt
?             ?/file.txt    ?/*.txt     ?/?.txt     ?/[a].txt   ?/{a,b}.txt
[]            [a]/file.txt  [a]/*.txt   [a]/?.txt   [a]/[b].txt [a]/{x,y}.txt
{}            {a,b}/file.txt {a,b}/*.txt {a,b}/?.txt {a,b}/[x].txt {a,b}/{x,y}.txt
**            **/file.txt   **/*.txt    **/?.txt    **/[a].txt  **/{a,b}.txt
```

**バリエーション展開**:
各セルで以下のバリエーションをテスト:
- ドットファイル/ドットディレクトリの組み合わせ (4パターン): `dir/file`, `.dir/file`, `dir/.file`, `.dir/.file`
- ネスト深度: 単一、2階層、3階層
- 範囲指定: `[a-z]`, `[0-9]`, `[a-zA-Z0-9]`
- 否定パターン: `[!abc]`, `[^abc]`
- ブレースネスト: `{a,{b,c}}`

**推定パターン数**:
- 基本マトリックス（ディレクトリ×ファイル名）: 6 × 5 = 30パターン
- ディレクトリ×ディレクトリ（ネスト）: 6 × 6 = 36パターン
- ドット組み合わせ: (30 + 36) × 4 = 264パターン
- 追加バリエーション（範囲、否定、ネスト等）: 約150パターン
- エッジケース・特殊パターン: 約50パターン
- **合計**: 約500パターン（プラットフォーム別）

**プラットフォーム別の扱い**:
- Linux/POSIX: パス区切り `/` で生成（約500パターン）
- Windows: パス区切り `\` で生成（約500パターン）
- 共通パターン: 区切り文字を含まないパターン（約100パターン）

##### 特殊パターン・エッジケース
基本マトリックス以外の重要なテストケース:

**空・特殊パス**:
- 空パターン: `""`
- カレント: `.`, `./file.txt`, `./*`
- 親ディレクトリ: `..`, `../file.txt`, `../*`

**複数パターン**(カンマ区切り):
- `*.txt,*.md`
- `dir/*.txt,sub/*.md`
- `**/*.rb,**/*.py`

**複雑な組み合わせ**:
- `[abc]*/{x,y}.???` (3種類の組み合わせ)
- `**/{a,b}[0-9].txt` (Globstar + ブレース + 文字クラス)

**エスケープシーケンス**:
- `\*.txt`, `\?.txt`, `\[abc\].txt`
- `file\ name.txt` (スペース)

**不正パターン**(エラーハンドリング):
- `[abc` (閉じていない)
- `{a,b` (閉じていない)

##### 生成ルール

`tests/scripts/gen_matrix.py` での実装:

```python
# ディレクトリパターンの定義
dir_patterns = {
    'literal': ['dir', '.dir', 'dir/sub', '.dir/sub', ''],
    'star': ['*', '.*', '*/sub', '.*/sub', 'dir/*'],
    'question': ['?', '.?', '??/sub', '?ir'],
    'bracket': ['[abc]', '.[abc]', '[a-z]', '[!abc]'],
    'brace': ['{a,b}', '.{a,b}', '{a,b}/sub'],
    'globstar': ['**', '**/dir', 'dir/**', '**/sub/dir']
}

# ファイル名パターンの定義
file_patterns = {
    'literal': ['file.txt', '.file.txt', '.hidden'],
    'star': ['*.txt', '.*.txt', 'file.*', '*'],
    'question': ['?.txt', 'file.???', '???.txt'],
    'bracket': ['[abc].txt', '[a-z]*.txt', '[!.]*.txt'],
    'brace': ['{a,b}.txt', 'file.{txt,md}']
}

# パス結合（プラットフォーム依存）
import platform

def get_separator():
    """プラットフォームに応じたパス区切り文字を返す"""
    return '\\' if platform.system() == 'Windows' else '/'

def combine(part1, part2):
    """2つのパス要素を結合する（ディレクトリ×ディレクトリ、ディレクトリ×ファイル両対応）"""
    sep = get_separator()
    if part1 == '':
        return part2
    elif part1.endswith(sep) or part1.endswith('/'):
        # 既に区切り文字で終わっている場合はそのまま連結
        return part1 + part2
    else:
        # プラットフォーム固有の区切り文字で結合
        return part1 + sep + part2

# 全組み合わせを生成
patterns = []

# 1. ディレクトリ × ファイル名の組み合わせ
for dir_type, dir_list in dir_patterns.items():
    for file_type, file_list in file_patterns.items():
        for d in dir_list:
            for f in file_list:
                patterns.append({
                    'pattern': combine(d, f),
                    'dir_type': dir_type,
                    'file_type': file_type,
                    'component1': d,
                    'component2': f,
                    'combination': 'dir_file'
                })

# 2. ディレクトリ × ディレクトリの組み合わせ（ネストパス）
for dir_type1, dir_list1 in dir_patterns.items():
    for dir_type2, dir_list2 in dir_patterns.items():
        for d1 in dir_list1:
            for d2 in dir_list2:
                if d1 and d2:  # 空パターンは除外
                    patterns.append({
                        'pattern': combine(d1, d2),
                        'dir_type': dir_type1,
                        'file_type': dir_type2,  # ディレクトリだが列名は維持
                        'component1': d1,
                        'component2': d2,
                        'combination': 'dir_dir'
                    })
```

**最終TSVフォーマット**:
```
id  pattern                 dir_type    file_type   description
1   file.txt                literal     literal     リテラル×リテラル
2   .file.txt               literal     literal     リテラル×ドットファイル
3   dir/file.txt            literal     literal     ディレクトリ×リテラル
4   .dir/file.txt           literal     literal     ドットディレクトリ×リテラル
5   dir/.file.txt           literal     literal     ディレクトリ×ドットファイル
...
250 **/{a,b}[0-9].txt       combined    combined    複雑な組み合わせ
```

**テストケース総数**:
- Linux/POSIX: 約500パターン × 16オプション = **8,000テストケース**
- Windows: 約500パターン × 16オプション = **8,000テストケース**
- 共通: 約100パターン × 16オプション = **1,600テストケース**
- **合計**: 約**17,600テストケース**（プラットフォーム依存を含む）

#### options.txt
TSV 形式。カラム定義:
```
id  flags           base            sort
1   0               NULL            1
2   0               NULL            0
3   0               .               1
4   0               tests/fixtures  1
5   FNM_NOESCAPE    NULL            1
6   FNM_NOESCAPE    tests/fixtures  1
7   FNM_PATHNAME    NULL            1
8   FNM_PATHNAME    tests/fixtures  1
9   FNM_PATHNAME    NULL            0
10  FNM_CASEFOLD    NULL            1
11  FNM_CASEFOLD    tests/fixtures  1
12  FNM_DOTMATCH    NULL            1
13  FNM_DOTMATCH    tests/fixtures  1
14  FNM_DOTMATCH    NULL            0
15  FNM_EXTGLOB     NULL            1
16  FNM_EXTGLOB     tests/fixtures  1
```

**カラムの意味**:
- `id`: オプションの一意な識別子（数値）
- `flags`: フラグ定数（`0`, `FNM_NOESCAPE`, `FNM_PATHNAME` 等）
  - Ruby: `File::FNM_NOESCAPE` 等に変換
  - C: そのまま使用（`FNM_NOESCAPE` 等）
- `base`: ベースディレクトリ（`NULL`, `.`, `tests/fixtures` 等）
  - Ruby: `Dir.glob(pattern, flags, base: value)` の `base:` 引数
  - C: `dirglob(..., base, ...)` の `base` 引数
- `sort`: ソートの有無（`0` = unsorted, `1` = sorted）
  - Ruby: `Dir.glob(pattern, flags, sort: value)` の `sort:` 引数
  - C: `dirglob(..., sort, ...)` の `sort` 引数

**フラグの変換規則**:
- 生成スクリプトが言語別に変換
  - `FNM_NOESCAPE` → Ruby: `File::FNM_NOESCAPE`, C: `FNM_NOESCAPE`
  - `FNM_PATHNAME` → Ruby: `File::FNM_PATHNAME`, C: `FNM_PATHNAME`
  - `0` → Ruby: `0`, C: `0`

**組み合わせ戦略**:
- **優先度 high**: フラグ別 × base=NULL × sort=1（デフォルト動作）
- **優先度 medium**: フラグ別 × base=fixtures × sort=1（実用的なケース）
- **優先度 low**: sort=0 のバリエーション（ソート順序の検証）
- 全組み合わせを網羅的にテストする場合は、上記を拡張して全ケースを追加

### 自動生成パイプライン

#### Step 1: マトリックス生成
`tests/scripts/gen_matrix.py`
- `patterns.txt × options.txt` の直積を生成（両方とも TSV 形式）
  - options.txt には既に `flags × base × sort` の組み合わせが含まれる
- パターン文字列の分割:
  - ブレースの深さを追跡してトップレベルのカンマのみで分割
  - 例: `{a,b}.txt,{c,d}.md` → `["{a,b}.txt", "{c,d}.md"]`（ブレース内のカンマは無視）
  - 実装例:
    ```python
    def split_patterns(s):
        patterns, current, depth = [], [], 0
        for c in s:
            if c == '{': depth += 1
            elif c == '}': depth -= 1
            elif c == ',' and depth == 0:
                patterns.append(''.join(current).strip())
                current = []
                continue
            current.append(c)
        if current:
            patterns.append(''.join(current).strip())
        return patterns
    ```
- 出力: テストケース一覧（JSON or TSV）
  - 各ケースに `case_id`, `pattern_id`, `option_id`, `pattern`, `flags`, `base`, `sort` を含む
  - `case_id` の形式: `p{pattern_id}_o{option_id}` (例: `p1_o1`, `p3_o5`)
  - オプション名を自動生成（例: `none_null_sorted`, `pathname_fixtures_sorted`）してメタデータに含める

**テストケース数の見積もり**:
- patterns（プラットフォーム別）: 約500パターン/環境
  - 基本マトリックス（dir×file）: 6 × 5 × バリエーション ≈ 180パターン
  - ディレクトリネスト（dir×dir）: 6 × 6 × バリエーション ≈ 200パターン
  - 特殊パターン・エッジケース: 約50パターン
  - 複数パターン（カンマ区切り）: 約40パターン
  - エスケープ・不正パターン: 約30パターン
- patterns（共通）: 約100パターン
- options: 16の組み合わせ（id: 1-16）
- 合計: **17,600テストケース** ((500 Linux + 500 Windows + 100 共通) × 16)
- 実用的には、CI でプラットフォームごとに実行: **8,000テストケース/環境**

#### Step 2: Ruby 期待出力生成（開発時のみ）
`tests/scripts/gen_ruby_expected.py`
- 各ケースで `Dir.glob(pattern, flags, base: base, sort: sort)` 実行
- パターンの処理:
  - 単一パターン（`,` なし）: 文字列として渡す
  - 複数パターン（`,` あり）: カンマで分割して配列として渡す（例: `["*.txt", "*.c"]`）
- base パラメータの変換:
  - `"NULL"` → Ruby 側では base: を省略（カレントディレクトリ）
  - `"."` → base: "."
  - `"tests/fixtures"` → base: "tests/fixtures"
- sort パラメータの変換:
  - `0` → sort: false
  - `1` → sort: true
- 出力: `tests/ruby_expected/<platform>/<case_id>.txt`
  - 例: `tests/ruby_expected/linux/p1_o1.txt`, `tests/ruby_expected/linux/p3_o12.txt`
- **Git にコミット**（CI で Ruby 不要にするため）

#### Step 3: C テスト生成
`tests/scripts/gen_c_tests.py`
- Unity テストソース生成: `build/tests/generated/test_parity_<case_id>.c`
- パターンの処理:
  - カンマで分割してパターン配列と `npatterns` を生成
  - 例: `*.txt,*.c` → `{"*.txt", "*.c"}`, `npatterns=2`
- base パラメータの変換:
  - `"NULL"` → C 側では `NULL`
  - `"."` → C 側では `"."`
  - `"tests/fixtures"` → C 側では `"tests/fixtures"`
- 生成コード例:
```c
// 単一パターンの例
void test_parity_p1_o12(void) {
    char **result = NULL;
    size_t count = 0;
    bool ok = dirglob((const char*[]){"*.txt"}, 1, FNM_DOTMATCH,
                      NULL, 1, &result, &count);

    assert_matches_expected(result, count,
        "tests/ruby_expected/linux/p1_o12.txt");

    dirglob_free(result, count);
}

// 複数パターンの例
void test_parity_p11_o1(void) {
    char **result = NULL;
    size_t count = 0;
    bool ok = dirglob((const char*[]){"*.txt", "*.c"}, 2, 0,
                      NULL, 1, &result, &count);

    assert_matches_expected(result, count,
        "tests/ruby_expected/linux/p11_o1.txt");
```

#### Step 4: ビルドと実行
CMake が生成ファイルをコンパイルし、`ctest` で実行

### ツール依存性

#### 必須（すべての環境）
- `git`: Unity フェッチに必要
- `python3`: テスト生成スクリプト

#### 開発時のみ必須
- `ruby`: 期待出力生成（`make update-ruby-expected`）

#### CI での扱い
- 通常ビルド: Ruby 不要（既存の期待出力を使用）
- 期待出力更新: Ruby 必須（週次 or パターン追加時）

### CMake ターゲット

```cmake
# 通常のテスト（Ruby 不要）
make test

# 期待出力を再生成（Ruby 必須）
make update-ruby-expected

# テストソースを再生成（Python 必須）
make regenerate-tests
```

### 比較ポリシー

#### 基本: Raw 比較
- Ruby の出力と文字列レベルで **完全一致** を要求
- ソート順序、パス表現、すべて同一であること

#### 例外処理
- プラットフォーム固有の差異は `tests/ruby_expected/<platform>/` で管理
- 正規化が必要な場合は明示的にドキュメント化

### 出力ファイル

#### コミット対象（Git 管理）
- `tests/ruby_expected/<platform>/<case_id>.txt` - Ruby 期待出力（例: `p1_o1.txt`）
- `tests/fixtures/**/*` - テスト用ファイル

#### ビルド成果物（Git 無視）
- `build/tests/generated/test_parity_<case_id>.c` - 生成テストソース
- `build/tests/c_results/<case_id>.txt` - デバッグ用 C 実行結果

### エラーハンドリング

#### Ruby 実行エラー
- generator が stderr をキャプチャ
- 明確なエラーメッセージで失敗

#### テスト失敗時
- 期待値と実際の値を diff 形式で出力
- デバッグ用に `c_results/<case>.txt` を保存

---

# C API 提案（草案）

設計方針：シンプルで可読性の高い API を優先し、まずは使いやすい「配列返却型」を実装する。後でコールバック/イテレータ版も追加できるようにする。

## 型とフラグ
```c
/* flags (bitfield) - Ruby File::FNM_* 互換 */
#define FNM_NOESCAPE (1U << 0)  /* Disable backslash escaping */
#define FNM_PATHNAME (1U << 1)  /* Wildcards don't match / */
#define FNM_CASEFOLD (1U << 2)  /* Case-insensitive matching */
#define FNM_DOTMATCH (1U << 3)  /* Wildcards match leading dots */
#define FNM_EXTGLOB  (1U << 4)  /* Extended glob (**, {}) */
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

---

## TDD 実装ロードマップ

### Phase 1: 基礎（Week 1）
**目標**: 最小限の動作する実装

1. ✅ API プロトタイプとスタブ実装
2. ✅ ユニットテスト（境界条件、エラーハンドリング）
3. ⬜ フィクスチャ準備（`tests/fixtures/` ディレクトリ構成）
4. ⬜ `dirglob_match`: リテラル文字列マッチング
5. ⬜ `dirglob_match`: `*` ワイルドカード（単一ファイル名）
6. ⬜ 統合テスト: 実ファイルで `*.txt` パターン

### Phase 2: 基本パターン（Week 2）
**目標**: 基本的なグロブ機能

7. ⬜ `?` 単一文字ワイルドカード
8. ⬜ `[abc]` 文字クラス
9. ⬜ `[!abc]` 否定文字クラス
10. ⬜ `FNM_PATHNAME` フラグ対応
11. ⬜ `FNM_DOTMATCH` フラグ対応

### Phase 3: 自動化（Week 3）
**目標**: 互換性テスト自動化

12. ⬜ `patterns.txt`, `options.txt` 作成
13. ⬜ `gen_matrix.py`, `gen_ruby_expected.py` 実装
14. ⬜ 初期 Ruby 期待出力を Git にコミット
15. ⬜ `gen_c_tests.py` 実装（Unity テスト生成）
16. ⬜ CMake 統合と自動テスト実行

### Phase 4: 高度な機能（Week 4+）
**目標**: Ruby 完全互換

17. ⬜ `**` globstar（再帰ディレクトリ）
18. ⬜ `{a,b}` brace expansion
19. ⬜ エスケープシーケンス処理
20. ⬜ 残りのフラグ（`FNM_CASEFOLD` 等）
21. ⬜ Windows 対応と CI 追加
22. ⬜ 全互換性テスト合格

---

## テストヘルパー関数（予定）

```c
// tests/test_helpers.h

// 期待出力ファイルを読み込み
char **load_expected_output(const char *filepath, size_t *count);

// Ruby 期待出力と C 結果を比較
void assert_matches_expected(char **c_result, size_t c_count,
                              const char *expected_file);

// 結果を配列と比較
void assert_result_equals(char **actual, size_t actual_count,
                          const char **expected, size_t expected_count);

// フィクスチャのセットアップ/クリーンアップ
void setup_test_fixtures(void);
void cleanup_test_fixtures(void);
```

---

## 次のアクション

### 即座に実装
1. フィクスチャディレクトリ構造作成
2. テストヘルパー関数実装
3. 最初の統合テスト（リテラルマッチ）

### 近日中
4. `dirglob_match` の基本実装（`*` ワイルドカード）
5. パターン・オプション定義ファイル作成
6. Ruby 期待出力生成スクリプト
