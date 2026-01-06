#ifndef DIRGLOB_INTERNAL_COMPILER_H
#define DIRGLOB_INTERNAL_COMPILER_H

#include <rbcglob/rbcglob.h>
#include <stdbool.h>

/**
 * @brief Token types for the glob execution engine.
 */
typedef enum rbcglob_token_type_e
{
    RBCGLOB_TOKEN_CHAR,         /* `t`, `o`, `k`, `e`, `n` */
    RBCGLOB_TOKEN_ANY_CHAR,     /* `?` */
    RBCGLOB_TOKEN_ANY_SEQUENCE, /* `*` */
    RBCGLOB_TOKEN_ANY_WITHIN,   /* [token]` */
    RBCGLOB_TOKEN_ANY_EXCEPT    /* [^token]`, `[!token]` */
} rbcglob_token_type_t;

/**
 * @brief Character range for ANY_WITHIN and ANY_EXCEPT tokens.
 */
typedef struct rbcglob_range_s
{
    char start; /* Start of range */
    char end;   /* End of range */
} rbcglob_range_t;

/**
 * @brief Token structure for the glob execution engine.
 */
typedef struct rbcglob_token_s
{
    rbcglob_token_type_t token_type; /* Token type */
    char c;                          /* Character for RBCGLOB_TOKEN_CHAR */
    rbcglob_range_t *ranges;         /* Ranges for RBCGLOB_TOKEN_ANY_WITHIN or RBCGLOB_TOKEN_ANY_EXCEPT */
    size_t range_count;              /* Number of ranges */
} rbcglob_token_t;

/**
 * @brief Segment types for compiled glob patterns.
 */
typedef enum rbcglob_segment_type_e
{
    RBCGLOB_SEGMENT_LITERAL,   /* `segment` */
    RBCGLOB_SEGMENT_WILDCARD,  /* `*egment` */
    RBCGLOB_SEGMENT_RECURSIVE, /* `**` */
    RBCGLOB_SEGMENT_END        /* MARKER */
} rbcglob_segment_type_t;

/**
 * @brief Segment structure for compiled glob patterns.
 */
typedef struct rbcglob_segment_s
{
    rbcglob_segment_type_t type; /* Segment type */
    char *pattern;               /* Original segment string (for debugging) */
    rbcglob_token_t *tokens;     /* Precompiled tokens for fast matching */
    size_t token_count;          /* Number of tokens */
    char *prefix;                /* Literal prefix for optimization (e.g., "test_" in "test_*.c") */
    size_t prefix_len;           /* Length of prefix */
    char *suffix;                /* Literal suffix for optimization (e.g., ".c" in "test_*.c") */
    size_t suffix_len;           /* Length of suffix */
} rbcglob_segment_t;

/**
 * @brief Sort order enumeration.
 */
typedef enum rbcglob_sort_order_e
{
    RBCGLOB_SORT_NONE,     /* Filesystem order (discovery order) */
    RBCGLOB_SORT_ASCENDING /* Lexicographic ascending order */
} rbcglob_sort_order_t;

/**
 * @brief Compiled glob pattern structure.
 */
typedef struct rbcglob_compiled_pattern_s
{
    rbcglob_segment_t *segments;     /* Array of compiled segments */
    size_t count;                    /* Number of segments */
    unsigned flags;                  /* RBCGLOB_FNM_* flags used during compilation */
    bool is_absolute;                /* Whether the pattern is absolute (starts with /) */
    bool has_trailing_slash;         /* Whether pattern ends with / (directory required) */
    rbcglob_sort_order_t sort_order; /* Sort order for results */
    /* P2 Optimization: Directory traversal pruning */
    bool has_recursive_segment;   /* Whether pattern contains ** */
    size_t leading_literal_count; /* Number of leading literal segments */
} rbcglob_compiled_pattern_t;

/**
 * @brief Compiled glob bundle with brace expansion support (full definition)
 *
 * @note Public API only sees the opaque typedef in rbcglob.h.
 *       This is the complete internal structure definition.
 */
struct rbcglob_compiled_glob_s
{
    rbcglob_compiled_pattern_t **patterns; /* Array of compiled patterns */
    size_t pattern_count;                  /* Number of patterns */
};

/**
 * @brief Compile a single glob pattern (internal API).
 *
 * @param pattern Single glob pattern (braces not expanded)
 * @param flags RBCGLOB_FNM_* flags
 * @return Pointer to compiled pattern, or NULL on error (errno set)
 *
 * @note This is an internal function used by rbcglob_compile_glob().
 *       Compiles a single pattern without brace expansion.
 *       For public API, use rbcglob_compile_glob() instead.
 */
rbcglob_compiled_pattern_t *rbcglob_compile(const char *pattern, unsigned flags);

/**
 * @brief Compile a glob pattern with brace expansion (public API declared in rbcglob.h).
 *
 * @note See rbcglob.h for full documentation.
 */
rbcglob_compiled_glob_t *rbcglob_compile_glob(const char *pattern, unsigned flags);

/**
 * @brief Free a single compiled pattern (internal API)
 *
 * @param cp Compiled pattern to free (NULL is safe to pass)
 */
void rbcglob_compiled_pattern_free(rbcglob_compiled_pattern_t *cp);

/**
 * @brief Free a compiled glob bundle (public API declared in rbcglob.h)
 *
 * @note See rbcglob.h for full documentation.
 */
void rbcglob_compiled_glob_free(rbcglob_compiled_glob_t *cg);

#endif /* DIRGLOB_INTERNAL_COMPILER_H */
