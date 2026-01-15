# Zero-Allocation Streaming 実装計画

## 実装方針

### 1. アーキテクチャ概要

```
┌─────────────────────────────────────────────────────┐
│                   API Layer                          │
├─────────────────────────────────────────────────────┤
│ rbc_fnmatch()         │ rbc_xfnmatch()               │
│ ↓ hints=NULL          │ ↓ hints=&p->hints            │
└───────────────┬───────┴──────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────┐
│         match_engine() - 統一マッチングエンジン       │
│  • Zero-Allocation（スタックのみ）                   │
│  • 単一のマッチングロジック（挙動の一貫性）           │
│  • hintsがあれば最適化、なくても動作                 │
└─────────────────────────────────────────────────────┘
                │
                ▼
┌─────────────────────────────────────────────────────┐
│         プリコンパイル（オプショナル）                │
│  rbc_fnmatch_compile() - 最適化ヒント生成のみ        │
│  • is_literal, is_star_only, is_question_only        │
│  • literal_prefix_len, literal_suffix_len            │
│  • min_match_len, max_match_len, is_fixed_len        │
│  • has_star, has_question, has_bracket, star_count   │
└─────────────────────────────────────────────────────┘
```

### 2. 既存コードからの変更点

**削除するもの:**
- `rbc_matcher_strategy_t`（戦略enum: EXACT, PREFIX, SUFFIX等）
- 戦略別の専用マッチング関数（match_exact, match_prefix等）
- アリーナベースのコンパイル（4KB確保）
- `src/matcher.c`の戦略ベース実装

**新規追加するもの:**
- `src/fnmatch_streaming.c`（統一エンジン）
- `precompiled_hints_t`構造体（最適化ヒント）
- `match_engine()`関数（単一のマッチングロジック）

**統合するもの:**
- `src/fnmatch.c` → `match_engine()`を呼ぶように変更
- `src/matcher.c` → ヒント生成ロジックに書き換え

---

## Phase 1: 基本実装（Week 1-2）

### Step 1.1: 構造体定義とスタブ作成（Day 1）

**ファイル:** `src/fnmatch_streaming.c`（新規作成）

```c
#include "internal.h"
#include <stdbool.h>
#include <string.h>
#include <limits.h>

// プリコンパイル最適化ヒント
typedef struct {
    // === メタ文字フラグ ===
    bool has_star;           // '*' を含むか
    bool has_question;       // '?' を含むか
    bool has_bracket;        // '[' を含むか
    bool has_escape;         // '\\' を含むか
    
    // === リテラル部分の検出 ===
    size_t literal_prefix_len;  // 先頭のリテラル文字数
    size_t literal_suffix_len;  // 末尾のリテラル文字数
    
    // === パターン長情報 ===
    size_t pattern_len;      // パターン全体の長さ
    size_t min_match_len;    // マッチ可能な最小テキスト長
    size_t max_match_len;    // マッチ可能な最大テキスト長（*なしのみ）
    bool is_fixed_len;       // 固定長パターン（*がない）
    
    // === 特殊パターンの事前判定 ===
    bool is_literal;         // 完全リテラル（メタ文字なし）
    bool is_star_only;       // "*" だけ
    bool is_question_only;   // "???" のような?だけ
    bool is_dotstar;         // ".*"
    bool starts_with_dot;    // '.'で始まる
    
    // === カウント情報 ===
    uint8_t star_count;      // '*'の個数
    uint8_t question_count;  // '?'の個数
    uint8_t bracket_count;   // '[]'の個数
} precompiled_hints_t;

// 実行時状態（完全スタックベース）
typedef struct {
    const char *p;      // Pattern position
    const char *t;      // Text position
    const char *star_p; // Backtrack pattern position
    const char *star_t; // Backtrack text position
    unsigned int flags : 8;
    const precompiled_hints_t *hints;  // NULL可（rbc_fnmatch時）
} stream_state_t;

// 統一マッチングエンジン
static bool match_engine(const char *pattern, const char *text, 
                         unsigned flags, const precompiled_hints_t *hints);

// API: 単発マッチング（プリコンパイルなし）
bool rbc_fnmatch(const char *pattern, const char *text, unsigned flags) {
    return match_engine(pattern, text, flags, NULL);
}

// ヒント生成（内部関数）
static void generate_hints(const char *pattern, unsigned flags, 
                           precompiled_hints_t *hints);

// API: プリコンパイル
rbc_fnmatch_pattern_t *rbc_fnmatch_compile(const char *pattern, unsigned flags) {
    rbc_fnmatch_pattern_t *p = malloc(sizeof(*p));
    if (!p) return NULL;
    
    p->pattern = strdup(pattern);
    if (!p->pattern) {
        free(p);
        return NULL;
    }
    p->flags = flags;
    
    generate_hints(pattern, flags, &p->hints);
    
    return p;
}

// API: 複数回マッチング（プリコンパイルあり）
bool rbc_xfnmatch(const rbc_fnmatch_pattern_t *p, const char *text) {
    return match_engine(p->pattern, text, p->flags, &p->hints);
}

// API: プリコンパイルパターンの解放
void rbc_fnmatch_pattern_free(rbc_fnmatch_pattern_t *p) {
    if (p) {
        free((void*)p->pattern);
        free(p);
    }
}
```

