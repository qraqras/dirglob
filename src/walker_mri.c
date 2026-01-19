/**
 * @file walker_mri.c
 * @brief MRI-style glob walker implementation
 *
 * This implementation closely follows MRI Ruby's dir.c:glob_helper()
 * Reference: https://github.com/ruby/ruby/blob/main/dir.c
 *
 * Key characteristics from MRI:
 * 1. Single glob_helper() recursive function
 * 2. Pattern-by-pattern processing (not segment-based)
 * 3. Integrated opendir/readdir/closedir flow
 * 4. Optional sorting with glob_opendir/glob_getent
 * 5. Match detection for plain/magical/recursive patterns
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
 * MRI-style Directory Entry Cache (for sorting)
 * Equivalent to ruby_glob_entries_t in MRI dir.c
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
        /* For nosort: keep DIR* open and stream */
        struct
        {
            DIR *dirp;
        } stream;

        /* For sort: read all, sort, iterate */
        struct
        {
            rbc_dirent_t **entries;
            size_t count;
            size_t idx;
        } sorted;
    } u;
} glob_dir_t;

/* ========================================================================
 * MRI-style glob_opendir() - Open directory and optionally sort
 * Reference: MRI dir.c:2608 glob_opendir()
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
        /* NOSORT: Just keep DIR* open for streaming */
        gdir->nosort = true;
        gdir->u.stream.dirp = dirp;
        return gdir;
    }

    /* SORT: Read all entries into array */
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
        /* Skip . and .. */
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
                /* Cleanup and fail */
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

        /* Copy dirent */
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

    closedir(dirp); /* MRI closes DIR* immediately after reading */

    /* Sort entries (MRI uses ruby_qsort) */
    if (gdir->u.sorted.count > 0)
    {
        qsort(gdir->u.sorted.entries, gdir->u.sorted.count, sizeof(rbc_dirent_t *), (int (*)(const void *, const void *)) [](const void *a, const void *b) -> int
              {
                  const rbc_dirent_t *ea = *(const rbc_dirent_t**)a;
                  const rbc_dirent_t *eb = *(const rbc_dirent_t**)b;
                  return strcmp(ea->d_name, eb->d_name); });
    }

    return gdir;
}

/* ========================================================================
 * MRI-style glob_getent() - Get next directory entry
 * Reference: MRI dir.c:2684 glob_getent()
 * ======================================================================== */

static rbc_dirent_t *glob_getent(glob_dir_t *gdir)
{
    if (gdir->nosort)
    {
        /* Stream mode: read directly from DIR* */
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
        static rbc_dirent_t temp_ent;
        temp_ent.d_name = dp->d_name;
        temp_ent.d_namlen = strlen(dp->d_name);
#ifdef _DIRENT_HAVE_D_TYPE
        temp_ent.d_type = dp->d_type;
#endif
        return &temp_ent;
    }
    else
    {
        /* Sorted mode: iterate over array */
        if (gdir->u.sorted.idx < gdir->u.sorted.count)
        {
            return gdir->u.sorted.entries[gdir->u.sorted.idx++];
        }
        return NULL;
    }
}

/* ========================================================================
 * MRI-style glob_dir_finish() - Close and cleanup
 * Reference: MRI dir.c:2608 glob_dir_finish()
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
 * Path manipulation helpers
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

    memcpy(buf, dir, dirlen);
    if (dirsep)
        buf[dirlen] = '/';
    memcpy(buf + dirlen + (dirsep ? 1 : 0), base, baselen);
    buf[len] = '\0';

    return buf;
}

/* ========================================================================
 * Pattern analysis (MRI checks for magical/recursive patterns)
 * Reference: MRI dir.c:2726 glob_helper()
 * ======================================================================== */

typedef struct glob_pattern
{
    const char *str;
    size_t len;
    bool magical;   /* Contains *, ?, [], {} */
    bool recursive; /* Contains ** */
    struct glob_pattern *next;
} glob_pattern_t;

static bool is_magical(const char *pattern, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        char c = pattern[i];
        if (c == '*' || c == '?' || c == '[' || c == '{')
            return true;
    }
    return false;
}

static bool is_recursive(const char *pattern, size_t len)
{
    for (size_t i = 0; i + 1 < len; i++)
    {
        if (pattern[i] == '*' && pattern[i + 1] == '*')
            return true;
    }
    return false;
}

/* ========================================================================
 * MRI-style glob_helper() - Main recursive glob function
 * Reference: MRI dir.c:2694 glob_helper()
 *
 * This is the heart of MRI's glob implementation
 * ======================================================================== */

typedef struct
{
    rbc_match_callback_t callback;
    void *userdata;
    unsigned flags;
} glob_funcs_t;

