#ifndef RBCGLOB_INTERNAL_PATTERN_H
#define RBCGLOB_INTERNAL_PATTERN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "rbcglob/internal/arena.h"

/**
 * @brief VM OpCodes for Glob matching (Backtracking)
 */
typedef enum
{
    OP_MATCH_LITERAL, // Exact string match
    OP_MATCH_STAR,    // Wildcard "*"
    OP_MATCH_STAR2,   // Recursive wildcard "**" (for fnmatch VM)
    OP_MATCH_QMARK,   // Single char match "?"
    OP_MATCH_CLASS,   // Character class "[...]"
    OP_END            // End of program
} rbcglob_opcode_type_t;

typedef struct
{
    uint32_t min;
    uint32_t max;
} rbcglob_range_t;

/**
 * @brief VM Instruction (Deprecated/Removed)
 */
// typedef struct rbcglob_instruction_t ...

// Removed VM Program struct
// typedef struct rbcglob_program_t ...

/**
 * @brief Segment Types for Path-Based Pattern
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
    STRATEGY_VM        // Complex pattern (VM approach)
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

        // STRATEGY_VM
        struct
        {
            char *pattern;
        } vm;
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
 * @brief Compile a pattern into a segment chain
 * @param arena The arena for memory allocation
 * @param pattern The glob pattern string
 * @return The start node of the segment chain
 */
rbcglob_segment_t *rbcglob_compile_segments(rbcglob_arena_t *arena, const char *pattern);

/**
 * @brief Callback for matches
 */
typedef void (*rbcglob_match_callback_t)(const char *path, void *user_data);

/**
 * @brief Execute the Segment Chain
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
 * @brief Execute VM matching
 */
bool rbcglob_vm_match(const char *text, const char *pattern, unsigned int flags);

#endif /* RBCGLOB_INTERNAL_PATTERN_H */
