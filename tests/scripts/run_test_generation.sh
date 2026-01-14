#!/bin/bash
#
# テスト生成パイプライン
# 1. Fixturesを生成
# 2. Ruby期待値を生成
# 3. Cテストコードを生成
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================"
echo "テスト生成パイプライン"
echo "========================================"
echo ""

# Step 1: Fixtures生成
echo "📂 Step 1: Generating fixtures..."
python3 gen_fixtures.py
echo ""

# Step 2: Ruby期待値生成
echo "📝 Step 2: Generating Ruby expected outputs..."
python3 gen_ruby_expected.py
echo ""

# Step 3: Cテストコード生成
echo "🔧 Step 3: Generating C test code..."
python3 gen_c_tests.py
echo ""

echo "========================================"
echo "✅ パイプライン完了"
echo "========================================"
echo ""
echo "次のステップ:"
echo "  1. cd ../build"
echo "  2. cmake .."
echo "  3. make test_glob_generated"
echo "  4. ./test_glob_generated"
echo ""
