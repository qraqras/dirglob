# Glob/Fnmatch 最適化戦略

## 概要

このドキュメントでは、rbcglobのパフォーマンス最適化のための包括的な戦略を説明します。
既存実装の改善から根本的な再設計まで、複数のアプローチを検討します。

---

## 現状分析

### ベンチマーク結果（macOS Apple Silicon, O2最適化, 9615ファイル, 1000回反復）

| パターン | fnmatch(3) | rbc_fnmatch | rbc_xfnmatch | 性能比(fnmatch) | 性能比(xfnmatch) |
|---------|------------|-------------|--------------|-----------------|------------------|
| `*.c`     | 2.074 sec  | 0.202 sec   | 0.048 sec    | **10.3x faster**  | **43.3x faster** |
| `a*c`     | 0.071 sec  | 0.343 sec   | 0.053 sec    | **4.8x slower**   | **1.3x faster**  |
| `???.*`   | 0.137 sec  | 0.266 sec   | 0.071 sec    | **1.9x slower**   | **1.9x faster**  |
| `*test*`  | 1.174 sec  | 0.338 sec   | 0.119 sec    | **3.5x faster**   | **9.9x faster**  |
| `test_*.c`| 0.129 sec  | 0.417 sec   | 0.052 sec    | **3.2x slower**   | **2.5x faster**  |

### 問題点

1. **rbc_fnmatchのコンパイルオーバーヘッド**
   - 毎回4KBのアリーナアロケーション
   - パターン解析と戦略選択のコスト
   - 簡単なパターンで4〜5倍遅い

2. **rbc_xfnmatchは優秀**
   - 全パターンでfnmatch(3)より高速
   - プリコンパイルの恩恵が大きい
   - Dir.glob（1パターン×多数ファイル）で有効

---

## 最適化戦略の階層

### アプローチ: 根本的な再設計

####.1 Zero-Allocation Streaming Architecture

**コンセプト:** Git wildmatch、ripgrepのアプローチ

```c
typedef struct {
    const char *pattern;
    const char *text;
    const char *pattern_pos;
    const char *text_pos;
    const char *star_pattern;  // バックトラック位置
    const char *star_text;
    unsigned flags;
} match_state_t;

bool rbc_fnmatch_v2(const char *pattern, const char *text, unsigned flags) {
    match_state_t state = {
        .pattern = pattern,
        .text = text,
        .pattern_pos = pattern,
        .text_pos = text,
        .star_pattern = NULL,
        .star_text = NULL,
        .flags = flags
    };

    // ループベースのマッチング（再帰なし、アロケーションなし）
    while (true) {
        if (*state.pattern_pos == '*') {
            // ... バックトラック設定
        } else if (*state.pattern_pos == '?') {
            // ... 1文字マッチ
        } else if (*state.pattern_pos == '[') {
            // ... 文字クラス（ビットマップ使用）
        } else {
            // ... リテラルマッチ
        }
    }
}
```

**特徴:**
- メモリアロケーション: **0バイト**
- スタック使用量: **~100バイト**
- コンパイル時間: **0秒**
- 実行速度: **fnmatch(3)と同等以上**

**期待効果:**
- `a*c`: 4.8倍遅い → **1.0〜1.5倍** (3〜5倍改善)
- `???.*`: 1.9倍遅い → **1.0倍** (2倍改善)
- メモリフットプリント: **4KB → 100B**

#### 1.2 Bytecode JIT Compilation

**コンセプト:** V8/LuaJIT的アプローチ

```c
typedef enum {
    OP_LITERAL,        // リテラル文字マッチ
    OP_ANY,            // ? (任意の1文字)
    OP_STAR,           // * (0個以上)
    OP_CHARSET,        // [a-z] (文字クラス)
    OP_JUMP,           // 条件ジャンプ
    OP_MATCH,          // マッチ成功
    OP_FAIL,           // マッチ失敗
} bytecode_op_t;

typedef struct {
    bytecode_op_t op;
    union {
        char literal;
        uint64_t charset[4];  // 256bit bitmap
        int jump_offset;
    } arg;
} bytecode_insn_t;

// パターンコンパイル（軽量）
bytecode_insn_t* compile_pattern(const char *pattern) {
    bytecode_insn_t *code = stack_alloc(256);
    int pc = 0;

    while (*pattern) {
        if (*pattern == '*') {
            code[pc++] = (bytecode_insn_t){.op = OP_STAR};
        } else if (*pattern == '?') {
            code[pc++] = (bytecode_insn_t){.op = OP_ANY};
        } else if (*pattern == '[') {
            uint64_t bitmap[4] = {0};
            pattern = compile_charset(pattern, bitmap);
            code[pc++] = (bytecode_insn_t){
                .op = OP_CHARSET,
                .arg.charset = {bitmap[0], bitmap[1], bitmap[2], bitmap[3]}
            };
        } else {
            code[pc++] = (bytecode_insn_t){
                .op = OP_LITERAL,
                .arg.literal = *pattern
            };
        }
        pattern++;
    }
    code[pc++] = (bytecode_insn_t){.op = OP_MATCH};
    return code;
}

// 超高速インタープリタ
bool execute_bytecode(bytecode_insn_t *code, const char *text) {
    int pc = 0;
    const char *tp = text;

    while (true) {
        switch (code[pc].op) {
        case OP_LITERAL:
            if (*tp != code[pc].arg.literal) goto backtrack;
            tp++; pc++;
            break;
        case OP_STAR:
            // ... バックトラック処理
            break;
        case OP_CHARSET:
            if (!test_charset(code[pc].arg.charset, *tp)) goto backtrack;
            tp++; pc++;
            break;
        case OP_MATCH:
            return (*tp == '\0');
        }
    }
}
```

**特徴:**
- コンパイル: **超軽量（スタックのみ）**
- 実行: **分岐予測に優しい**
- 拡張性: **新しいオペコード追加が容易**
- ビットマップ: **[a-z]が1命令**

**期待効果:**
- コンパイル時間: **現在の1/10**
- 実行速度: **現在の2〜3倍**
- 文字クラス: **劇的改善**

#### 1.3 Unified Hybrid Architecture（推奨）

