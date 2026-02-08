/**
 * @file platform.h
 * @brief Platform abstraction layer for cross-platform filesystem operations
 *
 * Provides unified API for:
 * - Directory traversal (opendir/readdir/closedir abstraction)
 * - File existence and type checking
 * - Path manipulation
 *
 * Implementations:
 * - platform_posix.c: Linux, macOS, BSD
 * - platform_win32.c: Windows (UTF-8 interface)
 */

#ifndef RBC_PLATFORM_H
#define RBC_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // ============================================================================
    // Constants
    // ============================================================================

#define RBC_MAX_PATH 4096

// Path separator (always use '/' internally, convert on Windows)
#ifdef _WIN32
#define RBC_PATH_SEP '\\'
#define RBC_PATH_SEP_STR "\\"
#define RBC_IS_PATH_SEP(c) ((c) == '/' || (c) == '\\')
#else
#define RBC_PATH_SEP '/'
#define RBC_PATH_SEP_STR "/"
#define RBC_IS_PATH_SEP(c) ((c) == '/')
#endif

    // ============================================================================
    // Directory Entry
    // ============================================================================

    typedef struct rbc_dirent_s
    {
        char name[RBC_MAX_PATH]; // Entry name (UTF-8)
        bool is_dir;             // true if directory
        bool is_link;            // true if symbolic link
    } rbc_dirent_t;

    // ============================================================================
    // Directory Handle (opaque)
    // ============================================================================

    typedef struct rbc_dir_s rbc_dir_t;

    // ============================================================================
    // Directory Operations
    // ============================================================================

    /**
     * @brief Open a directory for reading
     * @param path Directory path (UTF-8)
     * @return Directory handle, or NULL on error
     */
    rbc_dir_t *rbc_opendir(const char *path);

    /**
     * @brief Read next directory entry
     * @param dir Directory handle
     * @param entry Output entry structure
     * @return true if entry read, false if end of directory or error
     */
    bool rbc_readdir(rbc_dir_t *dir, rbc_dirent_t *entry);

    /**
     * @brief Close directory handle
     * @param dir Directory handle
     */
    void rbc_closedir(rbc_dir_t *dir);

    // ============================================================================
    // File Operations
    // ============================================================================

    /**
     * @brief Check if path exists
     * @param path File path (UTF-8)
     * @return true if exists
     */
    bool rbc_path_exists(const char *path);

    /**
     * @brief Check if path is a directory
     * @param path File path (UTF-8)
     * @return true if directory
     */
    bool rbc_is_directory(const char *path);

    // ============================================================================
    // Stat Type (for LITERAL optimization)
    // ============================================================================

    /**
     * @brief Result type for rbc_stat_type()
     */
    typedef enum rbc_stat_result_e
    {
        RBC_STAT_FILE,     ///< Regular file
        RBC_STAT_DIR,      ///< Directory
        RBC_STAT_OTHER,    ///< Other (device, socket, etc.)
        RBC_STAT_NOTFOUND, ///< Path does not exist (ENOENT)
        RBC_STAT_ERROR,    ///< Other error (EACCES, etc.)
    } rbc_stat_result_t;

    /**
     * @brief Get path type via stat
     *
     * Uses stat() to determine if path is file/directory/other.
     * Follows symlinks (uses stat, not lstat).
     *
     * @param path File path (UTF-8)
     * @param errnum Output errno value (may be NULL). Set to 0 on success.
     * @return Stat result type
     */
    rbc_stat_result_t rbc_stat_type(const char *path, int *errnum);

    // ============================================================================
    // Path Utilities
    // ============================================================================

    /**
     * @brief Check if path is absolute
     * @param path File path
     * @return true if absolute (starts with / on POSIX, or drive letter on Windows)
     */
    bool rbc_is_absolute_path(const char *path);

    /**
     * @brief Normalize path separators to forward slash
     *
     * On Windows: converts backslashes to forward slashes
     * On POSIX: returns input unchanged
     *
     * @param path Input path
     * @param buf Output buffer (used only if normalization needed)
     * @param buf_size Size of output buffer
     * @return Pointer to normalized path (may be input or buf)
     */
    const char *rbc_normalize_path(const char *path, char *buf, size_t buf_size);

    // ============================================================================
    // Absolute Path Root Parsing
    // ============================================================================

    /**
     * @brief Parsed absolute path root information
     */
    typedef struct rbc_path_root_s
    {
        const char *root;      ///< Root path ("/" or "C:/")
        size_t root_len;       ///< Length of root (1 or 3)
        const char *remainder; ///< Pattern after root (skipping leading slashes)
    } rbc_path_root_t;

    /**
     * @brief Parse absolute path into root and remainder
     *
     * Extracts the root portion of an absolute path and returns
     * pointers to both the root string and the remaining path.
     *
     * Examples:
     *   POSIX:   "/foo/bar"  -> root="/", remainder="foo/bar"
     *   Windows: "C:/foo"    -> root="C:/", remainder="foo"
     *   Windows: "/foo"      -> root="/", remainder="foo"
     *
     * @param path Input path (must be normalized: forward slashes only)
     * @param result Output structure
     * @param root_buf Buffer for root string (minimum 4 bytes)
     * @return true if path is absolute, false otherwise
     */
    bool rbc_parse_absolute_root(const char *path, rbc_path_root_t *result, char *root_buf);

#ifdef __cplusplus
}
#endif

#endif // RBC_PLATFORM_H
