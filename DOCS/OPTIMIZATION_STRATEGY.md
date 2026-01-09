# Optimized Glob Matching Strategy Design

This document outlines the architecture for the `rbcglob` matching engine. The goal is to achieve maximum performance by dynamically selecting the most efficient matching strategy based on pattern complexity.

## Core Philosophy

- **Specialization over Generalization**: Instead of using a single generic NFA for all patterns, use specialized C functions (e.g., `strcmp`, `strstr`) for common simple cases.
- **Lazy Compilation**: Only compile expensive NFA graphs when absolutely necessary (complex patterns with `?`, `[]`, `!`, or nested braces).
- **Zero Allocation on Hot Paths**: Simple strategies should require no per-match memory allocation, utilizing stack buffers or existing pointers.

## Strategy Hierarchy

The matcher parses each path segment (component) and assigns one of the following strategies, ordered from fastest to most capable.

| ID | Strategy Name | Description | Example Pattern | Implementation | Complexity |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | **EXACT** | Exact string match. | `file.txt` | `strcmp` | O(1) * k |
| 2 | **PREFIX** | Forward match only. | `img_*` | `strncmp` | O(1) * k |
| 3 | **SUFFIX** | Backward match only. | `*.c` | `strcmp` (tail) | O(1) * k |
| 4 | **INFIX** | Substring exists. | `*search*` | `strstr` | O(N) |
| 5 | **SEQUENCE** | Multiple simple wildcards. | `foo*bar*baz` | Loop `strstr` | O(N) |
| 6 | **NFA** | Complex patterns (Regex-like). | `f?o`, `[a-z]*`, `!(*.o)` | Automaton | O(N) |

### Strategy Details

#### 1. EXACT (`STRATEGY_EXACT`)
- **Criteria**: No wildcards (`*`, `?`, `[`, `{`, `(`, `|`).
- **Logic**: Directly compare entry name with pattern.
- **Optimization**: Used for static path resolution (Fast Path).

#### 2. PREFIX (`STRATEGY_PREFIX`)
- **Criteria**: Pattern starts with literal and ends with a single `*`, containing no other special characters.
- **Logic**: `strncmp(name, pattern, len - 1) == 0`.

#### 3. SUFFIX (`STRATEGY_SUFFIX`)
- **Criteria**: Pattern starts with a single `*` and ends with literal, containing no other special characters.
- **Logic**: `strcmp(name + (name_len - suffix_len), suffix) == 0`.

#### 4. INFIX (`STRATEGY_INFIX`)
- **Criteria**: Pattern starts and ends with `*`, with a single literal component in between. No other wildcards.
- **Logic**: `strstr(name, literal) != NULL`.

#### 5. SEQUENCE (`STRATEGY_SEQUENCE`)
- **Criteria**: Pattern contains only `*` as wildcards (no `?`, `[]`, etc.). Can have multiple `*`.
- **Logic**:
    1. **Prefix Trim**: If pattern starts with literal, check `strncmp` and advance pointer.
    2. **Suffix Trim**: If pattern ends with literal, check `ends_with` and reduce length.
    3. **Pre-check**: (Optional) Check presence of rare intermediate literals using `strstr` to fail fast.
    4. **Scan**: Loop through remaining intermediate literals using `strstr` (finding next occurrence after previous match).
- **Data Structure**: Array of string parts. `t*e*x*t` -> `["t", "e", "x", "t"]`.

#### 6. NFA (`STRATEGY_NFA`)
- **Criteria**: Any pattern containing `?`, `[...]` (character class), `!`, `(`, `|`, or brace expansion results that require complex branching.
- **Data Structure**:
    - `root`: NFA graph root.
    - `must_start`: Static prefix string (for Prefix Trim).
    - `must_end`: Static suffix string (for Suffix Trim).
    - `required_literals`: List of static substrings that MUST appear (Bloom filter / Pre-check).
- **Logic**:
    1. **Prefix Trim**: `strncmp`. Advance pointer.
    2. **Suffix Trim**: `strcmp` (tail). Reduce length.
    3. **Pre-filter**: Check if all `required_literals` exist using `strstr`. If fail, return false immediately.
    4. **NFA Execution**: Run the specialized NFA engine on the remaining substring.
- **Refactoring**:
    - Remove unnecessary OpCodes (`OP_MATCH_SEP`, etc.).
    - NFA only handles within-segment logic.

## Data Structures

```c
typedef enum {
    STRATEGY_EXACT,
    STRATEGY_PREFIX,
    STRATEGY_SUFFIX,
    STRATEGY_INFIX,
    STRATEGY_SEQUENCE,
    STRATEGY_NFA
} rbcglob_match_strategy_t;

typedef struct {
    rbcglob_match_strategy_t strategy;
    union {
        // EXACT
        char *literal;

        // PREFIX / SUFFIX / INFIX
        struct {
            char *pattern;
            size_t len;
        } affix;

        // SEQUENCE
        struct {
            char **parts;
            size_t count;
            bool match_start; // true if pattern doesn't start with *
            bool match_end;   // true if pattern doesn't end with *
        } seq;

        // NFA
        struct {
            rbcglob_node_t *root;
            char *must_start; // Optimization (Prefix Trim)
            size_t start_len;
            char *must_end;   // Optimization (Suffix Trim)
            size_t end_len;
            char **required_literals; // Optimization (Infix Pre-check)
            size_t req_count;
        } nfa;
    } pk;
} rbcglob_matcher_t;

struct rbcglob_segment_t {
    // ... linked list pointers ...
    rbcglob_matcher_t matcher;
};
```

## Compilation Flow

1. **Scan**: Analyze pattern string for special characters (`*`, `?`, `[`, etc.).
2. **Strategy Selection**:
    - If none -> `EXACT`
    - If only `*`:
        - `foo*` -> `PREFIX`
        - `*bar` -> `SUFFIX`
        - `*foo*` -> `INFIX`
        - `foo*bar` -> `SEQUENCE`
    - Else -> `NFA`
3. **Build**: Populate the corresponding union field in `rbcglob_matcher_t`.

## Execution Flow (Per Segment)

When visiting a directory and iterating entries:

```c
bool match_entry(const char *name, rbcglob_matcher_t *m) {
    switch (m->strategy) {
        case STRATEGY_EXACT: return strcmp(name, m->pk.literal) == 0;
        // ... optimized C calls ...
        case STRATEGY_SEQUENCE: return match_sequence(name, m);
        case STRATEGY_NFA: return match_nfa_optimized(name, m);
    }
}
```

## `fnmatch` Integration

`rbcglob_fnmatch` will:
1. Parse the pattern to determine strategy.
2. If simple enough, use the strategy directly.
3. If complex, build and run the NFA.
4. (Since `fnmatch` is one-shot, we might skip the full `SEQUENCE` struct build and just run a parsing loop if appropriate, but reusing the compiler logic ensures consistency.)

## `Dir.glob` Integration

`rbcglob_dirglob` will:
1. Split pattern into segments.
2. Compile each segment into `rbcglob_segment_t` with its optimal strategy.
3. Loop `readdir`.
4. Call `match_entry` for each file.
