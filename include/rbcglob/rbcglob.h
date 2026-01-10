#ifndef RBCGLOB_RBCGLOB_H
#define RBCGLOB_RBCGLOB_H

#include <stddef.h>
#include <stdbool.h>

/**
 * @file rbcglob.h
 * @brief Ruby-compatible file system glob library - Single Header
 */

/**
 * @brief Library version string
 */
#define RBCGLOB_VERSION "0.1.0"

/**
 * @brief Get the library version string
 */
const char *rbcglob_version(void);

/**
 * @brief Opaque type for compiled glob patterns
 */
typedef struct rbcglob_compiled_glob_s rbcglob_compiled_glob_t;

/* =========================================================================
 * Dir Class Methods (Ruby Dir.*)
 * ========================================================================= */

#define RBCGLOB_FNM_NOESCAPE 0x01
#define RBCGLOB_FNM_PATHNAME 0x02
#define RBCGLOB_FNM_DOTMATCH 0x04
#define RBCGLOB_FNM_CASEFOLD 0x08
#define RBCGLOB_FNM_EXTGLOB 0x10

/**
 * @brief Match glob patterns against filesystem
 *
 * @param patterns Array of patterns
 * @param npatterns Count
 * @param flags OR-ed flags
 * @param base Base dir (optional)
 * @param sort Sort results?
 * @param out [Output] Array of strings
 * @param count [Output] Count
 * @param lengths [Output] Lengths (optional)
 * @return true on success
 */
bool rbcglob_dirglob(const char **patterns, size_t npatterns, unsigned flags,
                     const char *base, bool sort, char ***out, size_t *count, size_t **lengths);

bool rbcglob_dirglob_compiled(const rbcglob_compiled_glob_t *cg, const char *base, bool sort,
                              char ***out, size_t *count, size_t **lengths);

void rbcglob_free(char **list, size_t count, size_t *lengths);

rbcglob_compiled_glob_t *rbcglob_compile_glob(const char *pattern, unsigned flags);
void rbcglob_compiled_glob_free(rbcglob_compiled_glob_t *cg);

char *rbcglob_home_dir(const char *user);

/* =========================================================================
 * File Class Methods (Ruby File.*)
 * ========================================================================= */

bool rbcglob_fnmatch(const char *pattern, const char *string, unsigned flags);
char *rbcglob_join(const char **paths, size_t count);
char *rbcglob_expand_path(const char *path, const char *dir);
char *rbcglob_dirname(const char *file_name, int level);
char *rbcglob_basename(const char *file_name, const char *suffix);
char *rbcglob_extname(const char *file_name);

#endif /* RBCGLOB_RBCGLOB_H */
