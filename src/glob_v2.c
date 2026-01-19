/**
 * @file glob_v2.c
 * @brief Main glob v2 implementation
 *
 * Hint-based execution routing with Fast/Optimized/Full paths.
 */

#include "rbc/glob_v2.h"
#include "internal.h" /* v1 implementation */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

/* Forward declarations for execution functions */
static rbc_glob_result_t *glob_exec_literal(const char *pattern);
static rbc_glob_result_t *glob_exec_simple(const char *pattern, int flags);
static rbc_glob_result_t *glob_exec_multi_segment(const char *pattern, int flags);
static rbc_glob_result_t *glob_exec_recursive(const char *pattern, int flags);
static rbc_glob_result_t *glob_exec_full_ast(const char *pattern, int flags);

/* External implementation from glob_v2_brace.c */
rbc_glob_result_t *rbc_glob_exec_brace_optimized(
    const rbc_glob_hints_t *hints,
    const char *pattern,
    int flags);

/* External implementation from glob_v2_recursive.c */
rbc_glob_result_t *rbc_glob_exec_recursive_optimized(
    const rbc_glob_hints_t *hints,
    const char *pattern,
    int flags);

/* External implementation from glob_v2_multi.c */
rbc_glob_result_t *rbc_glob_multi_v2_optimized(
    const char **patterns,
    size_t count,
    int flags);

/* Result management helpers */
static rbc_glob_result_t *create_result_set(void);
static void add_result(rbc_glob_result_t *result, const char *path);
static rbc_glob_result_t *create_empty_result(void);
static rbc_glob_result_t *create_single_result(const char *path);

/* ========================================================================
 * Main API Implementation
 * ======================================================================== */

rbc_glob_result_t *rbc_glob_v2(const char *pattern, int flags)
{
    if (!pattern)
    {
        return create_empty_result();
    }

    /* Step 1: Generate hints (20-100ns) */
    rbc_glob_hints_t hints = rbc_glob_hints_generate(pattern);

    /* Step 2: Route to appropriate execution path */
    return rbc_glob_exec_with_hints(&hints, pattern, flags);
}

rbc_glob_result_t *rbc_glob_exec_with_hints(
    const rbc_glob_hints_t *hints,
    const char *pattern,
    int flags)
{
    if (!hints || !pattern)
    {
        return create_empty_result();
    }

    /* Execution routing based on hint type */
    switch (hints->type)
    {
    case GLOB_HINT_LITERAL:
        /* Fast Path: Literal path (0ns overhead) */
        return glob_exec_literal(pattern);

    case GLOB_HINT_SIMPLE_PATTERN:
        /* Fast Path: Simple pattern (0ns overhead, v1 implementation) */
        return glob_exec_simple(pattern, flags);

    case GLOB_HINT_MULTI_SEGMENT:
        /* Fast Path: Multi-segment (0ns overhead, v1 implementation) */
        return glob_exec_multi_segment(pattern, flags);

    case GLOB_HINT_BRACE_SINGLE_DIR:
        /* Optimized Path: Brace optimization (20-100ns overhead) */
        return rbc_glob_exec_brace_optimized(hints, pattern, flags);

    case GLOB_HINT_BRACE_NESTED:
        /* Optimized Path: Nested brace (fallback to v1 for now) */
        return glob_exec_multi_segment(pattern, flags);

    case GLOB_HINT_RECURSIVE:
        /* Optimized Path: Recursive pattern */
        return rbc_glob_exec_recursive_optimized(hints, pattern, flags);

    case GLOB_HINT_COMPLEX:
        /* Full Path: Complex pattern (requires AST) */
        return glob_exec_full_ast(pattern, flags);

    default:
        return create_empty_result();
    }
}

/* ========================================================================
 * Execution Path Implementations
 * ======================================================================== */

/* Fast Path: Literal path - just stat() */
static rbc_glob_result_t *glob_exec_literal(const char *pattern)
{
    struct stat st;

    if (stat(pattern, &st) == 0)
    {
        return create_single_result(pattern);
    }

    return create_empty_result();
}

/* Helper: Convert v1 results to v2 format */
static rbc_glob_result_t *convert_v1_to_v2_results(char **v1_paths, size_t count, size_t *lengths)
{
    rbc_glob_result_t *result = create_result_set();
    if (!result)
    {
        rbc_glob_free(v1_paths, count, lengths);
        return NULL;
    }

    for (size_t i = 0; i < count; i++)
    {
        if (v1_paths[i])
        {
            add_result(result, v1_paths[i]);
        }
    }

    rbc_glob_free(v1_paths, count, lengths);
    return result;
}

