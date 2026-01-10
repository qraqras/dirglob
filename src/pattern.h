#ifndef RBCGLOB_INTERNAL_PATTERN_H
#define RBCGLOB_INTERNAL_PATTERN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "arena.h"

/**
 * @brief Segment Types
 */
typedef enum rbcg_segment_type_e
{
    RBCG_SEGMENT_LITERAL,
    RBCG_SEGMENT_WILDCARD,
    RBCG_SEGMENT_RECURSIVE,
    RBCG_SEGMENT_BRANCH,
} rbcg_segment_type_t;

/**
 * @brief Match Strategy Types
 */
typedef enum rbcg_match_strategy_e
{
    RBCG_STRATEGY_EXACT,         // Literal exact match ("abc")
    RBCG_STRATEGY_PREFIX,        // Literal prefix match ("abc*")
    RBCG_STRATEGY_SUFFIX,        // Literal suffix match ("*abc")
    RBCG_STRATEGY_INFIX,         // Literal substring match ("*abc*")
    RBCG_STRATEGY_PATTERN_CHAIN, // Sequence of fixed-length patterns separated by '*' ("a?b*c")
    RBCG_STRATEGY_FNMATCH,       // Complex match with recursion ("[a-z]*")
} rbcg_match_strategy_t;

/**
 * @brief Matcher Data
 */
typedef struct rbcg_matcher_s
{
    rbcg_match_strategy_t strategy;
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

        // STRATEGY_PATTERN_CHAIN
        struct
        {
            char **parts;     // Array of parts (may contain '?')
            size_t count;     // Number of parts
            bool match_start; // If true, first part must match at start
            bool match_end;   // If true, last part must match at end
        } chain;

        // STRATEGY_FNMATCH
        struct
        {
            char *pattern;
        } fnmatch;
    } pk;
} rbcg_matcher_t;

/**
 * @brief Segment Node
 */
typedef struct rbcglob_segment_t rbcglob_segment_t;
struct rbcglob_segment_t
{
    rbcg_segment_type_t type;
    union
    {
        // SEG_LITERAL
        char *literal;

        // SEG_WILDCARD
        struct
        {
            char *original_pattern;
            rbcg_matcher_t matcher;
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

rbcglob_segment_t *rbcglob_segment_new(rbcglob_arena_t *arena, rbcg_segment_type_t type);

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
 * @brief Execute recursive matching (formerly VM)
 */
bool rbcglob_recursive_match(const char *text, const char *pattern, unsigned int flags);

/* =========================================================================
 * Context & Results (Merged from traverse.h)
 * ========================================================================= */

/**
 * @brief Compare two paths using Ruby-style sorting rules
 *
 * Used internally by traverse module for sorting glob results
 * in Ruby-compatible order.
 *
 * @param s1 First path
 * @param s2 Second path
 * @return <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2
 */
int rbcglob_compare_paths(const char *s1, const char *s2);

/**
 * @brief Glob execution context
 */
typedef struct rbcglob_ctx_s
{
    rbcglob_arena_t arena;
    size_t discovery_counter;
} rbcglob_ctx_t;

/**
 * @brief Result collector for glob matches
 */
typedef struct rbcglob_results_s
{
    char **items;
    size_t *lengths;
    size_t *discovery_indices;
    size_t count;
    size_t capacity;
    rbcglob_ctx_t *ctx; /* Link back to context for arena access */
} rbcglob_results_t;

/**
 * @brief Initialize glob context
 */
void rbcglob_ctx_init(rbcglob_ctx_t *ctx);

/**
 * @brief Destroy glob context and free all memory (including arena)
 */
void rbcglob_ctx_free(rbcglob_ctx_t *ctx);

/**
 * @brief Initialize result collector
 */
void rbcglob_results_init(rbcglob_results_t *results, rbcglob_ctx_t *ctx);

/**
 * @brief Add a path to results (duplicates string)
 * @return 0 on success, -1 on error
 */
int rbcglob_results_add(rbcglob_results_t *results, const char *path);

/**
 * @brief Add a path to results with a specific discovery index
 * @return 0 on success, -1 on error
 */
int rbcglob_results_add_with_index(rbcglob_results_t *results, const char *path, size_t index);

/**
 * @brief Sort results alphabetically
 */
void rbcglob_results_sort(rbcglob_results_t *results);

/**
 * @brief Remove duplicate entries
 */
void rbcglob_results_deduplicate(rbcglob_results_t *results);

/**
 * @brief Free result collector (but not the items array itself)
 */
void rbcglob_results_clear(rbcglob_results_t *results);

#endif /* RBCGLOB_INTERNAL_PATTERN_H */
