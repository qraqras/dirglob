#ifndef RBC_RBC_H
#define RBC_RBC_H

#include <stddef.h>
#include <stdbool.h>

/// @brief Library Version
#define RBC_LIB_VERSION "0.1.0"

/// @defgroup rbc_flags Flags
/// @{
#define RBC_FNM_NOESCAPE 0x01
#define RBC_FNM_PATHNAME 0x02
#define RBC_FNM_DOTMATCH 0x04
#define RBC_FNM_CASEFOLD 0x08
#define RBC_FNM_EXTGLOB 0x10
// SYSCASE: On Windows, use case-insensitive matching by default
#ifdef _WIN32
#define RBC_FNM_SYSCASE RBC_FNM_CASEFOLD
#else
#define RBC_FNM_SYSCASE 0
#endif
/// @}

/// @defgroup rbc_glob Glob Functions
/// @{
typedef struct rbc_glob_pattern_s rbc_glob_pattern_t;

bool rbc_glob(const char **patterns, size_t npatterns, unsigned flags, const char *base, bool sort, char ***out, size_t *count, size_t **lengths);
bool rbc_glob_trie(const char **patterns, size_t npatterns, unsigned flags, const char *base, bool sort, char ***out, size_t *count, size_t **lengths);
bool rbc_xglob(const rbc_glob_pattern_t *gp, const char *base, bool sort, char ***out, size_t *count, size_t **lengths);
rbc_glob_pattern_t *rbc_glob_compile(const char *pattern, unsigned flags);
void rbc_glob_pattern_free(rbc_glob_pattern_t *gp);
void rbc_glob_free(char **list, size_t count, size_t *lengths);
/// @}

/// @defgroup rbc_fnmatch Fnmatch Functions
/// @{
typedef struct rbc_fnmatch_pattern_s rbc_fnmatch_pattern_t;
bool rbc_fnmatch(const char *pattern, const char *path, unsigned flags);
bool rbc_fnmatch_len(const char *pattern, size_t pattern_len, const char *path, unsigned flags);
bool rbc_xfnmatch(const rbc_fnmatch_pattern_t *fp, const char *path, unsigned flags);
rbc_fnmatch_pattern_t *rbc_fnmatch_compile(const char *pattern, unsigned flags);
void rbc_fnmatch_pattern_free(rbc_fnmatch_pattern_t *fp);
/// @}

#endif /* RBC_RBC_H */