/* Fast Path: Simple pattern - delegate to v1 */
static rbc_glob_result_t *glob_exec_simple(const char *pattern, int flags)
{
    /* Use v1 implementation directly */
    char **v1_paths = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    const char *patterns[] = {pattern};
    bool success = rbc_glob(patterns, 1, (unsigned)flags, NULL, true, &v1_paths, &count, &lengths);

    if (!success || !v1_paths)
    {
        return create_empty_result();
    }

    return convert_v1_to_v2_results(v1_paths, count, lengths);
}

/* Fast Path: Multi-segment - delegate to v1 */
static rbc_glob_result_t *glob_exec_multi_segment(const char *pattern, int flags)
{
    /* Use v1 implementation directly */
    char **v1_paths = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    const char *patterns[] = {pattern};
    bool success = rbc_glob(patterns, 1, (unsigned)flags, NULL, true, &v1_paths, &count, &lengths);

    if (!success || !v1_paths)
    {
        return create_empty_result();
    }

    return convert_v1_to_v2_results(v1_paths, count, lengths);
}

/* Optimized Path: Recursive pattern */
static rbc_glob_result_t *glob_exec_recursive(const char *pattern, int flags)
{
    /* Use v1 for now - TODO: optimize with directory cache */
    char **v1_paths = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    const char *patterns[] = {pattern};
    bool success = rbc_glob(patterns, 1, (unsigned)flags, NULL, true, &v1_paths, &count, &lengths);

    if (!success || !v1_paths)
    {
        return create_empty_result();
    }

    return convert_v1_to_v2_results(v1_paths, count, lengths);
}

/* Full Path: Complex patterns requiring AST */
static rbc_glob_result_t *glob_exec_full_ast(const char *pattern, int flags)
{
    /* Use v1 for now - TODO: implement full AST optimization */
    char **v1_paths = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    const char *patterns[] = {pattern};
    bool success = rbc_glob(patterns, 1, (unsigned)flags, NULL, true, &v1_paths, &count, &lengths);

    if (!success || !v1_paths)
    {
        return create_empty_result();
    }

    return convert_v1_to_v2_results(v1_paths, count, lengths);
}

/* ========================================================================
 * Multi-pattern API
 * ======================================================================== */

rbc_glob_result_t *rbc_glob_multi_v2(
    const char **patterns,
    size_t count,
    int flags)
{
    if (!patterns || count == 0)
    {
        return create_empty_result();
    }

    /* Use optimized multi-pattern implementation */
    return rbc_glob_multi_v2_optimized(patterns, count, flags);
}

/* ========================================================================
 * Result Management
 * ======================================================================== */

static rbc_glob_result_t *create_result_set(void)
{
    rbc_glob_result_t *result = calloc(1, sizeof(rbc_glob_result_t));
    if (!result)
        return NULL;

    result->capacity = 16;
    result->paths = malloc(result->capacity * sizeof(char *));
    if (!result->paths)
    {
        free(result);
        return NULL;
    }

    return result;
}

static rbc_glob_result_t *create_empty_result(void)
{
    return create_result_set();
}

static rbc_glob_result_t *create_single_result(const char *path)
{
    rbc_glob_result_t *result = create_result_set();
    if (result)
    {
        add_result(result, path);
    }
    return result;
}

static void add_result(rbc_glob_result_t *result, const char *path)
{
    if (!result || !path)
        return;

    /* Grow array if needed */
    if (result->count >= result->capacity)
    {
        size_t new_capacity = result->capacity * 2;
        char **new_paths = realloc(result->paths, new_capacity * sizeof(char *));
        if (!new_paths)
            return;

        result->paths = new_paths;
        result->capacity = new_capacity;
    }

    /* Add path (duplicate string) */
    result->paths[result->count] = strdup(path);
    if (result->paths[result->count])
    {
        result->count++;
    }
}

void rbc_glob_result_free(rbc_glob_result_t *result)
{
    if (!result)
        return;

    for (size_t i = 0; i < result->count; i++)
    {
        free(result->paths[i]);
    }

    free(result->paths);
    free(result);
}
