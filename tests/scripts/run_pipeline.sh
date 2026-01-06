#!/bin/bash
# テスト生成パイプライン実行スクリプト

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$TEST_DIR/../build"

echo "=== rbcglob_dirglob Test Generation Pipeline ==="
echo

# Step 1: マトリックス生成
echo "Step 1: Generating test matrix..."
python3 "$SCRIPT_DIR/gen_matrix.py"
echo

# Step 2: Ruby期待出力生成（Ruby必須）
if command -v ruby &> /dev/null; then
    echo "Step 2: Generating Ruby expected outputs..."
    python3 "$SCRIPT_DIR/gen_ruby_expected.py"
    echo
else
    echo "Step 2: SKIPPED (Ruby not found)"
    echo "  To generate expected outputs, install Ruby and run:"
    echo "    python3 $SCRIPT_DIR/gen_ruby_expected.py"
    echo
fi

# Step 3: C言語テスト生成
echo "Step 3: Generating C test files..."
python3 "$SCRIPT_DIR/gen_c_tests.py"
echo

echo "=== Pipeline Complete ==="
echo
echo "Next steps:"
echo "  1. Review generated files in $BUILD_DIR/tests/generated/"
echo "  2. Add Ruby expected outputs to git:"
echo "       git add $TEST_DIR/ruby_expected/"
echo "  3. Build and run tests:"
echo "       cd $BUILD_DIR && make && ctest"
