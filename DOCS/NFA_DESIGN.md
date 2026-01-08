# Segment-Based NFA Engine Design

## 1. Overview
This document outlines the architecture for the enhanced "Segment-Based NFA" engine.
This evolution moves from a character-based NFA to a **path-segment-based NFA**, significantly improving performance by reducing graph size and enabling powerful optimizations like literal prefix/suffix filtering inside wildcards.

## 2. Motivation
The initial character-based NFA successfully replicated Ruby's logic but had performance overheads:
- **Optimization Difficulty**: Optimizing `*.c` (suffix check) was hard because `*` and `.` and `c` were separate nodes.
- **Overhead**: Traversing the graph for every single character in every filename is CPU intensive.

The **Segment-Based Approach** treats a whole path component (e.g., `src`, `*.c`, `**`) as a single unit of work, allowing hybrid execution: "Graph Traversal for Directories, Native String operations for Filenames".

## 3. Architecture

### 3.1 Segment Graph (AST)
The compiler parses the glob pattern into a graph of "Segments".
A Segment corresponds to one level of directory depth or a control structure.

```c
typedef enum {
    SEG_LITERAL,   // Exact match: "src", "include"
    SEG_WILDCARD,  // Glob match: "*.c", "test_??"
    SEG_RECURSIVE, // Recursive match: "**"
    SEG_BRANCH,    // Brace expansion control: "{...}"
} seg_type_t;

struct rbcglob_segment_t {
    seg_type_t type;
    
    union {
        // SEG_LITERAL
        char *literal_path; 

        // SEG_WILDCARD
        struct {
            char *raw_pattern;
            
            // --- Optimization Flags (The "Fast Path") ---
            char *must_start;    // e.g., "test_" for "test_*.c"
            size_t start_len;
            char *must_end;      // e.g., ".c" for "*.c"
            size_t end_len;
            
            // --- Detailed Matching (The "Slow Path") ---
            // A mini, character-based NFA dedicated ONLY to matching 
            // the name string against the generalized pattern.
            // Directory operations (opendir) logic is NOT included here.
            rbcglob_node_t *local_nfa_root; 
        } glob;

        // SEG_BRANCH
        struct {
            struct rbcglob_segment_t *branches; // Linked list of alternatives
        } branch;
    };

    struct rbcglob_segment_t *next; // Next segment in the path
};
```

### 3.2 Compiler Strategy
The compiler logic splits the input pattern by `/`.

**Example**: `src/{a,b}/test_*.c`

1. **Segment 1**: `SEG_LITERAL` ("src")
   - Next -> Branch

2. **Segment 2**: `SEG_BRANCH`
   - Alt 1: `SEG_LITERAL` ("a") -> Next -> Wildcard
   - Alt 2: `SEG_LITERAL` ("b") -> Next -> Wildcard

3. **Segment 3**: `SEG_WILDCARD` ("test_*.c")
   - `must_start`: "test_"
   - `must_end`: ".c"
   - `local_nfa`: (Compiled NFA for `test_*.c`)

### 3.3 Executor Strategy
The executor functions as a hybrid VM.

#### Phase 1: Graph Traversal (Directory Navigation)
- Follows `SEG_LITERAL` nodes by checking `stat()`. Efficiently skips `opendir`.
- Handles `SEG_BRANCH` by recursively exploring paths (Depth First).
- Handles `SEG_RECURSIVE` (`**`) by invoking the standard recursive directory walker.

#### Phase 2: Directory Enumeration & Filtering (Leaf Processing)
When the executor hits a `SEG_WILDCARD`:
1. **Open Directory**: `opendir()`
2. **Read Loop**: `readdir()`
3. **Fast Filter**: 
   - Check `strncmp(name, seg->must_start)`
   - Check `suffix(name, seg->must_end)`
   - **Performance Win**: This eliminates 99% of candidates using cheap CPU instructions.
4. **Detailed Match**:
   - If filters pass, execute the `local_nfa` (or a helper function for simpler globs).
5. **Next Step**:
   - If matched, construct path and recurse to `seg->next`.

## 4. Advantages over Previous Design

| Feature | Old Character-NFA | New Segment-NFA |
| :--- | :--- | :--- |
| **Graph Size** | Huge (Nodes per Char) | Tiny (Nodes per Directory) |
| **Prefix Opt** | Possible (implemented) | Trivial & Native |
| **Suffix Opt** | Very Hard | Trivial |
| **Filename Check**| Cycle-heavy Graph Walk | Native String Ops |
| **Memory** | High (State Set allocation) | Low (Stack recursion) |

## 5. Implementation Roadmap
1. **Refactor Compiler**: Update `compiler.c` to generate `rbcglob_segment_t` structures.
2. **Implement Local NFA**: Re-purpose the existing `compiler.c` logic to generate "fragment NFAs" for the `SEG_WILDCARD` nodes.
3. **Rewrite Executor**: Replace `executor.c` with the new segment-walking logic.
