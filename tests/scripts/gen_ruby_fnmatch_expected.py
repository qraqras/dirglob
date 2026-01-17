#!/usr/bin/env python3
"""
Ruby期待値生成スクリプト (File.fnmatch版)

test_definitions.pyからfnmatchテストケースを読み込み、
RubyでFile.fnmatchを実行して期待値を生成する。
"""

import subprocess
import sys
from pathlib import Path
from test_definitions import get_fnmatch_test_cases, FNMFlags


def flags_to_ruby_int(flags: FNMFlags) -> int:
    """FNMFlagsをRuby互換の整数値に変換"""
    ruby_flags = 0
    if flags & FNMFlags.NOESCAPE:
        ruby_flags |= 0x01  # File::FNM_NOESCAPE
    if flags & FNMFlags.PATHNAME:
        ruby_flags |= 0x02  # File::FNM_PATHNAME
    if flags & FNMFlags.DOTMATCH:
        ruby_flags |= 0x04  # File::FNM_DOTMATCH
    if flags & FNMFlags.CASEFOLD:
        ruby_flags |= 0x08  # File::FNM_CASEFOLD
    if flags & FNMFlags.EXTGLOB:
        ruby_flags |= 0x10  # File::FNM_EXTGLOB
    return ruby_flags


def escape_ruby_string(s):
    """Ruby文字列リテラル用にエスケープ"""
    # バックスラッシュとダブルクォートをエスケープ
    # Rubyの文字列リテラル内で正しく扱えるように
    s = s.replace('\\', '\\\\')  # \ -> \\
    s = s.replace('"', '\\"')    # " -> \"
    s = s.replace('\n', '\\n')   # 改行 -> \n
    s = s.replace('\t', '\\t')   # タブ -> \t
    return s


def generate_ruby_code(test_case):
    """テストケースからRubyコードを生成"""
    pattern = escape_ruby_string(test_case.pattern)
    text = escape_ruby_string(test_case.text)
    flags = flags_to_ruby_int(test_case.flags)

    # File.fnmatch呼び出し
    code = f'File.fnmatch("{pattern}", "{text}", {flags})'
    return code


def run_ruby_fnmatch(test_case):
    """Rubyを実行してFile.fnmatchの結果を取得"""
    ruby_code = generate_ruby_code(test_case)

    full_code = f'''
result = {ruby_code}
puts result ? "true" : "false"
'''

    try:
        result = subprocess.run(
            ['ruby', '-e', full_code],
            capture_output=True,
            text=True,
            timeout=5
        )

        if result.returncode != 0:
            print(f"❌ Ruby error for {test_case.id}:", file=sys.stderr)
            print(f"   Pattern: '{test_case.pattern}'", file=sys.stderr)
            print(f"   Text: '{test_case.text}'", file=sys.stderr)
            print(f"   Flags: {test_case.flags}", file=sys.stderr)
            print(f"   STDERR: {result.stderr}", file=sys.stderr)
            return None

        return result.stdout.strip()

    except subprocess.TimeoutExpired:
        print(f"⏱️  Timeout for {test_case.id}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"❌ Exception for {test_case.id}: {e}", file=sys.stderr)
        return None


def main():
    """メイン処理"""
    print("=" * 60)
    print("🔨 Generating Ruby Expected Outputs (File.fnmatch)")
    print("=" * 60)

    # 出力ディレクトリ作成
    output_dir = Path(__file__).parent.parent / "ruby_fnmatch_expected"
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"📂 Output directory: {output_dir}")

    # テストケース取得
    test_cases = get_fnmatch_test_cases()
    print(f"📋 Total test cases: {len(test_cases)}\n")

    success_count = 0
    error_count = 0

    # 各テストケースを処理
    for i, test_case in enumerate(test_cases, 1):
        # 進捗表示（50件ごと）
        if i % 50 == 0 or i == 1:
            print(f"  Processing {i}/{len(test_cases)}...")

        # Ruby実行
        output = run_ruby_fnmatch(test_case)

        if output is None:
            error_count += 1
            continue

        # 結果検証
        if output not in ("true", "false"):
            print(f"❌ Invalid output for {test_case.id}: '{output}'", file=sys.stderr)
            error_count += 1
            continue

        # ファイル保存
        output_file = output_dir / f"{test_case.id}.txt"
        output_file.write_text(output + "\n")  # 末尾に改行追加
        success_count += 1

    print("\n" + "=" * 60)
    print("📊 Summary")
    print("=" * 60)
    print(f"  ✅ Success: {success_count}")
    print(f"  ❌ Errors: {error_count}")
    print(f"  📁 Output: {output_dir}")
    print("=" * 60)

    return 0 if error_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
