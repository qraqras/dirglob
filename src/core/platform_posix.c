#ifndef _WIN32

#include "platform.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

// ============================================================================
// Directory Handle
// ============================================================================

struct rbc_dir_s
{
    DIR *dirp;
    char path[RBC_MAX_PATH]; // For stat fallback
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

    DIR *dirp = opendir(actual_path);
    if (!dirp)
        return NULL;

    rbc_dir_t *dir = malloc(sizeof(rbc_dir_t));
    if (!dir)
    {
        closedir(dirp);
        return NULL;
    }

    dir->dirp = dirp;
    strncpy(dir->path, actual_path, RBC_MAX_PATH - 1);
    dir->path[RBC_MAX_PATH - 1] = '\0';

    return dir;
}

bool rbc_readdir(rbc_dir_t *dir, rbc_dirent_t *entry)
{
    if (!dir || !dir->dirp || !entry)
        return false;

    struct dirent *de = readdir(dir->dirp);
    if (!de)
        return false;

    // Copy name
    strncpy(entry->name, de->d_name, RBC_MAX_PATH - 1);
    entry->name[RBC_MAX_PATH - 1] = '\0';

    // Determine type using d_type if available
    entry->is_dir = false;
    entry->is_link = false;

#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_DIR)
    if (de->d_type == DT_DIR)
    {
        entry->is_dir = true;
    }
    else if (de->d_type == DT_LNK)
    {
        entry->is_link = true;
        // Need to stat to check if link points to directory
        char fullpath[RBC_MAX_PATH];
        size_t pathlen = strlen(dir->path);
        if (pathlen + 1 + strlen(entry->name) < RBC_MAX_PATH)
        {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", dir->path, entry->name);
            struct stat st;
            if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
            {
                entry->is_dir = true;
            }
        }
    }
    else if (de->d_type == DT_UNKNOWN)
    {
        // Fallback to stat
        char fullpath[RBC_MAX_PATH];
        size_t pathlen = strlen(dir->path);
        if (pathlen + 1 + strlen(entry->name) < RBC_MAX_PATH)
        {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", dir->path, entry->name);
            struct stat st;
            if (lstat(fullpath, &st) == 0)
            {
                entry->is_dir = S_ISDIR(st.st_mode);
                entry->is_link = S_ISLNK(st.st_mode);
                if (entry->is_link && stat(fullpath, &st) == 0)
                {
                    entry->is_dir = S_ISDIR(st.st_mode);
                }
            }
        }
    }
#else
    // No d_type support, always use stat
    char fullpath[RBC_MAX_PATH];
    size_t pathlen = strlen(dir->path);
    if (pathlen + 1 + strlen(entry->name) < RBC_MAX_PATH)
    {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir->path, entry->name);
        struct stat st;
        if (lstat(fullpath, &st) == 0)
        {
            entry->is_dir = S_ISDIR(st.st_mode);
            entry->is_link = S_ISLNK(st.st_mode);
            if (entry->is_link && stat(fullpath, &st) == 0)
            {
                entry->is_dir = S_ISDIR(st.st_mode);
            }
        }
    }
#endif

    return true;
}

void rbc_closedir(rbc_dir_t *dir)
{
    if (dir)
    {
        if (dir->dirp)
            closedir(dir->dirp);
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
    struct stat st;
    return stat(path, &st) == 0;
}

bool rbc_is_directory(const char *path)
{
    if (!path)
        return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

rbc_stat_result_t rbc_stat_type(const char *path, int *errnum)
{
    if (!path)
    {
        if (errnum)
            *errnum = ENOENT;
        return RBC_STAT_NOTFOUND;
    }

    struct stat st;
    if (stat(path, &st) != 0)
    {
        int saved = errno;
        if (errnum)
            *errnum = saved;
        return (saved == ENOENT) ? RBC_STAT_NOTFOUND : RBC_STAT_ERROR;
    }

    if (errnum)
        *errnum = 0;

    if (S_ISDIR(st.st_mode))
        return RBC_STAT_DIR;
    if (S_ISREG(st.st_mode))
        return RBC_STAT_FILE;
    return RBC_STAT_OTHER;
}

// ============================================================================
// Path Utilities
// ============================================================================

bool rbc_is_absolute_path(const char *path)
{
    return path && path[0] == '/';
}

const char *rbc_normalize_path(const char *path, char *buf, size_t buf_size)
{
    (void)buf;
    (void)buf_size;
    // On POSIX, '/' is already the separator, no normalization needed
    return path;
}

bool rbc_parse_absolute_root(const char *path, rbc_path_root_t *result, char *root_buf)
{
    if (!path || path[0] != '/')
        return false;

    // Skip leading slashes
    const char *p = path;
    while (*p == '/')
        p++;

    // Set root to "/"
    root_buf[0] = '/';
    root_buf[1] = '\0';
    result->root = root_buf;
    result->root_len = 1;
    result->remainder = p;

    return true;
}

#endif // !_WIN32
