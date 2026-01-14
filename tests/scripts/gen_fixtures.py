#!/usr/bin/env python3
"""
テストfixtures生成スクリプト (test_definitions.pyのPATTERNSに対応)
"""

import shutil
from pathlib import Path


def create_fixtures(fixtures_dir):
    """テストfixtures生成 - test_definitions.pyのPATTERNSに完全対応"""

    # Clean and recreate fixtures directory
    if fixtures_dir.exists():
        print(f"🗑️  Removing existing fixtures: {fixtures_dir}")
        shutil.rmtree(fixtures_dir)

    fixtures_dir.mkdir(parents=True)
    print(f"📁 Creating fixtures directory: {fixtures_dir}")

    # ========================================
    # 0. トップレベルのファイル
    # ========================================
    print("  📄 Top-level files")

    # 通常ファイル
    (fixtures_dir / "README.txt").write_text("Fixtures\n")
    (fixtures_dir / "test.c").write_text("// top test.c\n")
    (fixtures_dir / "main.h").write_text("// top main.h\n")
    (fixtures_dir / "data.txt").write_text("data\n")

    # 隠しファイル
    (fixtures_dir / ".hiddenfile1").write_text("hidden\n")
    (fixtures_dir / ".hiddenfile2").write_text("hidden\n")
    (fixtures_dir / ".hiddenfile3").write_text("hidden\n")


    # ========================================
    # 01_basic/
    # ========================================
    print("  📂 01_basic/")
    basic = fixtures_dir / "01_basic"
    basic.mkdir()

    (basic / "file").write_text("file\n")
    (basic / "file.txt").write_text("file.txt\n")
    (basic / "file.c").write_text("// C\n")
    (basic / "file.h").write_text("// H\n")

    dir_path = basic / "dir"
    dir_path.mkdir()
    (dir_path / "nested.txt").write_text("nested\n")

    # ========================================
    # 02_asterisk/
    # ========================================
    print("  📂 02_asterisk/")
    asterisk = fixtures_dir / "02_asterisk"
    asterisk.mkdir()

    (asterisk / "file1.txt").write_text("file1\n")
    (asterisk / "file2.txt").write_text("file2\n")
    (asterisk / "file.c").write_text("// c\n")
    (asterisk / "file.h").write_text("// h\n")
    (asterisk / "file.cpp").write_text("// cpp\n")
    (asterisk / "test.txt").write_text("test\n")

    # ディレクトリ構造
    for i in range(3):
        dir1 = asterisk / f"dir{i}"
        dir1.mkdir()
        (dir1 / "file.txt").write_text(f"dir{i}\n")

        dir2 = dir1 / f"subdir{i}"
        dir2.mkdir()
        (dir2 / "nested.txt").write_text(f"nested{i}\n")

    # ========================================
    # 03_questionmark/
    # ========================================
    print("  📂 03_questionmark/")
    qmark = fixtures_dir / "03_questionmark"
    qmark.mkdir()

    # 1文字
    for char in "abcxyz":
        (qmark / char).write_text(f"{char}\n")

    # 2文字
    for name in ["ab", "cd", "xy"]:
        (qmark / name).write_text(f"{name}\n")

    # 3文字
    for name in ["abc", "xyz", "foo"]:
        (qmark / name).write_text(f"{name}\n")

    # 4文字.c
    for name in ["test", "main", "file", "glob"]:
        (qmark / f"{name}.c").write_text(f"// {name}\n")

    # file.?
    for ext in "chd":
        (qmark / f"file.{ext}").write_text(f"file.{ext}\n")

    # ?/? 構造（ファイル名と被らないようにディレクトリ名を選択）
    for c1 in "pq":  # abcxyzfooと被らない
        dir1 = qmark / c1
        dir1.mkdir()
        for c2 in "rs":
            (dir1 / c2).write_text(f"{c1}/{c2}\n")

    # ??/?? 構造
    for d1 in ["mn", "op"]:
        dir1 = qmark / d1
        dir1.mkdir()
        for d2 in ["uv", "wx"]:
            (dir1 / d2).write_text(f"{d1}/{d2}\n")

    # ========================================
    # 04_characterclass/
    # ========================================
    print("  📂 04_characterclass/")
    charclass = fixtures_dir / "04_characterclass"
    charclass.mkdir()

    # [abc], [x-z]
    for char in "abcdefxyz":
        (charclass / char).write_text(f"{char}\n")

    # file[0-9].txt
    for i in range(10):
        (charclass / f"file{i}.txt").write_text(f"file{i}\n")

    (charclass / "test.txt").write_text("test\n")
    (charclass / "other.txt").write_text("other\n")

    # ========================================
    # 05_braceexpansion/
    # ========================================
    print("  📂 05_braceexpansion/")
    brace = fixtures_dir / "05_braceexpansion"
    brace.mkdir()

    # {a}, {a,b,c}
    for char in "abc":
        (brace / char).write_text(f"{char}\n")

    # file{1,2,3}.txt
    for i in range(1, 4):
        (brace / f"file{i}.txt").write_text(f"file{i}\n")

    # {dir1,dir2}/file1.txt
    for dirname in ["dir1", "dir2"]:
        dir_path = brace / dirname
        dir_path.mkdir()
        (dir_path / "file1.txt").write_text(f"{dirname}/file1\n")
        (dir_path / "file2.txt").write_text(f"{dirname}/file2\n")

    (brace / "file.txt").write_text("file\n")

    # ========================================
    # 06_casefold/
    # ========================================
    print("  📂 06_casefold/")
    casefold = fixtures_dir / "06_casefold"
    casefold.mkdir()

    (casefold / "lower.txt").write_text("lower\n")
    (casefold / "test.txt").write_text("test\n")

    # Case-insensitive対応
    (casefold / "UPPER_FILE.TXT").write_text("UPPER\n")
    (casefold / "MiXeD_CaSe.TxT").write_text("MiXeD\n")
    (casefold / "CamelCase.txt").write_text("Camel\n")

    # ========================================
    # 07_recursive/
    # ========================================
    print("  📂 07_recursive/")
    recursive = fixtures_dir / "07_recursive"
    recursive.mkdir()

    (recursive / "file1.txt").write_text("l1\n")
    (recursive / "file1.c").write_text("// l1\n")

    level2 = recursive / "level2"
    level2.mkdir()
    (level2 / "file2.txt").write_text("l2\n")
    (level2 / "file2.c").write_text("// l2\n")

    level3 = level2 / "level3"
    level3.mkdir()
    (level3 / "file3.txt").write_text("l3\n")
    (level3 / "file3.c").write_text("// l3\n")

    branch = recursive / "branch"
    branch.mkdir()
    (branch / "branch.txt").write_text("branch\n")

    # ========================================
    # 08_escapechars/
    # ========================================
    print("  📂 08_escapechars/")
    escapechars = fixtures_dir / "08_escapechars"
    escapechars.mkdir()

    (escapechars / "[brackets].txt").write_text("brackets\n")
    (escapechars / "{braces}.txt").write_text("braces\n")
    (escapechars / "*asterisk.txt").write_text("asterisk\n")
    (escapechars / "?question.txt").write_text("question\n")
    (escapechars / "\\backslash.txt").write_text("backslash\n")
    (escapechars / "(parentheses).txt").write_text("parens\n")

    # ========================================
    # 09_combined/
    # ========================================
    print("  📂 09_combined/")
    combined = fixtures_dir / "09_combined"
    combined.mkdir()

    for dirname in ["dir1", "dir2", "dir3"]:
        dir_path = combined / dirname
        dir_path.mkdir()
        (dir_path / "file.c").write_text(f"// {dirname}\n")
        (dir_path / "file.h").write_text(f"// {dirname}\n")
        (dir_path / "fila.c").write_text(f"// fila\n")
        (dir_path / "filb.h").write_text(f"// filb\n")
        (dir_path / "test.txt").write_text(f"test\n")

    # トップレベル
    (combined / "a").write_text("a\n")
    (combined / "b").write_text("b\n")
    for i in range(5):
        (combined / f"file{i}.txt").write_text(f"file{i}\n")

    # ========================================
    # .hidden/
    # ========================================
    print("  📂 .hidden/")
    hidden = fixtures_dir / ".hidden"
    hidden.mkdir()

    (hidden / "visible.txt").write_text("visible\n")
    (hidden / "normal.c").write_text("// normal\n")
    (hidden / "dotfile").write_text("dotfile\n")
    (hidden / "config").write_text("config\n")

    subdir = hidden / "subdir"
    subdir.mkdir()
    (subdir / "file.txt").write_text("in subdir\n")

    nested = subdir / "nested"
    nested.mkdir()
    (nested / "deep.txt").write_text("deep\n")

    # ========================================
    # エッジケース
    # ========================================
    print("  📄 Edge cases")

    # 日本語
    (fixtures_dir / "日本語.txt").write_text("Japanese\n")

    # 空ファイル
    (fixtures_dir / "empty.txt").write_text("")

    # 複数スラッシュテスト用
    multi = fixtures_dir / "multi"
    multi.mkdir()
    (multi / "file.txt").write_text("multi\n")

    print(f"✅ Fixtures created")


