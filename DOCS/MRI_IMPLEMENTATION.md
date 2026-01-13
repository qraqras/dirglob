# MRI Dir.glob実装の解析

## 概要

このドキュメントでは、Ruby MRI (Matz's Ruby Implementation) における`Dir.glob`の`**`パターンの実装を解析し、結果の順序決定メカニズムを説明します。

## ソースコード参照

- メインファイル: [ruby/dir.c](https://github.com/ruby/ruby/blob/master/dir.c)
- 主要関数: `glob_helper()`, `glob_opendir()`, `glob_getent()`

## 核心的な実装

### 1. RECURSIVEパターンの処理

MRIは`**`パターンを`RECURSIVE`型として認識し、以下のように処理します：

```c
// dir.c:2920-2938
for (cur = beg; cur < end; ++cur) {
    struct glob_pattern *p = *cur;
    if (p->type == RECURSIVE) {
        if (new_pathtype == path_directory || new_pathtype == path_exist) {
            if (dotfile < ((flags & FNM_DOTMATCH) ? 2 : 1))
                *new_end++ = p; /* append recursive pattern */
        }
        p = p->next; /* 0 times recursion */
    }
    // ... 各エントリのマッチング処理
}
```

**重要な動作**:
1. `p = p->next` で「0回再帰」(Match Zero) を処理
2. ディレクトリの場合、RECURSIVEパターンを`new_end`に追加して再帰を継続
3. 各エントリに対して`glob_helper()`を再帰呼び出し

### 2. ディレクトリトラバーサル順序

MRIは以下の順序でディレクトリを走査します：

```c
// dir.c:2694-2726
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
    // ... ディレクトリを開く
    // ... エントリを読み込む
    // ... 各エントリに対して即座に再帰呼び出し
}
```

### 3. ソート処理

MRIはデフォルトでディレクトリエントリをソートします（`sort: false`オプションがない限り）：

```c
// dir.c:2608-2634
static int glob_sort_cmp(const void *a, const void *b) {
    const rb_dirent_t *ent1 = *(void **)a;
    const rb_dirent_t *ent2 = *(void **)b;
    return strcmp(ent1->d_name, ent2->d_name);
}

static void glob_dir_finish(ruby_glob_entries_t *ent, int flags) {
    if (flags & FNM_GLOB_NOSORT) {
        check_closedir(ent->nosort.dirp);
        ent->nosort.dirp = NULL;
    }
    else if (ent->sort.entries) {
        for (size_t i = 0, count = ent->sort.count; i < count;) {
            GLOB_FREE(ent->sort.entries[i++]);
        }
        GLOB_FREE(ent->sort.entries);
        // ...
    }
}
```

## 結果順序の決定メカニズム

### `**/*.txt`の処理フロー

```
1. カレントディレクトリを開く
   ↓
2. エントリを読み込んでソート: ['.', '..', 'a/', 'a.txt', 'abc.txt', 'b.txt']
   ↓
3. 各エントリを順番に処理:

   a/ (ディレクトリ)
   ├─ glob_helper()を即座に再帰呼び出し
   │  ├─ a/のエントリをソート: ['a.txt', 'b/']
   │  ├─ 'a.txt'マッチ → 結果に追加 (a/a.txt)
   │  └─ 'b/' → さらに再帰
   │     └─ 'file.txt'マッチ → 結果に追加 (a/b/file.txt)
   │
   a.txt (ファイル)
   ├─ Match Zero処理
   └─ マッチ → 結果に追加 (a.txt)

   abc.txt (ファイル)
   ├─ Match Zero処理
   └─ マッチ → 結果に追加 (abc.txt)
```

### 結果の順序

上記のフローにより、結果は以下の順序になります：

```
[0] a/a.txt      # ディレクトリ a/ の再帰処理で出力
[1] a/b/file.txt # ディレクトリ a/b/ の再帰処理で出力
[2] a.txt        # カレントディレクトリのMatch Zero
[3] abc.txt      # カレントディレクトリのMatch Zero
```

**重要**: サブディレクトリの結果が現在ディレクトリのファイルより先に出力される

## MRIの処理戦略

### 深さ優先探索 (DFS)

MRIは**即座に再帰する深さ優先探索**を採用しています：

1. **ディレクトリエントリに遭遇** → **即座に再帰処理**
2. **再帰から戻る** → **次のエントリを処理**

この戦略により、自然にサブディレクトリの結果が先に出力されます。

### スタックを使わない理由

MRIは明示的なスタック構造を使わず、**関数の再帰呼び出し**を利用しています：

```c
// 擬似コード
void glob_helper(path, pattern) {
    entries = readdir_and_sort(path);

    for (entry in entries) {
        if (is_directory(entry)) {
            // 即座に再帰 → サブディレクトリの結果が先に出力される
            glob_helper(path + "/" + entry, pattern);
        } else if (match(entry, pattern)) {
            // ファイルのマッチング → 後で出力される
            output(entry);
        }
    }
}
```

## rbcglobとの比較

### 現在のrbcglob実装

```c
// walker.c:564-583 (RBC_SEGMENT_RECURSIVE in ST_INIT)
case RBC_SEGMENT_RECURSIVE: {
    // Match Zeroフレームを作成
    rbc_walker_frame_t *match_zero = push_next(...);  // TOP に push
    match_zero->state = ST_DIR_OPEN;

    // 再帰フレームは後で ST_DIR_LOOP で push_back
    // → BOTTOM に push されるため後で実行
}
```

**問題点**:
- Match Zeroが先に実行される (TOP)
- 再帰処理が後で実行される (BOTTOM)
- 結果: 現在ディレクトリのファイルが先、サブディレクトリが後

### MRIとの動作比較

| パターン | MRI | rbcglob (現在) |
|---------|-----|----------------|
| `**/*.txt` | `a/a.txt`, `a/b/file.txt`, `a.txt` | `a.txt`, `abc.txt`, `a/a.txt`, `a/b/file.txt` |
| 処理順序 | サブディレクトリ優先 | カレントディレクトリ優先 |
| 実装方式 | 即座に再帰 (DFS) | スタックベース (遅延再帰) |

## 修正方針

### オプション1: スタック順序の逆転

```c
// Match Zeroを後で実行するように変更
case RBC_SEGMENT_RECURSIVE: {
    // 再帰フレームを先に実行するため TOP に push
    // (ST_DIR_LOOP で処理)

    // Match Zeroフレームを後で実行するため BOTTOM に push
    rbc_walker_frame_t *match_zero = push_back(...);
    match_zero->state = ST_DIR_OPEN;
}
```

### オプション2: 即座再帰 (MRI互換)

ディレクトリエントリをソート後、順番に即座に再帰処理：

```c
entries = read_and_sort_directory();

for (entry in entries) {
    if (is_directory(entry)) {
        // スタックに積まず即座に再帰
        process_directory_recursively(entry);
    } else {
        // ファイルのマッチング
        if (match(entry, pattern)) {
            add_result(entry);
        }
    }
}
```

## 実装上の注意点

### ソートの重要性

MRIは**必ずソートしてから処理**します（`sort: false`オプションを除く）。これにより：

1. ディレクトリとファイルが名前順にソートされる
2. 同じ名前プレフィックスのディレクトリ/ファイルが隣接する
3. `a/`, `a.txt`, `abc.txt` のような順序が保証される

### dotfileの扱い

```c
if (dotfile < ((flags & FNM_DOTMATCH) ? 2 : 1))
    *new_end++ = p; /* append recursive pattern */
```

- `FNM_DOTMATCH`フラグがない場合、dotfileには再帰しない
- dotfileへの再帰には明示的なパターン（例: `a/.hidden/**/`）が必要

## パフォーマンス考察

### MRIの方式 (即座再帰)

**利点**:
- メモリ効率が良い（スタックフレームが少ない）
- 自然な深さ優先探索
- コードがシンプル

**欠点**:
- 関数呼び出しのオーバーヘッド
- スタックオーバーフローのリスク（非常に深いディレクトリ構造）

### スタックベース方式

**利点**:
- スタックオーバーフローを制御できる
- 処理順序を柔軟に変更可能
- 非再帰的実装が可能

**欠点**:
- メモリ使用量が増える（明示的なスタック）
- コードが複雑になる
- 順序制御が難しい

## 結論

MRIの`**`パターン処理は：

1. **即座に再帰する深さ優先探索**を採用
2. **ソート済みエントリを順番に処理**
3. **ディレクトリに遭遇したら即座に再帰呼び出し**
4. **結果としてサブディレクトリの結果が先に出力される**

rbcglobでMRI互換の順序を実現するには、スタックのpush順序を逆転するか、MRIと同様の即座再帰方式を採用する必要があります。
