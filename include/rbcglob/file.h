#ifndef RBCGLOB_FILE_H
#define RBCGLOB_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <rbcglob/types.h>

/**
 * @file file.h
 * @brief File class methods (Ruby File.* equivalent)
 */

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
#if defined(_WIN32) || defined(__APPLE__)
#define RBCGLOB_FNM_SYSCASE RBCGLOB_FNM_CASEFOLD
#else
#define RBCGLOB_FNM_SYSCASE 0
#endif
/**
 * @}
 */

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

#endif /* RBCGLOB_FILE_H */
