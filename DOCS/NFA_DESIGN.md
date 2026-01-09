# Segment-Based NFA Engine Design

## 1. Overview
This document outlines the architecture for the enhanced "Segment-Based NFA" engine with **Full Brace Expansion** and **Hybrid Execution Strategy**.
This evolution adopts a strategy similar to `micromatch` (Node.js) and `glob(3)` (libc), optimizing purely static paths differently from dynamic wildcard paths.

## 2. Motivation
- **Performance Gap**: Complex brace patterns like `*/{file1,file2,file3}` were slower than `glob(3)` because the previous engine treated braces as generic logical branches, causing redundant Graph Traversal overhead.
- **I/O Optimization**: We want to use `stat()` (Probe) for exact paths and `opendir/readdir` (Scan) only for wildcards. `glob(3)` and `micromatch` excel at this separation.
- **Micromatch Parity**: Adopting the "Expansion -> Analysis -> Execution" pipeline allows us to statically determine the best strategy for each segment.

## 3. Architecture

### 3.1 Compilation Pipeline

The process moves from "Streaming Parse" to "Segment-Level Expansion".

#### Phase 1: Logical Segment splitting (Chunking)
The pattern is split by path separators `/`, but **top-level braces act as grouping tokens**.
- `src/{a,b}/c` -> `src`, `{a,b}`, `c`
- `src/{pre/fix,other}/end` -> `src`, `{pre/fix,other}`, `end`

#### Phase 2: Brace Expansion (Normalization)
Each chunk is **fully expanded** into a list of concrete string patterns.
- Segment `{foo,bar}` -> `["foo", "bar"]`
- Segment `img_{0..2}.jpg` -> `["img_0.jpg", "img_1.jpg", "img_2.jpg"]`
- Segment `{a/b,c}` -> `["a/b", "c"]` (Supports cross-segment braces)

#### Phase 3: Strategy Selection (Optimizer)
The compiler analyzes the expanded list to decide the node type.

**Case A: No Wildcards (The "Stat" Path)**
If NONE of the expanded strings contain `*`, `?`, `[`...
-> **Compile to `SEG_BRANCH` of `SEG_LITERAL`s.**
- This triggers the "Probe" strategy.
- Executor will run `stat()` for each path.
- **Benefit**: Faster than scanning a directory with 10,000 files just to find 2 files. Matches `glob(3)`.

**Case B: Wildcards Present (The "Scan" Path)**
If ANY string contains wildcards...
-> **Compile to Single `SEG_WILDCARD` (Integrated NFA).**
- All variations are combined into one logical OR NFA: `(variant1)|(variant2)|...`
- Executor will run `opendir/readdir` ONCE.
- **Benefit**: Avoids re-scanning the same directory multiple times for patterns like `{*.txt,*.md}`. Matches `ripgrep`/`micromatch` scan logic.

### 3.2 Data Structures

```c
typedef enum {
    SEG_LITERAL,   // Exact match (stat optimization)
    SEG_WILDCARD,  // Directory scan (readdir + NFA)
    SEG_RECURSIVE, // Recursive scan (**)
    SEG_BRANCH,    // Logical branch (try alternatives sequentially)
} seg_type_t;

struct rbcglob_segment_t {
    seg_type_t type;

    union {
        // SEG_LITERAL
        char *literal_path;

        // SEG_WILDCARD
        struct {
            // Optimization: Filter entries before running NFA
            // Calculated from the Common Prefix/Suffix of all NFA branches
            char *must_start;
            size_t start_len;
            char *must_end;
            size_t end_len;

            // The NFA Graph (merged forks)
            rbcglob_node_t *local_nfa_root;
        } glob;

        // SEG_BRANCH
        struct {
            struct rbcglob_segment_t *head; // Linked list of alternative CHAINS
        } branch;
    };

    struct rbcglob_segment_t *next;
    struct rbcglob_segment_t *next_alt; // For SEG_BRANCH alternatives
};
```

### 3.3 Execution Flow

**Example 1: `src/{a,b}` (No wildcards)**
1. Compiler expands to `["a", "b"]`.
2. Generates `SEG_LITERAL("src")` -> `SEG_BRANCH`
   - Alt 1: `SEG_LITERAL("a")`
   - Alt 2: `SEG_LITERAL("b")`
3. Executor:
   - `stat("src")` OK.
   - `stat("src/a")` ?
   - `stat("src/b")` ?
   - **Result**: No `readdir` calls. High performance.

**Example 2: `src/{*.txt,*.md}` (With wildcards)**
1. Compiler expands to `["*.txt", "*.md"]`. Detects wildcards.
2. Generates `SEG_LITERAL("src")` -> `SEG_WILDCARD`
   - NFA: `(.*\.txt)|(.*\.md)`
3. Executor:
   - `stat("src")` OK.
   - `opendir("src")`.
   - `readdir()` loop... passing names to NFA.
   - **Result**: Single scan. Efficient filtering.

**Example 3: `src/{a/b, c}` (Cross-segment)**
1. Compiler expands to `["a/b", "c"]`.
2. Generates `SEG_LITERAL("src")` -> `SEG_BRANCH`
   - Alt 1: `SEG_LITERAL("a")` -> `SEG_LITERAL("b")`
   - Alt 2: `SEG_LITERAL("c")`
3. Executor handles the structures naturally.

## 4. Advantages

1.  **Best of Both Worlds**:
    - "Stat" speed of `glob(3)` for exact paths.
    - "Scan" efficiency of `ripgrep`/`micromatch` for complex patterns.
2.  **Code Simplicity**:
    - The NFA construction becomes simpler (just root-level ORs).
    - The Compiler handles complexity (Expansion), leaving the Executor dumb and fast.
3.  **Memory Safety**:
    - Expansion is limited to segment scope, preventing `node-glob` style explosion for deep trees.

## 5. Implementation Roadmap
1.  **Utils**: Add `rbcglob_brace_expand` to `src/utils.c`.
2.  **Compiler**: Rewrite compile loop to use the "Expand -> Check -> Dispatch" strategy.
3.  **NFA Builder**: Update NFA fragment compilation to handle lists of patterns (OR-ing them).
4.  **Executor**: (Already mostly compatible) Ensure NFA engine handles the new graph shape (Root Forks).
