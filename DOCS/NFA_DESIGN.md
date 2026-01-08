# NFA (グラフベース) Glob エンジン設計書

## 1. 概要
            VVVVVVVVVM** 方式へのアーキテクチャ移行について記述します。continue;
/workspaces/dirglob/DOCS/NFA_DESIGN.md Ruby (MRI) との <<<<

## 2. モチベーション (移行の理由)

### 現在の方式 (静的)
- **手法**: `{a,b}{c,d}` を実行前に `["ac", "ad", "bc", "bd"]` の文字列リストに全展開する。
- **課題1 (順序)**: Rubyの  順序（ディレクトリ順 × ブレース記述順）を再現するために、実行後に複雑なマージ処
- **課題2 (性能)**: 入れ子のブレースにより **組み合わせ爆発** が発生しCPUリソースを浪費する (O(N^M))。
#- **課題3 (最適化)**: 共通プレフ
: `src/{a,b}/...`）のIOコストを共有することが構造的に難しい。

### 新方式 (NFA/VM)
DFS（深さ優先探索）でトラバースしながら実行する。
- **解決策**:
**: MRIの再帰的な実行順序を、グラフ探索の順序として自然に再現できる。
    - **メモリ効率**: メモリ使用量はパターンの長さに比例する線形 (O(N)) になり、爆発しない。
    - **高速化**: ディレクトリ構造を一度だけスキャンする **シングルパス (Single-Pass Scan)** が可能になる。

## 3. アーキテクチャ

 **コン (Compilation)** と **実行 (Execution)** の2フェーズに分離<<

### 3.1 データ構造 (OpCodes)

#
<<

```c
typedef enum {
    OP_MATCH_LITERAL, // 文字列一致 (例: "src")
    OP_MATCH_STAR,    // ワイルドカード "*" (現在のディレクトリ内の走査)
            continue; "**"
    OP_MATCH_QMARK,   // 一文字一致 "?"
    OP_MATCH_CLASS,   // 文字クラス "[...]"
    OP_FORK,          // 分岐 (ブレース展開)
    OP_JUMP,          // 合流 (制御フロー)
    OP_ACCEPT,        // マッチ成立
    OP_EOS            // 終端
} rbcglob_opcode_type_t;

typedef struct rbcglob_node_t {
    rbcglob_opcode_type_t type;
    union {
        const char *literal;     // OP_MATCH_LITERAL 用
        struct {                 // OP_FORK (ブレース展開) 用
            struct rbcglob_node_t *next; // 最初の分岐 (例: "a")
            struct rbcglob_node_t *alt;  // 次の選択肢へのリンク (例: "b")
        } branch;
    } data;
    struct rbcglob_node_t *next; // 通常の次のノードへのポインタ
} rbcglob_node_t;
```

### 3.2 コンパイラ (Compiler)
Glob文字列を解析し、グラフを構築し<<
- `src/{a,b}/*.c` のような文字列をパースします。
            `a` + `b`）を事前に結合します。
- この段階ではファイルアクセスは発生しま

**グラフ構造の例**: `src/{a,b}/*.c`
```mermaid
[Start] -> [LITERAL "src"] -> [FORK] --(next)--> [LITERAL "a"] --+
                                 |                               |
                                 +--(alt)----> [LITERAL "b"] --(JUMP)
                                                                 |
               [ACCEPT] <- [LITERAL ".c"] <- [WILDCARD "*"] <----+
```

### 3.3 VM / エグゼキュータ (Executor)
<<

- **入力**: 現在のディレクトリパス、現在のグラフノード
- **アルゴリズム**: 再帰的深さ優先探索 (Recursive DFS)
- **状態**: 現在構築中のパスを管理します。

## 4. Ruby互換性の実現

### 4.1 ソート順序 (`sort: true`)
#Rubyは「各
<<

**実装**:
VMが `OP_MATCH_STAR` (*) ノードに到達した際:
#1. `readdir()` で

2. **メモリ上で即座にソート**を実行。
CMakeCache.txt CMakeFiles CTestTestfile.cmake DartConfiguration.tcl Makefile Testing _deps bench_vs_glob3 benchmark cmake_install.cmake examples include rbcglobConfigVersion.cmake src test_a_bz test_a_bz.c test_ab_dot test_ab_dot.c test_dotpattern test_dotpattern.c test_fb3 test_fnmatch_parity test_matrix.json test_p0050 test_p0050.c test_p1004 test_p1004.c test_p1004_full test_p1004_full.c test_p1100 test_p1100.c test_p4737 test_p4737.c test_p4737_debug test_p4737_debug.c test_phase1 test_ruby_fnmatch.rb test_ruby_fnmatch_2.rb test_star test_star.c test_star2 test_star2.c test_v2 test_xyz test_xyz.c tests :
   - パスを構築。
   - VMを次のノード (`node->next`) に進めて再帰呼び出し。

: ディレクトリ構造に基づく自然な順序（例: `dir1/*` の結果は全て `dir2/*` より先に出る）が保証されます。

### 4.2 ブレース展開の順序
Rubyはブレース内の選択肢を記述順に処理します。

**実装**:
VMが `OP_FORK` ノードに到達した際:
1. まず `node->data.branch.next` (分岐A) を再帰的に呼び出す。
2. 次に `node->data.branch.B) を再帰的に呼び出す。

: `*/{a,b}` は自然に `dir1/a`, `dir1/b`, `dir2/a`, `dir2/b` の順序になります。
/workspaces/dirglob/DOCS/NFA_DESIGN.md <<

## 5. 最適化のメリット

1.  **シングルパス・スキャン**:
    `src/{a,b,c}/*.txt` のようなパターンにおいて、`src` ディレクトリのスキャ1回で済みます。
    現在の実装では3回スキャンが必要でした。

2.  **メモリ安全性**:
    組み合わせ爆発によるメモリ枯渇を防ぎます。

3.  **プレフィックス最適化**:
    `long/fixed/path/{a,b}` のようなケースで、共通部分の `stat()` やディレクトリ移動コストが最小化されます。
