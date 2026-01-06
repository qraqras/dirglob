#ifndef RBCGLOB_RBCGLOB_RBCGLOB_H
#define RBCGLOB_RBCGLOB_RBCGLOB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Library version path
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
#define RBCGLOB_FNM_SHORTNAME (1U << 5)

/**
 * @brief System-default case sensitivity (matches Ruby File::FNM_SYSCASE)
 * On Windows/macOS, this is equivalent to RBCGLOB_FNM_CASEFOLD.
 * On Linux, this is 0.
 */
#if defined(_WIN32) || defined(__APPLE__)
#define RBCGLOB_FNM_SYSCASE RBCGLOB_FNM_CASEFOLD
#else
#define RBCGLOB_FNM_SYSCASE 0
#endif

/**
 * @}
 */

/**
 * @brief Opaque type for compiled glob patterns with brace expansion
 */
typedef struct rbcglob_compiled_glob_s rbcglob_compiled_glob_t;

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
 * @brief Free memory allocated by dirglob
 *
 * @param list Array returned by dirglob
 * @param count Number of elements in the array
 * @param lengths Array of lengths returned by dirglob (NULL if not returned)
 */
void rbcglob_free(char **list, size_t count, size_t *lengths);

/**
 * @brief Match a path against a glob pattern (Ruby File.fnmatch equivalent)
 *
 * @param pattern Glob pattern
 * @param path String to match
 * @param flags Bitwise OR of RBCGLOB_FNM_* flags
 * @return true if match, false if no match or error
 */
bool rbcglob_fnmatch(const char *pattern, const char *path, unsigned flags);

/**
 * @brief Match a path against a precompiled glob pattern (fnmatch compiled version)
 *
 * @param cg Precompiled glob bundle
 * @param path Path to test
 * @return true if match, false if no match or error
 */
bool rbcglob_fnmatch_compiled(const rbcglob_compiled_glob_t *cg, const char *path);

/**
 * @brief Join multiple path components (Ruby File.join equivalent)
 *
 * Joins components with a single separator, removing redundant ones.
 * The separator used is always '/'.
 *
 * @param args Array of strings to join
 * @param count Number of components
 * @return Newly allocated string, caller must free with free()
 */
char *rbcglob_join(const char **args, size_t count);

/**
 * @brief Expand a path to an absolute path (Ruby File.expand_path equivalent)
 *
 * Expands tildes (~), resolves relative paths, and simplifies . and ..
 * If base_dir is NULL, the current working directory is used.
 *
 * @param file_name Path to expand
 * @param dir_string Base directory for relative paths (can be NULL)
 * @return Newly allocated string, caller must free with free()
 */
char *rbcglob_expand_path(const char *file_name, const char *dir_string);

/**
 * @brief Get directory name from path (Ruby File.dirname equivalent)
 *
 * Returns all components except the last one (after stripping trailing separators).
 * If level is given, removes the last `level` components.
 *
 * @param file_name Path to process
 * @param level Number of trailing components to remove (default: 1)
 * @return Newly allocated string, caller must free with free()
 */
char *rbcglob_dirname(const char *file_name, int level);

/**
 * @brief Get basename from path (Ruby File.basename equivalent)
 *
 * Returns the last component of the filename (after stripping trailing separators).
 * If suffix is given and present at the end, it is removed.
 * If suffix is ".*", any extension will be removed.
 *
 * @param file_name Path to process
 * @param suffix Optional suffix to remove (NULL or "" for no removal)
 * @return Newly allocated string, caller must free with free()
 */
char *rbcglob_basename(const char *file_name, const char *suffix);

/**
 * @brief Get file extension from path (Ruby File.extname equivalent)
 *
 * Returns the extension (the portion of file name starting from the last period).
 * Handles dotfiles, trailing dots, and Windows-specific edge cases.
 *
 * @param path Path to process
 * @return Newly allocated string, caller must free with free()
 */
char *rbcglob_extname(const char *path);

/**
 * @brief Get the library version path
 *
 * @return A null-terminated path containing the library version
 */
const char *rbcglob_version(void);

/**
 * @brief Opaque type for compiled glob patterns with brace expansion
 *
 * This structure holds precompiled glob patterns that can be reused
 * for multiple glob operations. Use rbcglob_compile_glob() to create
 * and rbcglob_compiled_glob_free() to destroy.
 */

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

#endif /* RBCGLOB_RBCGLOB_RBCGLOB_H */