def validate_fixtures(fixtures_dir):
    """fixtures検証"""
    print("\n🔍 Validating fixtures...")

    required_dirs = [
        "01_basic", "02_asterisk", "03_questionmark", "04_characterclass",
        "05_braceexpansion", "06_casefold", "07_recursive", "08_combined",
        ".hidden"
    ]

    all_ok = True
    for dirname in required_dirs:
        dir_path = fixtures_dir / dirname
        if not dir_path.exists():
            print(f"  ⚠️  Missing: {dirname}/")
            all_ok = False
        else:
            file_count = len(list(dir_path.rglob("*")))
            print(f"  ✅ {dirname}/ ({file_count} items)")

    return all_ok


def print_summary(fixtures_dir):
    """統計サマリー"""
    print("\n" + "="*60)
    print("📊 Fixture Summary")
    print("="*60)

    all_files = list(fixtures_dir.rglob("*"))
    files = [f for f in all_files if f.is_file()]
    dirs = [f for f in all_files if f.is_dir()]

    print(f"  Total files: {len(files)}")
    print(f"  Total dirs: {len(dirs)}")
    print("="*60 + "\n")


def main():
    script_dir = Path(__file__).parent
    tests_dir = script_dir.parent
    fixtures_dir = tests_dir / "fixtures"

    print("="*60)
    print("🔨 Generating Test Fixtures (test_definitions.py compatible)")
    print("="*60)

    create_fixtures(fixtures_dir)
    validate_fixtures(fixtures_dir)
    print_summary(fixtures_dir)


if __name__ == "__main__":
    main()
