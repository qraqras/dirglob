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

/// @brief Glob operation status codes
typedef enum rbc_glob_status_e
{
    RBC_GLOB_SUCCESS, ///< Operation completed successfully
    RBC_GLOB_STOPPED, ///< User callback requested early termination
    RBC_GLOB_NOMEM,   ///< Memory allocation failure (fatal)
    RBC_GLOB_ABORTED, ///< Aborted by error callback
    RBC_GLOB_INVAL,   ///< Invalid arguments (NULL patterns, zero count, etc.)
} rbc_glob_status_t;

/// @brief Glob result structure (POSIX-style)
/// @note Caller provides this struct, rbc_glob() fills it
/// @note Must be freed with rbc_globfree() after use
typedef struct rbc_glob_result_s
{
    char **paths; ///< Array of matched path strings (null-terminated each)
    size_t count; ///< Number of matched paths
} rbc_glob_result_t;

/// @brief Callback function type for rbc_glob_each
/// @param path Matched path string (null-terminated)
/// @param path_len Length of the path
/// @param user_data User-provided context pointer
/// @return true to continue iteration, false to stop early
typedef bool (*rbc_glob_callback_t)(const char *path, size_t path_len, void *user_data);

/// @brief Error callback function type
/// @param path Path where error occurred (may be NULL for memory errors)
/// @param errnum System errno value (EACCES, ENOENT, etc.)
/// @param user_data User-provided context pointer
/// @return true to continue (ignore error), false to abort
typedef bool (*rbc_glob_errfunc_t)(const char *path, int errnum, void *user_data);

/// @brief Glob file paths matching patterns
/// @param patterns Array of glob pattern strings
/// @param npatterns Number of patterns
/// @param flags Matching flags (RBC_FNM_*)
/// @param base Base directory (NULL or "" for current directory)
/// @param sort true to sort results, false for filesystem order
/// @param result Output result structure (caller-provided)
/// @param errfunc Error callback (NULL to ignore errors)
/// @param errfunc_data User data for error callback
/// @return Status code
rbc_glob_status_t rbc_glob(const char **patterns, size_t npatterns, unsigned flags, const char *base, bool sort, rbc_glob_result_t *result, rbc_glob_errfunc_t errfunc, void *errfunc_data);

/// @brief Iterate over matching paths via callback (no allocation, no sorting)
rbc_glob_status_t rbc_glob_each(const char **patterns, size_t npatterns, unsigned flags, const char *base, rbc_glob_callback_t callback, void *user_data, rbc_glob_errfunc_t errfunc, void *errfunc_data);

/// @brief Free glob result
/// @param result Result structure to free (safe to call with zeroed struct)
void rbc_globfree(rbc_glob_result_t *result);

// Legacy/internal functions (may be removed)
bool rbc_glob_trie(const char **patterns, size_t npatterns, unsigned flags, const char *base, bool sort, char ***out, size_t *count, size_t **lengths);
bool rbc_xglob(const rbc_glob_pattern_t *gp, const char *base, bool sort, char ***out, size_t *count, size_t **lengths);
rbc_glob_pattern_t *rbc_glob_compile(const char *pattern, unsigned flags);
void rbc_glob_pattern_free(rbc_glob_pattern_t *gp);
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
