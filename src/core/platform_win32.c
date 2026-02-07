/**
 * @file platform_win32.c
 * @brief Windows implementation of platform abstraction layer
 *
 * Uses UTF-8 for external interface, converts to UTF-16 for Windows API calls.
 */

#ifdef _WIN32

#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// ============================================================================
// UTF-8 / UTF-16 Conversion
// ============================================================================

/**
 * @brief Convert UTF-8 string to UTF-16 (wide string)
 * @param utf8 Input UTF-8 string
 * @param wide Output buffer
 * @param wide_size Size of output buffer in wchar_t units
 * @return true on success
 */
static bool utf8_to_wide(const char *utf8, wchar_t *wide, size_t wide_size)
{
    if (!utf8 || !wide || wide_size == 0)
        return false;

    int result = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, (int)wide_size);
    return result > 0;
}

/**
 * @brief Convert UTF-16 (wide string) to UTF-8
 * @param wide Input wide string
 * @param utf8 Output buffer
 * @param utf8_size Size of output buffer in bytes
 * @return true on success
 */
static bool wide_to_utf8(const wchar_t *wide, char *utf8, size_t utf8_size)
{
    if (!wide || !utf8 || utf8_size == 0)
        return false;

    int result = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, (int)utf8_size,
                                     NULL, NULL);
    return result > 0;
}

// ============================================================================
// Directory Handle
// ============================================================================

struct rbc_dir_s
{
    HANDLE handle;
    WIN32_FIND_DATAW find_data;
    bool first_read; // true if first readdir call
    bool has_entry;  // true if find_data contains valid entry
    char path[RBC_MAX_PATH];
};

// ============================================================================
// Directory Operations
// ============================================================================

rbc_dir_t *rbc_opendir(const char *path)
{
    if (!path)
        return NULL;

    // Handle empty path as current directory
    const char *actual_path = (path[0] == '\0') ? "." : path;

    // Build search pattern: path\*
    char search_path[RBC_MAX_PATH];
    size_t len = strlen(actual_path);

    if (len >= RBC_MAX_PATH - 3) // Need room for \* and null
        return NULL;

    strcpy(search_path, actual_path);

    // Remove trailing separator if present
    if (len > 0 && (search_path[len - 1] == '\\' || search_path[len - 1] == '/'))
        len--;

    search_path[len] = '\\';
    search_path[len + 1] = '*';
    search_path[len + 2] = '\0';

    // Convert to wide string
    wchar_t wide_path[RBC_MAX_PATH];
    if (!utf8_to_wide(search_path, wide_path, RBC_MAX_PATH))
        return NULL;

    // Open directory
    rbc_dir_t *dir = malloc(sizeof(rbc_dir_t));
    if (!dir)
        return NULL;

    dir->handle = FindFirstFileW(wide_path, &dir->find_data);
    if (dir->handle == INVALID_HANDLE_VALUE)
    {
        free(dir);
        return NULL;
    }

    dir->first_read = true;
    dir->has_entry = true;
    strncpy(dir->path, actual_path, RBC_MAX_PATH - 1);
    dir->path[RBC_MAX_PATH - 1] = '\0';

    return dir;
}

bool rbc_readdir(rbc_dir_t *dir, rbc_dirent_t *entry)
{
    if (!dir || dir->handle == INVALID_HANDLE_VALUE || !entry)
        return false;

    // First call returns the entry from FindFirstFile
    if (dir->first_read)
    {
        dir->first_read = false;
        if (!dir->has_entry)
            return false;
    }
    else
    {
        // Subsequent calls use FindNextFile
        if (!FindNextFileW(dir->handle, &dir->find_data))
        {
            dir->has_entry = false;
            return false;
        }
    }

    // Convert filename to UTF-8
    if (!wide_to_utf8(dir->find_data.cFileName, entry->name, RBC_MAX_PATH))
        return false;

    // Determine type
    DWORD attrs = dir->find_data.dwFileAttributes;
    entry->is_dir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    entry->is_link = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

    // If it's a reparse point (symlink/junction), check if it points to directory
    if (entry->is_link && !entry->is_dir)
    {
        char fullpath[RBC_MAX_PATH];
        size_t pathlen = strlen(dir->path);
        size_t namelen = strlen(entry->name);

        if (pathlen + 1 + namelen < RBC_MAX_PATH)
        {
            snprintf(fullpath, sizeof(fullpath), "%s\\%s", dir->path, entry->name);
            entry->is_dir = rbc_is_directory(fullpath);
        }
    }

    return true;
}

