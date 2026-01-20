/**
 * @file glob_v2_recursive.c
 * @brief Recursive pattern (**) optimization for glob v2
 *
 * Optimizations (MRI-compatible):
 * 1. d_type usage - avoid stat() calls when possible
 * 2. fstatat()/openat() - reduce path construction overhead
 * 3. lstat() for symlink handling - avoid infinite loops
 * 4. Memory-efficient path construction
 * 5. Sort control via FNM_NOSORT flag
 */

#include "rbc/glob_hints.h"
#include "rbc/rbc.h"
#include "../core/internal.h"
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

/* Detect openat/fstatat support (like MRI's USE_OPENDIR_AT) */
#if defined(HAVE_FDOPENDIR) && defined(HAVE_DIRFD) && \
    defined(HAVE_OPENAT) && defined(HAVE_FSTATAT)
#define USE_OPENDIR_AT 1
#else
/* Auto-detect on POSIX systems */
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
#define USE_OPENDIR_AT 1
#else
#define USE_OPENDIR_AT 0
#endif
#endif

/* Configuration */
#define MAX_RECURSION_DEPTH 100
#define MAX_PATH_LENGTH 4096

/* Path type enum (like MRI's rb_pathtype_t) */
typedef enum
{
    PATH_UNKNOWN = 0,
    PATH_NOENT,
    PATH_REGULAR,
    PATH_DIRECTORY,
    PATH_SYMLINK,
    PATH_OTHER
} path_type_t;

/* Recursive scan context - avoids repeated parameter passing */
typedef struct
{
    const char *pattern_suffix;
    rbc_fnmatch_pattern_t *compiled_pattern; /* Precompiled pattern for fast matching */
    int flags;
    rbc_glob_result_t *results;
    char path_buf[MAX_PATH_LENGTH];
} scan_context_t;

/* Forward declarations */
static void recursive_scan_fd(
    int dir_fd,
    const char *dir_path,
    size_t dir_path_len,
    scan_context_t *ctx,
    int depth);

static void recursive_scan_path(
    const char *base_dir,
    size_t base_len,
    scan_context_t *ctx,
    int depth);

static inline bool should_skip_entry(const char *name, size_t namlen);
static path_type_t get_path_type_from_dtype(unsigned char d_type);
static path_type_t get_path_type_stat(int dir_fd, const char *name, const char *full_path);
static void add_recursive_result(rbc_glob_result_t *results, const char *path, size_t len);

/*
 * Execute recursive glob pattern with doublestar
 * Handles patterns like prefix slash doublestar slash suffix
 */
rbc_glob_result_t *rbc_glob_exec_recursive_optimized(
    const rbc_glob_hints_t *hints,
    const char *pattern,
    int flags)
{
    (void)hints; /* Use pattern directly for now */

    /* Find ** in pattern */
    const char *doublestar = strstr(pattern, "**");
    if (!doublestar)
    {
        /* No ** - this shouldn't be called, return NULL to let caller use standard path */
        return NULL;
    }

    /* Extract prefix (base directory) */
    char base_dir[MAX_PATH_LENGTH];
    size_t prefix_len = doublestar - pattern;

    if (prefix_len == 0)
    {
        /* Pattern starts with ** */
        strcpy(base_dir, ".");
    }
    else
    {
        /* Find last slash before ** */
        const char *last_slash = pattern + prefix_len - 1;
        while (last_slash > pattern && *last_slash != '/')
        {
            last_slash--;
        }

        if (*last_slash == '/')
        {
            size_t dir_len = last_slash - pattern;
            if (dir_len == 0)
            {
                strcpy(base_dir, "/");
            }
            else
            {
                memcpy(base_dir, pattern, dir_len);
                base_dir[dir_len] = '\0';
            }
        }
        else
        {
            strcpy(base_dir, ".");
        }
    }

    /* Extract suffix pattern (after **) */
    const char *pattern_suffix = doublestar + 2;
    if (*pattern_suffix == '/')
    {
        pattern_suffix++;
    }

    /* Initialize result set with larger capacity for recursive patterns */
    rbc_glob_result_t *results = calloc(1, sizeof(rbc_glob_result_t));
    if (!results)
        return NULL;

    results->capacity = 512; /* Larger initial capacity for recursive patterns */
    results->paths = malloc(results->capacity * sizeof(char *));
    if (!results->paths)
    {
        free(results);
        return NULL;
    }
    results->count = 0;

    /* Precompile suffix pattern for fast matching */
    rbc_fnmatch_pattern_t *compiled_pattern = NULL;
    if (pattern_suffix && pattern_suffix[0])
    {
        compiled_pattern = rbc_fnmatch_compile(pattern_suffix, (unsigned)flags);
    }

    /* Setup scan context */
    scan_context_t ctx = {
        .pattern_suffix = pattern_suffix,
        .compiled_pattern = compiled_pattern,
        .flags = flags,
        .results = results};

    size_t base_len = strlen(base_dir);

#if USE_OPENDIR_AT
    /* Use file descriptor based API for efficiency */
    int dir_fd = open(base_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd >= 0)
    {
        memcpy(ctx.path_buf, base_dir, base_len);
        recursive_scan_fd(dir_fd, base_dir, base_len, &ctx, 0);
        close(dir_fd);
    }
#else
    /* Fall back to path-based API */
    memcpy(ctx.path_buf, base_dir, base_len);
    recursive_scan_path(base_dir, base_len, &ctx, 0);
#endif

    /* Free precompiled pattern */
    if (compiled_pattern)
    {
        rbc_fnmatch_pattern_free(compiled_pattern);
    }

    return results;
}

