# テスト実行ガイド

## 概要

このドキュメントはrbcglobライブラリのテスト実行手順をまとめたものです。

## ディレクトリ構成

```
/workspaces/dirglob/
├── build/                          # ビルド出力ディレクトリ（CMake）
│   └── tests/
│       └── test_glob_generated     # コンパイル済みテスト実行ファイル
├── tests/
│   ├── fixtures/                   # テスト用ファイル構造
│   ├── ruby_expected/              # Ruby実装による期待値ファイル
│   ├── scripts/                    # テストコード生成スクリプト
│   │   ├── gen_ruby_expected.py   # Ruby期待値生成
│   │   ├── gen_c_tests.py         # Cテストコード生成
│   │   └── test_definitions.py    # テストケース定義
│   └── test_glob_generated.c       # 生成されたテストコード
```

## テスト実行手順

### 1. 期待値ファイルの生成（初回のみ）

Ruby実装でglob実行結果の期待値を生成します：

```bash
cd /workspaces/dirglob
python3 tests/scripts/gen_ruby_expected.py
```

出力先: `tests/ruby_expected/*.txt`（4515ファイル）

### 2. テストコードの生成（修正時）

テストケース定義からCテストコードを生成します：

```bash
cd /workspaces/dirglob
python3 tests/scripts/gen_c_tests.py
```

出力先: `tests/test_glob_generated.c`

### 3. ビルド

```bash
cd /workspaces/dirglob/build
make test_glob_generated
```

### 4. テスト実行

**重要**: テストは`tests/fixtures`ディレクトリから実行する必要があります。

```bash
cd /workspaces/dirglob/tests/fixtures
../../build/tests/test_glob_generated
```

## テスト結果の確認

### 全体のサマリー表示

```bash
cd /workspaces/dirglob/tests/fixtures
../../build/tests/test_glob_generated 2>&1 | tail -20
```

最後に以下のような出力が表示されます：

```
-----------------------
4515 Tests 824 Failures 0 Ignored
FAIL
```

### 失敗したテストのみ抽出

```bash
cd /workspaces/dirglob/tests/fixtures
../../build/tests/test_glob_generated 2>&1 | grep "FAIL:" | head -50
```

### 特定のテストケースのデバッグ

個別のテストケース（例: test_t1234）をデバッグする場合：

```bash
cd /workspaces/dirglob/tests/fixtures
gdb ../../build/tests/test_glob_generated
(gdb) break test_t1234
(gdb) run
```

## テストの仕組み

1. **テストケース定義** (`test_definitions.py`)
   - パターン、フラグ、base、sortなどのパラメータを定義
   - 4515個のテストケースが定義されている

2. **期待値生成** (`gen_ruby_expected.py`)
   - Ruby 4.0の`Dir.glob`を実行して結果を取得
   - 各テストケースの期待値を個別ファイルに保存

3. **テストコード生成** (`gen_c_tests.py`)
   - Unity フレームワーク用のCテストコードを自動生成
   - 各テストケースは以下を実行：
     - rbc_globを実行
     - Ruby期待値ファイルを読み込み
     - 結果を比較

4. **実行時の注意点**
   - 相対パスが`fixtures`ディレクトリ基準のため必ずそこから実行
   - 期待値ファイルのパスは`../ruby_expected/tXXXX.txt`

## トラブルシューティング

### "Failed to open expected file" エラー

期待値ファイルが見つからない場合：

```bash
# 期待値ファイルが存在するか確認
ls tests/ruby_expected/ | wc -l  # 4515ファイルあるはず

# ない場合は再生成
python3 tests/scripts/gen_ruby_expected.py
```

### 実行ディレクトリが間違っている

エラーが大量に出る場合、実行ディレクトリを確認：

```bash
pwd  # /workspaces/dirglob/tests/fixtures であるべき
```

### ビルドエラー

テストコード生成後は必ずリビルド：

```bash
cd /workspaces/dirglob/build
make clean
cmake ..
make test_glob_generated
```

## ワンライナー実行

全工程を一度に実行：

```bash
cd /workspaces/dirglob && \
  python3 tests/scripts/gen_c_tests.py && \
  cd build && make test_glob_generated && \
  cd ../tests/fixtures && \
  ../../build/tests/test_glob_generated 2>&1 | tail -20
```

## テスト結果の分析

失敗傾向を分析する場合：

```bash
cd /workspaces/dirglob/tests/fixtures
../../build/tests/test_glob_generated 2>&1 > /tmp/test_results.txt

# 失敗パターンを分類
grep "Pattern:" /tmp/test_results.txt | grep -B1 "FAIL" | head -100
```

---

## fnmatch テスト (File.fnmatch 互換テスト)

### ディレクトリ構成

```
/workspaces/dirglob/
├── build/tests/
│   └── test_fnmatch_ruby_compat    # コンパイル済みテスト実行ファイル
├── tests/
│   ├── ruby_fnmatch_expected/      # Ruby実装による期待値ファイル
│   ├── generated/
│   │   └── test_fnmatch_ruby_compat.c  # 生成されたテストコード
│   └── scripts/
│       ├── gen_ruby_fnmatch_expected.py  # Ruby期待値生成
│       ├── gen_c_fnmatch_tests.py        # Cテストコード生成
│       └── test_definitions.py           # テストケース定義（FNMATCH_TESTS）
```

### 1. 期待値ファイルの生成（初回のみ）

Ruby実装でfnmatch実行結果の期待値を生成します：

```bash
cd /workspaces/dirglob
python3 tests/scripts/gen_ruby_fnmatch_expected.py
```

出力先: `tests/ruby_fnmatch_expected/*.txt`（418ファイル）

### 2. テストコードの生成（修正時）

テストケース定義からCテストコードを生成します：

```bash
cd /workspaces/dirglob
python3 tests/scripts/gen_c_fnmatch_tests.py
```

出力先: `tests/generated/test_fnmatch_ruby_compat.c`

### 3. ビルド

```bash
cd /workspaces/dirglob/build
make test_fnmatch_ruby_compat
```

### 4. テスト実行

**重要**: テストは**プロジェクトルート**から実行する必要があります（globテストと異なる）。

```bash
cd /workspaces/dirglob
./build/tests/test_fnmatch_ruby_compat
```

### fnmatchテスト結果の確認
必ず`/workspaces/dirglob`から実行する必要があります。
```bash
cd /workspaces/dirglob
./build/tests/test_fnmatch_ruby_compat 2>&1 | tail -20
```

### fnmatchワンライナー実行

```bash
cd /workspaces/dirglob && \
  python3 tests/scripts/gen_c_fnmatch_tests.py && \
  cd build && make test_fnmatch_ruby_compat && \
  cd .. && ./build/tests/test_fnmatch_ruby_compat 2>&1 | tail -20
```

### fnmatchテストケースの定義

`test_definitions.py` の `FNMATCH_TESTS` リストで定義されています：

```python
FNMATCH_TESTS = [
    # (id, pattern, text, flags, description)
    ("fm001", "\\*", "*", 0, "Escaped star matches literal star"),
    ("f1000", "*", "abc", 0, "Star matches any string"),
    ...
]
```

### 実行ディレクトリの違い

| テスト | 実行ディレクトリ | 理由 |
|--------|------------------|------|
| glob | `tests/fixtures` | fixtureファイルにアクセスするため |
| fnmatch | プロジェクトルート | 期待値パスが `tests/ruby_fnmatch_expected/` |
