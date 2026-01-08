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
 * @brief Create a new graph node
 * @param arena The arena to allocate memory from
 * @param type The OpCode type
 */
rbcglob_node_t *rbcglob_graph_new_node(rbcglob_arena_t *arena, rbcglob_opcode_type_t type);

/**
 * @brief Compile a glob pattern into an NFA graph
 * @param arena The arena for memory allocation
 * @param pattern The glob pattern string
 * @return The start node of the graph
 */
rbcglob_node_t *rbcglob_nfa_compile(rbcglob_arena_t *arena, const char *pattern);

/**
 * @brief Callback for matches
 */
typedef void (*rbcglob_match_callback_t)(const char *path, void *user_data);

/**
 * @brief Execute the NFA graph
 * @param root The start node
 * @param base_path The base directory to start from (can be NULL or empty for current dir)
 * @param callback Function to call when a match is found
 * @param user_data User data passed to callback
 */
void rbcglob_nfa_execute(
    rbcglob_node_t *root,
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
