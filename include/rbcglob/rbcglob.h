#ifndef RBCGLOB_RBCGLOB_RBCGLOB_H
#define RBCGLOB_RBCGLOB_RBCGLOB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Library version string
 */
#define RBCGLOB_VERSION "0.1.0"

/**
 * @defgroup fnm_flags Pattern matching flags
 * @brief Flags compatible with Ruby's File::FNM_* constants
 * @{
 */
#define RBCGLOB_FNM_NOESCAPE (1U << 0)
#define RBCGLOB_FNM_PATHNAME (1U << 1)
#define RBCGLOB_FNM_DOTMATCH (1U << 2)
#define RBCGLOB_FNM_CASEFOLD (1U << 3)
#define RBCGLOB_FNM_EXTGLOB (1U << 4)
/**
 * @}
 */

/**
 * @brief Perform glob pattern matching and return matching paths
 *
 * @param patterns Array of glob patterns to match
 * @param npatterns Number of patterns in the array
 * @param flags Bitwise OR of RBCGLOB_FNM_* flags to control matching behavior
 * @param base Base directory for relative path resolution (NULL for current directory)
 * @param sort 1 to sort results (Ruby default), 0 to skip sorting
 * @param out Output parameter for array of matched paths (caller must free with rbcglob_free)
 * @param count Output parameter for number of results returned
 * @param lengths Output parameter for array of path lengths (optional, caller must free with rbcglob_free)
 * @return true on success, false on error (check errno for details)
 */
bool rbcglob_dirglob(const char **patterns, size_t npatterns, unsigned flags,
                     const char *base, int sort, char ***out, size_t *count, size_t **lengths);

/**
 * @brief Free memory allocated by dirglob
 *
 * @param list Array returned by dirglob
 * @param count Number of elements in the array
 * @param lengths Array of lengths returned by dirglob (NULL if not returned)
 */
void rbcglob_free(char **list, size_t count, size_t *lengths);

/**
 * @brief Test if a path matches a glob pattern
 *
 * @param pattern Glob pattern to match against
 * @param flags Bitwise OR of RBCGLOB_FNM_* flags to control matching behavior
 * @param path Path to test
 * @return 0 if path matches pattern, 1 if no match, negative value on error
 */
int rbcglob_match(const char *pattern, unsigned flags, const char *path);

/**
 * @brief Get the library version string
 *
 * @return A null-terminated string containing the library version
 */
const char *rbcglob_version(void);

/**
 * @brief Opaque type for compiled glob patterns with brace expansion
 *
 * This structure holds precompiled glob patterns that can be reused
 * for multiple glob operations. Use rbcglob_compile_glob() to create
 * and rbcglob_compiled_glob_free() to destroy.
 */
typedef struct rbcglob_compiled_glob_s rbcglob_compiled_glob_t;

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
 * @param pattern Glob pattern string (may contain braces)
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

#endif /* RBCGLOB_RBCGLOB_RBCGLOB_H */