**ファイル:** `src/internal.h`（変更）

```c
// 追加
typedef struct {
    const char *pattern;
    unsigned flags;
    precompiled_hints_t hints;  // 最適化ヒント
} rbc_fnmatch_pattern_t;
```

**タスク:**
- [ ] `src/fnmatch_streaming.c`を新規作成
- [ ] `src/internal.h`に構造体定義を追加
- [ ] コンパイル確認（まだ実装なし、スタブのみ）

---

### Step 1.2: ヒント生成関数の実装（Day 2-3）

**ファイル:** `src/fnmatch_streaming.c`

```c
// ヒント生成（パターン解析）
static void generate_hints(const char *pattern, unsigned flags, 
                           precompiled_hints_t *hints) {
    memset(hints, 0, sizeof(*hints));
    
    const char *p = pattern;
    hints->pattern_len = strlen(pattern);
    
    // 最小マッチ長を計算しながらスキャン
    size_t min_len = 0;
    size_t fixed_len = 0;
    bool has_wildcard = false;
    
    // メタ文字の検出とカウント
    while (*p) {
        switch (*p) {
        case '*':
            hints->has_star = true;
            hints->star_count++;
            has_wildcard = true;
            p++;
            break;
        case '?':
            hints->has_question = true;
            hints->question_count++;
            min_len++;
            fixed_len++;
            p++;
            break;
        case '[':
            hints->has_bracket = true;
            hints->bracket_count++;
            min_len++;
            fixed_len++;
            // ']'まで読み飛ばす
            p++;
            if (*p == '!' || *p == '^') p++;
            if (*p == ']') p++;  // 最初の']'はリテラル
            while (*p && *p != ']') {
                if (*p == '\\' && !(flags & FNM_NOESCAPE)) p++;
                if (*p) p++;
            }
            if (*p == ']') p++;
            break;
        case '\\':
            if (!(flags & FNM_NOESCAPE)) {
                hints->has_escape = true;
                p++;
                if (*p) {
                    min_len++;
                    fixed_len++;
                    p++;
                }
            } else {
                min_len++;
                fixed_len++;
                p++;
            }
            break;
        default:
            min_len++;
            fixed_len++;
            p++;
            break;
        }
    }
    
    hints->min_match_len = min_len;
    hints->is_fixed_len = !hints->has_star;
    hints->max_match_len = hints->is_fixed_len ? fixed_len : SIZE_MAX;
    
    // リテラルプレフィックスの検出
    p = pattern;
    while (*p && *p != '*' && *p != '?' && *p != '[' && 
           !(*p == '\\' && !(flags & FNM_NOESCAPE))) {
        hints->literal_prefix_len++;
        p++;
    }
    
    // リテラルサフィックスの検出（逆順スキャン）
    if (hints->pattern_len > 0) {
        p = pattern + hints->pattern_len - 1;
        while (p >= pattern) {
            if (*p == '*' || *p == '?' || *p == ']') break;
            if (*p == '\\' && p > pattern && !(flags & FNM_NOESCAPE)) {
                p--;  // エスケープされた文字
                if (p < pattern) break;
            }
            hints->literal_suffix_len++;
            p--;
        }
    }
    
    // 特殊パターンの判定
    hints->is_literal = !hints->has_star && !hints->has_question && 
                        !hints->has_bracket && !hints->has_escape;
    hints->is_star_only = (hints->pattern_len == 1 && pattern[0] == '*');
    hints->is_question_only = (hints->question_count > 0 && 
                               hints->question_count == hints->pattern_len);
    hints->is_dotstar = (hints->pattern_len == 2 && 
                         pattern[0] == '.' && pattern[1] == '*');
    hints->starts_with_dot = (hints->pattern_len > 0 && pattern[0] == '.');
}
```

