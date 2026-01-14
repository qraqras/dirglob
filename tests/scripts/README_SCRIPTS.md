# テスト生成スクリプト

このディレクトリには、test_definitions.pyからテストケースとフィクスチャを生成するスクリプトが含まれています。

## スクリプト一覧

### 1. test_definitions.py
- **役割**: テストケース定義の中心ファイル
- **内容**:
  - 60+パターン × 7フラグ × 4base × 2sort = 2,659テストケース
  - TestCaseデータクラス定義
  - FNMFlags定義
  - get_all_test_cases()関数

### 2. gen_fixtures.py
- **役割**: テストフィクスチャ（ファイル/ディレクトリ）を生成
- **出力**: `tests/fixtures/` (134ファイル、33ディレクトリ)
- **構造**:
  - 01_basic/, 02_asterisk/, 03_questionmark/, 04_characterclass/
  - 05_braceexpansion/, 06_casefold/, 07_recursive/
  - 08_escapechars/, 09_combined/, .hidden/

### 3. gen_ruby_expected.py
- **役割**: Ruby 4.0でDir.globを実行し、期待値を生成
- **入力**: test_definitions.pyのテストケース
- **出力**: `tests/ruby_expected/t1000.txt` - `t3658.txt` (2,659ファイル)
- **形式**: 各ファイルは改行区切りのパスリスト

### 4. gen_c_tests.py
- **役割**: Unity形式のCテストコードを生成
- **入力**: test_definitions.pyのテストケース
- **出力**: `tests/test_glob_generated.c`
- **内容**:
  - 2,659個のtest_tXXXX()関数
  - Ruby期待値との比較ロジック
  - Unity main()関数

### 5. run_test_generation.sh
- **役割**: 全スクリプトを順次実行するパイプライン
- **実行順序**:
  1. gen_fixtures.py → フィクスチャ生成
  2. gen_ruby_expected_v2.py → Ruby期待値生成
  3. gen_c_tests_v2.py → Cテストコード生成

## 使用方法

### 全自動実行
```bash
cd /workspaces/dirglob/tests/scripts
bash run_test_generation.sh
```

### 個別実行
```bash
# Fixtures生成のみ
python3 gen_fixtures.py

# Ruby期待値生成のみ
python3 gen_ruby_expected.py

# Cテストコード生成のみ
python3 gen_c_tests.py
```

### テスト実行
```bash
cd /workspaces/dirglob/build
cmake ..
make test_glob_generated
./test_glob_generated
```

## テストケース構造

```python
@dataclass
class TestCase:
    id: str              # "t1000"
    pattern: str         # "01_basic/*.txt"
    flags: FNMFlags      # FNMFlags.DOTMATCH | FNMFlags.PATHNAME
    base: Optional[str]  # "src" or None
    sort: bool           # True/False
    desc: str            # "Matrix: *.txt, flags=DOTMATCH"
```

## 出力ファイル

```
tests/
├── fixtures/              # gen_fixtures.py
│   ├── 01_basic/
│   ├── 02_asterisk/
│   └── ...
├── ruby_expected/         # gen_ruby_expected_v2.py
│   ├── t1000.txt
│   ├── t1001.txt
│   └── ...
└── test_glob_generated.c  # gen_c_tests_v2.py
```

## ワークフロー

```
test_definitions.py
         │
         ├─→ gen_fixtures.py ───→ tests/fixtures/
         │
         ├─→ gen_ruby_expected.py ───→ tests/ruby_expected/
         │
         └─→ gen_c_tests.py ───→ tests/test_glob_generated.c
                                            │
                                            └─→ Unity Test Runner
```

## 注意事項

1. **Ruby 4.0必須**: gen_ruby_expected.pyはRuby 4.0のDir.globに依存
2. **フラグ値**: RubyとCのフラグビット値は一致させている（0x01-0x10）
3. **パス**: Ruby期待値ファイルパスはCテスト実行時のcwdに依存
4. **エスケープ**: バックスラッシュは4重エスケープが必要（Python→Ruby）

## トラブルシューティング

### Ruby実行エラー
```bash
# Rubyバージョン確認
ruby --version  # ruby 4.0.0が必要

# 手動テスト
ruby -e 'puts Dir.glob("*")'
```

### Fixtures不足
```bash
# Fixturesディレクトリを確認
ls -la tests/fixtures/

# 再生成
python3 gen_fixtures.py
```

### Cコンパイルエラー
```bash
# Unity依存を確認
cd build && cmake .. && make
```
