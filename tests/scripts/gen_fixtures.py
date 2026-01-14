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


    # ========================================
    # 01_basic/
    # ========================================
    print("  📂 01_basic/")
    basic = fixtures_dir / "01_basic"
    basic.mkdir()

    (basic / ".file").write_text(".file\n")
    (basic / "file").write_text("file\n")
    (basic / "file.txt").write_text("file.txt\n")
    (basic / "file.csv").write_text("file.csv\n")
    (basic / "dir").mkdir()
    (basic / ".dir").mkdir()

    # ========================================
    # 02_asterisk/
    # ========================================
    print("  📂 02_asterisk/")
    asterisk = fixtures_dir / "02_asterisk"
    asterisk.mkdir()

    for i in "012":
        dir_i = asterisk / f"dir{i}"
        dir_i.mkdir()
        file_i = dir_i / f"file{i}.txt"
        file_i.write_text(f"dir{i}\n")
        for j in "012":
            dir_j = dir_i / f"subdir{j}"
            dir_j.mkdir()
            file_j = dir_j / f"file{j}.txt"
            file_j.write_text(f"dir{i}/subdir{j}/file{j}\n")

    for c in "012":
        (asterisk / f"file{c}.txt").write_text(f"{c}\n")
    for ext in "ch":
        (asterisk / f"file.{ext}").write_text(f"// {ext}\n")

    # ========================================
    # 03_questionmark/
    # ========================================
    print("  📂 03_questionmark/")
    qmark = fixtures_dir / "03_questionmark"
    qmark.mkdir()

    # 1文字
    for char in "abcdefghijklmnopqrstuvwxyz":
        (qmark / char).write_text(f"{char}\n")

    # 2文字
    for name in ["ab", "xy", "cd", "ef", "gh", "ij", "kl", "mn", "op", "qr", "st", "uv", "wx", "yz"]:
        (qmark / name).write_text(f"{name}\n")

    # 3文字
    for name in ["abc", "def", "ghi", "jkl", "mno", "pqr", "stu", "vwx", "yza"]:
        (qmark / name).write_text(f"{name}\n")

    # 4文字.c
    for name in ["abcd", "efgh", "ijkl", "mnop", "qrst", "uvwx", "yzab"]:
        (qmark / f"{name}.c").write_text(f"// {name}\n")

    # file.?
    for ext in "chd":
        (qmark / f"file.{ext}").write_text(f"file.{ext}\n")

    # ?/? 構造（ファイル名と被らないようにディレクトリ名を選択）
    for i in "0123456789":
        dir_i = qmark / i
        dir_i.mkdir()
        for j in "0123456789":
            (dir_i / j).mkdir()
        for j in "abcdefghijklmnopqrstuvwxyz":
            (dir_i / j).write_text(f"{i}/{j}\n")

    # ??/?? 構造
    for i in ["01", "23", "45", "67", "89"]:
        dir_i = qmark / i
        dir_i.mkdir()
        for j in ["01", "23", "45", "67", "89"]:
            (dir_i / j).mkdir()
        for j in ["ab", "xy", "cd", "ef", "gh", "ij", "kl", "mn", "op", "qr", "st", "uv", "wx", "yz"]:
            (dir_i / j).write_text(f"{i}/{j}\n")

    # ========================================
    # 04_characterclass/
    # ========================================
    print("  📂 04_characterclass/")
    charclass = fixtures_dir / "04_characterclass"
    charclass.mkdir()

    # [abc], [x-z]
    for char in "abcdefghijklmnopqrstuvwxyz":
        (charclass / char).write_text(f"{char}\n")

    # file[0-9].txt
    for i in "0123456789":
        (charclass / f"file{i}.txt").write_text(f"file{i}\n")

    # ========================================
    # 05_braceexpansion/
    # ========================================
    print("  📂 05_braceexpansion/")
    brace = fixtures_dir / "05_braceexpansion"
    brace.mkdir()

    # {a}, {a,b,c}
    for char in "abcdefghijklmnopqrstuvwxyz":
        (brace / char).write_text(f"{char}\n")

    # file{1,2,3}.txt
    for i in "0123456789":
        (brace / f"file{i}.txt").write_text(f"file{i}\n")

    # {dir1,dir2}/file1.txt
    for i in "0123456789":
        dir_i = brace / f"dir{i}"
        dir_i.mkdir()
        for j in "0123456789":
            (dir_i / f"file{j}.txt").write_text(f"dir{i}/file{j}\n")

    # ========================================
    # 06_casefold/
    # ========================================
    print("  📂 06_casefold/")
    casefold = fixtures_dir / "06_casefold"
    casefold.mkdir()

    (casefold / "lower.txt").write_text("lower\n")
    (casefold / "UPPER.TXT").write_text("UPPER\n")
    (casefold / "MiXeD.TxT").write_text("MiXeD\n")

    # ========================================
    # 07_recursive/
    # ========================================
    print("  📂 07_recursive/")
    recursive = fixtures_dir / "07_recursive"
    recursive.mkdir()

    for i in "012":
        (recursive / f"file{i}.txt").write_text(f"file{i}\n")

    for i in "012":
        dir_i = recursive / f"dir{i}"
        dir_i.mkdir()
        for j in "012":
            (dir_i / f"file{j}.txt").write_text(f"dir{i}/file{j}\n")
        for j in "012":
            dir_j = dir_i / f"dir{j}"
            dir_j.mkdir()
            for k in "012":
                (dir_j / f"file{k}.txt").write_text(f"dir{i}/dir{j}/file{k}\n")
            for k in "012":
                dir_k = dir_j / f"dir{k}"
                dir_k.mkdir()
                for l in "012":
                    (dir_k / f"file{l}.txt").write_text(f"dir{i}/dir{j}/dir{k}/file{l}\n")

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

    for c in "ch":
        (combined / f"fila.{c}").write_text(f"// fila.{c}\n")
        (combined / f"filb.{c}").write_text(f"// filb.{c}\n")
        (combined / f"filc.{c}").write_text(f"// filc.{c}\n")
        (combined / f"fild.{c}").write_text(f"// fild.{c}\n")
        (combined / f"file.{c}").write_text(f"// file.{c}\n")

    for i in "0123456789":
        (combined / f"file{i}.txt").write_text(f"file{i}\n")

    for c in "abcdefghijklmnopqrstuvwxyz":
        (combined / f"{c}").write_text(f"{c}\n")

    for i in "0123456789":
        dir_i = combined / f"dir{i}"
        dir_i.mkdir()
        for c in "ch":
            (dir_i / f"fila.{c}").write_text(f"// dir{i}/fila.{c}\n")
            (dir_i / f"filb.{c}").write_text(f"// dir{i}/filb.{c}\n")
            (dir_i / f"filc.{c}").write_text(f"// dir{i}/filc.{c}\n")
            (dir_i / f"fild.{c}").write_text(f"// dir{i}/fild.{c}\n")
            (dir_i / f"file.{c}").write_text(f"// dir{i}/file.{c}\n")

    # ========================================
    # .hidden/
    # ========================================
    print("  📂 .hidden/")
    hidden = fixtures_dir / ".hidden"
    hidden.mkdir()

    # top-level hidden files
    for c in "abcdefghijklmnopqrstuvwxyz":
        (fixtures_dir / f".{c}").write_text(f".{c}\n")
    for i in "0123456789":
        (fixtures_dir / f".hiddenfile{i}").write_text(f".hiddenfile{i}\n")

    def create_hidden_files_in_dir(dir_path, maxdepth, depth):
        if depth < maxdepth:
            for i in "012":
                sub_dir = dir_path / f"sub{i}"
                sub_dir.mkdir()
                (sub_dir / f"file{i}.txt").write_text(f"file{i}\n")
                (sub_dir / f".hiddenfile{i}").write_text(f".hiddenfile{i}\n")
                create_hidden_files_in_dir(sub_dir, maxdepth, depth + 1)

                sub_hidden_dir = dir_path / f".subhidden{i}"
                sub_hidden_dir.mkdir()
                (sub_hidden_dir / f"file{i}.txt").write_text(f"file{i}\n")
                (sub_hidden_dir / f".hiddenfile{i}").write_text(f".hiddenfile{i}\n")
                create_hidden_files_in_dir(sub_hidden_dir, maxdepth, depth + 1)

    create_hidden_files_in_dir(hidden, maxdepth=2, depth=0)

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
        "05_braceexpansion", "06_casefold", "07_recursive", "08_escapechars",
        "09_combined", ".hidden"
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