**タスク:**
- [ ] `generate_hints()`を実装
- [ ] エスケープシーケンス処理
- [ ] 文字クラス`[]`のスキップ処理
- [ ] テストケース作成（後述）

---

### Step 1.3: 統一マッチングエンジンの実装（Day 4-6）

**ファイル:** `src/fnmatch_streaming.c`

```c
// 文字クラスマッチング（既存のロジックを流用）
static bool match_bracket(const char **pattern_ptr, char c, unsigned flags);

// 統一マッチングエンジン
static bool match_engine(const char *pattern, const char *text, 
                         unsigned flags, const precompiled_hints_t *hints) {
    stream_state_t state = {
        .p = pattern,
        .t = text,
        .star_p = NULL,
        .star_t = NULL,
        .flags = flags,
        .hints = hints
    };
    
    // === Fast Path: ヒントベースの最適化 ===
    if (hints) {
        // 1. 完全リテラル
        if (hints->is_literal) {
            return strcmp(pattern, text) == 0;
        }
        
        // 2. "*" のみ
        if (hints->is_star_only) {
            return true;
        }
        
        // 3. "???" のみ
        if (hints->is_question_only) {
            return strlen(text) == hints->question_count;
        }
        
        // 4. ".*" パターン
        if (hints->is_dotstar) {
            if (flags & FNM_PERIOD) {
                return text[0] == '.';
            }
            return true;
        }
        
        // 5. 長さチェック（早期リターン）
        size_t tlen = strlen(text);
        if (tlen < hints->min_match_len) {
            return false;
        }
        if (hints->is_fixed_len && tlen != hints->max_match_len) {
            return false;
        }
        
        // 6. FNM_PERIOD早期チェック
        if ((flags & FNM_PERIOD) && hints->starts_with_dot && text[0] != '.') {
            return false;
        }
        
        // 7. リテラルプレフィックスの高速比較
        if (hints->literal_prefix_len > 0) {
            if (strncmp(state.p, state.t, hints->literal_prefix_len) != 0) {
                return false;
            }
            state.p += hints->literal_prefix_len;
            state.t += hints->literal_prefix_len;
        }
        
        // 8. リテラルサフィックスのチェック（*なし固定長）
        if (hints->is_fixed_len && hints->literal_suffix_len > 0) {
            size_t suffix_offset = hints->pattern_len - hints->literal_suffix_len;
            if (strcmp(state.t + tlen - hints->literal_suffix_len,
                       pattern + suffix_offset) != 0) {
                return false;
            }
        }
    }
    
    // === メインループ: Wildmatchスタイルバックトラック ===
    while (1) {
        // パターン終端チェック
        if (*state.p == '\0') {
            return (*state.t == '\0');
        }
        
        switch (*state.p) {
        case '*':
            // 連続する'*'をスキップ
            while (*state.p == '*') state.p++;
            
            if (*state.p == '\0') {
                return true;  // 末尾の'*'は残り全てにマッチ
            }
            
            // バックトラックポイントを記録
            state.star_p = state.p;
            state.star_t = state.t;
            break;
            
        case '?':
            if (*state.t == '\0') goto backtrack;
            if (*state.t == '/' && (flags & FNM_PATHNAME)) goto backtrack;
            if (*state.t == '.' && (flags & FNM_PERIOD) && 
                (state.t == text || (flags & FNM_PATHNAME && *(state.t - 1) == '/'))) {
                goto backtrack;
            }
            state.p++;
            state.t++;
            break;
            
        case '[':
            if (*state.t == '\0') goto backtrack;
            if (*state.t == '/' && (flags & FNM_PATHNAME)) goto backtrack;
            if (*state.t == '.' && (flags & FNM_PERIOD) && 
                (state.t == text || (flags & FNM_PATHNAME && *(state.t - 1) == '/'))) {
                goto backtrack;
            }
            
            const char *bracket_start = state.p;
            if (!match_bracket(&state.p, *state.t, flags)) {
                state.p = bracket_start;
                goto backtrack;
            }
            state.t++;
            break;
            
        case '\\':
            if (!(flags & FNM_NOESCAPE)) {
                state.p++;
                if (*state.p == '\0') goto backtrack;
            }
            // fall through
            
        default:
            // リテラル文字のマッチング
            if (*state.p != *state.t) {
                if ((flags & FNM_CASEFOLD) && 
                    tolower((unsigned char)*state.p) == tolower((unsigned char)*state.t)) {
                    // OK
                } else {
                    goto backtrack;
                }
            }
            state.p++;
            state.t++;
            break;
        }
        continue;
        
backtrack:
        if (!state.star_p) {
            return false;  // バックトラック不可
        }
        // バックトラック: テキストを1文字進める
        state.p = state.star_p;
        state.t = ++state.star_t;
    }
}
```