static int glob_helper(
    const char *path,
    size_t baselen,
    size_t namelen,
    bool dirsep,
    glob_pattern_t **patterns_beg,
    glob_pattern_t **patterns_end,
    const glob_funcs_t *funcs,
    bool do_sort)
{
    /* Check for plain/magical/recursive patterns */
    bool plain = true, magical = false, recursive = false;
    bool match_all = false, match_dir = false;

    for (glob_pattern_t **p = patterns_beg; p < patterns_end; p++)
    {
        if ((*p)->magical)
        {
            magical = true;
            plain = false;
        }
        if ((*p)->recursive)
        {
            recursive = true;
            plain = false;
        }
        /* Check if pattern matches everything */
        if (strcmp((*p)->str, "*") == 0 ||
            strcmp((*p)->str, "**") == 0)
        {
            match_all = true;
        }
        /* Check if pattern is directory match */
        if ((*p)->len > 0 && (*p)->str[(*p)->len - 1] == '/')
        {
            match_dir = true;
        }
    }

    /* If plain (no wildcards), check if path exists */
    if (plain)
    {
        struct stat st;
        if (stat(path, &st) == 0)
        {
            funcs->callback(path, funcs->userdata);
        }
        return 0;
    }

    /* Match current level if needed */
    if (match_all && baselen + namelen > 0)
    {
        funcs->callback(path, funcs->userdata);
    }

    /* Open directory and scan */
    if (magical || recursive)
    {
        glob_dir_t *gdir = glob_opendir(path, do_sort);
        if (!gdir)
            return 0;

        rbc_dirent_t *dp;
        while ((dp = glob_getent(gdir)) != NULL)
        {
            const char *name = dp->d_name;
            size_t namlen = dp->d_namlen;

            /* Check patterns */
            for (glob_pattern_t **p = patterns_beg; p < patterns_end; p++)
            {
                if (rbc_fnmatch((*p)->str, name, funcs->flags) == 0)
                {
                    /* Match found - build full path */
                    char *newpath = join_path(path, baselen + namelen,
                                              dirsep, name, namlen);
                    if (newpath)
                    {
                        /* Check if this is final match or needs recursion */
                        if (p + 1 >= patterns_end)
                        {
                            /* Last pattern - report match */
                            funcs->callback(newpath, funcs->userdata);
                        }
                        else
                        {
                            /* Continue to next pattern */
                            struct stat st;
                            if (stat(newpath, &st) == 0 && S_ISDIR(st.st_mode))
                            {
                                glob_helper(newpath, baselen + namelen + dirsep,
                                            namlen, true,
                                            p + 1, patterns_end,
                                            funcs, do_sort);
                            }
                        }
                        free(newpath);
                    }
                    break; /* Pattern matched, no need to check others */
                }
            }

            /* Handle recursive patterns */
            if (recursive)
            {
                /* Recurse into subdirectories */
#ifdef _DIRENT_HAVE_D_TYPE
                if (dp->d_type == DT_DIR || dp->d_type == DT_UNKNOWN)
#endif
                {
                    char *newpath = join_path(path, baselen + namelen,
                                              dirsep, name, namlen);
                    if (newpath)
                    {
                        struct stat st;
                        if (stat(newpath, &st) == 0 && S_ISDIR(st.st_mode))
                        {
                            glob_helper(newpath, baselen + namelen + dirsep,
                                        namlen, true,
                                        patterns_beg, patterns_end,
                                        funcs, do_sort);
                        }
                        free(newpath);
                    }
                }
            }
        }

        glob_dir_finish(gdir);
    }

    return 0;
}

/* ========================================================================
 * Public API - MRI-compatible entry point
 * ======================================================================== */

bool rbc_glob_walk_mri(
    rbc_segment_t *segments,
    rbc_match_callback_t callback,
    void *userdata,
    unsigned flags,
    bool sort)
{
    if (!segments || !callback)
        return false;

    /* Convert segments to MRI-style pattern array */
    /* TODO: Implement segment-to-pattern conversion */

    glob_funcs_t funcs = {
        .callback = callback,
        .userdata = userdata,
        .flags = flags};

    /* Start glob from current directory */
    glob_pattern_t *patterns[16]; /* TODO: dynamic allocation */
    int pattern_count = 0;

    /* Convert segments to patterns */
    for (rbc_segment_t *seg = segments; seg && pattern_count < 16; seg = seg->next)
    {
        glob_pattern_t *pat = malloc(sizeof(glob_pattern_t));
        if (!pat)
            break;

        if (seg->type == RBC_SEGMENT_LITERAL)
        {
            pat->str = seg->data.literal;
            pat->len = strlen(pat->str);
            pat->magical = false;
            pat->recursive = false;
        }
        else if (seg->type == RBC_SEGMENT_WILDCARD)
        {
            pat->str = seg->data.glob.original_pattern;
            pat->len = strlen(pat->str);
            pat->magical = is_magical(pat->str, pat->len);
            pat->recursive = false;
        }
        else if (seg->type == RBC_SEGMENT_RECURSIVE)
        {
            pat->str = "**";
            pat->len = 2;
            pat->magical = true;
            pat->recursive = true;
        }

        pat->next = NULL;
        patterns[pattern_count++] = pat;
    }

    /* Execute glob */
    glob_helper(".", 0, 0, false,
                patterns, patterns + pattern_count,
                &funcs, sort);

    /* Cleanup */
    for (int i = 0; i < pattern_count; i++)
    {
        free(patterns[i]);
    }

    return true;
}
