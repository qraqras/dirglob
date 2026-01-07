#ifndef RBCGLOB_DIR_H
#define RBCGLOB_DIR_H

#include <stdbool.h>
#include <stddef.h>
#include <rbcglob/types.h>

/**
 * @file dir.h
 * @brief Dir class methods (Ruby Dir.* equivalent)
 */

/**
 * @brief Perform glob pattern matching and return matching paths
 *
 * @param patterns Array of glob patterns to match
 * @param npatterns Number of patterns in the array
 * @param flags Bitwise OR of RBCGLOB_FNM_* flags to control matching behavior
 * @param base Base directory for relative path resolution (NULL for current directory)
 * @param sort true to sort results (Ruby default), false to skip sorting
 * @param out Output parameter for array of matched paths (caller must free with rbcglob_free)
 * @param count Output parameter for number of results returned
 * @param lengths Output parameter for array of path lengths (optional, caller must free with rbcglob_free)
 * @return true on success, false on error (check errno for details)
 */
bool rbcglob_dirglob(const char **patterns, size_t npatterns, unsigned flags,
                     const char *base, bool sort, char ***out, size_t *count, size_t **lengths);

/**
 * @brief Perform glob matching with a precompiled pattern
 *
 * More efficient than rbcglob_dirglob() when the same pattern is used multiple times.
 * The pattern should be compiled once with rbcglob_compile_glob() and reused.
 *
 * @param cg Precompiled glob pattern (must not be NULL)
 * @param base Base directory for relative path resolution (NULL for current directory)
 * @param sort true to sort results (Ruby default), false to skip sorting
 * @param out Output parameter for array of matched paths (caller must free with rbcglob_free)
 * @param count Output parameter for number of results returned
 * @param lengths Output parameter for array of path lengths (optional, caller must free with rbcglob_free)
 * @return true on success, false on error (check errno for details)
 */
bool rbcglob_dirglob_compiled(const rbcglob_compiled_glob_t *cg, const char *base, bool sort,
                              char ***out, size_t *count, size_t **lengths);

/**
 * @brief Free memory allocated by dirglob
 *
 * @param list Array returned by dirglob
 * @param count Number of elements in the array
 * @param lengths Array of lengths returned by dirglob (NULL if not returned)
 */
void rbcglob_free(char **list, size_t count, size_t *lengths);

/**
 * @brief Compile a glob pattern for reuse
 *
 * Compiles a glob pattern with brace expansion support into an optimized
 * internal representation. The compiled pattern can be reused multiple times
 * for efficient matching.
 *
 * Brace expansion examples:
 *   "*.{c,h}"     → expands to ["*.c", "*.h"]
 *   "test_{a,b}" → expands to ["test_a", "test_b"]
 *
 * @param pattern Glob pattern path (may contain braces)
 * @param flags Bitwise OR of RBCGLOB_FNM_* flags
 * @return Pointer to compiled glob, or NULL on error (errno set to EINVAL or ENOMEM)
 */
rbcglob_compiled_glob_t *rbcglob_compile_glob(const char *pattern, unsigned flags);

/**
 * @brief Free a compiled glob pattern
 *
 * @param cg Compiled glob to free (NULL is safe to pass)
 */
void rbcglob_compiled_glob_free(rbcglob_compiled_glob_t *cg);

#endif /* RBCGLOB_DIR_H */
