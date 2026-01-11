# rbc_glob テスト実装完了

## 実装されたテストシステム

### 📊 テスト規模
- **総テストケース数**: 4,848
  - 共通（プラットフォーム非依存）: 224ケース
  - Linux固有: 4,624ケース
- **パターン定義**: directories(13) × files(14) × options(16)

### 📁 ファイル構成

#### テストデータ（TSV）
```
tests/
├── directories.txt      # 13 ディレクトリパターン
├── files.txt           # 14 ファイルパターン
└── options.txt         # 16 オプション組み合わせ
```

#### 生成スクリプト
```
tests/scripts/
├── gen_matrix.py           # マトリックス生成
├── gen_ruby_expected.py    # Ruby期待出力生成
├── gen_c_tests.py          # C言語テスト生成
├── create_fixtures.sh      # フィクスチャ作成
└── run_pipeline.sh         # 一括実行
```

#### 生成されたファイル
```
tests/ruby_expected/
├── common/          # 224 期待出力ファイル
└── linux/           # 4,624 期待出力ファイル

build/tests/generated/
├── test_parity_common.c    # 224 テスト関数
└── test_parity_linux.c     # 4,624 テスト関数

tests/fixtures/
├── 27 ファイル
└── 19 ディレクトリ
```

## 🚀 使い方

### 1. テストパターンの追加・変更
```bash
# TSVファイルを編集
vim tests/directories.txt
vim tests/files.txt
vim tests/options.txt

# パイプライン再実行
cd tests/scripts
./run_pipeline.sh

# 期待出力をコミット
git add ../ruby_expected/
git commit -m "Update test patterns"
```

### 2. テスト実行
```bash
# ビルド
cd build
cmake ..
make

# テスト実行
ctest --output-on-failure
```

### 3. 個別スクリプト実行
```bash
cd tests/scripts

# Step 1: マトリックス生成
python3 gen_matrix.py

# Step 2: Ruby期待出力生成
python3 gen_ruby_expected.py

# Step 3: C言語テスト生成
python3 gen_c_tests.py
```

## 📝 テストケース例

### パターン組み合わせ
| 組み合わせタイプ | ケース数 | 例 |
|----------------|---------|-----|
| file_only | 224 | `*.txt`, `file.???` |
| dir_file | 2,688 | `dir/*.txt`, `*/{a,b}.c` |
| dir_dir | 1,936 | `dir/sub`, `*/[abc]` |

### オプション組み合わせ
- フラグ: `0`, `FNM_PATHNAME`, `FNM_DOTMATCH`, `FNM_CASEFOLD` 等
- ベース: `NULL`, `.`, `tests/fixtures`
- ソート: `sorted`, `unsorted`

## 🔧 実装状態

### ✅ 完了
- [x] テストデータ定義（TSV）
- [x] マトリックス生成スクリプト
- [x] Ruby期待出力生成（4,848ケース）
- [x] C言語テスト生成
- [x] テストフィクスチャ作成
- [x] テストヘルパー関数

### ⬜ 未実装（次のステップ）
- [ ] `dirglobv()` 関数の実装
- [ ] CMakeLists.txt の更新（生成テスト追加）
- [ ] CI/CD設定
- [ ] Windows対応

## 📖 参照
- [TESTPLANS.md](../../DOCS/TESTPLANS.md) - 包括的なテスト戦略
- [scripts/README.md](scripts/README.md) - スクリプト詳細

---

**作成日**: 2026-01-02
**テスト総数**: 4,848 cases
**プラットフォーム**: Linux (Windows対応準備済み)
