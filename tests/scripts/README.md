# Test Scripts

このディレクトリには、rbc_glob の自動テスト生成スクリプトが含まれています。

## ファイル一覧

### テストデータ（TSVファイル）
- `../directories.txt` - ディレクトリパターン定義
- `../files.txt` - ファイルパターン定義
- `../options.txt` - オプション組み合わせ定義

### 生成スクリプト（Python）
- `gen_matrix.py` - テストマトリックス生成（directories × files × options）
- `gen_ruby_expected.py` - Ruby Dir.glob の期待出力生成
- `gen_c_tests.py` - Unity C言語テストコード生成

### ユーティリティ
- `run_pipeline.sh` - 全ステップを一括実行
- `gen_unity_runner.py` - Unity ランナー生成（既存）

## 使い方

### 初回セットアップ

```bash
# 1. テストマトリックス生成
python3 gen_matrix.py

# 2. Ruby期待出力生成（Ruby必須）
python3 gen_ruby_expected.py

# 3. C言語テスト生成
python3 gen_c_tests.py

# 4. Git に追加
git add ../ruby_expected/
```

### 一括実行

```bash
./run_pipeline.sh
```

### パターン追加後

`directories.txt`, `files.txt`, `options.txt` を編集した後：

```bash
# パイプライン再実行
./run_pipeline.sh

# 新しい期待出力をコミット
git add ../ruby_expected/
git commit -m "Add new test patterns"
```

## 生成されるファイル

### ビルドディレクトリ（Git無視）
- `../../build/test_matrix.json` - 全テストケース一覧
- `../../build/tests/generated/test_parity_*.c` - プラットフォーム別テスト

### テストディレクトリ（Git管理）
- `../ruby_expected/linux/*.txt` - Linux用期待出力
- `../ruby_expected/windows/*.txt` - Windows用期待出力
- `../ruby_expected/common/*.txt` - 共通期待出力

## テストケース数

初期実装規模：
- directories: 13パターン
- files: 14パターン
- options: 16組み合わせ

推定テストケース数：
- ファイル単体: 14 × 16 = 224
- dir×file: 12 × 14 × 16 = 2,688
- dir×dir: 12 × 12 × 16 = 2,304
- **合計**: 約 5,200 テストケース

## 依存関係

### 必須
- Python 3.7+
- Git

### 開発時のみ必須
- Ruby 2.7+ （期待出力生成用）

### CI環境
- Ruby不要（コミット済みの期待出力を使用）
