#!/bin/bash
# Windows Build Test Script (Cross-compilation only)
# This script builds Windows binaries on Linux using MinGW-w64
# Actual execution testing requires Windows or x86_64 Wine environment

set -e

BUILD_DIR="${1:-build-win64}"

echo "=== Windows Cross-Build Test ==="
echo "Build directory: $BUILD_DIR"
echo ""

# Clean and create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Configure with MinGW toolchain
echo "[1/3] Configuring with MinGW-w64..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
    -DBUILD_TESTING=ON \
    -DBUILD_EXAMPLES=ON

# Build
echo "[2/3] Building..."
cmake --build "$BUILD_DIR" --parallel

# Check built binaries
echo "[3/3] Checking built binaries..."
echo ""
echo "Built executables:"
find "$BUILD_DIR" -name "*.exe" -type f | while read f; do
    size=$(stat -c %s "$f" 2>/dev/null || stat -f %z "$f" 2>/dev/null)
    echo "  $(basename "$f") ($(numfmt --to=iec $size 2>/dev/null || echo "${size} bytes"))"
done

echo ""
echo "Build artifacts:"
find "$BUILD_DIR" -name "*.a" -o -name "*.lib" | head -5

echo ""
echo "=== Windows cross-build completed successfully ==="
echo ""
echo "To run tests on Windows:"
echo "  1. Push to GitHub and check CI results"
echo "  2. Or copy binaries to a Windows machine"
echo ""
echo "GitHub Actions Windows CI: .github/workflows/ci.yml"