/* ========================================================================
 * Path type detection from d_type (like MRI's IFTODT macro)
 * ======================================================================== */

static path_type_t get_path_type_from_dtype(unsigned char d_type)
{
#ifdef _DIRENT_HAVE_D_TYPE
    switch (d_type)
    {
    case DT_REG:
        return PATH_REGULAR;
    case DT_DIR:
        return PATH_DIRECTORY;
    case DT_LNK:
        return PATH_SYMLINK;
    case DT_UNKNOWN:
        return PATH_UNKNOWN;
    default:
        return PATH_OTHER;
    }
#else
    (void)d_type;
    return PATH_UNKNOWN;
#endif
}

/* ========================================================================
 * Path type detection via stat/lstat (with fstatat optimization)
 * ======================================================================== */

static path_type_t get_path_type_stat(int dir_fd, const char *name, const char *full_path)
{
    struct stat st;
    int ret;

#if USE_OPENDIR_AT
    if (dir_fd >= 0)
    {
        /* Use fstatat with AT_SYMLINK_NOFOLLOW (like MRI's do_lstat) */
        ret = fstatat(dir_fd, name, &st, AT_SYMLINK_NOFOLLOW);
    }
    else
#endif
    {
        (void)dir_fd;
        (void)name;
        ret = lstat(full_path, &st);
    }

    if (ret != 0)
        return PATH_NOENT;

    if (S_ISDIR(st.st_mode))
        return PATH_DIRECTORY;
    if (S_ISREG(st.st_mode))
        return PATH_REGULAR;
    if (S_ISLNK(st.st_mode))
        return PATH_SYMLINK;
    return PATH_OTHER;
}

/* ========================================================================
 * Entry skip check (optimized inline)
 * ======================================================================== */

static inline bool should_skip_entry(const char *name, size_t namlen)
{
    if (namlen == 1 && name[0] == '.')
        return true;
    if (namlen == 2 && name[0] == '.' && name[1] == '.')
        return true;
    return false;
}

/* ========================================================================
 * Add result to result set (with length parameter to avoid strlen)
 * ======================================================================== */

static inline void add_recursive_result(rbc_glob_result_t *results, const char *path, size_t len)
{
    if (!results || !path)
        return;

    /* Grow array if needed - use larger growth factor for better performance */
    if (results->count >= results->capacity)
    {
        size_t new_capacity = results->capacity + (results->capacity >> 1) + 64; /* 1.5x + 64 */
        char **new_paths = realloc(results->paths, new_capacity * sizeof(char *));
        if (!new_paths)
            return;

        results->paths = new_paths;
        results->capacity = new_capacity;
    }

    /* Add path (duplicate string with known length) */
    char *dup = malloc(len + 1);
    if (dup)
    {
        memcpy(dup, path, len);
        dup[len] = '\0';
        results->paths[results->count++] = dup;
    }
}

/* ========================================================================
 * Recursive scan using file descriptors (openat/fstatat)
 * This is the fast path on Linux/macOS/BSD
 * ======================================================================== */

#if USE_OPENDIR_AT