```c
typedef enum {
    FAST_PATH_LITERAL,      // "abc" → strcmp
    FAST_PATH_SUFFIX,       // "*.ext" → SIMD suffix check
    FAST_PATH_PREFIX,       // "test_*" → memcmp + strlen
    FAST_PATH_CONTAINS,     // "*test*" → SIMD strstr
    BYTECODE_PATH,          // a*c, ???.* → Bytecode interpreter
    COMPLEX_PATH,           // [a-z]*, {a,b}* → Full engine
} match_path_t;

match_path_t select_path(const char *pattern) {
    if (!strchr(pattern, '*') && !strchr(pattern, '?') && !strchr(pattern, '['))
        return FAST_PATH_LITERAL;

    if (pattern[0] == '*' && pattern[1] == '.' && is_simple_suffix(pattern + 2))
        return FAST_PATH_SUFFIX;

    if (count_wildcards(pattern) <= 2 && !strchr(pattern, '['))
        return BYTECODE_PATH;

    return COMPLEX_PATH;
}

bool rbc_fnmatch_ultimate(const char *pattern, const char *text, unsigned flags) {
    match_path_t path = select_path(pattern);

    switch (path) {
    case FAST_PATH_LITERAL:
        return strcmp(pattern, text) == 0;

    case FAST_PATH_SUFFIX:
        return fast_suffix_match(text, pattern + 1);

    case BYTECODE_PATH: {
        bytecode_insn_t code[256];
        compile_pattern_fast(pattern, code);
        return execute_bytecode(code, text);
    }

    case COMPLEX_PATH:
        return wildmatch(pattern, text, flags);
    }
}
```

---

## Glob特有の最適化

### 1. パターン複雑度による分類

```
Level 0: リテラル（ワイルドカードなし）
  例: src/main.c
  戦略: stat()のみ（ディレクトリ走査不要）

Level 1: 単一セグメント・単純ワイルドカード
  例: *.c, test_*.c, *test*
  戦略: 単一ディレクトリのreaddir() + 高速フィルタ

Level 2: 複数セグメント・再帰なし
  例: src/*.c, tests/*/*.c
  戦略: 階層的readdir() + パスごとのマッチ

Level 3: 再帰パターン
  例: **/*.c, src/**/test.c
  戦略: 深さ優先探索 + 枝刈り最適化

Level 4: ブレース展開 + 複雑パターン
  例: {src,tests}/**/*.{c,h}
  戦略: パターン分解 + 重複除去
```

### 2. ** (再帰)パターンの最適化

```c
typedef struct {
    // メモ化: 同じディレクトリを2回走査しない
    hash_set_t *visited_dirs;

    // 深さ制限（無限ループ防止）
    int max_depth;
    int current_depth;

    // 枝刈り: .git, node_modules などをスキップ
    const char **ignore_patterns;

    // 並列化: 独立したサブツリーを並列処理
    bool parallel_enabled;
} recursive_optimizer_t;

void traverse_recursive(const char *base, recursive_optimizer_t *opt) {
    // 1. 訪問済みチェック（シンボリックリンクループ対策）
    if (hash_set_contains(opt->visited_dirs, get_inode(base))) {
        return;
    }
    hash_set_add(opt->visited_dirs, get_inode(base));

    // 2. 深さ制限チェック
    if (opt->current_depth >= opt->max_depth) {
        return;
    }

    // 3. 無視パターンチェック
    for (int i = 0; opt->ignore_patterns[i]; i++) {
        if (fnmatch(opt->ignore_patterns[i], basename(base), 0) == 0) {
            return;  // .git, node_modules などをスキップ
        }
    }

    // 4. ディレクトリ走査
    DIR *dir = opendir(base);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (is_directory(entry)) {
            traverse_recursive(join_path(base, entry->d_name), opt);
        } else {
            if (match_pattern(entry->d_name)) {
                add_result(join_path(base, entry->d_name));
            }
        }
    }
    closedir(dir);
}
```

### 3. ブレース展開の最適化

```c
// 悪い例: 3回readdir()
for (pattern in expand_braces("{src,tests,examples}/*.c")) {
    results += glob_single(pattern);  // 3回ディレクトリを開く
}

// 良い例: LCP (Longest Common Prefix) 最適化
patterns = expand_braces("{src,tests,examples}/*.c");
// → ["src/*.c", "tests/*.c", "examples/*.c"]

// 各ディレクトリで1回だけreaddir()
for (dir in ["src", "tests", "examples"]) {
    if (!exists(dir)) continue;
    entries = readdir(dir);
    for (entry in entries) {
        if (match("*.c", entry)) {
            add_result(join(dir, entry));
        }
    }
}
```

---

## 他の実装からの学び

### 1. RE2 (Google)
- リテラル文字列 → `strstr`直接実行
- 単純パターン → 軽量DFA
- 複雑パターン → フルNFA/DFAコンパイル

### 2. PCRE2
- Simple → インタープリタ直接実行
- Medium → スタディ結果で最適化
- Complex → JITコンパイル（ネイティブコード生成）

### 3. V8 JavaScript Engine
- Atom → リテラルマッチ（`indexOf`）
- Bytecode → バイトコードインタープリタ
- Native → ネイティブコード（最高速）

### 4. Rust regex crate
- Literal → 文字列検索アルゴリズム（Boyer-Moore等）
- DFA → 高速だがメモリ使用量に制限
- NFA → 遅いが全パターン対応

**共通パターン: 3層アーキテクチャ**

| Layer | 条件 | 実装 | 速度 |
|-------|------|------|------|
| **Fast Path** | リテラル、単純パターン | 直接実行（strcmp, strstr等） | 最速 |
| **Medium Path** | 中程度の複雑さ | 軽量コンパイル/DFA | 速い |
| **Full Path** | 複雑パターン | フルコンパイル/NFA | 正確 |

---

## 実装ロードマップ

### Phase 1: インクリメンタル改善（1〜2週間）

1. **直接実行パス追加**（優先度: 高）
   - `*.ext`, `*`, `?`, リテラルの高速パス
   - 期待効果: 5〜10倍改善

2. **アリーナサイズ最適化**（優先度: 中）
   - 簡単なパターンで256Bバッファ使用
   - 期待効果: 2〜3倍改善

3. **SIMD文字列検索**（優先度: 低）
   - SSE4.2/NEON対応
   - 期待効果: 特定パターンで10倍以上

### Phase 2: アーキテクチャ改善（2〜4週間）

1. **Zero-allocation streaming**
   - wildmatchスタイルの完全書き直し
   - スタックのみ、アロケーションなし
   - 目標: fnmatch(3)と同等以上

