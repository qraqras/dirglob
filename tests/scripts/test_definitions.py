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
    SYSCASE  = 0x20  # 1 << 5


@dataclass
class GlobTestCase:
    """Dir.glob Test Case Definition"""
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


@dataclass
class FnmatchTestCase:
    """File.fnmatch Test Case Definition"""
    id: str
    pattern: str
    text: str
    flags: FNMFlags = FNMFlags.NONE
    desc: str = ""

    def to_dict(self):
        """Convert to dictionary format"""
        return {
            "id": self.id,
            "pattern": self.pattern,
            "text": self.text,
            "flags": self.flags.value,
            "desc": self.desc,
        }


# Backward compatibility alias
TestCase = GlobTestCase


# ============================================================================
# Dir.glob Test Definitions
# ============================================================================

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
    # 10_SpecialChars (spaces, tabs, multiple dots)
    "10_specialchars/file with spaces.txt",
    "10_specialchars/file\twith\ttabs.txt",
    "10_specialchars/multiple..dots...txt",
    "10_specialchars/.dotfile",
    "10_specialchars/..doubledot",
    "10_specialchars/trailing ",
    "10_specialchars/ leading",
    # 11_DeepRecursion
    "11_deeprecursion/**/*.txt",
    "11_deeprecursion/**/deep",
    # 12_EmptyAndSingle
    "12_empty/*",
    "12_single/*",
    # 13_Symlinks
    "13_symlinks/link_to_file",
    "13_symlinks/link_to_dir/*",
    "13_symlinks/**/linked",
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
    ".",
    "..",
    "/",
    "*/*//*///*",
    "日本語.txt",
    # LongPath
    "a" * 100 + "/*.txt",
    # MultipleStars
    "***",
    "*****",
    # DotPatterns
    ".*.*",
    ".*/.*",
    "**/.*",

    # Windows-specific patterns
    "C:/*",
    "C:\\*",
    "//server/share/*",
    "\\\\server\\share\\*",

    # tilde patterns
    "~/*",

    # Complex escape + path combinations
    "dir\\/*.txt",        # Backslash before wildcard
    "dir\\*\\file",       # Multiple backslash-star
]

