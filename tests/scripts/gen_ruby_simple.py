#!/usr/bin/env python3
"""
主要テストケースからRuby期待値を生成

test_cases.txtから直接Rubyを実行して期待値を生成する。
マトリクス方式よりシンプルで高速。
"""

import os
import sys
import subprocess
from pathlib import Path


def parse_test_cases(filepath):
    """test_cases.txtをパース"""
    cases = []
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            # Skip comments and empty lines
            if not line or line.startswith('#'):
                continue

            parts = line.split('\t')
            if len(parts) < 6:
                continue

            test_id, pattern, flags, base, sort, description = parts
            cases.append({
                'id': test_id,
                'pattern': pattern,
                'flags': flags,
                'base': base,
                'sort': sort,
                'description': description
            })
    return cases


def convert_flags_to_ruby(flags_str):
    """フラグをRuby形式に変換"""
    if flags_str == '0':
        return '0'

    flag_map = {
        'DOTMATCH': 'File::FNM_DOTMATCH',
        'PATHNAME': 'File::FNM_PATHNAME',
        'CASEFOLD': 'File::FNM_CASEFOLD',
        'NOESCAPE': 'File::FNM_NOESCAPE',
        'EXTGLOB': 'File::FNM_EXTGLOB'
    }

    flags = flags_str.split('|')
    ruby_flags = [flag_map.get(f.strip(), f.strip()) for f in flags]
    return ' | '.join(ruby_flags)


def run_ruby_glob(test_case, fixtures_dir):
    """Rubyでglob実行"""
    pattern = test_case['pattern']
    flags = convert_flags_to_ruby(test_case['flags'])
    base = test_case['base']
    sort = test_case['sort']

    # Base path handling
    if base == 'NULL':
        base_arg = ''
    else:
        base_arg = f', base: "{base}"'

    # Sort handling
    sort_value = 'true' if sort == '1' else 'false'

    ruby_code = f'''
Dir.chdir("{fixtures_dir}") do
  results = Dir.glob("{pattern}", {flags}{base_arg}, sort: {sort_value})
  puts results.join("\\n")
end
'''

    try:
        result = subprocess.run(
            ['ruby', '-e', ruby_code],
            capture_output=True,
            text=True,
            timeout=5
        )

        if result.returncode != 0:
            print(f"Error in {test_case['id']}: {result.stderr}", file=sys.stderr)
            return []

        output = result.stdout.strip()
        return output.split('\n') if output else []

    except subprocess.TimeoutExpired:
        print(f"Timeout in {test_case['id']}", file=sys.stderr)
        return []
    except Exception as e:
        print(f"Exception in {test_case['id']}: {e}", file=sys.stderr)
        return []


def main():
    script_dir = Path(__file__).parent
    tests_dir = script_dir.parent
    fixtures_dir = tests_dir / 'fixtures'

    # Check fixtures directory
    if not fixtures_dir.exists():
        print(f"Error: Fixtures directory not found: {fixtures_dir}", file=sys.stderr)
        sys.exit(1)

    # Parse test cases
    test_cases_file = tests_dir / 'test_cases.txt'
    if not test_cases_file.exists():
        print(f"Error: Test cases file not found: {test_cases_file}", file=sys.stderr)
        sys.exit(1)

    cases = parse_test_cases(test_cases_file)
    print(f"Loaded {len(cases)} test cases")

    # Generate expected outputs
    output_dir = tests_dir / 'ruby_expected' / 'common'
    output_dir.mkdir(parents=True, exist_ok=True)

    success_count = 0
    for i, case in enumerate(cases, 1):
        test_id = case['id']
        print(f"[{i}/{len(cases)}] Generating {test_id}: {case['description']}")

        results = run_ruby_glob(case, fixtures_dir.absolute())

        # Save to file
        output_file = output_dir / f"{test_id}.txt"
        with open(output_file, 'w') as f:
            f.write('\n'.join(results))
            if results:
                f.write('\n')

        success_count += 1

    print(f"\n=== Ruby Expected Output Generated ===")
    print(f"Total: {len(cases)} cases")
    print(f"Success: {success_count} cases")
    print(f"Output: {output_dir}")


if __name__ == '__main__':
    main()
