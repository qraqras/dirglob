#ifndef DIRGLOB_INTERNAL_COMPILER_H
#define DIRGLOB_INTERNAL_COMPILER_H

#include <rbcglob/rbcglob.h>
#include <stdbool.h>

/**
 * @brief Token types for matching within a segment (inspired by Rust's glob crate)
 */
typedef enum
{
    TOKEN_CHAR,         /* `a` */
    TOKEN_ANY_CHAR,     /* `?` */
    TOKEN_ANY_SEQUENCE, /* `*` */
    TOKEN_ANY_WITHIN,   /* `[abc]` */
    TOKEN_ANY_EXCEPT    /* `[^abc]` or `[!abc]` */
} glob_token_type_t;

/**
 * @brief Character range for `[a-z]` style patterns
 */
typedef struct
{
    char start; /* start of range like `a` */
    char end;   /* end of range like `z` */
} glob_range_t;

/**
 * @brief A single token within a segment's pattern
 *
 * Note: For future extensions requiring complex character classes (e.g., intersection),
 * add a new token type (TOKEN_CHAR_CLASS_COMPLEX) with extended data structure.
 */
typedef struct
{
    glob_token_type_t type; /* Type */
    char c;                 /* Character for TOKEN_CHAR */
    glob_range_t *ranges;   /* Ranges for TOKEN_ANY_WITHIN or TOKEN_ANY_EXCEPT (mutually exclusive, owned, must be freed) */
    size_t range_count;     /* Number of ranges */
} glob_token_t;

/**
 * @brief Segment types for the glob execution engine
 */
typedef enum
{
    SEGMENT_LITERAL,   /* Exact match segment (e.g., "src") */
    SEGMENT_WILDCARD,  /* Segment with metachars (e.g., "test_*.c") */
    SEGMENT_RECURSIVE, /* ** Recursive segment */
    SEGMENT_END        /* End of program indicator */
} glob_segment_type_t;

/**
 * @brief Metadata for a single segment
 */
typedef struct
{
    glob_segment_type_t type;
    char *pattern;        /* Raw segment string for debugging/fallback */
    glob_token_t *tokens; /* Precompiled tokens for fast matching */
    size_t token_count;   /* */
    char *prefix;         /* Literal prefix for optimization (e.g., "test_" in "test_*.c") */
    size_t prefix_len;    /* */
    char *suffix;         /* Literal suffix for optimization (e.g., ".c" in "test_*.c") */
    size_t suffix_len;    /* */
} glob_segment_t;

/**
 * @brief Sort order for glob results
 */
typedef enum
{
    GLOB_SORT_NONE = 0,     /* Filesystem order (discovery order) */
    GLOB_SORT_ASCENDING = 1 /* Lexicographic ascending order */
} glob_sort_order_t;

/**
 * @brief Top-level structure for a compiled glob pattern
 */
typedef struct
{
    glob_segment_t *segments;     /* Array of segments */
    size_t count;                 /* Number of segments */
    unsigned flags;               /* FNM_* flags used during compilation */
    bool is_absolute;             /* Whether the pattern is absolute */
    bool has_trailing_slash;      /* Whether pattern ends with / (last segment must be directory) */
    glob_sort_order_t sort_order; /* Sort order preference */
} compiled_pattern_t;

/**
 * @brief Compiled glob pattern bundle (handles brace expansion)
 */
typedef struct
{
    compiled_pattern_t **patterns; /* Array of compiled patterns from brace expansion */
    size_t pattern_count;          /* Number of patterns (1 if no braces) */
} compiled_glob_t;

/**
 * @brief Compile a glob pattern into instructions
 *
 * @param pattern The raw pattern string (must not be NULL)
 * @param flags FNM_* flags
 * @return compiled_pattern_t* Pointer to compiled pattern, or NULL on error.
 *                             On error, errno is set:
 *                             - EINVAL: invalid pattern syntax
 *                             - ENOMEM: out of memory
 */
compiled_pattern_t *rbcglob_compile(const char *pattern, unsigned flags);

/**
 * @brief Free a compiled pattern and all its resources
 *
 * @param cp Compiled pattern to free (NULL is safe to pass)
 */
void rbcglob_compiled_free(compiled_pattern_t *cp);

/**
 * @brief Compile a glob pattern with brace expansion support
 *
 * Expands braces (e.g., "*.{c,h}" → ["*.c", "*.h"]) and compiles each pattern.
 * Result can be reused for multiple executions.
 *
 * @param pattern Pattern string (may contain braces like "*.{c,h}")
 * @param flags FNM_* flags
 * @return compiled_glob_t* Pointer to compiled glob bundle, or NULL on error.
 *                          On error, errno is set (EINVAL, ENOMEM)
 */
compiled_glob_t *rbcglob_compile_glob(const char *pattern, unsigned flags);

/**
 * @brief Free a compiled glob bundle and all its resources
 *
 * @param cg Compiled glob to free (NULL is safe to pass)
 */
void rbcglob_compiled_glob_free(compiled_glob_t *cg);

#endif /* DIRGLOB_INTERNAL_COMPILER_H */