# フラグ組み合わせ（重要なもの）
FLAG_OPTIONS = [
    FNMFlags.NONE,                                   # No flags
    FNMFlags.NOESCAPE,                               # Disable backslash escaping
    FNMFlags.DOTMATCH,                               # Show hidden files
    FNMFlags.PATHNAME,                               # Slash handling
    FNMFlags.CASEFOLD,                               # Case insensitive
    FNMFlags.EXTGLOB,                                # Brace expansion {a,b}
    FNMFlags.SYSCASE,                                # System case sensitivity
    FNMFlags.DOTMATCH | FNMFlags.PATHNAME,           # Common combo
    FNMFlags.PATHNAME | FNMFlags.CASEFOLD,           # Common combo
    FNMFlags.DOTMATCH | FNMFlags.CASEFOLD,           # Common combo
    FNMFlags.NOESCAPE | FNMFlags.PATHNAME,           # Escape handling
    FNMFlags.PATHNAME | FNMFlags.EXTGLOB,            # Brace with pathname
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

                    cases.append(GlobTestCase(
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

def get_glob_test_cases():
    """Dir.glob用の全テストケースを取得"""
    cases = []
    cases.extend(MANUAL_CASES)
    cases.extend(generate_matrix_cases())
    return cases


# Backward compatibility alias
get_all_test_cases = get_glob_test_cases


# ============================================================================
# File.fnmatch Test Definitions
# ============================================================================

# === Pattern-Text Pairs (fnmatch用) ===

FNMATCH_PATTERN_TEXT_PAIRS = [
    # 基本パターン - サフィックス
    ("*.c", "test.c"),
    ("*.c", "test.h"),
    ("*.c", ".hidden.c"),
    ("*.txt", "file.txt"),
    ("*.so", "libssl.so"),

    # 基本パターン - プレフィックス
    ("test*", "test.c"),
    ("test*", "testing"),
    ("lib*", "libssl.so"),

    # ワイルドカード - 単一
    ("*", ""),
    ("*", "anything"),
    ("*", "a/b"),
    ("*", ".hidden"),

    # ワイルドカード - 複合
    ("a*c", "abc"),
    ("a*c", "axyzc"),
    ("a*c", "adc"),
    ("*test*", "testing"),
    ("*test*", "atestb"),
    ("*test*", "test"),

    # ワイルドカード - 複数
    ("**", "test"),
    ("***", "test"),
    ("****c", "test.c"),

    # プレフィックス+サフィックス
    ("test_*.c", "test_main.c"),
    ("lib*.so", "libssl.so"),
    ("*.so.1.0", "libssl.so.1.0"),

    # 疑問符 - 単一
    ("?", "a"),
    ("?", ""),
    ("?", "ab"),

    # 疑問符 - 複数
    ("??", "ab"),
    ("??", "a"),
    ("???", "abc"),
    ("????.c", "test.c"),

    # 文字クラス - 基本
    ("[abc]", "a"),
    ("[abc]", "b"),
    ("[abc]", "c"),
    ("[abc]", "d"),
    ("[abc]", ""),

    # 文字クラス - 範囲
    ("[a-z]", "a"),
    ("[a-z]", "m"),
    ("[a-z]", "z"),
    ("[a-z]", "A"),
    ("[0-9]", "5"),
    ("[0-9]", "a"),

    # 文字クラス - 否定
    ("[!abc]", "a"),
    ("[!abc]", "d"),
    ("[^abc]", "."), # DOTMATCHなしでは"."はマッチしない
    ("[^a-z]", "a"),
    ("[^a-z]", "1"),
    ("[^a-z]", "."), # DOTMATCHなしでは"."はマッチしない


    # 文字クラス - 複合
    ("file[0-9].txt", "file1.txt"),
    ("file[0-9].txt", "file5.txt"),
    ("file[0-9].txt", "filea.txt"),
    ("[a-z][0-9]", "a1"),
    ("[a-z][0-9]", "z9"),

    # エスケープシーケンス
    ("\\*", "*"),
    ("\\*", "test"),
    ("\\?", "?"),
    ("\\[", "["),
    ("\\\\", "\\"),
    ("test\\*.c", "test*.c"),

    # パス - 基本
    ("a/b", "a/b"),
    ("a/b", "a/c"),
    ("*/*", "a/b"),
    ("*/*", "a/b/c"),

    # パス - ワイルドカード
    ("*/*.c", "src/test.c"),
    ("*/*.c", "test.c"),
    ("dir/*", "dir/file"),
    ("dir/*", "dir/sub/file"),

    # パス - 隠しファイル
    ("*/*.c", "src/.hidden.c"),
    (".*/*.c", ".hidden/test.c"),
    ("*/.*", "dir/.hidden"),

    # 隠しファイル - ドット開始
    (".*", ".hidden"),
    (".*", "visible"),
    (".?", ".a"),
    (".??", ".ab"),

    # 隠しファイル - パス内
    ("*", ".hidden"),
    (".*", ".hidden"),
    ("a/.*", "a/.hidden"),

    # 大文字小文字
    ("test", "TEST"),
    ("test", "Test"),
    ("TEST", "test"),
    ("*.TXT", "file.txt"),
    ("*.txt", "FILE.TXT"),

    # エッジケース - 空文字列
    ("", ""),
    ("", "x"),
    ("x", ""),

    # エッジケース - 長い文字列
    ("a" * 100, "a" * 100),
    ("a" * 100, "a" * 99),
    ("*" + "a" * 100, "x" + "a" * 100),

    # 特殊文字
    ("file with spaces.txt", "file with spaces.txt"),
    ("file\twith\ttabs", "file\twith\ttabs"),
    ("multiple..dots", "multiple..dots"),

    # Unicode
    ("*.txt", "日本語.txt"),
    ("日本語*", "日本語ファイル"),
    ("日本語*", "日本語"),

    # 複雑なパターン
    ("**/*.c", "a/b/test.c"),
    ("a*b*c", "abc"),
    ("a*b*c", "aXbYc"),
    ("*.*.*", "a.b.c"),
]

# === Flag Options (fnmatch用) ===

FNMATCH_FLAG_OPTIONS = [
    FNMFlags.NONE,
    FNMFlags.PATHNAME,
    FNMFlags.DOTMATCH,
    FNMFlags.CASEFOLD,
    FNMFlags.NOESCAPE,
    FNMFlags.PATHNAME | FNMFlags.DOTMATCH,
    FNMFlags.PATHNAME | FNMFlags.CASEFOLD,
]

# === Manual Cases (手動定義の特殊ケース) ===

FNMATCH_MANUAL_CASES = [
    # NOESCAPEフラグの重要テスト
    FnmatchTestCase("fm001", "\\*", "*", FNMFlags.NONE, "Escaped star matches literal star"),
    FnmatchTestCase("fm002", "\\*", "\\*", FNMFlags.NOESCAPE, "NOESCAPE - backslash literal"),
    FnmatchTestCase("fm003", "\\\\", "\\", FNMFlags.NONE, "Escaped backslash"),

    # DOTMATCHフラグの重要テスト
    FnmatchTestCase("fm010", "*", ".hidden", FNMFlags.NONE, "Star no match dotfile (no DOTMATCH)"),
    FnmatchTestCase("fm011", "*", ".hidden", FNMFlags.DOTMATCH, "Star matches dotfile (DOTMATCH)"),
    FnmatchTestCase("fm012", ".*", ".hidden", FNMFlags.NONE, "Explicit dot matches"),

    # PATHNAMEフラグの重要テスト
    FnmatchTestCase("fm020", "*", "a/b", FNMFlags.NONE, "Star matches slash (no PATHNAME)"),
    FnmatchTestCase("fm021", "*", "a/b", FNMFlags.PATHNAME, "Star no match slash (PATHNAME)"),
    FnmatchTestCase("fm022", "?", "/", FNMFlags.PATHNAME, "Question no match slash (PATHNAME)"),

    # CASEFOLDフラグの重要テスト
    FnmatchTestCase("fm030", "test", "TEST", FNMFlags.NONE, "Case sensitive - no match"),
    FnmatchTestCase("fm031", "test", "TEST", FNMFlags.CASEFOLD, "Case insensitive match"),
    FnmatchTestCase("fm032", "[a-z]", "A", FNMFlags.CASEFOLD, "Bracket with CASEFOLD"),

    # フラグ組み合わせ
    FnmatchTestCase("fm040", "*/*.c", "dir/.hidden.c", FNMFlags.PATHNAME, "PATHNAME - dotfile no match"),
    FnmatchTestCase("fm041", "*/*.c", "dir/.hidden.c", FNMFlags.PATHNAME | FNMFlags.DOTMATCH, "PATHNAME + DOTMATCH"),
]

# === Matrix Generation ===

def generate_fnmatch_matrix_cases():
    """(pattern, text)ペア × フラグのマトリクスを生成"""
    cases = []
    test_id = 1000

    for pattern, text in FNMATCH_PATTERN_TEXT_PAIRS:
        for flags in FNMATCH_FLAG_OPTIONS:
            # 最適化: 不要な組み合わせをスキップ

            # エスケープパターンはNONEとNOESCAPEのみテスト
            if "\\" in pattern and flags != FNMFlags.NONE and flags != FNMFlags.NOESCAPE:
                if not (flags & FNMFlags.NOESCAPE):
                    continue

            # パスが含まれない場合、PATHNAMEフラグ単体は不要
            if "/" not in pattern and "/" not in text:
                if flags == FNMFlags.PATHNAME:
                    continue

            # ドットファイルでない場合、DOTMATCH単体は不要
            if not text.startswith(".") and "/." not in text:
                if flags == FNMFlags.DOTMATCH:
                    continue

            # 大文字小文字が同じ場合、CASEFOLD単体は冗長
            if pattern.lower() == pattern and text.lower() == text:
                if flags == FNMFlags.CASEFOLD:
                    continue

            flag_name = flags.name if flags != FNMFlags.NONE else "NONE"
            desc = f"Pattern: '{pattern}', Text: '{text}', Flags: {flag_name}"

            cases.append(FnmatchTestCase(
                id=f"f{test_id:04d}",
                pattern=pattern,
                text=text,
                flags=flags,
                desc=desc
            ))
            test_id += 1

    return cases


def get_fnmatch_test_cases():
    """File.fnmatch用の全テストケースを取得"""
    cases = []
    cases.extend(FNMATCH_MANUAL_CASES)
    cases.extend(generate_fnmatch_matrix_cases())
    return cases


# ============================================================================
# Main
# ============================================================================

if __name__ == "__main__":
    # Dir.glob cases
    glob_cases = get_glob_test_cases()
    glob_matrix_cases = generate_matrix_cases()

    # Count by type
    base_cases = [c for c in glob_matrix_cases if c.base is not None]
    unsorted_cases = [c for c in glob_matrix_cases if c.sort is False]
    base_and_unsorted = [c for c in glob_matrix_cases if c.base is not None and c.sort is False]

    print("=" * 60)
    print("Dir.glob Test Cases")
    print("=" * 60)
    print(f"Total test cases: {len(glob_cases)}")
    print(f"  Manual cases: {len(MANUAL_CASES)}")
    print(f"  Matrix cases: {len(glob_matrix_cases)}")
    print(f"    - with base: {len(base_cases)}")
    print(f"    - unsorted: {len(unsorted_cases)}")
    print(f"    - base+unsorted: {len(base_and_unsorted)}")

    # File.fnmatch cases
    fnmatch_cases = get_fnmatch_test_cases()
    fnmatch_matrix_cases = generate_fnmatch_matrix_cases()

    print()
    print("=" * 60)
    print("File.fnmatch Test Cases")
    print("=" * 60)
    print(f"Total test cases: {len(fnmatch_cases)}")
    print(f"  Manual cases: {len(FNMATCH_MANUAL_CASES)}")
    print(f"  Matrix cases: {len(fnmatch_matrix_cases)}")
    print(f"  Pattern-text pairs: {len(FNMATCH_PATTERN_TEXT_PAIRS)}")
    print(f"  Flag options: {len(FNMATCH_FLAG_OPTIONS)}")