2. **Bytecode compiler**（オプション）
   - 軽量バイトコード生成
   - 高速インタープリタ
   - 目標: 現在の2〜3倍高速

### Phase 3: Glob最適化（2週間）

1. **リテラルセグメントの stat() 最適化**
2. **** の枝刈り（.git等を無視）**
3. **ブレース展開のLCP最適化**

---

## 性能予測

### fnmatch最適化後

| パターン | 現在 | Phase 1 | Phase 2 | 目標 |
|---------|------|---------|---------|------|
| `*.c` | 10.3x faster | **20x** | **30x** | **fnmatch(3)の30倍** |
| `a*c` | 4.8x slower | **1.5x faster** | **2x faster** | **fnmatch(3)の2倍** |
| `???.*` | 1.9x slower | **1.0x** | **1.5x faster** | **fnmatch(3)と同等以上** |
| `*test*` | 3.5x faster | **10x** | **15x** | **fnmatch(3)の15倍** |

### glob最適化後

- リテラルパス: **readdir不要（stat()のみ）**
- **パターン: **枝刈りで90%削減**
- ブレース展開: **重複走査なし**

---

## 結論

1. **単一のマッチングエンジンで挙動を統一**
   - fnmatchとxfnmatchは**同じロジック**で動作
   - 違いは「プリコンパイル済み情報の有無」のみ
   - globとfnmatchで完全な挙動一致を保証
   - **これが最重要**

2. **Zero-Allocation Streamingエンジンの実装**
   - 単発（rbc_fnmatch）: パターン文字列から直接実行
   - 複数回（rbc_xfnmatch）: プリコンパイル情報を活用して実行
   - どちらも**同じmatch_engine()関数**を使用
   - **新規実装**

3. **プリコンパイルの最適化効果**
   ```c
   // プリコンパイル時に保存する情報
   typedef struct {
       const char *pattern;
       unsigned flags;
       // 最適化ヒント（パターン解析結果）
       bool has_star;          // * の有無
       bool has_bracket;       // [] の有無
       size_t literal_prefix;  // 先頭のリテラル文字数
       // etc...
   } precompiled_state_t;

   // エンジンはこの情報でパターン解析をスキップ可能
   ```

4. **段階的な実装が現実的**
   - Phase 1: Zero-Allocation Streamingエンジン → 5〜10倍改善
   - Phase 2: プリコンパイル最適化情報の追加 → さらに高速化
   - Phase 3: Bytecode JIT（オプション） → 業界最速レベル
   - **全フェーズで挙動は同一**

5. **他の高性能実装から学ぶ**
   - RE2: コンパイル済み/直接実行どちらも同じVMを使用
   - PCRE2: JITコンパイルとインタープリタで同じマッチング結果
   - V8: バイトコードとインタープリタで完全互換
   - **統一エンジン + 最適化情報の活用 = 業界標準**

---

## 根本的な再設計: 詳細実装方針

### アプローチA: Zero-Allocation Streaming（推奨第一候補）

#### プリコンパイルの役割の変化

**旧設計（現在の実装）:**
```c
// 戦略を選択し、専用のマッチング実装を使う
rbc_fnmatch_pattern_t *p = rbc_fnmatch_compile("*.c", 0);
// → p->strategy = SUFFIX
// → rbc_xfnmatch内で match_suffix() を呼ぶ

// 問題点:
// - 戦略ごとに別実装 → 挙動の不一致リスク
// - 複雑な保守コスト
```

**新設計（Zero-Allocation Streaming）:**
```c
// 最適化ヒントのみを生成、マッチングエンジンは統一
rbc_fnmatch_pattern_t *p = rbc_fnmatch_compile("*.c", 0);
// → p->hints.literal_prefix_len = 0
// → p->hints.has_star = true
// → rbc_xfnmatch内で match_engine() を呼ぶ（rbc_fnmatchと同じ）

// メリット:
// - 単一のマッチング実装 → 挙動の完全一致保証
// - ヒント情報で実行時最適化（if文で分岐）
// - シンプルな保守
```

**つまり:**
- **旧**: プリコンパイル = 戦略選択 + 専用実装への振り分け
- **新**: プリコンパイル = 最適化ヒント生成のみ、実装は単一

**戦略システムは廃止:**
```c
// これらは削除
typedef enum {
    EXACT, PREFIX, SUFFIX, INFIX,
    PATTERN_CHAIN, ALTERNATIVES, RECURSIVE
} rbc_matcher_strategy_t;  // 不要になる

// 代わりに最適化ヒント
typedef struct {
    bool has_star;           // * の有無
    bool has_bracket;        // [] の有無
    size_t literal_prefix_len;  // "test*.c" → literal_prefix_len=4
    // Phase 2: ビットマップ、ジャンプテーブルなど
} precompiled_hints_t;
```

#### 最適化ヒントに含める情報（詳細）

**Phase 1: 基本的なヒント（即座に実装可能）**

