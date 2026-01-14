#!/usr/bin/env python3
"""
Ruby期待値生成スクリプト v2

test_definitions.pyから直接テストケースを読み込み、
RubyでDir.globを実行して期待値を生成する。
"""

import subprocess
import sys
from pathlib import Path
from test_definitions import get_all_test_cases, FIXTURES_DIR, FNMFlags


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
    # バックスラッシュは2つ必要（Python → Ruby文字列リテラル）
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('"', '\\"')


def generate_ruby_code(test_case):
    """テストケースからRubyコードを生成"""
    pattern = escape_ruby_string(test_case.pattern)
    flags = flags_to_ruby_int(test_case.flags)
    base = test_case.base
    sort = test_case.sort

    # baseパラメータ処理
    if base is None:
        base_arg = ""
    else:
        # FIXTURES_DIRからの相対パスに変換
        if base == ".":
            base_arg = ', base: "."'
        else:
            base_arg = f', base: "{escape_ruby_string(base)}"'

    # sortパラメータ
    sort_arg = f', sort: {"true" if sort else "false"}'

    # Dir.glob呼び出し
    code = f'Dir.glob("{pattern}", {flags}{base_arg}{sort_arg})'

    return code


def run_ruby_glob(test_case):
    """Rubyを実行してDir.globの結果を取得"""
    ruby_code = generate_ruby_code(test_case)

    full_code = f'''
fixtures_dir = "{FIXTURES_DIR}"
Dir.chdir(fixtures_dir) do
  results = {ruby_code}
  puts results.join("\\n")
end
'''

    try:
        result = subprocess.run(
            ['ruby', '-e', full_code],
            capture_output=True,
            text=True,
            timeout=10
        )

        if result.returncode != 0:
            print(f"❌ Ruby error for {test_case.id}:", file=sys.stderr)
            print(f"   Pattern: {test_case.pattern}", file=sys.stderr)
            print(f"   STDERR: {result.stderr}", file=sys.stderr)
            return None

        return result.stdout

    except subprocess.TimeoutExpired:
        print(f"⏱️  Timeout for {test_case.id}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"❌ Exception for {test_case.id}: {e}", file=sys.stderr)
        return None


def main():
    """メイン処理"""
    print("=" * 60)
    print("🔨 Generating Ruby Expected Outputs")
    print("=" * 60)

    # 出力ディレクトリ作成
    output_dir = Path(__file__).parent.parent / "ruby_expected"
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"📂 Output directory: {output_dir}")
    print(f"📂 Fixtures directory: {FIXTURES_DIR}")

    # テストケース取得
    test_cases = get_all_test_cases()
    print(f"📋 Total test cases: {len(test_cases)}\n")

    success_count = 0
    error_count = 0

    # 各テストケースを処理
    for i, test_case in enumerate(test_cases, 1):
        # 進捗表示（100件ごと）
        if i % 100 == 0 or i == 1:
            print(f"  Processing {i}/{len(test_cases)}...")

        # Ruby実行
        output = run_ruby_glob(test_case)

        if output is None:
            error_count += 1
            continue

        # ファイル保存
        output_file = output_dir / f"{test_case.id}.txt"
        output_file.write_text(output)
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
