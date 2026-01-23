# MRIのドットエントリ処理 - 完全分析

## 概要
このドキュメントは、Ruby MRI (Matz's Ruby Implementation) の `Dir.glob` がドットエントリ（`.`で始まるファイル・ディレクトリ）をどのように扱うかを詳細に分析したものです。

## 1. MRIの基本ルール

### 1.1 FNM_DOTMATCHフラグなしの挙動

#### パターン中のドット
```
パターンが明示的にドットで始まる場合:
  .* → .dotfile, .hidden にマッチ
  .?? → .ab, .cd にマッチ
  .[abc] → .a, .b, .c にマッチ

パターンがドットで始まらない場合:
  * → file.txt にマッチ、.dotfile にマッチしない
  ? → a にマッチ、.a にマッチしない
  [abc] → a にマッチ、.a にマッチしない
```

#### ワイルドカード動作の原則
- **wildcards (* ? []) は先頭のドットにマッチしない**
- 明示的な `.` はマッチする

ソースコード証拠:
```c
// dir.c L442-475, fnmatch()
const int period = !(flags & FNM_DOTMATCH);

// L77 (fnmatch.c相当)
// <Ruby>: DOTMATCH - single * should not match leading dot
if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags))
    return RBC_NOMATCH;
```

### 1.2 FNM_DOTMATCHフラグありの挙動

フラグが設定されている場合、**ワイルドカードは先頭のドットにもマッチする**:
```
* → file.txt, .dotfile 両方にマッチ
? → a, .a 両方にマッチ (1文字なら)
[abc] → a, .a 両方にマッチ
```

ただし、**特別なエントリ `.` と `..` は別途処理される**。

## 2. globにおける "." と ".." の扱い

### 2.1 "." エントリ (カレントディレクトリ)

#### MRIの動作
```ruby
Dir.glob('*')                    # => "." は含まれない
Dir.glob('*', File::FNM_DOTMATCH) # => "." は含まれる場合がある
Dir.glob('**', File::FNM_DOTMATCH) # => "." が含まれる
Dir.glob('.*')                   # => "." が含まれる
```

#### コード内の処理 (dir.c L2837-2871)
```c
int skipdot = (flags & FNM_GLOB_SKIPDOT);
flags |= FNM_GLOB_SKIPDOT;  // デフォルトで "." をスキップ

while ((dp = glob_getent(&globent, flags, enc)) != NULL) {
    // ...ディレクトリエントリを処理
}
```

**重要**: MRIは内部で `FNM_GLOB_SKIPDOT` フラグを使用して "." をスキップ制御。

### 2.2 ".." エントリ (親ディレクトリ)

**MRIのルール: ".." は常にスキップ** (マッチ対象にならない)

証拠:
```ruby
Dir.glob('**', File::FNM_DOTMATCH)
# => "." は含まれるが、".." は含まれない
```

理由: 無限再帰の防止、セマンティクス上の妥当性

## 3. 再帰パターン (**) とドットエントリ

### 3.1 ** の基本挙動

#### パターン例と動作
```ruby
# FNM_DOTMATCHなし
Dir.glob('**/*')
# => .hidden/ ディレクトリに入らない
# => .dotfile はマッチしない

# FNM_DOTMATCHあり
Dir.glob('**/*', File::FNM_DOTMATCH)
# => .hidden/ ディレクトリに入る
# => .dotfile にマッチする
```

### 3.2 明示的なドットを持つ再帰パターン

#### .**/pattern の挙動
```ruby
Dir.glob('.**/foo')
# => .hidden/foo にマッチ
# => hidden/foo にマッチしない (ドット始まりのディレクトリのみ)
```

**ルール**: `.**` はドットで始まるディレクトリにのみ再帰する。

#### **/.* の挙動
```ruby
Dir.glob('**/.* ')
# => すべてのディレクトリ内の .dotfile にマッチ
# => FNM_DOTMATCHなしでも、パターンに明示的な . があるため

Dir.glob('**/.* ', File::FNM_DOTMATCH)
# => "." エントリも含む (各ディレクトリの)
```

### 3.3 MRIのコード内での処理

dir.c L2837-2871 (glob_helper):
```c
// ** 処理前に SKIPDOT を設定
int skipdot = (flags & FNM_GLOB_SKIPDOT);
flags |= FNM_GLOB_SKIPDOT;  // 再帰時は基本的に "." をスキップ

while ((dp = glob_getent(&globent, flags, enc)) != NULL) {
    // ディレクトリ走査
    if (dirent_match(p->str, enc, name, dp, flags))
        *new_end++ = p->next;
}
```

**ポイント**:
1. 再帰処理に入る前に `FNM_GLOB_SKIPDOT` を設定
2. ディレクトリエントリ列挙時、"." は基本的にスキップされる
3. ただし、パターンが明示的に "." を要求する場合は例外

## 4. FNM_PATHNAME フラグとの相互作用

### 4.1 基本動作
```ruby
# FNM_PATHNAMEあり
File.fnmatch('*/*', 'dir/.hidden', File::FNM_PATHNAME)
# => false (先頭のドットにマッチしない)

File.fnmatch('*/*', 'dir/.hidden', File::FNM_PATHNAME | File::FNM_DOTMATCH)
# => true
```

### 4.2 **との組み合わせ
```ruby
File.fnmatch('**/foo', 'a/.b/c/foo', File::FNM_PATHNAME)
# => false (.b がマッチしない)

File.fnmatch('**/foo', 'a/.b/c/foo', File::FNM_PATHNAME | File::FNM_DOTMATCH)
# => true
```

**ルール**: 各パスセグメントごとにドットマッチングルールが適用される。

## 5. ディレクトリ走査時のドット処理ロジック

### 5.1 MRIの判定フロー

```
for each directory entry:
    1. name == "." ?
       → FNM_GLOB_SKIPDOT が設定されている → スキップ
       → 設定されていない AND パターンがマッチ → 含める

    2. name == ".." ?
       → 常にスキップ

    3. name starts with '.' (but not "." or "..") ?
       → パターンが明示的に '.' で始まる → マッチ判定へ
       → パターンが '.' で始まらない:
          - FNM_DOTMATCH が設定 → マッチ判定へ
          - FNM_DOTMATCH なし → スキップ

    4. 通常のファイル
       → マッチ判定へ
```

### 5.2 MRIのコード (dir.c L2694-2871 glob_helper)

```c
static int glob_helper(
    int fd,
    const char *path,
    size_t baselen,
    size_t namelen,
    int dirsep,
    rb_pathtype_t pathtype,
    struct glob_pattern **beg,
    struct glob_pattern **end,
    int flags,
    const ruby_glob_funcs_t *funcs,
    VALUE arg,
    rb_encoding *enc)
{
    // ...

    // 再帰処理の前に SKIPDOT を保存・設定
    int skipdot = (flags & FNM_GLOB_SKIPDOT);
    flags |= FNM_GLOB_SKIPDOT;

    while ((dp = glob_getent(&globent, flags, enc)) != NULL) {
        char *buf;
        rb_pathtype_t new_pathtype = path_unknown;
        const char *name = dp->d_name;

        // dirent_match() でパターンマッチング
        if (dirent_match(p->str, enc, name, dp, flags))
            *new_end++ = p->next;
    }
    // ...
}
```

## 6. 実装すべきポイント (rbcglob)

### 6.1 必須の動作

1. **"." と ".." の扱い**
   - ".." は常にスキップ
   - "." は `SKIPDOT` フラグで制御

2. **FNM_DOTMATCH フラグ**
   - なし: ワイルドカードは先頭ドットにマッチしない
   - あり: ワイルドカードは先頭ドットにマッチする
   - ただし、パターンが明示的に `.` で始まる場合はフラグ関係なくマッチ

3. **再帰パターン (**)**
   - 再帰開始時に `SKIPDOT` を設定
   - `FNM_DOTMATCH` がない限り、ドット始まりディレクトリに入らない
   - `.**` パターンはドット始まりディレクトリにのみ入る

### 6.2 実装チェックリスト

- [ ] `rbc_should_skip_dotfile()` が MRI ルールに準拠しているか
- [ ] "." エントリの処理 (`SKIPDOT` フラグ)
- [ ] ".." エントリは常にスキップ
- [ ] パターンの先頭ドット検出 (`starts_with_dot`)
- [ ] `**` 再帰時のドットディレクトリ処理
- [ ] `FNM_DOTMATCH` と `FNM_PATHNAME` の組み合わせ

## 7. MRI fnmatch() の詳細実装

### 7.1 コアロジック (dir.c L442-475)

```c
static int fnmatch(
    const char *pattern,
    rb_encoding *enc,
    const char *string,
    int flags)
{
    const char *p = pattern;
    const char *s = string;
    const char *send = s + strlen(string);
    const int period = !(flags & FNM_DOTMATCH);  // ★重要
    const int pathname = flags & FNM_PATHNAME;

    // ... fnmatch core logic
}
```

### 7.2 '*' ワイルドカードの処理

```c
case '*':
    // <Ruby>: DOTMATCH
    if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags))
        return RBC_NOMATCH;

    // ** 処理
    if (*++p == '*') {
        // ... doublestar logic
    }

    // single * 処理
    while (1) {
        if (t_ch == '\0')
            break;

        // ドットマッチチェック
        if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags) && !IS_SLASH_DOT_PATTERN(p))
        {
            matched = RBC_NOMATCH;
        }
        // ...
    }
```

### 7.3 '?' ワイルドカードの処理

```c
case '?':
    if ((flags & FNM_PATHNAME) && t_ch == '/')
        return RBC_NOMATCH;

    // <Ruby>: DOTMATCH
    if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags))
        return RBC_NOMATCH;

    continue;
```

## 8. テストケース (MRIの動作)

### 8.1 基本パターン
```ruby
# ディレクトリ構造:
# .
# ├── file.txt
# ├── .dotfile
# └── .hidden/
#     └── secret.txt

Dir.glob('*')
# => ["file.txt"]

Dir.glob('*', File::FNM_DOTMATCH)
# => [".", "file.txt", ".dotfile", ".hidden"]

Dir.glob('.*')
# => [".", ".dotfile", ".hidden"]

Dir.glob('.*', File::FNM_DOTMATCH)
# => [".", "..", ".dotfile", ".hidden"]  # ← ".." は含まれない (MRI仕様)
```

### 8.2 再帰パターン
```ruby
Dir.glob('**/*')
# => ["file.txt", ".hidden/secret.txt" は含まれない]

Dir.glob('**/*', File::FNM_DOTMATCH)
# => [".dotfile", ".hidden/secret.txt" を含む]

Dir.glob('**/.* ')
# => [".", ".dotfile", ".hidden/.", ".hidden/secret.txt" は含まれない]

Dir.glob('**/.* ', File::FNM_DOTMATCH)
# => [".", ".dotfile", ".hidden", ".hidden/."]
```

### 8.3 明示的ドットパターン
```ruby
Dir.glob('.**/foo')
# => [".hidden/foo" のみ、hidden/foo は含まれない]

Dir.glob('.hidden/*')
# => [".hidden/secret.txt"]  # FNM_DOTMATCHなしでもマッチ
```

## 9. MRI との相違点 (現在の実装)

### 9.1 確認すべき項目

1. **"." エントリの扱い**
   - [ ] `**` パターンで "." が正しく含まれるか
   - [ ] `SKIPDOT` フラグが正しく機能しているか

2. **".." エントリの扱い**
   - [ ] 常にスキップされているか

3. **FNM_DOTMATCH の実装**
   - [ ] ワイルドカードが先頭ドットにマッチするか
   - [ ] パターンの明示的ドットは優先されるか

4. **再帰パターン**
   - [ ] ドットディレクトリへの再帰が正しいか
   - [ ] `.**` パターンの動作

5. **パスセグメント毎の判定**
   - [ ] `FNM_PATHNAME` との組み合わせ
   - [ ] 各セグメントでのドットルール適用

## 10. 再設計の方針

### 10.1 設計原則

1. **MRI の内部フラグを再現**
   - `FNM_GLOB_SKIPDOT`: "." エントリのスキップ制御
   - 再帰レベルで適切に設定・解除

2. **パターン解析の改善**
   - セグメント毎に `starts_with_dot` を正確に判定
   - パターンの先頭文字を解析時に記録

3. **ディレクトリ走査の統一**
   - すべてのディレクトリ列挙で統一されたドット処理
   - "." と ".." を明示的に区別

### 10.2 実装ステップ

1. **Phase 1: "." と ".." の処理**
   - ".." を常にスキップ
   - "." を `SKIPDOT` フラグで制御
   - テストケース作成

2. **Phase 2: FNM_DOTMATCH の実装検証**
   - fnmatch レベルでの動作確認
   - glob でのフラグ伝播確認

3. **Phase 3: 再帰パターンの修正**
   - `**` 開始時の `SKIPDOT` 設定
   - `.**` パターンの特別処理

4. **Phase 4: 統合テスト**
   - Ruby テストスイートとの比較
   - エッジケースの確認

## 11. 参考: MRI ソースコード位置

- **dir.c L271-301**: フラグ定義
- **dir.c L442-475**: fnmatch() 本体
- **dir.c L2441-2475**: dirent_match()
- **dir.c L2694-2871**: glob_helper() (メインロジック)
- **dir.c L2837-2871**: ディレクトリ走査とSKIPDOT設定
- **dir.c L3066-3131**: ruby_glob0() (エントリポイント)

## 12. まとめ

### MRIのドットエントリ処理の本質

1. **".." は常に除外** (セマンティクス)
2. **"." は `SKIPDOT` で制御** (再帰防止と明示的マッチングの両立)
3. **FNM_DOTMATCH は "ワイルドカードの動作" を変える** (明示的ドットには影響しない)
4. **パターンの先頭文字が重要** (`.` で始まるパターンは特別扱い)
5. **再帰パターンは `SKIPDOT` を設定してから開始** (無限ループ防止)

### 次のアクション

- [ ] この分析を元に `rbc_should_skip_dotfile()` を再実装
- [ ] "." と ".." の処理を明確に分離
- [ ] `RBC_GLOB_SKIPDOT` フラグの正しい設定・解除
- [ ] Ruby テストスイートで動作検証