```c
typedef struct {
    // === パターンの特徴フラグ ===
    bool has_star;           // '*' を含むか
    bool has_bracket;        // '[' を含むか
    bool has_question;       // '?' を含むか
    bool has_escape;         // '\\' を含むか（FNM_NOESCAPE時は常にfalse）
    bool has_brace;          // '{' を含むか（FNM_EXTGLOB時）

    // === リテラル部分の検出 ===
    size_t literal_prefix_len;  // 先頭のリテラル文字数
                                 // "test*.c" → 4 ("test")
                                 // "*.log" → 0
    size_t literal_suffix_len;  // 末尾のリテラル文字数
                                 // "*.c" → 1 ("c")
                                 // "*test" → 4 ("test")

    // === パターン長情報 ===
    size_t pattern_len;      // パターン全体の長さ（strlen）
    size_t min_match_len;    // マッチ可能な最小テキスト長
                             // "a?c" → 3, "a*c" → 2, "abc" → 3
                             // "???" → 3, "???.*" → 3
    size_t max_match_len;    // マッチ可能な最大テキスト長（*がない場合のみ有効）
                             // "a?c" → 3, "abc" → 3, "???" → 3
                             // "a*c" → SIZE_MAX（無制限）
    bool is_fixed_len;       // 固定長パターン（*がない）
                             // "a?c", "abc", "???" → true
                             // "a*c", "???.*" → false

    // === 特殊パターンの事前判定（汎用的なもののみ）===
    bool is_literal;         // 完全リテラル（メタ文字なし）"abc.txt"
    bool is_star_only;       // "*" だけ（最も単純なケース）
    bool is_question_only;   // "???" のような?だけのパターン（汎用的）
                             // question_count個の任意文字とマッチ

    // === ドットファイル関連（頻出パターン）===
    bool is_dotstar;         // ".*" - 隠しファイル全体（FNM_PERIOD考慮）
    bool starts_with_dot;    // ".abc", ".*", ".?"等（FNM_PERIODで重要）
} precompiled_hints_t;

// 使用例: エンジン側でヒントを組み合わせて判断
if (hints->is_literal) {
    // 完全リテラル → strcmp()で終了
    return strcmp(pattern, text) == 0;
}
if (hints->is_star_only) {
    return true;  // 即座にマッチ
}
if (hints->is_question_only) {
    // "???" → 長さチェックのみ（超高速）
    return strlen(text) == hints->question_count;
}
if (hints->is_dotstar) {
    // ".*" の場合
    if (flags & FNM_PERIOD) {
        // FNM_PERIOD: 先頭の'.'は明示的にマッチ必要
        return text[0] == '.';
    } else {
        // FNM_PERIODなし: すべてにマッチ
        return true;
    }
}

// FNM_PERIODフラグの早期チェック（汎用的）
if ((flags & FNM_PERIOD) && hints->starts_with_dot && text[0] != '.') {
    // パターンが'.'で始まるがテキストが'.'で始まらない → 即座にfalse
    return false;
}

// 長さの事前チェック（汎用的）
size_t tlen = strlen(text);
if (tlen < hints->min_match_len) {
    return false;  // 早期リターン
}
if (hints->is_fixed_len && tlen != hints->max_match_len) {
    // 固定長パターン（*なし）で長さ不一致 → 即座にfalse
    return false;
}

// リテラルプレフィックスの活用（汎用的）
if (hints->literal_prefix_len > 0) {
    if (strncmp(pattern, text, hints->literal_prefix_len) != 0) {
        return false;
    }
    // マッチしたらポインタを進める
}

// リテラルサフィックスの活用（汎用的）
if (hints->literal_suffix_len > 0 && !hints->has_star) {
    // *がない → サフィックスの位置が確定
    size_t suffix_offset = pattern_len - literal_suffix_len;
    if (tlen < pattern_len) return false;
    if (strcmp(text + tlen - literal_suffix_len,
               pattern + suffix_offset) != 0) {
        return false;
    }
}

// "*.ext" のような頻出パターンは上記の組み合わせで判定
// is_literal==false && literal_prefix_len==0 && has_star &&
// !has_question && !has_bracket && star_count==1 && literal_suffix_len>0
// → これは"*.ext"パターン（でも明示的なフラグは不要）
```

**Phase 2: 高度なヒント（追加最適化）**

```c
typedef struct {
    // Phase 1のすべて + 以下

    // === 文字クラス最適化 ===
    bool has_simple_charset;    // 単純な[abc]のみ（範囲なし）
    uint64_t charset_bitmap[4]; // [a-z]用ビットマップ（256bit = 4*64bit）
                                // 範囲がASCIIの場合のみ

    // === Boyer-Moore最適化 ===
    bool use_boyer_moore;       // リテラル部分が長い場合
    uint8_t bad_char_table[256]; // Bad Character Rule用テーブル

    // === パターン構造の解析 ===
    uint8_t star_count;         // '*'の個数（バックトラック深さ予測）
    uint8_t bracket_count;      // '[]'の個数
    （実装必須）
   - `is_literal` - "abc.txt" → strcmp()
   - `is_star_only` - "*" → 即座にtrue
   - `is_star_ext` - "*.c" → 末尾チェックのみ（**最頻出**）
   - `is_question_only` - "???" → 長さチェックのみ
   - `is_question_ext` - "???.c" → 長さ + 末尾チェック
   - `is_question_star_ext` - "???.*" → 最小長チェック（**ベンチマークにあり**）
   - 実装コスト: **低**、効果: **超大**（頻出パターン）

2. **Phase 1のリテラルprefix/suffix** → 大きな効果
   - `literal_prefix_len`, `literal_suffix_len`
   - 実装コスト: **低**、効果: **大**

3. **Phase 1のカウント情報** → min_match_len計算
   - `question_count`, `star_count`
   - 実装コスト: **低**、効果: **中**

4. **Phase 2のビットマップ** → 中程度の効果
   - `charset_bitmap`（[a-z]の高速化）
   - 実装コスト: **中**、効果: **中**

5. **Phase 3のBytecode/JIT** → 限定的な効果
   - 複雑パターンのみで有効
   - 実装コスト: **高**、効果: **小〜中**

**ベンチマークパターンとの対応:**

| パターン | 最適化ヒント | 効果 |
|---------|------------|------|
| `*.c` | `is_star_ext` | 末尾2文字比較のみ → 43倍高速維持 |
| `a*c` | `literal_prefix_len=1, literal_suffix_len=1` | 先頭・末尾チェック → 大幅改善 |
| `???.*` | `is_question_star_ext` | 長さチェックのみ → **超高速化** |
| `*teis_question_only` - "???"等の?のみ → 長さチェックだけ（**超高速**）
   - `literal_prefix_len` - 先頭リテラル長（多くのパターンで有効）
   - `literal_suffix_len` - 末尾リテラル長（多くのパターンで有効）
   - `min_match_len` / `max_match_len` - 長さチェック（早期リターン）
   - `is_fixed_len` - 固定長判定（*なしパ/ コンパイル済みバイトコード
    size_t bytecode_len;        // バイトコード長

    // === JIT情報 ===
    void *jit_func;             // JITコンパイル済み関数ポインタ
                                // NULLなら未コンパイル
} precompiled_hints_t;
```

**実装優先度:**

1. **Phase 1の基本ヒント** → 汎用的で効果大（実装必須）
   - `is_literal` - メタ文字なし → strcmp()
   - `is_star_only` - "*"のみ → 即座にtrue
   - `literal_prefix_len` - 先頭リテラル長（多くのパターンで有効）
   - `literal_suffix_len` - 末尾リテラル長（多くのパターンで有効）
   - `min_match_len` - 最小マッチ長（早期リターン）
   - 実装コスト: **低**、効果: **超大**（あらゆるパターンに適用可能）

2. **Phase 1のメタ文字フラグ** → 分岐削減
   - `has_star`, `has_question`, `has_bracket`, `has_escape`
   - 実装コスト: **低**、効果: **大**

