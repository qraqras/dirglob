/**
 * @file walker.c
 * @brief MRI-style glob walker implementation
 *
 * This implementation closely follows MRI Ruby's dir.c glob_helper() logic.
 * Reference: https://github.com/ruby/ruby/blob/main/dir.c
 *
 * MRI design philosophy:
 * 1. Single recursive glob_helper() function (dir.c:2694)
 * 2. glob_opendir() reads and optionally sorts entries (dir.c:2608)
 * 3. glob_getent() iterates over sorted or streamed entries (dir.c:2684)
 * 4. Pattern-by-pattern matching with fnmatch
 * 5. Integrated plain/magical/recursive pattern handling
 *
 * Key MRI characteristics:
 * - Two-phase: read all entries → sort → match
 * - closedir() immediately after readdir loop
 * - Sorting via qsort() on cached entries
 * - Pattern list processing (not segment tree)
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "internal.h"
#include "utils.h"
#include "rbc/rbc.h"

/* ========================================================================
 * MRI-style Directory Entry Cache (glob_opendir/glob_getent)
 * Equivalent to ruby_glob_entries_t in MRI dir.c:2584
 * ======================================================================== */

typedef struct rbc_dirent
{
    char *d_name;
    size_t d_namlen;
#ifdef _DIRENT_HAVE_D_TYPE
    unsigned char d_type;
#endif
} rbc_dirent_t;

typedef struct
{
    bool nosort;
    union
    {
        /* For nosort: stream directly from DIR* */
        struct
        {
            DIR *dirp;
            rbc_dirent_t temp_ent; /* Temporary storage */
        } stream;

        /* For sort: cached and sorted entries */
        struct
        {
            rbc_dirent_t **entries;
            size_t count;
            size_t idx;
        } sorted;
    } u;
} glob_dir_t;

/* ========================================================================
 * Directory entry comparison for qsort (MRI uses ruby_qsort)
 * ======================================================================== */

static int glob_sort_cmp(const void *a, const void *b)
{
    const rbc_dirent_t *ea = *(const rbc_dirent_t **)a;
    const rbc_dirent_t *eb = *(const rbc_dirent_t **)b;
    return strcmp(ea->d_name, eb->d_name);
}

/* ========================================================================
 * MRI dir.c:2608 glob_opendir() - Open and optionally sort directory
 * ======================================================================== */