static void recursive_scan_fd(
    int dir_fd,
    const char *dir_path __attribute__((unused)),
    size_t dir_path_len,
    scan_context_t *ctx,
    int depth)
{
    if (depth > MAX_RECURSION_DEPTH)
        return;

    DIR *dir = fdopendir(dup(dir_fd));
    if (!dir)
        return;

    struct dirent *entry;
    const bool dotmatch = (ctx->flags & RBC_FNM_DOTMATCH) != 0;
    const bool has_suffix = ctx->pattern_suffix && ctx->pattern_suffix[0];

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;

#ifdef _DIRENT_HAVE_D_NAMLEN
        size_t namlen = entry->d_namlen;
#else
        size_t namlen = strlen(name);
#endif

        /* Skip . and .. */
        if (should_skip_entry(name, namlen))
            continue;

        /* Skip hidden entries unless DOTMATCH (MRI behavior) */
        bool is_dotfile = (name[0] == '.');
        if (is_dotfile && !dotmatch)
            continue;

        /* Build full path efficiently (reuse buffer) */
        if (dir_path_len + 1 + namlen >= MAX_PATH_LENGTH)
            continue;

        ctx->path_buf[dir_path_len] = '/';
        memcpy(ctx->path_buf + dir_path_len + 1, name, namlen);
        size_t path_len = dir_path_len + 1 + namlen;
        ctx->path_buf[path_len] = '\0';

        /* Get path type from d_type first (avoid stat) */
        path_type_t ptype;
#ifdef _DIRENT_HAVE_D_TYPE
        ptype = get_path_type_from_dtype(entry->d_type);
        /* Only fall back to fstatat if d_type is truly unknown */
        if (ptype == PATH_UNKNOWN)
        {
            ptype = get_path_type_stat(dir_fd, name, ctx->path_buf);
        }
#else
        ptype = get_path_type_stat(dir_fd, name, ctx->path_buf);
#endif

        /* Match against suffix pattern */
        if (has_suffix)
        {
            /* Use precompiled pattern for fast matching */
            bool matched = ctx->compiled_pattern
                               ? rbc_xfnmatch(ctx->compiled_pattern, name, (unsigned)ctx->flags)
                               : rbc_fnmatch(ctx->pattern_suffix, name, (unsigned)ctx->flags);

            if (matched)
            {
                add_recursive_result(ctx->results, ctx->path_buf, path_len);
            }
        }
        else
        {
            /* No suffix pattern (**) - match all files including directories */
            add_recursive_result(ctx->results, ctx->path_buf, path_len);
        }

        /* Recurse into real directories (not symlinks - MRI behavior) */
        if (ptype == PATH_DIRECTORY)
        {
            int subdir_fd = openat(dir_fd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (subdir_fd >= 0)
            {
                recursive_scan_fd(subdir_fd, ctx->path_buf, path_len, ctx, depth + 1);
                close(subdir_fd);
                /* Restore path buffer for next iteration */
                ctx->path_buf[dir_path_len] = '\0';
            }
        }
    }

    closedir(dir);
}

#endif /* USE_OPENDIR_AT */

/* ========================================================================
 * Recursive scan using path strings (fallback for systems without openat)
 * ======================================================================== */

static void recursive_scan_path(
    const char *base_dir,
    size_t base_len,
    scan_context_t *ctx,
    int depth)
{
    if (depth > MAX_RECURSION_DEPTH)
        return;

    DIR *dir = opendir(base_dir);
    if (!dir)
        return;

    struct dirent *entry;
    const bool dotmatch = (ctx->flags & RBC_FNM_DOTMATCH) != 0;
    const bool has_suffix = ctx->pattern_suffix && ctx->pattern_suffix[0];

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;

#ifdef _DIRENT_HAVE_D_NAMLEN
        size_t namlen = entry->d_namlen;
#else
        size_t namlen = strlen(name);
#endif

        /* Skip . and .. */
        if (should_skip_entry(name, namlen))
            continue;

        /* Skip hidden entries unless DOTMATCH */
        bool is_dotfile = (name[0] == '.');
        if (is_dotfile && !dotmatch)
            continue;

        /* Build full path */
        if (base_len + 1 + namlen >= MAX_PATH_LENGTH)
            continue;

        ctx->path_buf[base_len] = '/';
        memcpy(ctx->path_buf + base_len + 1, name, namlen);
        size_t path_len = base_len + 1 + namlen;
        ctx->path_buf[path_len] = '\0';

        /* Get path type from d_type first */
        path_type_t ptype;
#ifdef _DIRENT_HAVE_D_TYPE
        ptype = get_path_type_from_dtype(entry->d_type);
        /* Only fall back to lstat if d_type is truly unknown */
        if (ptype == PATH_UNKNOWN)
        {
            ptype = get_path_type_stat(-1, NULL, ctx->path_buf);
        }
#else
        ptype = get_path_type_stat(-1, NULL, ctx->path_buf);
#endif

        /* Match against suffix pattern */
        if (has_suffix)
        {
            /* Use precompiled pattern for fast matching */
            bool matched = ctx->compiled_pattern
                               ? rbc_xfnmatch(ctx->compiled_pattern, name, (unsigned)ctx->flags)
                               : rbc_fnmatch(ctx->pattern_suffix, name, (unsigned)ctx->flags);

            if (matched)
            {
                add_recursive_result(ctx->results, ctx->path_buf, path_len);
            }
        }
        else
        {
            /* No suffix pattern (**) - match all */
            add_recursive_result(ctx->results, ctx->path_buf, path_len);
        }

        /* Recurse into real directories (not symlinks) */
        if (ptype == PATH_DIRECTORY)
        {
            recursive_scan_path(ctx->path_buf, path_len, ctx, depth + 1);
            /* Restore path buffer for next iteration */
            ctx->path_buf[base_len] = '\0';
        }
    }

    closedir(dir);
}