3. **Phase 1のカウント情報** → 詳細な最適化
   - `star_count`, `question_count`, `bracket_count`
   - 実装コスト: **低**、効果: **中**

4. **Phase 2のビットマップ** → 文字クラス最適化
   - `charset_bitmap`（[a-z]の高速化）
   - 実装コスト: **中**、効果: **中**

5. **Phase 2のBoyer-Moore** → 長いリテラル検索
   - `bad_char_table`（"*longtext*"等で有効）
   - 実装コスト: **中**、効果: **中**

6. **Phase 3のBytecode/JIT** → 限定的な効果
   - 複雑パターンのみで有効
   - 実装コスト: **高**、効果: **小〜中**

**ベンチマークパターンへの汎用的な適用:**

| パターン | ヒントの組み合わせ | 最適化手法 |
|---------|------------------|-----------|
| `*.c` | `liis_fixed_len=false, question_count=3, min_match_len=3` | 長さチェック + バックトラック |
| `???` | `is_question_only=true, question_count=3` | **長さチェックのみ（超高速）** |
| `*test*` | `literal_prefix_len=0, literal_suffix_len=0, star_count=2` | Boyer-Moore検索 |
| `test_*.c` | `literal_prefix_len=5, literal_suffix_len=2` | 先頭・末尾チェック |
| `a?c` | `is_fixed_len=true, max_match_len=3` | 長さチェック + リテラル比較 |

**重要**:
- 特殊ケースフラグ（`is_star_ext`等）は不要。基本ヒントの組み合わせで判定できる。
- `is_question_only`は汎用的（"???"だけでなく"?????"等にも適用）
- `is_fixed_len`で固定長パターン全般を高速化（`*`なしパターン）en=2` | 先頭・末尾チェック |

**重要**: 特殊ケースフラグ（`is_star_ext`等）は不要。基本ヒントの組み合わせで判定できる。

```c
// 統一アーキテクチャ: 共通のマッチングエンジン

// コアエンジン（Zero-Allocation Streaming）
static bool match_engine(const char *pattern, const char *text,
                         unsigned flags, const void *compiled_state) {
    // compiled_state == NULL: パターン文字列から直接実行
    // compiled_state != NULL: プリコンパイル済み状態から実行
    // ※どちらも同じロジックで動作（挙動の一貫性）
}

// 1. rbc_fnmatch（単発マッチング）
bool rbc_fnmatch(const char *pattern, const char *text, unsigned flags) {
    return match_engine(pattern, text, flags, NULL);  // 直接実行
}

// 2. rbc_xfnmatch（複数回マッチング）
bool rbc_xfnmatch(const rbc_fnmatch_pattern_t *p, const char *text) {
    return match_engine(p->pattern, text, p->flags, p->state);  // プリコンパイル済み
}
```

**設計方針:**
- **単一のマッチングエンジン** → fnmatchとxfnmatchで挙動が完全一致
- **rbc_fnmatch**: エンジンをその場で実行（0アロケーション）
- **rbc_xfnmatch**: プリコンパイルした状態をエンジンに渡す（高速化）
- **プリコンパイルの効果**: パターン解析のスキップ、最適化ヒントの活用

**RE2などの実装と同様:**
- RE2も「コンパイル済み正規表現」と「直接実行」で**同じVMを使用**
- 違いは「バイトコードから実行」か「パターンをパース中に実行」かだけ

#### 設計原則

1. **完全スタックベース**
   - ヒープアロケーション: 0バイト
   - アリーナ不要
   - メモリリークの可能性: なし

2. **単一パス処理**
   - パターン解析とマッチングを同時実行
   - プリコンパイル不要
   - パターン文字列を1回だけ走査

3. **状態機械ベース**
   - 明示的な状態管理
   - バックトラックポイントの記録
   - goto/continueによる高速制御フロー

#### 実装詳細

##### ステップ1: コア状態機械の実装

```c
// src/fnmatch_streaming.c

typedef struct {
    // 入力ポインタ（読み取り専用）
    const char *pattern_start;
    const char *text_start;

    // 現在位置（可変）
    const char *p;  // パターン位置
    const char *t;  // テキスト位置

    // バックトラック状態
    const char *star_p;  // 最後の*の次の位置
    const char *star_t;  // *がマッチした開始位置

    // フラグ（ビットフィールドで圧縮）
    unsigned int flags : 8;
    unsigned int in_bracket : 1;
    unsigned int negated : 1;
    unsigned int has_star : 1;
    unsigned int _padding : 21;
} stream_state_t;

// 初期化（インライン展開される）
static inline void init_state(stream_state_t *state,
                               const char *pattern,
                               const char *text,
                               unsigned int flags) {
    state->pattern_start = pattern;
    state->text_start = text;
    state->p = pattern;
    state->t = text;
    state->star_p = NULL;
    state->star_t = NULL;
    state->flags = flags;
    state->in_bracket = 0;
    state->negated = 0;
    state->has_star = 0;
}

