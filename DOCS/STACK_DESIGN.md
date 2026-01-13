# スタックループ設計（MRI互換）

## 現在の問題

現在の実装では同じパスが何千回も重複列挙されています。原因は：

1. **フレームの責任が曖昧**: ディレクトリイテレーション中に子フレームをpushするが、親フレームが継続して同じエントリを再処理してしまう
2. **状態管理の不備**: `ST_DIR_LOOP`で`break`した後、スタックトップの子フレームが実行され、その後また親フレームが同じ位置から再開される
3. **再帰処理の重複**: RECURSIVEセグメントで「ゼロマッチ」と「再帰」を両方pushするが、その制御が不適切

## MRIの処理フロー（再帰版）

```ruby
def glob_helper(segments, path)
  return [path] if segments.empty?

  seg = segments.first
  rest = segments[1..]

  case seg.type
  when :literal
    new_path = path + seg.value
    return [] unless File.exist?(new_path)
    return glob_helper(rest, new_path)

  when :wildcard
    results = []
    Dir.entries(path).each do |entry|
      next if entry == '.' || entry == '..'
      next unless matches?(entry, seg.pattern)
      results += glob_helper(rest, path + entry)
    end
    return results

  when :recursive  # **
    results = glob_helper(rest, path)  # ゼロマッチ
    Dir.entries(path).each do |entry|
      next if entry == '.' || entry == '..'
      next if entry.start_with?('.') unless dotmatch?
      subpath = path + entry
      next unless File.directory?(subpath)
      results += glob_helper(segments, subpath)  # 再帰（segmentsを再利用）
    end
    return results
  end
end
```

## 正しいスタックループ設計

### 原則

1. **1フレーム = 1タスク**: 各フレームは1つの明確なタスクを持つ
2. **完全な状態保存**: フレームは中断・再開可能な状態を全て保持
3. **明示的な継続**: 次の処理は明示的にpushし、現在のフレームは完了したらpop

### フレーム構造

```c
typedef struct {
    rbc_segment_t *seg;           // 処理対象のセグメント
    segment_stack_t *stack_ptr;   // 継続スタック
    char *path;                    // 現在のパス
    size_t path_len;

    // 状態
    int state;                     // ST_INIT, ST_LITERAL_CHECK, ST_DIR_ITER, ST_BRANCH_ITER

    // ディレクトリイテレーション用
    sorted_entry_t *entries;       // ソート済みエントリ配列
    size_t entry_count;
    size_t entry_index;            // 現在処理中のインデックス

    // ブランチ用
    rbc_segment_t *branch_current;

    // フラグ
    bool from_wildcard;
    bool post_recursive;
} frame_t;
```

### 状態遷移

#### ST_INIT
- セグメントタイプを判定
- LITERAL → ST_LITERAL_CHECK へ
- WILDCARD/RECURSIVE → ST_DIR_ITER へ（ディレクトリ列挙開始）
- BRANCH → ST_BRANCH_ITER へ

#### ST_LITERAL_CHECK
- パスにリテラルを追加
- 存在確認
- 存在すれば次のセグメント用フレームをpush
- 自分はpop

#### ST_DIR_ITER（WILDCARD/RECURSIVE共通）
1. **初回**: ディレクトリを開き、全エントリをソート済み配列に格納
2. **ループ**: `entry_index < entry_count`の間：
   - エントリを1つ取得
   - フィルタリング（., .., 隠しファイル等）
   - マッチング判定
   - **マッチした場合**:
     - 新しいパスを構築
     - 次のセグメント用フレームをpush
     - `entry_index++`
     - **continue**（次のエントリへ）
   - **マッチしない場合**:
     - `entry_index++`
     - continue
3. **全エントリ処理完了**: 自分をpop

#### ST_BRANCH_ITER
- 各代替セグメントに対してフレームをpush
- 全代替を処理したらpop

### RECURSIVE（``**``）の特殊処理

RECURSIVEセグメントの処理は2段階：

1. **ST_INIT時**:
   - 「ゼロマッチ」フレームをpush（次のセグメントを現在のパスで試す）
   - 自身の状態をST_DIR_ITERに変更してcontinue

2. **ST_DIR_ITER時**:
   - ディレクトリエントリに対して、**同じRECURSIVEセグメント**で再帰
   - つまり、`seg->next`ではなく`seg`自身をpush

### 実装の鍵

**重要**: ディレクトリイテレーション中、エントリごとにフレームをpushするが、親フレームは**自分の状態を進めてからcontinue**する。これにより：

- 子フレームがスタックトップに積まれる
- 次のループで子フレームが実行される
- 子フレームが完了してpopされる
- 親フレームが再開され、次のエントリを処理する

## 疑似コード

```c
while (st.count > 0) {
    frame_t *f = &st.items[st.count - 1];

    switch (f->state) {
    case ST_INIT:
        if (seg->type == LITERAL) {
            f->state = ST_LITERAL_CHECK;
        } else if (seg->type == WILDCARD) {
            f->state = ST_DIR_ITER;
            // ディレクトリを開いて全エントリをロード
            load_directory_entries(f);
        } else if (seg->type == RECURSIVE) {
            // ゼロマッチをpush
            push_frame(seg->next, f->path);
            // 自分は再帰イテレーションへ
            f->state = ST_DIR_ITER;
            load_directory_entries(f);
        } else if (seg->type == BRANCH) {
            f->state = ST_BRANCH_ITER;
            f->branch_current = seg->data.branch.head;
        }
        break;

    case ST_LITERAL_CHECK:
        // パス存在確認
        if (exists(f->path + seg->literal)) {
            push_frame(seg->next, f->path + seg->literal);
        }
        pop_frame();
        break;

    case ST_DIR_ITER:
        if (f->entry_index >= f->entry_count) {
            // 全エントリ処理完了
            pop_frame();
            break;
        }

        entry = f->entries[f->entry_index];
        f->entry_index++;  // 先に進める！

        // フィルタリング
        if (should_skip(entry)) {
            continue;  // 次のエントリへ（同じフレームを再実行）
        }

        // マッチング
        if (matches(entry, seg)) {
            new_path = f->path + "/" + entry.name;

            if (seg->type == WILDCARD) {
                push_frame(seg->next, new_path, from_wildcard=true);
            } else { // RECURSIVE
                push_frame(seg, new_path, from_wildcard=true);  // 同じRECURSIVEセグメント
            }
        }
        // continue（スタックトップの子フレームまたは次のエントリを処理）
        break;

    case ST_BRANCH_ITER:
        if (!f->branch_current) {
            pop_frame();
            break;
        }

        branch_seg = f->branch_current;
        f->branch_current = f->branch_current->next_alt;

        // 継続を構築
        continuation = merge(seg->next, f->stack_ptr);
        push_frame(branch_seg, f->path, continuation);
        break;
    }
}
```

## キーポイント

1. **エントリインデックスを先に進める**: `f->entry_index++`を**pushの前**に実行
2. **continueで次へ**: フィルタリングやpush後は`break`または`continue`で次のループへ
3. **RECURSIVEの特殊性**: `seg`自身を再利用してサブディレクトリへ再帰
4. **ゼロマッチの優先**: RECURSIVEではまずゼロマッチをpushし、その後ディレクトリイテレーション

この設計により、各エントリは正確に1回だけ処理され、重複が発生しません。
