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
    OP_MATCH_STAR,    // Wildcard "*" (readdir within current dir)
    OP_MATCH_STAR2,   // Recursive wildcard "**" (kept for fnmatch NFA)
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
 * @brief Match Strategy Types
 */
typedef enum
{
    STRATEGY_EXACT,    // "literal"
    STRATEGY_PREFIX,   // "prefix*"
    STRATEGY_SUFFIX,   // "*suffix"
    STRATEGY_INFIX,    // "*infix*"
    STRATEGY_SEQUENCE, // "foo*bar*baz" (only stars)
    STRATEGY_NFA       // Complex pattern
} rbcglob_match_strategy_t;

/**
 * @brief Matcher Data
 */
typedef struct
{
    rbcglob_match_strategy_t strategy;
    union
    {
        // STRATEGY_EXACT
        char *literal;

        // STRATEGY_PREFIX, STRATEGY_SUFFIX, STRATEGY_INFIX
        struct
        {
            char *pattern;
            size_t len;
        } affix;

        // STRATEGY_SEQUENCE
        struct
        {
            char **parts;     // Array of literal parts
            size_t count;     // Number of parts
            bool match_start; // If true, first part must match at start
            bool match_end;   // If true, last part must match at end
        } seq;

        // STRATEGY_NFA
        struct
        {
            rbcglob_node_t *root;
            char *must_start; // Optimization (Prefix Trim)
            size_t start_len;
            char *must_end; // Optimization (Suffix Trim)
            size_t end_len;
            char **required_literals; // Optimization (Infix Pre-check)
            size_t req_count;
        } nfa;
    } pk;
} rbcglob_matcher_t;

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
            rbcglob_matcher_t matcher;
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