// メインマッチング関数
bool rbc_fnmatch_streaming(const char *pattern,
                           const char *text,
                           unsigned int flags) {
    stream_state_t state;
    init_state(&state, pattern, text, flags);

    // メインループ: パターンと文字列を同時走査
    while (true) {
        char p_char = *state.p;
        char t_char = *state.t;

        // === Fast path: リテラルマッチ ===
        if (likely(p_char != '\0' && p_char != '*' &&
                   p_char != '?' && p_char != '[' && p_char != '\\')) {
            if (unlikely(t_char == '\0')) goto fail_or_backtrack;

            // Case folding
            if (flags & RBC_FNM_CASEFOLD) {
                if (tolower_fast(p_char) != tolower_fast(t_char))
                    goto fail_or_backtrack;
            } else {
                if (p_char != t_char)
                    goto fail_or_backtrack;
            }

            state.p++;
            state.t++;
            continue;
        }

        // === Special characters ===
        switch (p_char) {
        case '\0':
            // パターン終了 → テキストも終了していればマッチ
            return (t_char == '\0');

        case '*':
            state.has_star = 1;
            // 連続する*を読み飛ばし
            do { state.p++; } while (*state.p == '*');

            // 末尾の*は残り全てにマッチ
            if (unlikely(*state.p == '\0')) {
                if (flags & RBC_FNM_PATHNAME)
                    return strchr(state.t, '/') == NULL;
                return true;
            }

            // バックトラックポイントを設定
            state.star_p = state.p;
            state.star_t = state.t;
            continue;

        case '?':
            if (unlikely(t_char == '\0')) goto fail_or_backtrack;

            // PATHNAMEフラグで/にマッチしない
            if (unlikely((flags & RBC_FNM_PATHNAME) && t_char == '/'))
                goto fail_or_backtrack;

            // DOTMATCHフラグなしで先頭の.にマッチしない
            if (unlikely(!(flags & RBC_FNM_DOTMATCH) && t_char == '.' &&
                        (state.t == state.text_start ||
                         ((flags & RBC_FNM_PATHNAME) && state.t[-1] == '/'))))
                goto fail_or_backtrack;

            state.p++;
            state.t++;
            continue;

        case '[':
            if (unlikely(t_char == '\0')) goto fail_or_backtrack;

            // 文字クラスマッチング（専用関数）
            const char *bracket_end = match_bracket_class(
                state.p + 1, t_char, flags);

            if (unlikely(bracket_end == NULL))
                goto fail_or_backtrack;

            state.p = bracket_end;
            state.t++;
            continue;

        case '\\':
            if (!(flags & RBC_FNM_NOESCAPE) && state.p[1] != '\0') {
                state.p++;  // エスケープ文字をスキップ
                p_char = *state.p;

                if (p_char != t_char)
                    goto fail_or_backtrack;

                state.p++;
                state.t++;
                continue;
            }
            // NOESCAPEの場合はリテラルとして処理
            goto literal_match;

        default:
        literal_match:
            if (t_char != p_char)
                goto fail_or_backtrack;
            state.p++;
            state.t++;
            continue;
        }

fail_or_backtrack:
        // バックトラック可能か確認
        if (likely(state.star_p != NULL)) {
            // テキスト位置を進める
            state.t = ++state.star_t;

            // テキスト終了ならマッチ失敗
            if (unlikely(*state.t == '\0'))
                return false;

            // PATHNAMEで/を越えない
            if (unlikely((flags & RBC_FNM_PATHNAME) && *state.t == '/'))
                return false;

            // パターン位置をリセット
            state.p = state.star_p;
            continue;
        }

        return false;
    }
}

// 高速tolower（ルックアップテーブル使用）
static inline unsigned char tolower_fast(unsigned char c) {
    static const unsigned char table[256] = {
        // 0-64: そのまま
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
        32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
        48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,
        // 65-90 (A-Z): 小文字化
        97,98,99,100,101,102,103,104,105,106,107,108,109,
        110,111,112,113,114,115,116,117,118,119,120,121,122,
        // 91-255: そのまま
        91,92,93,94,95,96,
        97,98,99,100,101,102,103,104,105,106,107,108,109,
        110,111,112,113,114,115,116,117,118,119,120,121,122,
        123,124,125,126,127,
        // 128-255: そのまま
        128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
        144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
        160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
        176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
        192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
        208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
        224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
        240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
    };
    return table[c];
}
```

##### ステップ2: 文字クラス最適化

```c
// 文字クラスをビットマップで高速化
static const char* match_bracket_class(const char *pattern,
                                       unsigned char test_char,
                                       unsigned int flags) {
    // ビットマップ: 256ビット = 32バイト
    uint64_t bitmap[4] = {0, 0, 0, 0};

    const char *p = pattern;
    bool negated = false;

    // 否定チェック
    if (*p == '!' || *p == '^') {
        negated = true;
        p++;
    }

    // ビットマップ構築
    while (*p && *p != ']') {
        unsigned char lower = (unsigned char)*p;
        unsigned char upper = lower;

        // 範囲指定 [a-z]
        if (p[1] == '-' && p[2] && p[2] != ']') {
            upper = (unsigned char)p[2];
            p += 2;
        }

        // ビットマップにセット
        for (unsigned char c = lower; c <= upper && c >= lower; c++) {
            // Case folding
            if (flags & RBC_FNM_CASEFOLD) {
                bitmap[tolower_fast(c) >> 6] |= (1ULL << (tolower_fast(c) & 63));
                bitmap[toupper_fast(c) >> 6] |= (1ULL << (toupper_fast(c) & 63));
            } else {
                bitmap[c >> 6] |= (1ULL << (c & 63));
            }
        }
        p++;
    }

    if (*p != ']')
        return NULL;  // 不正な文字クラス

    // ビットマップでテスト（1命令）
    bool matched = (bitmap[test_char >> 6] & (1ULL << (test_char & 63))) != 0;

    // 否定の場合は反転
    if (negated)
        matched = !matched;

    return matched ? (p + 1) : NULL;
}
```

##### ステップ3: 特殊ケースの高速パス

```c
// *.ext の超高速パス
static inline bool match_suffix_fast(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);

    if (text_len < suffix_len)
        return false;

    // 末尾から比較（SIMD使用可能）
    return memcmp(text + text_len - suffix_len, suffix, suffix_len) == 0;
}

// test_* の超高速パス
static inline bool match_prefix_fast(const char *text,
                                     const char *prefix,
                                     size_t prefix_len) {
    return memcmp(text, prefix, prefix_len) == 0;
}

// パターン振り分けエントリーポイント
bool rbc_fnmatch_v2(const char *pattern, const char *text, unsigned int flags) {
    // === Ultra fast paths ===

    // 完全一致
    if (!strchr(pattern, '*') && !strchr(pattern, '?') && !strchr(pattern, '[')) {
        return strcmp(pattern, text) == 0;
    }

    // *.ext パターン
    if (pattern[0] == '*' && pattern[1] == '.' &&
        !strchr(pattern + 2, '*') && !strchr(pattern + 2, '?') &&
        !strchr(pattern + 2, '[')) {
        return match_suffix_fast(text, pattern + 1);
    }

    // * のみ
    if (pattern[0] == '*' && pattern[1] == '\0') {
        if (flags & RBC_FNM_PATHNAME)
            return strchr(text, '/') == NULL;
        return true;
    }

    // ? のみ
    if (pattern[0] == '?' && pattern[1] == '\0') {
        return text[0] != '\0' && text[1] == '\0';
    }

    // 一般的なケース
    return rbc_fnmatch_streaming(pattern, text, flags);
}
```

#### 性能最適化テクニック

##### 1. 分岐予測ヒント

```c
// 既に実装済み
#ifdef __GNUC__
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif
```

##### 1. ループアンローリング

```c
// リテラル文字列の高速比較
static inline bool match_literal_unrolled(const char *p, const char *t, size_t len) {
    // 4文字ずつ処理
    while (len >= 4) {
        if (*(uint32_t*)p != *(uint32_t*)t)
            return false;
        p += 4;
        t += 4;
        len -= 4;
    }

    // 残り処理
    while (len > 0) {
        if (*p != *t)
            return false;
        p++;
        t++;
        len--;
    }

    return true;
}
```

##### 3. メモリアクセスパターン最適化

```c
// キャッシュラインアライメント
typedef struct __attribute__((aligned(64))) {
    stream_state_t state;
    uint64_t bitmap[4];
    char temp_buffer[32];
} aligned_match_context_t;
```

#### テスト戦略

```c
// tests/test_streaming.c