void rbc_closedir(rbc_dir_t *dir)
{
    if (dir)
    {
        if (dir->handle != INVALID_HANDLE_VALUE)
            FindClose(dir->handle);
        free(dir);
    }
}

// ============================================================================
// File Operations
// ============================================================================

bool rbc_path_exists(const char *path)
{
    if (!path)
        return false;

    wchar_t wide_path[RBC_MAX_PATH];
    if (!utf8_to_wide(path, wide_path, RBC_MAX_PATH))
        return false;

    DWORD attrs = GetFileAttributesW(wide_path);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

bool rbc_is_directory(const char *path)
{
    if (!path)
        return false;

    wchar_t wide_path[RBC_MAX_PATH];
    if (!utf8_to_wide(path, wide_path, RBC_MAX_PATH))
        return false;

    DWORD attrs = GetFileAttributesW(wide_path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false;

    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

rbc_stat_result_t rbc_stat_type(const char *path, rbc_stat_errc_t *errc)
{
    if (!path)
    {
        if (errc)
            *errc = RBC_STAT_E_NOENT;
        return RBC_STAT_NOTFOUND;
    }

    wchar_t wide_path[RBC_MAX_PATH];
    if (!utf8_to_wide(path, wide_path, RBC_MAX_PATH))
    {
        if (errc)
            *errc = RBC_STAT_E_NAMETOOLONG;
        return RBC_STAT_ERROR;
    }

    DWORD attrs = GetFileAttributesW(wide_path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        DWORD err = GetLastError();
        if (errc)
        {
            switch (err)
            {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                *errc = RBC_STAT_E_NOENT;
                break;
            case ERROR_ACCESS_DENIED:
                *errc = RBC_STAT_E_ACCES;
                break;
            default:
                *errc = RBC_STAT_E_IO;
                break;
            }
        }
        return (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                   ? RBC_STAT_NOTFOUND
                   : RBC_STAT_ERROR;
    }

    if (errc)
        *errc = RBC_STAT_E_NONE;

    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        return RBC_STAT_DIR;
    return RBC_STAT_FILE;
}

// ============================================================================
// Path Utilities
// ============================================================================

bool rbc_is_absolute_path(const char *path)
{
    if (!path || path[0] == '\0')
        return false;

    // Check for drive letter (e.g., "C:\")
    if (((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':')
    {
        return true;
    }

    // Check for UNC path (e.g., "\\server\share")
    if ((path[0] == '\\' || path[0] == '/') &&
        (path[1] == '\\' || path[1] == '/'))
    {
        return true;
    }

    // Check for root path (e.g., "\foo" - relative to current drive)
    if (path[0] == '\\' || path[0] == '/')
    {
        return true;
    }

    return false;
}

const char *rbc_normalize_path(const char *path, char *buf, size_t buf_size)
{
    if (!path)
        return path;

    // Check if normalization is needed
    bool needs_norm = false;
    for (const char *p = path; *p; p++)
    {
        if (*p == '\\')
        {
            needs_norm = true;
            break;
        }
    }

    if (!needs_norm)
        return path;

    // Copy and normalize backslashes to forward slashes
    size_t i = 0;
    for (const char *p = path; *p && i < buf_size - 1; p++, i++)
    {
        buf[i] = (*p == '\\') ? '/' : *p;
    }
    buf[i] = '\0';
    return buf;
}

bool rbc_parse_absolute_root(const char *path, rbc_path_root_t *result, char *root_buf)
{
    if (!path || path[0] == '\0')
        return false;

    const char *p = path;

    // Check for drive letter (e.g., "C:/")
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
    {
        // Drive letter path
        root_buf[0] = p[0];
        root_buf[1] = ':';
        root_buf[2] = '/';
        root_buf[3] = '\0';
        result->root = root_buf;
        result->root_len = 3;

        // Skip drive letter and colon
        p += 2;
        // Skip leading slashes
        while (*p == '/' || *p == '\\')
            p++;
        result->remainder = p;
        return true;
    }

    // Check for root path ("/foo" or "\\foo")
    if (p[0] == '/' || p[0] == '\\')
    {
        root_buf[0] = '/';
        root_buf[1] = '\0';
        result->root = root_buf;
        result->root_len = 1;

        // Skip leading slashes
        while (*p == '/' || *p == '\\')
            p++;
        result->remainder = p;
        return true;
    }

    return false;
}

#endif // _WIN32
