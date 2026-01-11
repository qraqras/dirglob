#ifndef RBC_INTERNAL_PATTERN_H
#define RBC_INTERNAL_PATTERN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "arena.h"

/**
 * @brief Segment Types
 */
typedef enum rbc_segment_type_e
{
    RBC_SEGMENT_LITERAL,
    RBC_SEGMENT_WILDCARD,
    RBC_SEGMENT_RECURSIVE,
    RBC_SEGMENT_BRANCH,
} rbc_segment_type_t;

/**
 * @brief Match Strategy Types
 */
typedef enum rbc_match_strategy_e
{
    RBC_STRATEGY_EXACT,         // Literal exact match ("abc")
    RBC_STRATEGY_PREFIX,        // Literal prefix match ("abc*")
    RBC_STRATEGY_SUFFIX,        // Literal suffix match ("*abc")
    RBC_STRATEGY_INFIX,         // Literal substring match ("*abc*")
    RBC_STRATEGY_PATTERN_CHAIN, // Sequence of fixed-length patterns separated by '*' ("a?b*c")
    RBC_STRATEGY_FNMATCH,       // Complex match with recursion ("[a-z]*")
} rbc_match_strategy_t;

/**
 * @brief Matcher Data
 */
typedef struct rbc_matcher_s
{
    rbc_match_strategy_t strategy;
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
} rbc_matcher_t;

/**
 * @brief Segment Node
 */
typedef struct rbc_segment_t rbc_segment_t;
struct rbc_segment_t
{
    rbc_segment_type_t type;
    union
    {
        // SEG_LITERAL
        char *literal;

        // SEG_WILDCARD
        struct
        {
            char *original_pattern;
            rbc_matcher_t matcher;
        } glob;

        // SEG_BRANCH
        struct
        {
            rbc_segment_t *head; // First alternative
        } branch;
    } data;
    rbc_segment_t *next;     // Next segment in sequence
    rbc_segment_t *next_alt; // Next alternative (for siblings in a branch list)
};

rbc_segment_t *rbc_segment_new(rbc_arena_t *arena, rbc_segment_type_t type);

/**
 * @brief Compile a pattern into a segment chain
 * @param arena The arena for memory allocation
 * @param pattern The glob pattern string
 * @return The start node of the segment chain
 */
rbc_segment_t *rbc_compile_segments(rbc_arena_t *arena, const char *pattern, unsigned int flags);

/**
 * @brief Callback for matches
 */
typedef void (*rbc_match_callback_t)(const char *path, void *user_data);

/**
 * @brief Execute the Segment Chain
 * @param root The start segment
 * @param base_path The base directory to start from (can be NULL or empty for current dir)
 * @param callback Function to call when a match is found
 * @param user_data User data passed to callback
 */
void rbc_execute_segments(
    rbc_segment_t *root,
    const char *base_path,
    unsigned flags,
    bool sort,
    rbc_match_callback_t callback,
    void *user_data);

/**
 * @brief Execute recursive matching (formerly VM)
 */
bool rbc_recursive_match(const char *text, const char *pattern, unsigned int flags);

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
int rbc_compare_paths(const char *s1, const char *s2);

/**
 * @brief Glob execution context
 */
typedef struct rbc_ctx_s
{
    rbc_arena_t arena;
    size_t discovery_counter;
} rbc_ctx_t;

/**
 * @brief Result collector for glob matches
 */
typedef struct rbc_results_s
{
    char **items;
    size_t *lengths;
    size_t *discovery_indices;
    size_t count;
    size_t capacity;
    rbc_ctx_t *ctx; /* Link back to context for arena access */
} rbc_results_t;

/**
 * @brief Initialize glob context
 */
void rbc_ctx_init(rbc_ctx_t *ctx);

/**
 * @brief Destroy glob context and free all memory (including arena)
 */
void rbc_ctx_free(rbc_ctx_t *ctx);

/**
 * @brief Initialize result collector
 */
void rbc_results_init(rbc_results_t *results, rbc_ctx_t *ctx);

/**
 * @brief Add a path to results (duplicates string)
 * @return 0 on success, -1 on error
 */
int rbc_results_add(rbc_results_t *results, const char *path);

/**
 * @brief Add a path to results with a specific discovery index
 * @return 0 on success, -1 on error
 */
int rbc_results_add_with_index(rbc_results_t *results, const char *path, size_t index);

/**
 * @brief Sort results alphabetically
 */
void rbc_results_sort(rbc_results_t *results);

/**
 * @brief Remove duplicate entries
 */
void rbc_results_deduplicate(rbc_results_t *results);

/**
 * @brief Free result collector (but not the items array itself)
 */
void rbc_results_clear(rbc_results_t *results);

void rbc_build_matcher(rbc_arena_t *arena, rbc_matcher_t *m, const char *pattern, unsigned int flags);
bool rbc_matcher_exec(const rbc_matcher_t *m, const char *name, unsigned int flags);

#endif /* RBC_INTERNAL_PATTERN_H */