void test_streaming_basic(void) {
    TEST_ASSERT_TRUE(rbc_fnmatch_v2("*.c", "test.c", 0));
    TEST_ASSERT_FALSE(rbc_fnmatch_v2("*.c", "test.h", 0));
}

void test_streaming_star(void) {
    TEST_ASSERT_TRUE(rbc_fnmatch_v2("a*c", "abc", 0));
    TEST_ASSERT_TRUE(rbc_fnmatch_v2("a*c", "aXYZc", 0));
    TEST_ASSERT_FALSE(rbc_fnmatch_v2("a*c", "ab", 0));
}

void test_streaming_backtrack(void) {
    // 複雑なバックトラックケース
    TEST_ASSERT_TRUE(rbc_fnmatch_v2("*a*b*c*", "XaYbZc", 0));
    TEST_ASSERT_TRUE(rbc_fnmatch_v2("*.*.*", "a.b.c", 0));
}

void test_streaming_bracket(void) {
    TEST_ASSERT_TRUE(rbc_fnmatch_v2("[a-z]", "a", 0));
    TEST_ASSERT_TRUE(rbc_fnmatch_v2("[!0-9]", "a", 0));
    TEST_ASSERT_FALSE(rbc_fnmatch_v2("[!0-9]", "5", 0));
}
```

#### ベンチマーク予測

| パターン | 現在 | Streaming | 改善率 |
|---------|------|-----------|--------|
| `*.c` | 0.202s | **0.020s** | **10倍** |
| `a*c` | 0.343s | **0.050s** | **7倍** |
| `???.*` | 0.266s | **0.080s** | **3倍** |
| `*test*` | 0.338s | **0.100s** | **3倍** |

---

### アプローチB: Bytecode JIT（実験的）

#### 設計原則

1. **2段階処理**
   - Stage 1: パターン → バイトコードコンパイル
   - Stage 2: バイトコード実行

2. **スタックアロケーション**
   - バイトコード: スタック上の固定配列
   - 最大256命令（99%のパターンをカバー）

3. **最適化されたオペコード**
   - 単一命令で多くの処理
   - ジャンプテーブルによる高速ディスパッチ

#### バイトコード命令セット

```c
// src/fnmatch_bytecode.h

typedef enum {
    // 基本マッチング
    OP_LITERAL      = 0x00,  // 1バイトリテラルマッチ
    OP_LITERAL_N    = 0x01,  // Nバイトリテラルマッチ（連続最適化）
    OP_ANY          = 0x02,  // ? (任意の1文字)
    OP_STAR         = 0x03,  // * (0個以上)

    // 文字クラス
    OP_CHARSET      = 0x10,  // [a-z] (ビットマップ使用)
    OP_CHARSET_NEG  = 0x11,  // [^a-z] (否定)

    // 制御フロー
    OP_JUMP         = 0x20,  // 無条件ジャンプ
    OP_SPLIT        = 0x21,  // 分岐（NFAシミュレーション）
    OP_SAVE         = 0x22,  // バックトラック位置保存

    // 終了
    OP_MATCH        = 0xF0,  // マッチ成功
    OP_FAIL         = 0xFF,  // マッチ失敗
} opcode_t;

typedef struct {
    uint8_t opcode;
    uint8_t arg1;     // 汎用引数1
    uint16_t arg2;    // 汎用引数2
    union {
        char literal[4];           // OP_LITERAL_N用
        uint64_t charset[4];       // OP_CHARSET用
        int16_t jump_offset;       // OP_JUMP用
    } data;
} bytecode_insn_t;

// バイトコードプログラム
typedef struct {
    bytecode_insn_t code[256];
    uint16_t length;
    uint16_t flags;
} bytecode_program_t;
```

#### コンパイラ実装

```c
// src/fnmatch_compiler.c

bool compile_pattern(const char *pattern,
                     unsigned int flags,
                     bytecode_program_t *program) {
    uint16_t pc = 0;
    const char *p = pattern;

    while (*p && pc < 255) {
        if (*p == '*') {
            // STAR命令
            program->code[pc++] = (bytecode_insn_t){
                .opcode = OP_STAR,
                .arg1 = 0,
                .arg2 = 0
            };
            p++;

            // 連続する*をスキップ
            while (*p == '*') p++;

        } else if (*p == '?') {
            // ANY命令
            program->code[pc++] = (bytecode_insn_t){
                .opcode = OP_ANY,
                .arg1 = 0,
                .arg2 = 0
            };
            p++;

        } else if (*p == '[') {
            // CHARSET命令
            p++;  // '['をスキップ

            uint64_t bitmap[4] = {0};
            bool negated = false;

            if (*p == '!' || *p == '^') {
                negated = true;
                p++;
            }

            // ビットマップ構築
            while (*p && *p != ']') {
                unsigned char lower = *p;
                unsigned char upper = lower;

                if (p[1] == '-' && p[2] && p[2] != ']') {
                    upper = p[2];
                    p += 2;
                }

                for (unsigned char c = lower; c <= upper && c >= lower; c++) {
                    bitmap[c >> 6] |= (1ULL << (c & 63));
                }
                p++;
            }

            if (*p != ']')
                return false;  // エラー
            p++;

            program->code[pc++] = (bytecode_insn_t){
                .opcode = negated ? OP_CHARSET_NEG : OP_CHARSET,
                .arg1 = 0,
                .arg2 = 0,
                .data.charset = {bitmap[0], bitmap[1], bitmap[2], bitmap[3]}
            };

        } else {
            // リテラル文字
            // 連続するリテラルを最適化
            const char *literal_start = p;
            size_t literal_len = 0;

            while (*p && *p != '*' && *p != '?' && *p != '[' &&
                   literal_len < 4) {
                if (!(flags & RBC_FNM_NOESCAPE) && *p == '\\' && p[1]) {
                    p++;
                }
                literal_len++;
                p++;
            }

            if (literal_len == 1) {
                program->code[pc++] = (bytecode_insn_t){
                    .opcode = OP_LITERAL,
                    .arg1 = *literal_start,
                    .arg2 = 0
                };
            } else {
                program->code[pc] = (bytecode_insn_t){
                    .opcode = OP_LITERAL_N,
                    .arg1 = literal_len,
                    .arg2 = 0
                };
                memcpy(program->code[pc].data.literal, literal_start, literal_len);
                pc++;
            }
        }
    }

    // MATCH命令
    program->code[pc++] = (bytecode_insn_t){
        .opcode = OP_MATCH,
        .arg1 = 0,
        .arg2 = 0
    };

    program->length = pc;
    program->flags = flags;
    return true;
}
```

#### バイトコードインタープリタ

```c
// src/fnmatch_vm.c

