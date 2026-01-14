#!/bin/bash
# 包括的なベンチマークスクリプト

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"

cd "$BUILD_DIR"

# ビルド
echo "Building benchmark..."
make bench_glob -j$(nproc) 2>&1 | grep -E "(Building|Linking)" || true
echo

# テストディレクトリに移動
cd "$SCRIPT_DIR/../tests/fixtures"

BENCH="$BUILD_DIR/examples/bench_glob"
ITERATIONS=1000

echo "========================================"
echo "   rbc_glob Performance Benchmarks"
echo "========================================"
echo

# 1. glob(3)と公平に比較できるパターン
echo "==== Comparable Patterns (glob(3) vs rbc_glob) ===="
echo
echo "Simple wildcards:"
$BENCH "*.c" $ITERATIONS --glob-only
echo
$BENCH "*.txt" $ITERATIONS --glob-only
echo
$BENCH "a*" $ITERATIONS --glob-only
echo

echo "Question mark patterns:"
$BENCH "?.c" $ITERATIONS --glob-only
echo
$BENCH "???.*" $ITERATIONS --glob-only
echo

echo "Complex ? and * combinations:"
$BENCH "*.?" $ITERATIONS --glob-only
echo
$BENCH "?*.c" $ITERATIONS --glob-only
echo
$BENCH "test_*.c" $ITERATIONS --glob-only
echo
$BENCH "*_?.c" $ITERATIONS --glob-only
echo
$BENCH "*test*.c" $ITERATIONS --glob-only
echo
$BENCH "?*?*.txt" $ITERATIONS --glob-only
echo

echo "Bracket patterns:"
$BENCH "*.[ch]" $ITERATIONS --glob-only
echo
$BENCH "[a-z]*.c" $ITERATIONS --glob-only
echo
$BENCH "*[0-9].c" $ITERATIONS --glob-only
echo
$BENCH "test[_-]*.c" $ITERATIONS --glob-only
echo

echo "Multi-level patterns:"
$BENCH "*/*" $ITERATIONS --glob-only
echo
$BENCH "*/*/*" $ITERATIONS --glob-only
echo

echo "Brace expansion (GLOB_BRACE):"
$BENCH "{a,b,c}/*" $ITERATIONS --glob-only
echo
$BENCH "{test,debug}_*.c" $ITERATIONS --glob-only
echo

echo "Complex combinations:"
$BENCH "*[0-9]?.txt" $ITERATIONS --glob-only
echo
$BENCH "[a-z]*[0-9].c" $ITERATIONS --glob-only
echo

# 2. rbc_glob独自機能（glob(3)は未対応）
echo "==== rbc_glob Exclusive Features ===="
echo "Note: glob(3) does NOT support ** recursive patterns"
echo

echo "Recursive patterns (** - Ruby/zsh extension):"
cd "$BUILD_DIR"
$BENCH "**/test.c" 100 --glob-only
echo
cd "$SCRIPT_DIR/../tests/fixtures"
$BENCH "**/*.c" 100 --glob-only
echo
$BENCH "**/.*" 100 --glob-only
echo
$BENCH "**/" 100 --glob-only
echo
$BENCH "a/**/c" 100 --glob-only
echo

# 3. fnmatchベンチマーク（両方対応）
echo "==== Fnmatch Patterns (fnmatch(3) vs rbc_fnmatch) ===="
echo

echo "Basic patterns:"
$BENCH "*.c" $ITERATIONS --fnmatch-only
echo
$BENCH "*test*" $ITERATIONS --fnmatch-only
echo
$BENCH "a*c" $ITERATIONS --fnmatch-only
echo
$BENCH "???.*" $ITERATIONS --fnmatch-only
echo

echo "Complex ? and * combinations:"
$BENCH "*.?" $ITERATIONS --fnmatch-only
echo
$BENCH "?*.c" $ITERATIONS --fnmatch-only
echo
$BENCH "test_*.c" $ITERATIONS --fnmatch-only
echo
$BENCH "*_?.c" $ITERATIONS --fnmatch-only
echo
$BENCH "?*?*.txt" $ITERATIONS --fnmatch-only
echo

echo "Bracket patterns:"
$BENCH "*.[ch]" $ITERATIONS --fnmatch-only
echo
$BENCH "[a-z]*.c" $ITERATIONS --fnmatch-only
echo
$BENCH "*[0-9].c" $ITERATIONS --fnmatch-only
echo
$BENCH "test[_-]*.c" $ITERATIONS --fnmatch-only
echo

echo "Complex combinations:"
$BENCH "*[0-9]?.txt" $ITERATIONS --fnmatch-only
echo
$BENCH "[a-z]*[0-9].c" $ITERATIONS --fnmatch-only
echo

echo "========================================"
echo "   Benchmark Complete"
echo "========================================"
echo
echo "Summary:"
echo "- Comparable patterns: glob(3) and rbc_glob both support"
echo "- Exclusive patterns: Only rbc_glob supports (**, etc.)"
echo "- fnmatch: Both implementations compared"
