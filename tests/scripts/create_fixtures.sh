#!/bin/bash
# テストフィクスチャ作成スクリプト

set -e

FIXTURES_DIR="/workspaces/rbcglob_dirglob/tests/fixtures"

echo "Creating test fixtures in $FIXTURES_DIR..."

# ディレクトリ構造作成
mkdir -p "$FIXTURES_DIR"
cd "$FIXTURES_DIR"

# クリーンアップ
rm -rf *

# 基本ディレクトリ
mkdir -p dir
mkdir -p .dir
mkdir -p sub
mkdir -p a
mkdir -p b
mkdir -p c
mkdir -p x
mkdir -p y
mkdir -p z

# ネストディレクトリ
mkdir -p dir/sub
mkdir -p dir/a
mkdir -p dir/b
mkdir -p .dir/sub
mkdir -p a/b
mkdir -p a/c
mkdir -p x/y

# 深いネスト
mkdir -p dir/sub/deep
mkdir -p a/b/c

# 基本ファイル
touch file.txt
touch .file.txt
touch test.c
touch a.txt
touch b.txt
touch c.txt

# ディレクトリ内ファイル
touch dir/file.txt
touch dir/test.c
touch dir/.file.txt
touch .dir/file.txt
touch sub/file.txt
touch a/a.txt
touch b/b.txt

# 様々な拡張子
touch file.c
touch file.md
touch a.c
touch b.md

# ネストしたファイル
touch dir/sub/file.txt
touch dir/sub/test.c
touch a/b/file.txt
touch x/y/file.txt

# ドットファイル
touch .hidden
touch dir/.hidden
touch .dir/.hidden

# 1文字ファイル（?パターン用）
touch a.txt
touch b.txt
touch z.txt

# 3文字ファイル（???パターン用）
touch abc.txt
touch xyz.txt

# ツリー表示
echo ""
echo "Fixtures created:"
find . -type f | sort | head -20
echo "... (truncated)"
echo ""
echo "Total files: $(find . -type f | wc -l)"
echo "Total directories: $(find . -type d | wc -l)"
