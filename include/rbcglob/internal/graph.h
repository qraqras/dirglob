#ifndef RBCGLOB_INTERNAL_GRAPH_H
#define RBCGLOB_INTERNAL_GRAPH_H

#include <stddef.h>
#include <stdbool.h>
#include "rbcglob/internal/arena.h"

/**
 * @brief NFA OpCodes for Glob matching
 */
typedef enum
{
    OP_MATCH_LITERAL, // Exact string match (e.g., "src")
    OP_MATCH_SEP,     // Directory separator "/"
    OP_MATCH_DOT,     // Current directory "."
    OP_MATCH_DOTDOT,  // Parent directory ".."
    OP_MATCH_STAR,    // Wildcard "*" (readdir within current dir)
    OP_MATCH_STAR2,   // Recursive wildcard "**"
    OP_MATCH_QMARK,   // Single char match "?"
    OP_MATCH_CLASS,   // Character class "[...]"
    OP_FORK,          // Branching point for brace expansion
    OP_JUMP,          // Control flow jump (merge paths)
    OP_ACCEPT,        // Successful match marker
    OP_EOS            // End of string/segment (internal use)
} rbcglob_opcode_type_t;

/**
 * @brief Graph Node
 */
typedef struct rbcglob_node_t
{
    rbcglob_opcode_type_t type;
    union
    {
        char *literal; // For OP_MATCH_LITERAL (owned string)
        struct
        {                                // For OP_FORK
            struct rbcglob_node_t *next; // First branch (e.g., "a")
            struct rbcglob_node_t *alt;  // Next alternative (e.g., "b")
        } branch;
        struct
        {                          // For OP_MATCH_CLASS
            unsigned char map[32]; // 256 bits bitmap for ASCII/byte-matching
            bool is_negated;       // True if [^...] or [!...]
        } char_class;
    } data;
    struct rbcglob_node_t *next; // Standard next node pointer (success transition)
} rbcglob_node_t;

/**
 * @brief Segment Types for Path-Based NFA
 */
typedef enum
{
    SEG_LITERAL,   // Exact match: "src", "include"
    SEG_WILDCARD,  // Glob match: "*.c", "test_??"
    SEG_RECURSIVE, // Recursive match: "**"
    SEG_BRANCH,    // Brace expansion control: "{...}"
} rbcglob_seg_type_t;

/**
 * @brief Segment Node
 */
typedef struct rbcglob_segment_t rbcglob_segment_t;
struct rbcglob_segment_t
{
    rbcglob_seg_type_t type;
    union
    {
        // SEG_LITERAL
        char *literal;

        // SEG_WILDCARD
        struct
        {
            char *original_pattern;

            // --- Optimization Flags ---
            char *must_start;
            size_t start_len;
            char *must_end;
            size_t end_len;

            // --- Detailed Matching ---
            // A mini, character-based NFA dedicated ONLY to matching
            // the name string against the generalized pattern.
            rbcglob_node_t *local_nfa_root;
        } glob;

        // SEG_BRANCH
        struct
        {
            rbcglob_segment_t *head; // First alternative
        } branch;
    } data;
    rbcglob_segment_t *next;     // Next segment in sequence
    rbcglob_segment_t *next_alt; // Next alternative (for siblings in a branch list)
};

rbcglob_segment_t *rbcglob_segment_new(rbcglob_arena_t *arena, rbcglob_seg_type_t type);

/**
 * @brief Create a new graph node
 * @param arena The arena to allocate memory from
 * @param type The OpCode type
 */
rbcglob_node_t *rbcglob_graph_new_node(rbcglob_arena_t *arena, rbcglob_opcode_type_t type);

/**
 * @brief Compile a pattern into a segment graph
 * @param arena The arena for memory allocation
 * @param pattern The glob pattern string
 * @return The start node of the segment graph
 */
rbcglob_segment_t *rbcglob_compile_segments(rbcglob_arena_t *arena, const char *pattern);

/**
 * @brief Compile a local NFA fragment (internal use for SEG_WILDCARD)
 * @param arena The arena for memory allocation
 * @param pattern The glob pattern string
 * @return The start node of the NFA graph
 */
rbcglob_node_t *rbcglob_compile_nfa_fragment(rbcglob_arena_t *arena, const char *pattern);

/**
 * @brief Callback for matches
 */
typedef void (*rbcglob_match_callback_t)(const char *path, void *user_data);

/**
 * @brief Execute the Segment Graph
 * @param root The start segment
 * @param base_path The base directory to start from (can be NULL or empty for current dir)
 * @param callback Function to call when a match is found
 * @param user_data User data passed to callback
 */
void rbcglob_execute_segments(
    rbcglob_segment_t *root,
    const char *base_path,
    unsigned flags,
    bool sort,
    rbcglob_match_callback_t callback,
    void *user_data);

/**
 * @brief Dump graph to stdout
 */
void rbcglob_graph_dump(const rbcglob_node_t *node);

#endif /* RBCGLOB_INTERNAL_GRAPH_H */
