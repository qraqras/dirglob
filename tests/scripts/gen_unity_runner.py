#!/usr/bin/env python3
"""Generate a Unity test runner by scanning test source files for functions named test_*.
Usage: gen_unity_runner.py <tests_dir> <out_file>
"""
import os
import re
import sys

if len(sys.argv) != 3:
    print("Usage: gen_unity_runner.py <tests_dir> <out_file>", file=sys.stderr)
    sys.exit(2)

tests_dir = sys.argv[1]
out_file = sys.argv[2]

# Collect .c files in tests_dir (non-recursive), ignore vendor and scripts directories
c_files = []
# List of standalone tests that have their own main() and should not be included
standalone_tests = {
    'test_glob_generated.c',
    'test_glob_v2_hints.c',
}
for name in sorted(os.listdir(tests_dir)):
    if not name.endswith('.c'):
        continue
    if name == os.path.basename(out_file):
        continue
    # Skip standalone tests (they have their own test executables)
    if name in standalone_tests:
        continue
    c_files.append(os.path.join(tests_dir, name))

# Also scan generated test files if they exist
generated_dir = os.path.join(os.path.dirname(out_file), 'generated')
if os.path.isdir(generated_dir):
    for name in sorted(os.listdir(generated_dir)):
        if name.endswith('.c'):
            c_files.append(os.path.join(generated_dir, name))

pattern = re.compile(r"^\s*(?:void|int)\s+(test_[A-Za-z0-9_]+)\s*\(", re.MULTILINE)
static_pat = re.compile(r"^\s*static\s+(?:void|int)\s+(test_[A-Za-z0-9_]+)\s*\(", re.MULTILINE)

found = []
for path in c_files:
    try:
        with open(path, 'r', encoding='utf-8') as fh:
            src = fh.read()
    except Exception as e:
        print(f"Warning: could not read {path}: {e}", file=sys.stderr)
        continue

    for m in pattern.finditer(src):
        name = m.group(1)
        if name not in found:
            # detect static
            if static_pat.search(src):
                # if function specifically marked static, warn (but still add if not static)
                # Check if this specific function is static
                specific_static = re.search(r"^\s*static\s+(?:void|int)\s+%s\s*\(" % re.escape(name), src, re.MULTILINE)
                if specific_static:
                    print(f"Warning: test function '{name}' in {path} is static; it will not be callable by runner.", file=sys.stderr)
            found.append(name)

# Sort for stable order
found.sort()

# Build output
lines = []
lines.append('/* Auto-generated test runner. Do not edit. */')
lines.append('#include "unity.h"')
lines.append('')
for name in found:
    lines.append(f"extern void {name}(void);")
lines.append('')
lines.append('int main(void)')
lines.append('{')
lines.append('    UNITY_BEGIN();')
for name in found:
    lines.append(f"    RUN_TEST({name});")
lines.append('    return UNITY_END();')
lines.append('}')
new_content = '\n'.join(lines) + '\n'

old_content = None
if os.path.exists(out_file):
    try:
        with open(out_file, 'r', encoding='utf-8') as fh:
            old_content = fh.read()
    except Exception:
        old_content = None

if old_content != new_content:
    os.makedirs(os.path.dirname(out_file), exist_ok=True)
    with open(out_file, 'w', encoding='utf-8') as fh:
        fh.write(new_content)
    print(f"Generated {out_file} with {len(found)} tests.")
else:
    print(f"No changes to {out_file}.")

sys.exit(0)
