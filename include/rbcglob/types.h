#ifndef RBCGLOB_TYPES_H
#define RBCGLOB_TYPES_H

/**
 * @file types.h
 * @brief Common type definitions shared across rbcglob modules
 */

/**
 * @brief Opaque type for compiled glob patterns with brace expansion
 *
 * This structure holds precompiled glob patterns that can be reused
 * for multiple glob operations. Use rbcglob_compile_glob() to create
 * and rbcglob_compiled_glob_free() to destroy.
 */
typedef struct rbcglob_compiled_glob_s rbcglob_compiled_glob_t;

#endif /* RBCGLOB_TYPES_H */
