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
FLAG_COMBINATIONS = [
    FNMFlags.NONE,                                    # No flags
    FNMFlags.DOTMATCH,                                # Show hidden files
    FNMFlags.PATHNAME,                                # Slash handling
    FNMFlags.CASEFOLD,                                # Case insensitive
    FNMFlags.DOTMATCH | FNMFlags.PATHNAME,           # Common combo
    FNMFlags.PATHNAME | FNMFlags.CASEFOLD,           # Common combo
    FNMFlags.DOTMATCH | FNMFlags.CASEFOLD,           # Common combo
]


# === Programmatic Generation ===

def generate_matrix_cases():
    """パターン×フラグのマトリクスを生成"""
    cases = []
    test_id = 1000  # t1000から開始

    for pattern in PATTERNS:
        for flags in FLAG_COMBINATIONS:
            # Skip some redundant combinations
            if flags == FNMFlags.NONE and pattern in [".*", ".*.c"]:
                # Hidden patterns without DOTMATCH are less interesting
                continue

            cases.append(TestCase(
                id=f"t{test_id:04d}",
                pattern=pattern,
                flags=flags,
                desc=f"Matrix: {pattern} with {flags.name if flags != FNMFlags.NONE else 'no flags'}"
            ))
            test_id += 1

    return cases


# === Base Path Variations ===

BASE_PATH_CASES = [
    TestCase("t2000", "*.c", base="src", desc="Base: src"),
    TestCase("t2001", "**/*.c", base="tests", desc="Base: tests"),
    TestCase("t2002", "*", flags=FNMFlags.DOTMATCH, base=".", desc="Base: current"),
]


# === Unsorted Tests ===

UNSORTED_CASES = [
    TestCase("t3000", "*", sort=False, desc="Unsorted: all files"),
    TestCase("t3001", "**/*.c", sort=False, desc="Unsorted: recursive"),
]


# === All Test Cases ===

def get_all_test_cases():
    """全テストケースを取得"""
    cases = []
    cases.extend(MANUAL_CASES)
    cases.extend(generate_matrix_cases())
    cases.extend(BASE_PATH_CASES)
    cases.extend(UNSORTED_CASES)
    return cases


if __name__ == "__main__":
    cases = get_all_test_cases()
    print(f"Total test cases: {len(cases)}")
    print(f"  Manual cases: {len(MANUAL_CASES)}")
    print(f"  Matrix cases: {len(generate_matrix_cases())}")
    print(f"  Base path cases: {len(BASE_PATH_CASES)}")
    print(f"  Unsorted cases: {len(UNSORTED_CASES)}")
