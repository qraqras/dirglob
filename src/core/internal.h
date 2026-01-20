#ifndef RBC_INTERNAL_H
#define RBC_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "../utils/arena.h"
#include "../utils/utils.h"
#include "rbc/rbc.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/// @brief Segment Types
typedef enum rbc_segment_type_e
{
    RBC_SEGMENT_LITERAL,   // Literal segment (`/abc/`)
    RBC_SEGMENT_WILDCARD,  // Wildcard segment (`/a*c/`)
    RBC_SEGMENT_RECURSIVE, // Recursive wildcard segment (`/**/`)
    RBC_SEGMENT_BRANCH,    // Branch segment (`/{a, *, c}/`)
} rbc_segment_type_t;

/// @brief Match strategy enumeration
typedef enum rbc_match_strategy_e
{
    RBC_MATCH_STRATEGY_LITERAL,       // `literal`
    RBC_MATCH_STRATEGY_STAR,          // `*`
    RBC_MATCH_STRATEGY_QUESTION,      // `??`
    RBC_MATCH_STRATEGY_PREFIX,        // `prefix*`
    RBC_MATCH_STRATEGY_SUFFIX,        // `*suffix`
    RBC_MATCH_STRATEGY_PREFIX_SUFFIX, // `prefix*suffix`
} rbc_match_strategy_t;

/// @brief Match hints structure
typedef struct rbc_match_hints_s
{
    rbc_match_strategy_t strategy; // Fast path type (1 byte)
    uint16_t pattern_len;          // Pattern length (avoids strlen)
    uint16_t prefix_len;           // Literal prefix length
    uint16_t suffix_len;           // Literal suffix length
} rbc_match_hints_t;

/// @brief Precompiled fnmatch pattern structure
struct rbc_fnmatch_pattern_s
{
    const char *pattern;     // Original pattern string
    rbc_match_hints_t hints; // Optimization hints
};

/// @brief Branch alternatives structure (for brace expansion)
typedef struct rbc_alternatives_s
{
    rbc_fnmatch_pattern_t **patterns; // Array of precompiled patterns
    size_t count;                     // Number of alternatives
} rbc_alternatives_t;

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
            rbc_fnmatch_pattern_t *compiled;  // Precompiled pattern (may be NULL if fallback needed)
            rbc_alternatives_t *alternatives; // For brace-expanded patterns (mutually exclusive with compiled)
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

/// @brief Pattern Execution Strategy Types
typedef enum rbc_pattern_type_e
{
    RBC_PATTERN_LITERAL,       // Pure literal path (no braces, no wildcards)
    RBC_PATTERN_BRACE_LITERAL, // Braces containing only literals
    RBC_PATTERN_GENERAL        // General case with wildcards
} rbc_pattern_type_t;

/// @brief Pre-compiled Glob Pattern Structure
struct rbc_glob_pattern_s
{
    rbc_ctx_t *ctx;
    rbc_segment_t *segments;
    unsigned flags;
    rbc_pattern_type_t type;
    char *original_pattern;
};

/// @brief Callback for match results
typedef void (*rbc_match_callback_t)(const char *path, void *user_data);

/// @brief Result collection context
typedef struct rbc_walker_ctx_s
{
    rbc_results_t *results;
    const char *base_strip; /* If set, strip this prefix from results */
    size_t base_len;
    rbc_ctx_t *ctx;
    const char *base;
    unsigned int flags;
    bool sort;
} rbc_walker_ctx_t;

/// @name String List Utilities
/// @{

/// @brief Fixed-size String List for brace expansion (max 64 options)
#define RBC_BRACE_MAX_OPTIONS 64

typedef struct rbc_str_list_s
{
    const char *items[RBC_BRACE_MAX_OPTIONS];
    size_t count;
} rbc_str_list_t;

/// @}

/// @defgroup Brace Expansion Functions
/// @{

/// @brief Callback for brace expansion visitor
typedef bool (*rbc_brace_visit_cb)(const char *expanded_pattern, void *arg);

bool rbc_brace_visit(const char *pattern, rbc_arena_t *arena, rbc_brace_visit_cb cb, void *arg);
rbc_str_list_t rbc_brace_collect(const char *pattern, rbc_arena_t *arena);

/// @}

/// @defgroup Context Functions
/// @{
bool rbc_glob_ctx_init(rbc_ctx_t *ctx);
void rbc_glob_ctx_free(rbc_ctx_t *ctx);
/// @}

/// @defgroup Strategy Functions
/// @{
bool rbc_has_wildcard(const char *str);
bool rbc_is_recursive_wildcard(const char *str);
bool rbc_has_brace(const char *str);
rbc_pattern_type_t rbc_analyze_pattern(const char *pattern);
/// @}

/// @defgroup Results Functions
/// @{
bool rbc_glob_results_init(rbc_results_t *results, rbc_ctx_t *ctx);
bool rbc_glob_results_add(rbc_results_t *results, const char *path);
bool rbc_glob_results_add_with_index(rbc_results_t *results, const char *path, size_t index);
void rbc_glob_results_sort(rbc_results_t *results);
void rbc_glob_results_deduplicate(rbc_results_t *results);
void rbc_glob_results_clear(rbc_results_t *results);
/// @}

/// @defgroup Segment Functions
/// @{
rbc_segment_t *rbc_glob_segment_compile(rbc_arena_t *arena, const char *pattern, unsigned int flags);
void rbc_segment_exec(rbc_segment_t *root, const char *base_path, unsigned flags, bool sort, rbc_match_callback_t callback, void *user_data, rbc_arena_t *arena);
bool rbc_segment_match(const rbc_segment_t *seg, const char *name, unsigned int flags);
/// @}

/// @defgroup Helper Functions
/// @{
rbc_alternatives_t *rbc_alternatives_compile(rbc_arena_t *arena, const char *pattern, unsigned int flags);
void rbc_alternatives_free(rbc_alternatives_t *alt, rbc_arena_t *arena);
bool rbc_alternatives_match(const rbc_alternatives_t *alt, const char *name, unsigned int flags);
/// @}

#endif /* RBC_INTERNAL_H */
