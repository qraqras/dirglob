# Ruby 4.0 互換 API 一覧

`rbcglob` ライブラリは、Ruby 4.0 の `Dir` クラスおよび `File` クラスが提供するパス操作機能を C 言語で完全再現することを目指しています。

## 実装済みメソッド

| Ruby メソッド | C API 関数 | 説明 |
| :--- | :--- | :--- |
| `Dir.glob` | `rbcglob_dirglob` | ワイルドカードを用いたファイル列挙。`**` (再帰) や `{a,b}` (展開) に対応。 |
| `File.fnmatch` | `rbcglob_fnmatch` | パターンと文字列のマッチ判定。Ruby と同じフラグをサポート。 |
| `File.join` | `rbcglob_join` | パス成分の結合。Windows の `\` 認識や重複スラッシュの Ruby 流処理を再現。 |
| `File.expand_path` | `rbcglob_expand_path` | 絶対パスへの展開。`~` 展開、`..` の解決、Windows UNC パスに対応。 |
| `File.dirname` | `rbcglob_dirname` | ディレクトリ名の取得。`level` パラメータで複数階層の削除に対応。 |
| `File.basename` | `rbcglob_basename` | ファイル名の取得。`suffix` パラメータで拡張子の削除に対応。 |
| `File.extname` | `rbcglob_extname` | 拡張子の取得。dotfile や Windows の末尾ドット処理など、エッジケースに対応。 |

## フラグ互換性 (Flags)

Ruby の `File::FNM_*` 定数に対応する以下のフラグを提供しています。

| Ruby 定数 | C 定数 | 内容 |
| :--- | :--- | :--- |
| `File::FNM_NOESCAPE` | `RBCGLOB_FNM_NOESCAPE` | `\` によるエスケープを無効化 |
| `File::FNM_PATHNAME` | `RBCGLOB_FNM_PATHNAME` | `*` が `/` にマッチしない |
| `File::FNM_DOTMATCH` | `RBCGLOB_FNM_DOTMATCH` | 先頭の `.` にマッチさせる |
| `File::FNM_CASEFOLD` | `RBCGLOB_FNM_CASEFOLD` | 大文字小文字を区別しない |
| `File::FNM_EXTGLOB` | `RBCGLOB_FNM_EXTGLOB` | `{a,b}` などの拡張パターンを有効化 |
| `File::FNM_SYSCASE` | `RBCGLOB_FNM_SYSCASE` | OS の標準（Win/MacはFold, LinuxはCase-sensitive）に従う |
| `File::FNM_SHORTNAME` | `RBCGLOB_FNM_SHORTNAME` | (Windowsのみ) 8.3形式の短縮名にマッチさせる |

## 今後の実装予定 (Proposed)

以下のメソッドについても、Ruby 互換の挙動（特に Windows 環境での `/` と `\` の混在処理など）を維持した形での提供を予定しています。

- `File.absolute_path?`
- `Dir.home`

## 実装ポリシー

1. **引数名の完全一致**: Ruby のドキュメントに記載されている引数名（`file_name`, `dir_string` 等）をそのまま C の引数名として採用しています。
2. **パス区切りの統一**: Windows 環境でも、Ruby と同様に内部処理および結合には `/` を優先的に使用します。
3. **副作用の再現**: 複数のスラッシュが連続する場合の処理や、空文字列を `File.join` した際の挙動など、Ruby 特有のエッジケースをテストで検証しています。