static glob_dir_t *glob_opendir(const char *path, bool do_sort)
{
    DIR *dirp = opendir(path[0] ? path : ".");
    if (!dirp)
        return NULL;

    glob_dir_t *gdir = calloc(1, sizeof(glob_dir_t));
    if (!gdir)
    {
        closedir(dirp);
        return NULL;
    }

    if (!do_sort)
    {
        /* NOSORT mode: Keep DIR* open for streaming */
        gdir->nosort = true;
        gdir->u.stream.dirp = dirp;
        return gdir;
    }

    /* SORT mode: Read all entries, close DIR*, then sort */
    gdir->nosort = false;
    gdir->u.sorted.count = 0;
    gdir->u.sorted.idx = 0;

    size_t capacity = 64;
    gdir->u.sorted.entries = malloc(sizeof(rbc_dirent_t *) * capacity);
    if (!gdir->u.sorted.entries)
    {
        closedir(dirp);
        free(gdir);
        return NULL;
    }

    struct dirent *dp;
    while ((dp = readdir(dirp)) != NULL)
    {
        /* Skip . and .. (MRI pattern) */
        if (dp->d_name[0] == '.' &&
            (dp->d_name[1] == '\0' ||
             (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
            continue;

        /* Grow array if needed */
        if (gdir->u.sorted.count >= capacity)
        {
            capacity *= 2;
            rbc_dirent_t **new_entries = realloc(gdir->u.sorted.entries,
                                                 sizeof(rbc_dirent_t *) * capacity);
            if (!new_entries)
            {
                /* Cleanup on failure */
                for (size_t i = 0; i < gdir->u.sorted.count; i++)
                {
                    free(gdir->u.sorted.entries[i]->d_name);
                    free(gdir->u.sorted.entries[i]);
                }
                free(gdir->u.sorted.entries);
                closedir(dirp);
                free(gdir);
                return NULL;
            }
            gdir->u.sorted.entries = new_entries;
        }

        /* Copy dirent (MRI's dirent_copy pattern) */
        rbc_dirent_t *rdp = malloc(sizeof(rbc_dirent_t));
        if (!rdp)
            continue;

        size_t namlen = strlen(dp->d_name);
        rdp->d_name = malloc(namlen + 1);
        if (!rdp->d_name)
        {
            free(rdp);
            continue;
        }
        memcpy(rdp->d_name, dp->d_name, namlen + 1);
        rdp->d_namlen = namlen;

#ifdef _DIRENT_HAVE_D_TYPE
        rdp->d_type = dp->d_type;
#endif

        gdir->u.sorted.entries[gdir->u.sorted.count++] = rdp;
    }

    /* MRI pattern: Close DIR* immediately after reading */
    closedir(dirp);

    /* Sort entries (MRI uses ruby_qsort) */
    if (gdir->u.sorted.count > 0)
    {
        qsort(gdir->u.sorted.entries, gdir->u.sorted.count,
              sizeof(rbc_dirent_t *), glob_sort_cmp);
    }

    return gdir;
}

/* ========================================================================
 * MRI dir.c:2684 glob_getent() - Get next entry (sorted or streamed)
 * ======================================================================== */

static rbc_dirent_t *glob_getent(glob_dir_t *gdir)
{
    if (!gdir)
        return NULL;

    if (gdir->nosort)
    {
        /* Stream mode: Read directly from DIR* */
        struct dirent *dp = readdir(gdir->u.stream.dirp);
        if (!dp)
            return NULL;

        /* Skip . and .. */
        while (dp && dp->d_name[0] == '.' &&
               (dp->d_name[1] == '\0' ||
                (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
        {
            dp = readdir(gdir->u.stream.dirp);
        }

        if (!dp)
            return NULL;

        /* Return temporary dirent (valid until next call) */
        gdir->u.stream.temp_ent.d_name = dp->d_name;
        gdir->u.stream.temp_ent.d_namlen = strlen(dp->d_name);
#ifdef _DIRENT_HAVE_D_TYPE
        gdir->u.stream.temp_ent.d_type = dp->d_type;
#endif
        return &gdir->u.stream.temp_ent;
    }
    else
    {
        /* Sorted mode: Iterate over cached array */
        if (gdir->u.sorted.idx < gdir->u.sorted.count)
        {
            return gdir->u.sorted.entries[gdir->u.sorted.idx++];
        }
        return NULL;
    }
}

/* ========================================================================
 * MRI dir.c:2608 glob_dir_finish() - Cleanup directory iteration
 * ======================================================================== */

static void glob_dir_finish(glob_dir_t *gdir)
{
    if (!gdir)
        return;

    if (gdir->nosort)
    {
        if (gdir->u.stream.dirp)
            closedir(gdir->u.stream.dirp);
    }
    else
    {
        for (size_t i = 0; i < gdir->u.sorted.count; i++)
        {
            free(gdir->u.sorted.entries[i]->d_name);
            free(gdir->u.sorted.entries[i]);
        }
        free(gdir->u.sorted.entries);
    }
    free(gdir);
}

/* ========================================================================
 * Path manipulation utilities
 * ======================================================================== */

static char *join_path(const char *dir, size_t dirlen, bool dirsep,
                       const char *base, size_t baselen)
{
    size_t len = dirlen + (dirsep ? 1 : 0) + baselen;
    if (len >= PATH_MAX)
        return NULL;

    char *buf = malloc(len + 1);
    if (!buf)
        return NULL;

    if (dirlen > 0)
        memcpy(buf, dir, dirlen);
    if (dirsep)
        buf[dirlen] = '/';
    if (baselen > 0)
        memcpy(buf + dirlen + (dirsep ? 1 : 0), base, baselen);
    buf[len] = '\0';

    return buf;
}

/* ========================================================================
 * Helper: Check if entry is directory using d_type or stat
 * ======================================================================== */

static bool is_directory(const char *path, unsigned char d_type)
{
#ifdef _DIRENT_HAVE_D_TYPE
    if (d_type != DT_UNKNOWN)
        return d_type == DT_DIR;
#else
    (void)d_type;
#endif

    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ========================================================================
 * MRI-style glob_helper() - Main recursive glob function
 * Reference: MRI dir.c:2694 glob_helper()
 *
 * This is the core of MRI's glob implementation
 * ======================================================================== */

typedef struct
{
    rbc_match_callback_t callback;
    void *userdata;
    unsigned flags;
} glob_funcs_t;

/**
 * MRI-style recursive glob walker
 *
 * @param path Current directory path
 * @param baselen Length of base path
 * @param namelen Length of current path component
 * @param dirsep Whether to add separator before next component
 * @param seg Current segment to match
 * @param funcs Callback functions
 * @param do_sort Whether to sort entries
 */
static void glob_helper(
    const char *path,
    size_t baselen,
    size_t namelen,
    bool dirsep,
    rbc_segment_t *seg,
    const glob_funcs_t *funcs,
    bool do_sort)
{
    if (!seg)
    {
        /* No more segments - path is a match */
        funcs->callback(path, funcs->userdata);
        return;
    }

    size_t pathlen = baselen + namelen;

    /* Handle segment type */
    if (seg->type == RBC_SEGMENT_LITERAL)
    {
        /* Plain segment - check if path exists and continue */
        const char *literal = seg->data.literal;
        size_t litlen = strlen(literal);

        char *newpath = join_path(path, pathlen, dirsep, literal, litlen);
        if (!newpath)
            return;

        if (!seg->next)
        {
            /* Last segment - check existence and match */
            struct stat st;
            if (stat(newpath, &st) == 0)
            {
                funcs->callback(newpath, funcs->userdata);
            }
        }
        else
        {
            /* Continue to next segment */
            glob_helper(newpath, pathlen + dirsep, litlen, true,
                        seg->next, funcs, do_sort);
        }

        free(newpath);
        return;
    }

    /* Magical segment (wildcard or recursive) */
    glob_dir_t *gdir = glob_opendir(pathlen > 0 ? path : ".", do_sort);
    if (!gdir)
        return;

    rbc_dirent_t *dp;
    while ((dp = glob_getent(gdir)) != NULL)
    {
        const char *name = dp->d_name;
        size_t namlen = dp->d_namlen;

        /* Handle WILDCARD segment */
        if (seg->type == RBC_SEGMENT_WILDCARD)
        {
            const char *pattern = seg->data.glob.original_pattern;
            if (!pattern)
                continue;

            /* Check dotfile handling */
            if (name[0] == '.')
            {
                if (!(funcs->flags & RBC_FNM_DOTMATCH) && pattern[0] != '.')
                    continue;
            }

            /* Match pattern */
            if (!rbc_fnmatch(pattern, name, funcs->flags))
                continue;

            /* Build new path */
            char *newpath = join_path(path, pathlen, dirsep, name, namlen);
            if (!newpath)
                continue;

            if (!seg->next)
            {
                /* Last segment - match found */
                funcs->callback(newpath, funcs->userdata);
            }
            else
            {
                /* Continue if directory */
#ifdef _DIRENT_HAVE_D_TYPE
                unsigned char d_type = dp->d_type;
#else
                unsigned char d_type = DT_UNKNOWN;
#endif
                if (is_directory(newpath, d_type))
                {
                    glob_helper(newpath, pathlen + dirsep, namlen, true,
                                seg->next, funcs, do_sort);
                }
            }

            free(newpath);
        }
        /* Handle RECURSIVE segment (**) */
        else if (seg->type == RBC_SEGMENT_RECURSIVE)
        {
            /* Skip dot entries unless DOTMATCH */
            if (name[0] == '.' && !(funcs->flags & RBC_FNM_DOTMATCH))
                continue;

            /* Build new path */
            char *newpath = join_path(path, pathlen, dirsep, name, namlen);
            if (!newpath)
                continue;

#ifdef _DIRENT_HAVE_D_TYPE
            unsigned char d_type = dp->d_type;
#else
            unsigned char d_type = DT_UNKNOWN;
#endif
            bool is_dir = is_directory(newpath, d_type);

            /* If no next segment, ** matches everything */
            if (!seg->next)
            {
                funcs->callback(newpath, funcs->userdata);

                /* Recurse into subdirectories */
                if (is_dir)
                {
                    glob_helper(newpath, pathlen + dirsep, namlen, true,
                                seg, funcs, do_sort);
                }
            }
            else
            {
                /* Try matching next segment at this level */
                glob_helper(newpath, pathlen + dirsep, namlen, true,
                            seg->next, funcs, do_sort);

                /* Also recurse deeper */
                if (is_dir)
                {
                    glob_helper(newpath, pathlen + dirsep, namlen, true,
                                seg, funcs, do_sort);
                }
            }

            free(newpath);
        }
    }

    glob_dir_finish(gdir);
}

/* ========================================================================
 * Public API - Entry point for glob walker
 * ======================================================================== */

/**
 * Execute glob pattern with MRI-style recursive walker
 *
 * @param segments Linked list of pattern segments to match
 * @param callback Function to call for each match
 * @param userdata User data passed to callback
 * @param flags fnmatch-style flags (FNM_PATHNAME, FNM_DOTMATCH, etc.)
 * @param sort Whether to sort directory entries before matching
 * @return true on success, false on error
 */
bool rbc_glob_walk(
    rbc_segment_t *segments,
    rbc_match_callback_t callback,
    void *userdata,
    unsigned flags,
    bool sort)
{
    if (!segments || !callback)
        return false;

    glob_funcs_t funcs = {
        .callback = callback,
        .userdata = userdata,
        .flags = flags};

    /* Start glob from current directory or root */
    const char *start_path = "";
    size_t start_baselen = 0;
    size_t start_namelen = 0;
    bool start_dirsep = false;

    /* Handle absolute paths */
    if (segments->type == RBC_SEGMENT_LITERAL &&
        segments->data.literal &&
        segments->data.literal[0] == '/')
    {
        start_path = "";
        start_baselen = 0;
        start_namelen = 0;
        start_dirsep = false;
    }

    glob_helper(start_path, start_baselen, start_namelen, start_dirsep,
                segments, &funcs, sort);

    return true;
}
