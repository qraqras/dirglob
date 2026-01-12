#ifndef RBC_INTERNAL_H
#define RBC_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "arena.h"
#include "utils.h"

/// @brief Segment Types
typedef enum rbc_segment_type_e
{
    RBC_SEGMENT_LITERAL,   // Literal segment (`/abc/`)
    RBC_SEGMENT_WILDCARD,  // Wildcard segment (`/a*c/`)
    RBC_SEGMENT_RECURSIVE, // Recursive wildcard segment (`/**/`)
    RBC_SEGMENT_BRANCH,    // Branch segment (`/{a, *, c}/`)
} rbc_segment_type_t;

/// @brief Match Strategy Types
typedef enum rbc_match_strategy_e
{
    RBC_STRATEGY_EXACT,         // Literal exact match (`abc`)
    RBC_STRATEGY_PREFIX,        // Literal prefix match (`abc*`)
    RBC_STRATEGY_SUFFIX,        // Literal suffix match (`*abc`)
    RBC_STRATEGY_INFIX,         // Literal infix match (`*abc*`)
    RBC_STRATEGY_PATTERN_CHAIN, // Sequence of fixed-length patterns separated by '*' (`a?b*c`)
    RBC_STRATEGY_RECURSIVE,     // Complex match with recursion (`[a-c]*`)
} rbc_match_strategy_t;

/// @brief Context Structure
typedef struct rbc_ctx_s
{
    rbc_arena_t arena;
    size_t discovery_counter;
} rbc_ctx_t;

/// @brief Results Structure
typedef struct rbc_results_s
{
    char **items;
    size_t *lengths;
    size_t *discovery_indices;
    size_t count;
    size_t capacity;
    rbc_ctx_t *ctx; // Link back to context for arena access
} rbc_results_t;

/// @brief Matcher Structure
typedef struct rbc_matcher_s
{
    rbc_match_strategy_t strategy;
    union
    {
        // STRATEGY_EXACT | STRATEGY_PREFIX | STRATEGY_SUFFIX | STRATEGY_INFIX
        struct
        {
            char *ptr;
            size_t len;
        } str;
        // STRATEGY_PATTERN_CHAIN
        struct
        {
            char **parts;     // Array of parts (may contain '?')
            size_t count;     // Number of parts
            bool match_start; // If true, first part must match at start
            bool match_end;   // If true, last part must match at end
        } chain;
        // STRATEGY_RECURSIVE
        struct
        {
            char *pattern;
        } recursive;
    } pk;
} rbc_matcher_t;

/// @defgroup Segment Structure
/// @{
typedef struct rbc_segment_s rbc_segment_t;
struct rbc_segment_s
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
/// @}

/// @brief Callback for match results
typedef void (*rbc_match_callback_t)(const char *path, void *user_data);

/// @name String List Utilities
/// @{

/// @brief Dynamic String List (Internal)
typedef struct rbc_str_list_s
{
    char **items;
    size_t count;
    size_t capacity;
    rbc_arena_t *arena;
} rbc_str_list_t;

void rbc_str_list_init(rbc_str_list_t *list, size_t initial_cap, rbc_arena_t *arena);
void rbc_str_list_add(rbc_str_list_t *list, const char *str);
void rbc_str_list_free(rbc_str_list_t *list);

/// @}

/// @defgroup Brace Expansion Functions
/// @{

/// @brief Callback for brace expansion visitor
typedef void (*rbc_brace_visit_cb)(const char *expanded_pattern, void *arg);

void rbc_brace_visit(const char *pattern, rbc_arena_t *arena, rbc_brace_visit_cb cb, void *arg);
rbc_str_list_t rbc_brace_collect(const char *pattern, rbc_arena_t *arena);

/// @}

/// @defgroup Context Functions
/// @{
void rbc_ctx_init(rbc_ctx_t *ctx);
void rbc_ctx_free(rbc_ctx_t *ctx);
/// @}

/// @defgroup Results Functions
/// @{
void rbc_results_init(rbc_results_t *results, rbc_ctx_t *ctx);
int rbc_results_add(rbc_results_t *results, const char *path);
int rbc_results_add_with_index(rbc_results_t *results, const char *path, size_t index);
void rbc_results_sort(rbc_results_t *results);
void rbc_results_deduplicate(rbc_results_t *results);
void rbc_results_clear(rbc_results_t *results);
/// @}

/// @defgroup Matcher Functions
/// @{
void rbc_matcher_build(rbc_arena_t *arena, rbc_matcher_t *m, const char *pattern, unsigned int flags);
bool rbc_matcher_exec(const rbc_matcher_t *m, const char *name, unsigned int flags);
/// @}

/// @defgroup Segment Functions
/// @{
rbc_segment_t *rbc_compile_segments(rbc_arena_t *arena, const char *pattern, unsigned int flags);
void rbc_segments_exec(rbc_segment_t *root, const char *base_path, unsigned flags, bool sort, rbc_match_callback_t callback, void *user_data);
/// @}

#endif /* RBC_INTERNAL_H */
