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

**テスト方針**:
1. ディレクトリパターン、ファイルパターン、オプションを独立した3つのTSVファイルで管理
2. 生成時に `dir × file × options` の3次元マトリックスを作成
3. パス区切り文字はTSVには含めず、生成時にプラットフォームに応じて注入
4. ディレクトリ同士の組み合わせ（ネスト）も同様に生成時に処理

これにより、以下を実現：
- **プラットフォーム中立**: TSVは `/` や `\` を含まない中立的な形式
- **保守性**: 新パターン追加時、1ファイルのみ編集で全組み合わせに反映
- **柔軟性**: dir×file、dir×dir、file単体など組み合わせ方を制御可能
- **可読性**: 各ファイルの役割が明確

#### directories.txt
TSV 形式。ディレクトリ部分のパターンを定義。

**カラム定義**:
```
id  pattern    type       can_nest  description
```

**フィールド説明**:
- `id`: ディレクトリパターンの一意な識別子（数値）
- `pattern`: ディレクトリパターン（**区切り文字なし**）
- `type`: パターン種別（`literal`, `star`, `question`, `bracket`, `brace`, `globstar`）
- `can_nest`: ネスト可能か（`0`=不可、`1`=可）。dir×dir組み合わせ生成の制御用
- `description`: パターンの説明

**パターン種別 (6種類)**:
```
種類            バリエーション例                      説明                        can_nest
1. literal      dir, .dir, sub                       固定ディレクトリ名           1
2. star         *, .*, subdir                        ワイルドカード               1
3. question     ?, .?, d?r                           単一文字マッチ               1
4. bracket      [abc], .[abc], [a-z], [!abc]         文字クラス、範囲、否定       1
5. brace        {a,b}, .{a,b}                        ブレース展開                 1
6. globstar     **                                   再帰ディレクトリ             0
```

**TSV例**:
```
id  pattern  type      can_nest  description
1       literal   1         空（ファイル単体パターン用）
2   dir     literal   1         固定ディレクトリ名
3   .dir    literal   1         ドット付きディレクトリ
4   sub     literal   1         サブディレクトリ名
5   *       star      1         任意のディレクトリ
6   .*      star      1         ドット付き任意ディレクトリ
7   ?       question  1         1文字ディレクトリ
8   .?      question  1         ドット+1文字
9   d?r     question  1         部分ワイルドカード
10  [abc]   bracket   1         文字クラス
11  .[abc]  bracket   1         ドット+文字クラス
12  [a-z]   bracket   1         範囲指定
13  [!abc]  bracket   1         否定文字クラス
14  {a,b}   brace     1         ブレース展開
15  .{a,b}  brace     1         ドット+ブレース
16  **      globstar  0         再帰ディレクトリ（ネスト不可）
```

**推定レコード数**: 約30〜40パターン（バリエーション含む）

#### files.txt
TSV 形式。ファイル名部分のパターンを定義。

**カラム定義**:
```
id  pattern      type       description
```

**フィールド説明**:
- `id`: ファイルパターンの一意な識別子（数値）
- `pattern`: ファイル名パターン
- `type`: パターン種別（`literal`, `star`, `question`, `bracket`, `brace`）
- `description`: パターンの説明

**パターン種別 (5種類)**:
```
種類            バリエーション例                      説明
1. literal      file.txt, .file.txt, .hidden         固定ファイル名、ドットファイル
2. star         *.txt, .*.txt, file.*, *             ワイルドカード、拡張子
3. question     ?.txt, file.???, ???.txt             単一文字マッチ
4. bracket      [abc].txt, [a-z]*.txt, [!.]*.txt     文字クラス、範囲、否定
5. brace        {a,b}.txt, file.{txt,md}             ブレース展開、拡張子選択
```

**TSV例**:
```
id  pattern      type      description
1   file.txt     literal   固定ファイル名
2   .file.txt    literal   ドットファイル
3   .hidden      literal   拡張子なしドットファイル
4   *.txt        star      txt拡張子
5   .*.txt       star      ドット+拡張子
6   file.*       star      ファイル名固定+任意拡張子
7   *            star      任意のファイル
8   ?.txt        question  1文字+拡張子
9   file.???     question  3文字拡張子
10  ???.txt      question  3文字ファイル名
11  [abc].txt    bracket   文字クラス
12  [a-z]*.txt   bracket   範囲+ワイルドカード
13  [!.]*.txt    bracket   否定+ワイルドカード
14  {a,b}.txt    brace     ブレース展開
15  file.{txt,md} brace    拡張子選択
```

**推定レコード数**: 約20〜30パターン（バリエーション含む）

#### 組み合わせ戦略

**3次元マトリックス**: `directories × files × options`

**1. ファイル単体パターン** (dir.id=1, pattern="")
- `"" + file` → ファイル名のみのパターン
- 例: `*.txt`, `file.??`, `[abc].txt`
- 件数: 約30パターン × 16オプション = **480テストケース**

**2. ディレクトリ×ファイル組み合わせ** (dir.id≥2, can_nest=1)
- `dir + sep + file` → 完全なパスパターン
- 例: `dir/file.txt`, `*/*.txt`, `[a-z]/{x,y}.md` (sep=`/` or `\`)
- 件数: 約30 dir × 30 file = 900組み合わせ
  - Linux: 900パターン × 16オプション = **14,400テストケース**
  - Windows: 900パターン × 16オプション = **14,400テストケース**

**3. ディレクトリ×ディレクトリ（ネスト）** (dir1.can_nest=1, dir2.can_nest=1)
- `dir1 + sep + dir2` → ネストディレクトリパターン
- 例: `dir/sub`, `*/[abc]`, `{a,b}/*` (sep=`/` or `\`)
- 件数: 約30 dir × 30 dir = 900組み合わせ
  - Linux: 900パターン × 16オプション = **14,400テストケース**
  - Windows: 900パターン × 16オプション = **14,400テストケース**

**4. Globstar特殊処理** (dir.type=globstar)
- `**` は他のディレクトリと組み合わせ不可（can_nest=0）
- ファイルとのみ組み合わせ: `**/file`, `**/*.txt`
- 件数: 1 globstar × 30 file = 30パターン
  - プラットフォーム共通: 30パターン × 16オプション = **480テストケース**

**5. 複雑な組み合わせ・エッジケース**
- 3階層以上のネスト: `dir1/dir2/file`
- 複数パターン（カンマ区切り）: `*.txt,*.md`
- エスケープ・不正パターン
- 件数: 約100パターン × 16オプション = **1,600テストケース**

**推定総テストケース数**:
```
カテゴリ                     Linux    Windows  共通     合計
─────────────────────────────────────────────────────
ファイル単体                  480      -        -        480
dir×file                     14,400   14,400   -        28,800
dir×dir                      14,400   14,400   -        28,800
Globstar                     -        -        480      480
複雑・エッジケース            800      800      -        1,600
─────────────────────────────────────────────────────
合計                         30,080   29,600   480      60,160
```

**実運用時の削減戦略**:
上記は理論上の最大値。実際には以下で削減：
1. **サンプリング**: 各type組み合わせから代表的なパターンのみ選択
2. **優先度付け**: 高頻度・重要な組み合わせを優先
3. **段階的拡張**: 初期は基本パターン、後から網羅性を追加

**初期実装の推奨規模**:
- dir: 15パターン（各typeから2-3個）
- file: 15パターン（各typeから3個）
- 組み合わせ: 15×15 + 15 (file単体) = 240パターン/プラットフォーム
- テストケース: 240 × 2 (platforms) × 16 (options) ≈ **7,680ケース**

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

#### 特殊パターン・エッジケース
基本マトリックス以外の重要なテストケース（手動で追加）:

**空・特殊パス**:
- 空パターン: `""`
- カレント: `.`, `./file.txt`, `./*`
- 親ディレクトリ: `..`, `../file.txt`, `../*`

**複数パターン** (カンマ区切り):
- `*.txt,*.md`
- `dir/*.txt,sub/*.md`
- `**/*.rb,**/*.py`

**エスケープシーケンス**:
- `\*.txt`, `\?.txt`, `\[abc\].txt`
- `file\ name.txt` (スペース)

**不正パターン** (エラーハンドリング):
- `[abc` (閉じていない)
- `{a,b` (閉じていない)

**3階層以上のネスト**:
- `dir1/dir2/file.txt`
- `*/*/file.txt`
- `**/sub/dir/*.txt`

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