**タスク:**
- [ ] `match_engine()`を実装
- [ ] バックトラックロジック（wildmatchスタイル）
- [ ] `match_bracket()`を既存コードから移植
- [ ] FNM_PATHNAME, FNM_PERIOD, FNM_CASEFOLDの処理

---

### Step 1.4: 既存コードの統合（Day 7-8）

**ファイル:** `src/fnmatch.c`（変更）

```c
// 旧実装をコメントアウトまたは削除
// bool rbc_fnmatch(...) { ... }

// 新実装への切り替え
// #include "fnmatch_streaming.c"  // または関数宣言

// rbc_fnmatch()はfnmatch_streaming.cに移動済み
```

**ファイル:** `src/CMakeLists.txt`（変更）

```cmake
add_library(rbcglob
    arena.c
    brace.c
    fnmatch.c
    fnmatch_streaming.c  # 追加
    glob.c
    # matcher.c  # 削除または後で削除
    push_back_helper.c
    str_list.c
    utils.c
    walker.c
)
```

**タスク:**
- [ ] `src/fnmatch_streaming.c`をビルドに追加
- [ ] 既存の`matcher.c`との競合解決
- [ ] コンパイル確認

---

### Step 1.5: テストとデバッグ（Day 9-10）

**テスト項目:**

1. **基本パターン:**
   - `"abc"` vs `"abc"` → true
   - `"abc"` vs `"def"` → false
   - `"*"` vs `"anything"` → true
   - `"???"` vs `"abc"` → true
   - `"???"` vs `"ab"` → false

2. **ベンチマークパターン:**
   - `"*.c"` vs `"test.c"` → true
   - `"a*c"` vs `"abc"` → true
   - `"???.*"` vs `"abc.txt"` → true
   - `"*test*"` vs `"my_test_file"` → true
   - `"test_*.c"` vs `"test_main.c"` → true

