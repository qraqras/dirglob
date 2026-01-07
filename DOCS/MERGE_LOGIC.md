# Rubyのブレース展開マージロジック

## 概要

Dir.globのブレース展開では、`*.{c,h}`のようなパターンが`*.c`と`*.h`に展開されます。
展開後の各パターンの結果をどのようにマージするかがポイントです。

## 実測結果（Ruby 4.0）

### 実験1: `*.{c,h}` パターン

```ruby
Dir.glob('*.{c,h}', sort: false)
# => ["x.c", "y.c", "z.c", "y.h", "z.h", "x.h"]
```

- 展開: `*.c` → `[x.c, y.c, z.c]`、`*.h` → `[y.h, z.h, x.h]`
- 結果: 最初に`.c`ファイル全て（発見順）、次に`.h`ファイル全て（発見順）
- **ブレース展開の順序を維持**

### 実験2: `{z,y,x}.{c,h}` パターン

```ruby
Dir.glob('{z,y,x}.{c,h}', sort: false)
# => ["z.c", "z.h", "y.c", "y.h", "x.c", "x.h"]
```

- 展開: `z.{c,h}` → `[z.c, z.h]`、`y.{c,h}` → `[y.c, y.h]`、`x.{c,h}` → `[x.c, x.h]`
- 結果: ブレース展開の順序通り
- **ブレース展開の順序を維持**

## 結論

**Rubyはブレース前のワイルドカードを判断していません。**

マージロジックは極めてシンプル：
1. パターンをブレース展開（例: `*.{c,h}` → `["*.c", "*.h"]`）
2. 各展開パターンを個別に実行（ファイルシステムの発見順）
3. 結果を展開順に連結

つまり、`original_pattern`を保存する必要はなく、単に展開順にマージするだけで良い。

## 実装

現在の実装では、`original_pattern`を保存して最初の`{`の前にワイルドカードがあるか判定しています。

```c
/* Check if there are wildcards before the first brace */
int prefix_has_wildcards = 0;
const char *first_brace = strchr(original_pattern, '{');
if (first_brace)
{
  for (const char *p = original_pattern; p < first_brace; p++)
  {
    if (*p == '*' || *p == '?' || *p == '[')
    {
      prefix_has_wildcards = 1;
      break;
    }
  }
}
```

## 代替案の検討

### 案1: フラグとして保存

`original_pattern`文字列全体を保存せず、`bool has_prefix_wildcards`フラグだけ保存：

```c
struct rbcglob_compiled_glob_s
{
    rbcglob_compiled_pattern_t **patterns;
    size_t pattern_count;
    bool has_prefix_wildcards;  // original_patternの代わり
};
```

**メリット**: メモリ効率が良い（文字列の代わりにbool 1バイト）
**デメリット**: コンパイル時に追加の判定処理が必要

### 案2: パターンごとに判定

展開されたパターンを見て推定（完全な判定は不可能だが近似は可能）：

**メリット**: original_pattern不要
**デメリット**:
- 正確な判定ができない（`{*.c,*.h}`と`*.{c,h}`を区別できない）
- Rubyとの互換性が失われる可能性

## 結論

`original_pattern`を保存する現在の実装が最もシンプルで確実です。
文字列のメモリオーバーヘッドは通常のglobパターンでは数十バイト程度なので、
正確性とのトレードオフとして妥当です。

将来的に最適化するなら、`bool has_prefix_wildcards`フラグ方式が検討できます。
