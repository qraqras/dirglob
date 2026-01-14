#!/usr/bin/env python3

from dataclasses import dataclass, field
from typing import List, Optional
from enum import Flag, auto
from pathlib import Path


# Fixtures directory path
SCRIPT_DIR = Path(__file__).parent
FIXTURES_DIR = SCRIPT_DIR.parent / "fixtures"


class FNMFlags(Flag):
    """FNM_* Flags"""
    NONE     = 0x0   # 0
    NOESCAPE = 0x01  # 1 << 0
    PATHNAME = 0x02  # 1 << 1
    DOTMATCH = 0x04  # 1 << 2
    CASEFOLD = 0x08  # 1 << 3
    EXTGLOB  = 0x10  # 1 << 4


@dataclass
class TestCase:
    """Test Case Definition"""
    id: str
    pattern: str
    flags: FNMFlags = FNMFlags.NONE
    base: Optional[str] = None
    sort: bool = False
    desc: str = ""

    def to_dict(self):
        """Convert to dictionary format"""
        return {
            "id": self.id,
            "pattern": self.pattern,
            "flags": self.flags.value,
            "desc": self.desc,
        }


# === Manual Test Cases (重要なエッジケースを手動定義) ===

MANUAL_CASES = [
]


# === Pattern Definitions (マトリクス生成用) ===

# 基本パターン
PATTERNS = [
    # 01_Basic
    "01_basic/file",
    "01_basic/file.txt",
    "01_basic/dir",
    "01_basic/dir/",
    # 02_Asterisk
    "02_asterisk/*",
    "02_asterisk/*/",
    "02_asterisk/*/*",
    "02_asterisk/*/*/",
    "02_asterisk/*.txt",
    "02_asterisk/file.*",
    # 03_QuestionMark
    "03_questionmark/?",
    "03_questionmark/??",
    "03_questionmark/???",
    "03_questionmark/????.c",
    "03_questionmark/file.?",
    "03_questionmark/?/?",
    "03_questionmark/?/?/",
    "03_questionmark/??/??",
    "03_questionmark/??/??/",
    # 04_CharacterClass
    "04_characterclass/[abc]",
    "04_characterclass/[x-z]",
    "04_characterclass/file[0-9].txt",
    "04_characterclass/[^a-c]",
    "04_characterclass/[!x-z]",
    # 05_BraceExpansion
    "05_braceexpansion/{a}",
    "05_braceexpansion/{a,a}",
    "05_braceexpansion/{a,b,c}",
    "05_braceexpansion/file{1,2,3}.txt",
    "05_braceexpansion/{dir1, dir2}/file1.txt",
    "05_braceexpansion/{{a,b},c}",
    "05_braceexpansion/{dir1,}/file1.txt",
    # 06_CaseFold
    "06_casefold/lower.txt",
    "06_casefold/UPPER.TXT",
    "06_casefold/MiXeD.TxT",
    # 07_Recursive
    "07_recursive/**",
    "07_recursive/**/",
    "07_recursive/**/**",
    "07_recursive/**/**/",
    "07_recursive/**/*.txt",
    # 08_EscapeChars
    "08_escapechars/\\[brackets\\].txt",
    "08_escapechars/\\{braces\\}.txt",
    "08_escapechars/\\*asterisk.txt",
    "08_escapechars/\\?question.txt",
    "08_escapechars/\\\\backslash.txt",
    "08_escapechars/\\(parentheses\\).txt",
    # 09_Combined
    "09_combined/*/*.{c,h}",
    "09_combined/**/fil?.[ch]",
    "09_combined/{file[0-9].txt,[a-z]}",
    "09_combined/{**, *}/*.txt",
    # TopLevel
    "*",
    "*/",
    "README.md",
    # Hidden
    ".*",
    ".?",
    ".**/*/",
    ".**/.*/",
    ".hidden/.**/*",
    ".hidden/.**/.*",
    # AbsolutePath
    f"{FIXTURES_DIR}/*",
    f"{FIXTURES_DIR}/*/",
    # RelativePath
    "./*",
    "./*/",
    # EdgeCases
    "",
    "*//*",
    "*///*",
    "日本語.txt",
]

# フラグ組み合わせ（重要なもの）
FLAG_OPTIONS = [
    FNMFlags.NONE,                                   # No flags
    FNMFlags.DOTMATCH,                               # Show hidden files
    FNMFlags.PATHNAME,                               # Slash handling
    FNMFlags.CASEFOLD,                               # Case insensitive
    FNMFlags.DOTMATCH | FNMFlags.PATHNAME,           # Common combo
    FNMFlags.PATHNAME | FNMFlags.CASEFOLD,           # Common combo
    FNMFlags.DOTMATCH | FNMFlags.CASEFOLD,           # Common combo
]

# Base path variations
BASE_OPTIONS = [None, f"{FIXTURES_DIR}/", "."]

# Sort variations
SORT_OPTIONS = [True, False]


# === Programmatic Generation ===

def generate_matrix_cases():
    """パターン*フラグ*base*sortのマトリクスを生成"""
    cases = []
    test_id = 1000  # t1000から開始

    for pattern in PATTERNS:
        for flags in FLAG_OPTIONS:
            for base in BASE_OPTIONS:
                for sort in SORT_OPTIONS:
                    # Skip some redundant combinations
                    if flags == FNMFlags.NONE and pattern in [".*", ".*.c"]:
                        continue

                    # Limit base+unsorted combinations to key patterns only
                    if base is not None and sort is False:
                        # Only test unsorted with base for important patterns
                        if not any(p in pattern for p in ["*", "**", "?"]):
                            continue

                    # Limit base to certain patterns (avoid explosion)
                    if base is not None and base != ".":
                        # Only use non-current base with wildcard patterns
                        if not any(p in pattern for p in ["*", "**", ".c", ".txt"]):
                            continue

                    # Build description
                    desc_parts = [f"Matrix: {pattern}"]
                    if flags != FNMFlags.NONE:
                        desc_parts.append(f"flags={flags.name}")
                    if base is not None:
                        desc_parts.append(f"base={base}")
                    if sort is False:
                        desc_parts.append("unsorted")

                    cases.append(TestCase(
                        id=f"t{test_id:04d}",
                        pattern=pattern,
                        flags=flags,
                        base=base,
                        sort=sort,
                        desc=", ".join(desc_parts)
                    ))
                    test_id += 1

    return cases


# === All Test Cases ===

def get_all_test_cases():
    """全テストケースを取得"""
    cases = []
    cases.extend(MANUAL_CASES)
    cases.extend(generate_matrix_cases())
    return cases


if __name__ == "__main__":
    cases = get_all_test_cases()
    matrix_cases = generate_matrix_cases()

    # Count by type
    base_cases = [c for c in matrix_cases if c.base is not None]
    unsorted_cases = [c for c in matrix_cases if c.sort is False]
    base_and_unsorted = [c for c in matrix_cases if c.base is not None and c.sort is False]

    print(f"Total test cases: {len(cases)}")
    print(f"  Manual cases: {len(MANUAL_CASES)}")
    print(f"  Matrix cases: {len(matrix_cases)}")
    print(f"    - with base: {len(base_cases)}")
    print(f"    - unsorted: {len(unsorted_cases)}")
    print(f"    - base+unsorted: {len(base_and_unsorted)}")
