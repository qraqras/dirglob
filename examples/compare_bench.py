#!/usr/bin/env python3
"""
ベンチマーク結果をわかりやすく表示するスクリプト
glob(3)とrbc_globの公平な比較を行う
"""

import subprocess
import re
import sys

def parse_benchmark_line(line):
    """ベンチマーク結果行をパース"""
    # "glob(3)      : 0.106878 sec total, 0.000107 sec/iter, matches: 3, 9356.44 iter/sec"
    match = re.search(r'(\S+)\s*:\s*([\d.]+) sec total.*?([\d.]+) iter/sec', line)
    if match:
        impl = match.group(1)
        total_time = float(match.group(2))
        iter_per_sec = float(match.group(3))
        return impl, total_time, iter_per_sec
    return None, None, None

def run_single_benchmark(pattern, iterations=1000, mode="--glob-only"):
    """単一パターンのベンチマークを実行"""
    try:
        result = subprocess.run(
            ['./build/examples/bench_glob', pattern, str(iterations), mode],
            cwd='/workspaces/dirglob',
            capture_output=True,
            text=True,
            timeout=30
        )
        return result.stdout
    except Exception as e:
        print(f"Error running benchmark: {e}", file=sys.stderr)
        return ""

def compare_benchmarks():
    """主要なパターンで比較"""

    # glob(3)とrbc_glob両方が対応しているパターン
    comparable_patterns = [
        # Basic patterns
        ("*.c", "Simple wildcard"),
        ("*.txt", "Simple wildcard"),
        ("a*", "Prefix wildcard"),
        ("?.c", "Question mark"),
        ("???.*", "Multiple ?"),

        # Complex ? and * combinations
        ("*.?", "Suffix+?"),
        ("?*.c", "?+wildcard"),
        ("test_*.c", "Prefix+*+suffix"),
        ("*_?.c", "*+?+suffix"),
        ("*test*.c", "Infix wildcard"),
        ("?*?*.txt", "Complex ?*?*"),

        # Bracket patterns
        ("*.[ch]", "Bracket wildcard"),
        ("[a-z]*.c", "Bracket+wildcard"),
        ("*[0-9].c", "Wildcard+bracket"),
        ("test[_-]*.c", "Bracket in middle"),

        # Multi-level
        ("*/*", "Two-level"),
        ("*/*/*", "Three-level"),

        # Brace and complex
        ("{a,b,c}/*", "Brace expansion"),
        ("{test,debug}_*.c", "Brace+pattern"),
        ("*[0-9]?.txt", "Bracket+?"),
        ("[a-z]*[0-9].c", "Bracket+*+bracket"),
    ]

    # rbc_glob独自機能（glob(3)未対応）
    exclusive_patterns = [
        ("**/*.c", "Recursive + wildcard"),
        ("**/", "Recursive dirs"),
        ("a/**/c", "Recursive middle"),
    ]

    fnmatch_patterns = [
        # Basic patterns
        ("*.c", "Simple wildcard"),
        ("*test*", "Infix wildcard"),
        ("a*c", "Prefix+suffix"),
        ("???.*", "Multiple ?"),

        # Complex ? and * combinations
        ("*.?", "Suffix+?"),
        ("?*.c", "?+wildcard"),
        ("test_*.c", "Prefix+*+suffix"),
        ("*_?.c", "*+?+suffix"),
        ("?*?*.txt", "Complex ?*?*"),

        # Bracket patterns
        ("*.[ch]", "Bracket wildcard"),
        ("[a-z]*.c", "Bracket+wildcard"),
        ("*[0-9].c", "Wildcard+bracket"),
        ("test[_-]*.c", "Bracket in middle"),
        ("*[0-9]?.txt", "Bracket+?"),
        ("[a-z]*[0-9].c", "Bracket+*+bracket"),
    ]

    print("=" * 100)
    print("GLOB BENCHMARK COMPARISON - FAIR COMPARISON (1000 iterations)")
    print("=" * 100)
    print()
    print("Both glob(3) and rbc_glob support these patterns:")
    print()
    print(f"{'Pattern':<20} {'Type':<20} {'glob(3) (ms)':<15} {'rbc_glob (ms)':<15} {'Speedup':<10}")
    print("-" * 100)

    for pattern, desc in comparable_patterns:
        output = run_single_benchmark(pattern, 1000, "--glob-only")

        glob3_time = None
        rbc_time = None

        for line in output.split('\n'):
            impl, total_time, _ = parse_benchmark_line(line)
            if impl == "glob(3)":
                glob3_time = total_time * 1000  # convert to ms
            elif impl == "rbc_glob":
                rbc_time = total_time * 1000

        if glob3_time and rbc_time:
            speedup = glob3_time / rbc_time
            if speedup >= 1.0:
                speedup_str = f"{speedup:.2f}x"
                status = "✓" if speedup >= 0.95 else ""
            else:
                speedup_str = f"{1/speedup:.2f}x slower"
                status = ""
            print(f"{pattern:<20} {desc:<20} {glob3_time:<15.3f} {rbc_time:<15.3f} {speedup_str:<10} {status}")

    print()
    print("=" * 100)
    print("RBC_GLOB EXCLUSIVE FEATURES (100 iterations)")
    print("=" * 100)
    print()
    print("Note: glob(3) does NOT support ** recursive patterns")
    print()
    print(f"{'Pattern':<20} {'Type':<25} {'rbc_glob (ms)':<15} {'Comment':<30}")
    print("-" * 100)

    for pattern, desc in exclusive_patterns:
        output = run_single_benchmark(pattern, 100, "--glob-only")

        rbc_time = None

        for line in output.split('\n'):
            impl, total_time, _ = parse_benchmark_line(line)
            if impl == "rbc_glob":
                rbc_time = total_time * 1000

        if rbc_time:
            comment = "Ruby/zsh extension"
            print(f"{pattern:<20} {desc:<25} {rbc_time:<15.3f} {comment:<30}")

    print()
    print("=" * 100)
    print("FNMATCH BENCHMARK COMPARISON (1000 iterations)")
    print("=" * 100)
    print()
    print(f"{'Pattern':<20} {'Type':<20} {'fnmatch(3) (ms)':<18} {'rbc_fnmatch (ms)':<18} {'Speedup':<10}")
    print("-" * 100)

    for pattern, desc in fnmatch_patterns:
        output = run_single_benchmark(pattern, 1000, "--fnmatch-only")

        fnmatch3_time = None
        rbc_time = None

        for line in output.split('\n'):
            impl, total_time, _ = parse_benchmark_line(line)
            if impl == "fnmatch(3)":
                fnmatch3_time = total_time * 1000  # convert to ms
            elif impl == "rbc_fnmatch":
                rbc_time = total_time * 1000

        if fnmatch3_time and rbc_time:
            speedup = fnmatch3_time / rbc_time
            if speedup >= 1.0:
                speedup_str = f"{speedup:.2f}x"
                status = "✓" if speedup >= 0.95 else ""
            else:
                speedup_str = f"{1/speedup:.2f}x slower"
                status = ""
            print(f"{pattern:<20} {desc:<20} {fnmatch3_time:<18.3f} {rbc_time:<18.3f} {speedup_str:<10} {status}")

    print()
    print("=" * 100)
    print("SUMMARY")
    print("=" * 100)
    print()
    print("Comparable Patterns (both implementations support):")
    print("  - Simple wildcards and multi-level patterns: competitive performance")
    print("  - Brace expansion: both support via GLOB_BRACE flag")
    print()
    print("Exclusive Features (rbc_glob only):")
    print("  - ** recursive patterns: Ruby/zsh/bash extension")
    print("  - glob(3) treats ** as literal '**' (not recursive)")
    print()
    print("Fnmatch:")
    print("  - fnmatch(3) is optimized at C library level")
    print("  - rbc_fnmatch provides consistent cross-platform behavior")

if __name__ == "__main__":
    import os
    os.chdir('/workspaces/dirglob/tests/fixtures')
    compare_benchmarks()