bool execute_bytecode(const bytecode_program_t *program, const char *text) {
    uint16_t pc = 0;
    const char *tp = text;

    // バックトラックスタック
    struct {
        uint16_t pc;
        const char *tp;
    } backtrack_stack[64];
    int backtrack_sp = -1;

    // ジャンプテーブル（GCC computed goto）
    #ifdef __GNUC__
    static void* dispatch_table[] = {
        &&op_literal, &&op_literal_n, &&op_any, &&op_star,
        &&op_charset, &&op_charset_neg, &&op_jump, &&op_match
    };
    #define DISPATCH() goto *dispatch_table[program->code[pc].opcode]
    #define NEXT_INSN() pc++; DISPATCH()
    #else
    #define DISPATCH() goto switch_dispatch
    #define NEXT_INSN() pc++; goto switch_dispatch
    #endif

    DISPATCH();

    #ifdef __GNUC__
    op_literal:
    #else
    switch_dispatch:
    switch (program->code[pc].opcode) {
    case OP_LITERAL:
    #endif
    {
        if (*tp != program->code[pc].arg1)
            goto backtrack;
        tp++;
        NEXT_INSN();
    }

    #ifdef __GNUC__
    op_literal_n:
    #else
    case OP_LITERAL_N:
    #endif
    {
        uint8_t len = program->code[pc].arg1;
        if (memcmp(tp, program->code[pc].data.literal, len) != 0)
            goto backtrack;
        tp += len;
        NEXT_INSN();
    }

    #ifdef __GNUC__
    op_any:
    #else
    case OP_ANY:
    #endif
    {
        if (*tp == '\0')
            goto backtrack;
        tp++;
        NEXT_INSN();
    }

    #ifdef __GNUC__
    op_star:
    #else
    case OP_STAR:
    #endif
    {
        // バックトラックポイントを保存
        backtrack_stack[++backtrack_sp] = (typeof(backtrack_stack[0])){
            .pc = pc + 1,
            .tp = tp
        };
        NEXT_INSN();
    }

    #ifdef __GNUC__
    op_charset:
    #else
    case OP_CHARSET:
    #endif
    {
        unsigned char c = *tp;
        uint64_t *bitmap = program->code[pc].data.charset;

        if ((bitmap[c >> 6] & (1ULL << (c & 63))) == 0)
            goto backtrack;

        tp++;
        NEXT_INSN();
    }

    #ifdef __GNUC__
    op_match:
    #else
    case OP_MATCH:
    #endif
    {
        return (*tp == '\0');
    }

backtrack:
    if (backtrack_sp >= 0) {
        pc = backtrack_stack[backtrack_sp].pc;
        tp = ++backtrack_stack[backtrack_sp].tp;

        if (*tp == '\0') {
            backtrack_sp--;
            goto backtrack;
        }

        DISPATCH();
    }

    return false;

    #ifndef __GNUC__
    }
    #endif
}
```

#### エントリーポイント

```c
// src/fnmatch_bytecode.c

bool rbc_fnmatch_bytecode(const char *pattern,
                          const char *text,
                          unsigned int flags) {
    bytecode_program_t program;

    // コンパイル（スタック上）
    if (!compile_pattern(pattern, flags, &program))
        return false;

    // 実行
    return execute_bytecode(&program, text);
}
```

---

### 実装順序とマイルストーン

#### Week 1-2: Zero-Allocation Streaming

- [ ] Day 1-2: コア状態機械実装
- [ ] Day 3-4: 文字クラス最適化
- [ ] Day 5-6: 特殊ケース高速パス
- [ ] Day 7-8: テスト完全実装
- [ ] Day 9-10: ベンチマーク、デバッグ

**マイルストーン1:** fnmatch(3)と同等の速度達成

#### Week 3-4: Bytecode JIT（オプション）

- [ ] Day 11-13: バイトコード設計とコンパイラ
- [ ] Day 14-16: インタープリタ実装
- [ ] Day 17-18: 最適化（computed goto等）
- [ ] Day 19-20: ベンチマーク比較

**マイルストーン2:** 現在実装の2倍速達成

#### Week 5: 統合とポリッシュ

- [ ] Day 21-22: rbc_fnmatchへの統合
- [ ] Day 23-24: 全テストパス確認
- [ ] Day 25: ドキュメント更新

**最終マイルストーン:** 全パターンでfnmatch(3)以上の性能

---

### デバッグとプロファイリング

#### デバッグ戦略

```c
// デバッグ用トレース機能
#ifdef DEBUG_STREAMING
#define TRACE(fmt, ...) fprintf(stderr, "[TRACE] " fmt "\n", ##__VA_ARGS__)
#else
#define TRACE(fmt, ...) do {} while(0)
#endif

// 使用例
TRACE("Pattern: %s, Text: %s, Position: p=%ld t=%ld",
      state.pattern_start, state.text_start,
      state.p - state.pattern_start, state.t - state.text_start);
```

#### プロファイリング

```bash
# perf でホットスポット分析
perf record -g ./bench_fnmatch "a*c" 10000
perf report

# Valgrind でメモリ使用量確認（アロケーション0を確認）
valgrind --tool=massif ./bench_fnmatch "*.c" 1000

# cachegrind でキャッシュ効率分析
valgrind --tool=cachegrind ./bench_fnmatch "*test*" 10000
```
