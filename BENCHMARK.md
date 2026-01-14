# rbc_glob vs glob(3)/fnmatch(3) ベンチマーク結果

## テスト環境
- Platform: Ubuntu 24.04.2 LTS (Dev Container)
- Iterations: 1000回 (再帰パターンは100回)
- Test Directory: tests/fixtures (45ファイル、複数ディレクトリ階層)

## 重要な注意事項

⚠️ **glob(3)の制限**:
- **`**` 再帰パターンには対応していません** (glob(3)は`**`をリテラル文字列として扱う)
- `**`はRuby/zsh/bashの拡張機能
- ブレース展開は`GLOB_BRACE`フラグで対応（GNU拡張）

以下の比較は、両実装が対応している機能のみで公平に比較しています。

## Glob性能比較（公平な比較）

### 両実装が対応しているパターン

| パターン | タイプ | glob(3) (ms) | rbc_glob (ms) | 性能比 |
|---------|--------|-------------|--------------|-------|
| `*.c` | Simple wildcard | 114.6 | 93.3 | **1.23x faster** ✓ |
| `*.txt` | Simple wildcard | 91.4 | 92.7 | **1.01x slower** ≈ |
| `a*` | Prefix wildcard | 87.4 | 93.1 | **1.06x slower** ≈ |
| `*/*` | Two-level | 687.5 | 691.1 | **1.01x slower** ≈ |
| `*/*/*` | Three-level | 2740.8 | 3280.0 | **1.20x slower** |
| `?.c` | Question mark | 154.8 | 96.0 | **1.61x faster** ✓ |
| `{a,b,c}/*` | Brace expansion | 42.7 | 40.7 | **1.05x faster** ✓ |

### rbc_glob独自機能（glob(3)未対応）

| パターン | タイプ | rbc_glob (ms) | 備考 |
|---------|--------|--------------|------|
| `**/*.c` | Recursive + wildcard | 1825.6 | Ruby/zsh拡張 |
| `**/` | Recursive dirs | 1782.0 | Ruby/zsh拡張 |
| `a/**/c` | Recursive middle | 1.5 | Ruby/zsh拡張 |

## Fnmatch性能比較

| パターン | タイプ | fnmatch(3) (ms) | rbc_fnmatch (ms) | 性能比 |
|---------|--------|----------------|-----------------|-------|
| `*.c` | Simple wildcard | 286.7 | 210.2 | **1.36x faster** ✓ |
| `*test*` | Infix wildcard | 192.4 | 277.0 | **1.44x slower** |
| `a*c` | Prefix+suffix | 41.2 | 294.3 | **7.14x slower** ⚠️ |
| `???.*` | Question marks | 62.8 | 210.9 | **3.36x slower** |

## 分析

### 🎯 rbc_globの強み

#### 公平な比較で優れている領域
1. **シンプルなワイルドカード** (`*.c`): 1.23倍高速
2. **Question markパターン** (`?.c`): 1.61倍高速
3. **ブレース展開** (`{a,b,c}/*`): 1.05倍高速
4. **競合性能**: 多くのパターンでglob(3)と同等（±10%以内）

#### 独自機能
- **`**` 再帰パターン**: glob(3)は未対応、rbc_globのみ実装
- Ruby/MRIとの完全互換性

### 🔄 rbc_fnmatchの状況

#### 良好な性能
- **シンプルなワイルドカード** (`*.c`): fnmatch(3)より1.36倍高速

#### 改善が必要
- **Prefix+suffixパターン** (`a*c`): 7.14倍遅い
- **Question marks** (`???.*`): 3.36倍遅い
- **Infix wildcard** (`*test*`): 1.44倍遅い

## 推奨される最適化

### 優先度: 高
1. **fnmatch prefix/suffix最適化**
   - Boyer-Mooreやその他の文字列検索アルゴリズム導入
   - 単純なprefix/suffix検索のfast path追加
   - 目標: `a*c`パターンで2-3倍改善

### 優先度: 中
2. **再帰パターンの最適化**
   - 現在の性能でも実用的だが、さらなる高速化の余地あり
   - ディレクトリスキャンのキャッシング検討

3. **メモリアロケーション最適化**
   - アリーナアロケータの調整
   - 結果配列の事前割り当て

## まとめ

### glob機能
✅ **対応しているパターンでは競合性能**
- シンプルなパターン: glob(3)と同等またはやや高速
- 複雑なパターン: 概ね同等（1-2割程度の差）
- ブレース展開: 同等の性能

✨ **独自機能の提供**
- `**` 再帰パターン: glob(3)は未対応
- Ruby/MRI完全互換

### fnmatch機能
✅ **シンプルなパターンで優秀**
- `*.c`のような基本パターンでfnmatch(3)より高速

⚠️ **特定パターンで改善余地**
- prefix+suffix検索の最適化が必要
- 全体的な性能向上の余地あり
