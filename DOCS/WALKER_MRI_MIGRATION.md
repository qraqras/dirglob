# Walker実装: muslスタイル → MRIスタイル移行

## 変更概要

### Before (muslスタイル)
```c
// 単一パスでストリーミング処理
DIR *dir = opendir(...);
while ((ent = readdir(dir)) != NULL) {
    if (match_pattern(ent->d_name, pattern)) {
        callback(path);
    }
}
closedir(dir);
```

### After (MRIスタイル)
```c
// 2フェーズ処理: 読み込み→ソート→マッチ
glob_dir_t *gdir = glob_opendir(path, do_sort);
// Phase 1: opendir() + readdir() + closedir()
// Phase 2: qsort()

while ((dp = glob_getent(gdir)) != NULL) {
    // ソート済みエントリを順次処理
}
glob_dir_finish(gdir);
```

---

## MRI実装の特徴

### 1. **glob_opendir()** (MRI dir.c:2608相当)

```c
static glob_dir_t *glob_opendir(const char *path, bool do_sort) {
    DIR *dirp = opendir(path);

    if (!do_sort) {
        // ストリーミングモード
        return keep_dir_open(dirp);
    }

    // ソートモード: 全エントリを読み込み
    while ((dp = readdir(dirp)) != NULL) {
        entries[count++] = copy_dirent(dp);
    }
    closedir(dirp);  // すぐ閉じる

    qsort(entries, count, sizeof(entry), strcmp);
    return entries;
}
```

**特徴:**
- ✅ ソート時はDIR*をすぐ閉じる（MRIと同じ）
- ✅ ノーソート時はストリーミング
- ✅ メモリ使用量はO(n) (nはエントリ数)

### 2. **glob_getent()** (MRI dir.c:2684相当)

```c
static rbc_dirent_t *glob_getent(glob_dir_t *gdir) {
    if (gdir->nosort) {
        return readdir(gdir->dirp);  // ストリーム
    }
    else {
        return gdir->entries[gdir->idx++];  // ソート済み配列
    }
}
```

**特徴:**
- ✅ 統一されたインターフェース
- ✅ ソート/ノーソート両対応
- ✅ MRIと同じ抽象化レベル

### 3. **glob_helper()** (MRI dir.c:2694相当)

```c
static int glob_helper(
    const char *path,
    glob_pattern_t **patterns_beg,
    glob_pattern_t **patterns_end,
    const glob_funcs_t *funcs,
    bool do_sort)
{
    // パターン解析
    bool plain = true, magical = false, recursive = false;
    for (p = patterns_beg; p < patterns_end; p++) {
        if (has_wildcard(*p)) magical = true;
        if (is_doublestar(*p)) recursive = true;
    }

    // Plainパターン（ワイルドカードなし）
    if (plain) {
        if (exists(path))
            callback(path);
        return 0;
    }

    // ディレクトリスキャン
    glob_dir_t *gdir = glob_opendir(path, do_sort);
    while ((dp = glob_getent(gdir)) != NULL) {
        for (p = patterns_beg; p < patterns_end; p++) {
            if (fnmatch(*p, dp->d_name) == 0) {
                // マッチ: 再帰またはコールバック
                if (is_last_pattern(p)) {
                    callback(build_path(path, dp->d_name));
                }
                else {
                    glob_helper(new_path, p+1, patterns_end, ...);
                }
            }
        }
    }
    glob_dir_finish(gdir);
}
```

**特徴:**
- ✅ パターンタイプの自動検出
- ✅ plain/magical/recursiveの統合処理
- ✅ MRIの制御フローを忠実に再現

---

## muslスタイル vs MRIスタイル比較

| 特性 | muslスタイル | MRIスタイル |
|------|-------------|------------|
| **スキャン方式** | 単一パス | 2フェーズ（読込→ソート） |
| **メモリ** | ストリーム（小） | 配列保持（大） |
| **ソート** | 難しい | 簡単（qsort） |
| **速度（ソートなし）** | 速い | 同等 |
| **速度（ソート）** | 実装複雑 | シンプル |
| **fnmatch呼び出し** | 1回 | 1回 |
| **Ruby互換性** | 低 | **高** |

---

## ベンチマーク結果（予想）

### ソートなし (sort=false)
```
muslスタイル:  372.3 μs
MRIスタイル:   380.0 μs  (ほぼ同等)
```

### ソートあり (sort=true, default)
```
muslスタイル:  569.3 μs  (実装が複雑で遅い)
MRIスタイル:   420.0 μs  (qsortが効率的)
```

---

## 実装ステータス

### ✅ 完了
- [x] glob_opendir() 実装
- [x] glob_getent() 実装
- [x] glob_dir_finish() 実装
- [x] 基本的なglob_helper()構造

### 🚧 TODO
- [ ] パターン配列の正しい処理
- [ ] セグメント→パターン変換
- [ ] 再帰パターン(**) の完全実装
- [ ] エッジケーステスト
- [ ] MRIテストケースとの照合

---

## 次のステップ

1. **セグメント変換の改善**
   - `rbc_segment_t` → `glob_pattern_t` 変換ロジック
   - ブレース展開との統合

2. **MRIテストケース実行**
   - Ruby 4.0 Dir.globテストスイート
   - エッジケースの検証

3. **パフォーマンス測定**
   - ベンチマーク実行
   - MRI vs rbc_glob 比較

4. **プリコンパイル統合**
   - MRIスタイルベース + プリコンパイル最適化
   - 複数パターンの最適化

---

## 設計判断の理由

### なぜMRIスタイル？

1. **信頼性**: 30年の実績
2. **互換性**: Ruby仕様に完全準拠
3. **保守性**: コードが理解しやすい
4. **テスト**: MRIテストケース流用可能
5. **最適化**: 基盤が確実なら最適化は後から可能

### なぜmuslスタイルでない？

1. **ソートが難しい**: ストリーミングとソートの両立が複雑
2. **Rubyと違う**: MRIとの動作差異が発生しやすい
3. **最適化より正しさ**: まず正しく、次に速く

---

## まとめ

**MRIスタイル採用により:**
- ✅ Ruby 4.0完全互換への道筋が明確
- ✅ ソート機能がシンプルに実装可能
- ✅ MRIのバグ修正履歴を活用可能
- ✅ プリコンパイル最適化の土台が堅牢

**次の作業:**
1. セグメント→パターン変換の完成
2. MRIテストケースでの検証
3. ベンチマーク実行
4. プリコンパイルレイヤーの追加
