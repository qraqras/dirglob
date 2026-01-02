#ifndef DIRGLOB_DIRGLOB_DIRGLOB_H
#define DIRGLOB_DIRGLOB_DIRGLOB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Library version string
 */
#define DIRGLOB_VERSION "0.1.0"

/**
 * @defgroup fnm_flags Pattern matching flags
 * @brief Flags compatible with Ruby's File::FNM_* constants
 * @{
 */
#ifndef FNM_NOESCAPE
#define FNM_NOESCAPE (1U << 0)
#endif
#ifndef FNM_PATHNAME
#define FNM_PATHNAME (1U << 1)
#endif
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD (1U << 2)
#endif
#ifndef FNM_DOTMATCH
#define FNM_DOTMATCH (1U << 3)
#endif
#ifndef FNM_EXTGLOB
#define FNM_EXTGLOB (1U << 4)
#endif
/**
 * @}
 */

/**
 * @brief Perform glob pattern matching and return matching paths
 *
 * @param patterns Array of glob patterns to match
 * @param npatterns Number of patterns in the array
 * @param flags Bitwise OR of FNM_* flags to control matching behavior
 * @param base Base directory for relative path resolution (NULL for current directory)
 * @param sort 1 to sort results (Ruby default), 0 to skip sorting
 * @param out Output parameter for array of matched paths (caller must free with dirglob_free)
 * @param count Output parameter for number of results returned
 * @return true on success, false on error (check errno for details)
 */
bool dirglob(const char **patterns, size_t npatterns, unsigned flags,
             const char *base, int sort, char ***out, size_t *count);

/**
 * @brief Free memory allocated by dirglob
 *
 * @param list Array returned by dirglob
 * @param count Number of elements in the array
 */
void dirglob_free(char **list, size_t count);

/**
 * @brief Test if a path matches a glob pattern
 *
 * @param pattern Glob pattern to match against
 * @param flags Bitwise OR of FNM_* flags to control matching behavior
 * @param path Path to test
 * @return 0 if path matches pattern, 1 if no match, negative value on error
 */
int dirglob_match(const char *pattern, unsigned flags, const char *path);

/**
 * @brief Get the library version string
 *
 * @return A null-terminated string containing the library version
 */
const char *dirglob_version(void);

#endif /* DIRGLOB_DIRGLOB_DIRGLOB_H */
