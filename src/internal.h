#ifndef RBC_INTERNAL_H
#define RBC_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "arena.h"
#include "utils.h"
#include "rbc/rbc.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// ============================================================================
// Fnmatch Streaming API (separate from arena-based fnmatch)
// ============================================================================

// Forward declaration
typedef struct rbc_fnmatch_pattern_streaming_s rbc_fnmatch_pattern_streaming_t;

// Streaming API functions
bool rbc_fnmatch_streaming(const char *pattern, const char *text, unsigned flags);
rbc_fnmatch_pattern_streaming_t *rbc_fnmatch_compile_streaming(const char *pattern, unsigned flags);
bool rbc_xfnmatch_streaming(const rbc_fnmatch_pattern_streaming_t *p, const char *text);
void rbc_fnmatch_pattern_free_streaming(rbc_fnmatch_pattern_streaming_t *p);

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
    RBC_STRATEGY_ALTERNATIVES,  // Multiple matchers (OR condition) from brace expansion
    RBC_STRATEGY_RECURSIVE,     // Complex match with recursion (`[a-c]*`)
} rbc_match_strategy_t;

/// @brief Forward declaration of matcher structure
typedef struct rbc_matcher_s rbc_matcher_t;

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

/// @brief Pre-filter for fast early rejection
typedef struct rbc_prefilter_s
{
    bool enabled;
    size_t min_length; // Minimum required length
    char *prefix;      // Required prefix (NULL if none)
    size_t prefix_len;
    char *suffix; // Required suffix (NULL if none)
    size_t suffix_len;
} rbc_prefilter_t;

/// @brief Matcher Structure
struct rbc_matcher_s
{
    rbc_match_strategy_t strategy;
    unsigned int flags;        // Flags used for compilation and matching
    rbc_prefilter_t prefilter; // Fast pre-filter for early rejection
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
            size_t *lengths;  // Cached lengths of each part
            size_t count;     // Number of parts
            bool match_start; // If true, first part must match at start
            bool match_end;   // If true, last part must match at end
        } chain;
        // STRATEGY_ALTERNATIVES (from brace expansion)
        struct
        {
            rbc_matcher_t *matchers; // Array of matchers
            size_t count;            // Number of alternatives
        } alternatives;
        // STRATEGY_RECURSIVE
        struct
        {
            char *pattern;
        } recursive;
    } pk;
};

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

bool rbc_walker_run(const char *pattern, rbc_walker_ctx_t *ctx);
bool rbc_walker_run_compiled(const rbc_glob_pattern_t *cg, rbc_walker_ctx_t *ctx);

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

bool rbc_str_list_init(rbc_str_list_t *list, size_t initial_cap, rbc_arena_t *arena);
bool rbc_str_list_add(rbc_str_list_t *list, const char *str);
void rbc_str_list_free(rbc_str_list_t *list);

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

/// @defgroup Matcher Functions
/// @{
bool rbc_matcher_build(rbc_arena_t *arena, rbc_matcher_t *m, const char *pattern, unsigned int flags);
bool rbc_matcher_exec(const rbc_matcher_t *m, const char *name);
/// @}

/// @defgroup Segment Functions
/// @{
rbc_segment_t *rbc_glob_segment_compile(rbc_arena_t *arena, const char *pattern, unsigned int flags);
void rbc_segment_exec(rbc_segment_t *root, const char *base_path, unsigned flags, bool sort, rbc_match_callback_t callback, void *user_data, rbc_arena_t *arena);
/// @}

#endif /* RBC_INTERNAL_H */