3. **エッジケース:**
   - `"a*b*c"` vs `"aXbYc"` → true
   - `"[a-z]"` vs `"m"` → true
   - `"\\*"` vs `"*"` → true (FNM_NOESCAPE=0)
   - `".*"` vs `".hidden"` → true (FNM_PERIOD=0)

4. **フラグ処理:**
   - FNM_PATHNAME: `/` マッチング制限
   - FNM_PERIOD: `.` の特殊扱い
   - FNM_CASEFOLD: 大文字小文字無視

**デバッグ手法:**

```c
// src/fnmatch_streaming.c にデバッグマクロ追加
#ifdef DEBUG_MATCH
#define TRACE(fmt, ...) fprintf(stderr, "[MATCH] " fmt "\n", ##__VA_ARGS__)
#else
#define TRACE(fmt, ...)
#endif

// match_engine()内で使用
TRACE("p='%s' t='%s' star_p=%p", state.p, state.t, state.star_p);
```

**タスク:**
- [ ] テストケース作成（`tests/test_fnmatch_streaming.c`）
- [ ] 既存テストとの比較（挙動一致確認）
- [ ] デバッグ出力の追加
- [ ] Valgrindでメモリリーク確認

---

## Phase 2: ベンチマークと最適化（Week 3）

### Step 2.1: ベンチマーク実行（Day 11-12）

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
./bench_fnmatch
```

**測定項目:**
- `*.c` - 既存と同等以上を確認
- `a*c` - 改善を確認（目標: fnmatch(3)と同等以上）
- `???.*` - 大幅改善を確認
- `*test*` - 改善を確認
- `test_*.c` - 改善を確認

### Step 2.2: プロファイリングと改善（Day 13-14）

```bash
# プロファイリング
perf record ./bench_fnmatch
perf report

# ホットスポット分析
# - strlen()呼び出しが多い → キャッシュ
# - 分岐予測ミス → likely/unlikely追加
```

**最適化候補:**
1. `strlen()`のキャッシング（hintsに保存）
2. `likely()/unlikely()`マクロの追加
3. ループアンローリング（短いパターン）

---

## Phase 3: Phase 2最適化（Week 4-5、オプション）

### 追加最適化ヒント

```c
typedef struct {
    // Phase 1の全て +
    
    // === 文字クラス最適化 ===
    uint64_t charset_bitmap[4];  // [a-z]用ビットマップ
    
    // === Boyer-Moore ===
    uint8_t bad_char_table[256];
} precompiled_hints_t;
```

---

## 実装チェックリスト

### Week 1
- [ ] Day 1: 構造体定義とスタブ作成
- [ ] Day 2-3: ヒント生成関数の実装
- [ ] Day 4-6: 統一マッチングエンジンの実装
- [ ] Day 7-8: 既存コードの統合
- [ ] Day 9-10: テストとデバッグ

### Week 2
- [ ] Day 11-12: ベンチマーク実行と分析
- [ ] Day 13-14: プロファイリングと最適化

### Week 3-5（オプション）
- [ ] Phase 2最適化ヒントの実装
- [ ] ビットマップ、Boyer-Moore等

---

## 期待される結果

**Phase 1完了時:**
- rbc_fnmatch: fnmatch(3)と同等以上の速度
- rbc_xfnmatch: 既存の10〜40倍を維持
- `a*c`, `???.*`等が大幅改善（5〜10倍）
- メモリアロケーション: 0バイト（スタックのみ）
- 挙動: fnmatch/xfnmatch/globで完全一致

**Phase 2完了時:**
- さらに2〜3倍の改善（文字クラス、長いリテラル等）

---

## 最初に取り組むべきこと

1. **`src/fnmatch_streaming.c`の新規作成** → Step 1.1
2. **`generate_hints()`の実装** → Step 1.2
3. **`match_engine()`の実装** → Step 1.3

順番に進めてください。各ステップで動作確認しながら進めます。
