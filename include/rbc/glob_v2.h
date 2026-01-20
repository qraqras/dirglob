/**
 * @file glob_v2.h
 * @brief glob v2 API - Hint-based optimized glob implementation
 *
 * Design Philosophy:
 * - Hint-driven approach (same as fnmatch)
 * - Minimal overhead for simple patterns (0-100ns)
 * - Maximum I/O reduction for complex patterns
 * - Consistent with fnmatch architecture
 */

#ifndef RBC_GLOB_V2_H
#define RBC_GLOB_V2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ========================================================================
     * Hint Types
     * ======================================================================== */

    /**
     * @brief Pattern complexity types
     *
     * These determine which execution path to use:
     * - LITERAL: No wildcards, direct stat()
     * - SIMPLE_PATTERN: Single segment, use v1
     * - MULTI_SEGMENT: Multiple segments, use v1
     * - BRACE_SINGLE_DIR: Brace expansion optimization
     * - BRACE_NESTED: Nested brace expansion
     * - RECURSIVE: ** pattern
     * - COMPLEX: Requires full AST
     */
    typedef enum
    {
        GLOB_HINT_LITERAL = 0,
        GLOB_HINT_SIMPLE_PATTERN,
        GLOB_HINT_MULTI_SEGMENT,
        GLOB_HINT_BRACE_SINGLE_DIR,
        GLOB_HINT_BRACE_NESTED,
        GLOB_HINT_RECURSIVE,
        GLOB_HINT_COMPLEX,
    } glob_hint_type_t;

    /**
     * @brief Pattern flags (bitfield)
     */
    typedef struct
    {
        bool has_brace : 1;
        bool has_doublestar : 1;
        bool has_wildcard : 1;
        bool has_bracket : 1;
        bool has_escape : 1;
    } glob_pattern_flags_t;

    /**
     * @brief Brace expansion information
     *
     * Stores pre-parsed brace expansion details for optimization.
     * All pointers reference the original pattern string (no copies).
     */
    typedef struct
    {
        const char *prefix; /**< Common prefix before brace */
        size_t prefix_len;
        const char *suffix; /**< Common suffix after brace */
        size_t suffix_len;

        /**
         * @brief Choice positions (max 32 alternatives)
         *
         * Example: {a,b,c} -> choices[0]={.start="a", .len=1}, etc.
         */
        struct
        {
            const char *start; /**< Start of choice in pattern */
            size_t len;        /**< Length of choice */
        } choices[32];
        int choice_count;

        /* Optimization hints */
        bool can_use_hashset; /**< 4+ choices, use hashset */
        bool all_single_char; /**< All choices are 1 char */
    } glob_brace_info_t;

    /**
     * @brief Segment information
     */
    typedef struct
    {
        const char *segments[16]; /**< Segment pointers (max 16 depth) */
        size_t lengths[16];       /**< Lengths of each segment */
        int count;
    } glob_segment_info_t;

    /**
     * @brief Cost estimation
     */
    typedef struct
    {
        size_t estimated_dirs;    /**< Estimated directory scans */
        size_t estimated_io_cost; /**< Estimated I/O cost */
    } glob_cost_info_t;

    /**
     * @brief Glob hints structure
     *
     * Lightweight pattern analysis result (stack-allocated).
     * Generated in 20-100ns by scanning pattern once.
     *
     * Similar to rbc_match_hints_t in fnmatch.
     */
    typedef struct
    {
        glob_hint_type_t type;
        glob_pattern_flags_t flags;

        int segment_count;
        int brace_depth;

        glob_brace_info_t brace_info;
        glob_segment_info_t segment_info;
        glob_cost_info_t cost;
    } rbc_glob_hints_t;

    /* ========================================================================
     * Result Types
     * ======================================================================== */

    /**
     * @brief Glob result structure
     */
    typedef struct
    {
        char **paths;           /**< Array of matched paths */
        size_t count;           /**< Number of matched paths */
        size_t capacity;        /**< Allocated capacity */
        bool single_allocation; /**< True if paths+strings in single malloc */

        /* Statistics */
        size_t dirs_scanned;    /**< Number of directories scanned */
        size_t entries_checked; /**< Number of entries checked */
    } rbc_glob_result_t;

    /* ========================================================================
     * Public API
     * ======================================================================== */

    /**
     * @brief Generate hints from a glob pattern
     *
     * Performs lightweight 1-pass analysis of the pattern.
     * Cost: 20-100ns depending on pattern length.
     *
     * @param pattern Glob pattern string
     * @return Hint structure (on stack)
     *
     * @note This function is very fast and can be called frequently.
     * @note The returned hints reference the original pattern string.
     */
    rbc_glob_hints_t rbc_glob_hints_generate(const char *pattern);

    /**
     * @brief Execute glob with v2 optimizations
     *
     * Uses hint-based execution routing:
     * - Simple patterns: Fast path (0ns overhead, v1 implementation)
     * - Brace patterns: Optimized path (20-100ns overhead, 3-10x speedup)
     * - Complex patterns: Full AST path (500-1000ns overhead, big speedup)
     *
     * @param pattern Glob pattern string
     * @param flags Glob flags (FNM_* flags)
     * @return Result structure (must be freed with rbc_glob_result_free)
     *
     * @example
     *   rbc_glob_result_t *result = rbc_glob_v2("*.txt", 0);
     *   for (size_t i = 0; i < result->count; i++) {
     *       printf("%s\n", result->paths[i]);
     *   }
     *   rbc_glob_result_free(result);
     */
    rbc_glob_result_t *rbc_glob_v2(const char *pattern, int flags);

    /**
     * @brief Execute glob using pre-generated hints
     *
     * Useful when you want to generate hints once and reuse them.
     *
     * @param hints Pre-generated hints
     * @param pattern Original pattern string
     * @param flags Glob flags
     * @return Result structure
     */
    rbc_glob_result_t *rbc_glob_exec_with_hints(
        const rbc_glob_hints_t *hints,
        const char *pattern,
        int flags);

    /**
     * @brief Free glob result
     *
     * @param result Result to free
     */
    void rbc_glob_result_free(rbc_glob_result_t *result);

    /**
     * @brief Execute multiple glob patterns efficiently
     *
     * Merges patterns that access the same directories.
     *
     * @param patterns Array of pattern strings
     * @param count Number of patterns
     * @param flags Glob flags
     * @return Merged result structure
     */
    rbc_glob_result_t *rbc_glob_multi_v2(
        const char **patterns,
        size_t count,
        int flags);

    /* ========================================================================
     * Debug / Testing API
     * ======================================================================== */

    /**
     * @brief Get hint type name (for debugging)
     */
    const char *rbc_glob_hint_type_name(glob_hint_type_t type);

    /**
     * @brief Print hint information (for debugging)
     */
    void rbc_glob_hints_dump(const rbc_glob_hints_t *hints);

    /**
     * @brief Execute brace expansion optimization (internal use)
     */
    rbc_glob_result_t *rbc_glob_exec_brace_optimized(
        const rbc_glob_hints_t *hints,
        const char *pattern,
        int flags);

    /**
     * @brief Execute recursive pattern (**) optimization (internal use)
     */
    rbc_glob_result_t *rbc_glob_exec_recursive_optimized(
        const rbc_glob_hints_t *hints,
        const char *pattern,
        int flags);

    /**
     * @brief Execute simple pattern optimization (*.c, etc.) (internal use)
     */
    rbc_glob_result_t *rbc_glob_exec_simple_optimized(
        const rbc_glob_hints_t *hints,
        const char *pattern,
        int flags);

    /**
     * @brief Execute multi-segment pattern optimization (src/*.c, etc.) (internal use)
     */
    rbc_glob_result_t *rbc_glob_exec_multi_segment_optimized(
        const rbc_glob_hints_t *hints,
        const char *pattern,
        int flags);

    /**
     * @brief Execute multi-pattern optimization (internal use)
     */
    rbc_glob_result_t *rbc_glob_multi_v2_optimized(
        const char **patterns,
        size_t count,
        int flags);

#ifdef __cplusplus
}
#endif

#endif /* RBC_GLOB_V2_H */
