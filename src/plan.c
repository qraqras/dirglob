/**
 * @file plan.c
 * @brief Multi-pattern execution plan compiler and executor
 *
 * This module implements the execution plan strategy for optimizing
 * multiple glob patterns by merging common directory traversals.
 */

#include "internal.h"
#include "rbc/rbc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#ifndef RBC_GLOB_NOSORT
#define RBC_GLOB_NOSORT 0x01
#endif

/* Import MRI-style walker types from walker.c */
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
        struct
        {
            DIR *dirp;
            rbc_dirent_t temp_ent;
        } stream;
        struct
        {
            rbc_dirent_t **entries;
            size_t count;
            size_t idx;
        } sorted;
    } u;
} glob_dir_t;

/* MRI-style walker functions from walker.c */
static glob_dir_t *glob_opendir(const char *path, bool do_sort);
static rbc_dirent_t *glob_getent(glob_dir_t *gdir);
static void glob_dir_finish(glob_dir_t *gdir);
static bool is_directory(const char *path, unsigned char d_type);

// Forward declarations
static rbc_plan_node_t *create_plan_node(rbc_arena_t *arena);
static bool add_pattern_to_plan(
    rbc_plan_node_t *root,
    size_t pattern_id,
    rbc_segment_t *segments,
    rbc_arena_t *arena,
    unsigned int flags);
static void execute_plan_node(
    rbc_plan_node_t *node,
    const char *current_path,
    rbc_results_t *results,
    unsigned int flags);

/**
 * @brief Create a new plan node
 */
static rbc_plan_node_t *create_plan_node(rbc_arena_t *arena)
{
    rbc_plan_node_t *node = rbc_arena_alloc(arena, sizeof(rbc_plan_node_t));
    if (!node)
        return NULL;

    memset(node, 0, sizeof(rbc_plan_node_t));
    return node;
}

/**
 * @brief Find or create a child node for the given segment
 */
static rbc_plan_node_t *find_or_create_child(
    rbc_plan_node_t *parent,
    const char *segment_str,
    bool is_literal,
    rbc_arena_t *arena)
{
    // Search for existing child
    for (size_t i = 0; i < parent->child_count; i++)
    {
        rbc_plan_node_t *child = parent->children[i];
        if (child->is_literal == is_literal &&
            strcmp(child->path_segment, segment_str) == 0)
        {
            return child;
        }
    }

    // Create new child
    rbc_plan_node_t *child = create_plan_node(arena);
    if (!child)
        return NULL;

    child->path_segment = rbc_arena_strdup(arena, segment_str);
    child->is_literal = is_literal;

    // Add to parent's children
    size_t new_count = parent->child_count + 1;
    rbc_plan_node_t **new_children = rbc_arena_alloc(
        arena, sizeof(rbc_plan_node_t *) * new_count);

    if (parent->children && parent->child_count > 0)
    {
        memcpy(new_children, parent->children,
               sizeof(rbc_plan_node_t *) * parent->child_count);
    }

    new_children[parent->child_count] = child;
    parent->children = new_children;
    parent->child_count = new_count;

    return child;
}

/**
 * @brief Add a pattern to the execution plan tree
 */
static bool add_pattern_to_plan(
    rbc_plan_node_t *root,
    size_t pattern_id,
    rbc_segment_t *segments,
    rbc_arena_t *arena,
    unsigned int flags)
{
    if (!segments)
        return false;

    rbc_plan_node_t *current = root;
    rbc_segment_t *seg = segments;

    // Walk through segments
    while (seg)
    {
        if (seg->type == RBC_SEGMENT_LITERAL)
        {
            // Literal segment: create/find child node
            current = find_or_create_child(current, seg->data.literal, true, arena);
            if (!current)
                return false;

            seg = seg->next;
        }
        else if (seg->type == RBC_SEGMENT_RECURSIVE)
        {
            // Recursive segment (**): mark node as recursive
            current->recursive = true;
            seg = seg->next;
        }
        else
        {
            // Wildcard or branch: add pattern at this level
            break;
        }
    }

    // Add pattern to current node
    size_t new_count = current->pattern_count + 1;
    rbc_plan_pattern_t *new_patterns = rbc_arena_alloc(
        arena, sizeof(rbc_plan_pattern_t) * new_count);

    if (current->patterns && current->pattern_count > 0)
    {
        memcpy(new_patterns, current->patterns,
               sizeof(rbc_plan_pattern_t) * current->pattern_count);
    }

    rbc_plan_pattern_t *ptn = &new_patterns[current->pattern_count];
    ptn->pattern_id = pattern_id;
    ptn->remaining_segs = seg;
    ptn->matcher = NULL;
    ptn->pattern_str = NULL;

    // Compile matcher if we have a wildcard segment
    if (seg && seg->type == RBC_SEGMENT_WILDCARD)
    {
        ptn->pattern_str = seg->data.glob.original_pattern;
        ptn->matcher = seg->data.glob.compiled;
    }

    current->patterns = new_patterns;
    current->pattern_count = new_count;

    return true;
}

/**
 * @brief Compile multiple patterns into an execution plan
 */
rbc_glob_plan_t *rbc_glob_plan_compile(
    const char **patterns,
    size_t count,
    unsigned int flags)
{
    if (!patterns || count == 0)
        return NULL;

    // Allocate plan structure
    rbc_glob_plan_t *plan = malloc(sizeof(rbc_glob_plan_t));
    if (!plan)
        return NULL;

    memset(plan, 0, sizeof(rbc_glob_plan_t));

    // Initialize arena
    if (!rbc_arena_init(&plan->arena, 4096))
    {
        free(plan);
        return NULL;
    }

    plan->pattern_count = count;
    plan->flags = flags;

    // Store original patterns
    plan->original_patterns = rbc_arena_alloc(
        &plan->arena, sizeof(char *) * count);
    for (size_t i = 0; i < count; i++)
    {
        plan->original_patterns[i] = rbc_arena_strdup(&plan->arena, patterns[i]);
    }

    // Create root node
    plan->root = create_plan_node(&plan->arena);
    if (!plan->root)
    {
        rbc_glob_plan_free(plan);
        return NULL;
    }

    plan->root->path_segment = rbc_arena_strdup(&plan->arena, ".");
    plan->root->is_literal = true;

    // Parse and add each pattern to the plan
    for (size_t i = 0; i < count; i++)
    {
        rbc_segment_t *segments = rbc_glob_segment_compile(
            &plan->arena, patterns[i], flags);

        if (!segments)
            continue;

        if (!add_pattern_to_plan(plan->root, i, segments, &plan->arena, flags))
        {
            // Continue with other patterns even if one fails
            continue;
        }
    }

    return plan;
}

/**
 * @brief Build full path by concatenating current_path and entry name
 */
static char *build_path(const char *dir, const char *name, char *buffer, size_t bufsize)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);

    if (dir_len + name_len + 2 > bufsize)
        return NULL;

    char *p = buffer;
    memcpy(p, dir, dir_len);
    p += dir_len;

    if (dir_len > 0 && dir[dir_len - 1] != '/')
    {
        *p++ = '/';
    }

    memcpy(p, name, name_len);
    p += name_len;
    *p = '\0';

    return buffer;
}

/**
 * @brief Check if a directory entry matches any pattern in the node
 */
static bool matches_any_pattern(
    rbc_plan_node_t *node,
    const char *name,
    unsigned int flags,
    size_t *matched_pattern_id)
{
    for (size_t i = 0; i < node->pattern_count; i++)
    {
        rbc_plan_pattern_t *ptn = &node->patterns[i];

        if (!ptn->remaining_segs)
        {
            // No remaining segments means exact match
            *matched_pattern_id = ptn->pattern_id;
            return true;
        }

        // Match against first remaining segment
        if (ptn->matcher && rbc_xfnmatch(ptn->matcher, name, flags))
        {
            *matched_pattern_id = ptn->pattern_id;
            return true;
        }
        else if (ptn->remaining_segs &&
                 rbc_segment_match(ptn->remaining_segs, name, flags))
        {
            *matched_pattern_id = ptn->pattern_id;
            return true;
        }
    }

    return false;
}

/**
 * @brief Execute a plan node recursively (MRI-style)
 */
static void execute_plan_node(
    rbc_plan_node_t *node,
    const char *current_path,
    rbc_results_t *results,
    unsigned int flags)
{
    if (!node)
        return;

    // Open directory using MRI-style walker
    bool do_sort = !(flags & RBC_GLOB_NOSORT);
    glob_dir_t *gdir = glob_opendir(current_path, do_sort);
    if (!gdir)
        return;

    rbc_dirent_t *dp;
    char path_buffer[PATH_MAX];

    // Iterate using MRI-style glob_getent (handles sorted/nosort automatically)
    while ((dp = glob_getent(gdir)) != NULL)
    {
        const char *name = dp->d_name;

        // Check if matches any pattern at this level
        size_t matched_id;
        bool is_pattern_match = matches_any_pattern(node, name, flags, &matched_id);

        // Check if entry is a directory (MRI pattern)
#ifdef _DIRENT_HAVE_D_TYPE
        unsigned char d_type = dp->d_type;
#else
        unsigned char d_type = DT_UNKNOWN;
#endif

        // Build full path for directory check
        char check_path[PATH_MAX];
        if (!build_path(current_path, name, check_path, sizeof(check_path)))
            continue;

        bool is_dir = is_directory(check_path, d_type) || d_type == DT_UNKNOWN;

        // If matched and the pattern has no remaining segments, add to results
        if (is_pattern_match)
        {
            rbc_plan_pattern_t *matched_ptn = &node->patterns[0];
            for (size_t i = 0; i < node->pattern_count; i++)
            {
                if (node->patterns[i].pattern_id == matched_id)
                {
                    matched_ptn = &node->patterns[i];
                    break;
                }
            }

            // Only add if this is the final segment (no remaining_segs->next)
            bool is_final = !matched_ptn->remaining_segs ||
                            !matched_ptn->remaining_segs->next;

            if (is_final)
            {
                if (!build_path(current_path, name, path_buffer, sizeof(path_buffer)))
                    continue;

                rbc_glob_results_add_with_index(results, path_buffer, matched_id);
            }
        }

        // Check if we should descend into this directory
        bool should_descend = false;

        if (is_dir)
        {
            // Check if any child node matches this directory
            for (size_t i = 0; i < node->child_count; i++)
            {
                rbc_plan_node_t *child = node->children[i];
                if (child->is_literal && strcmp(child->path_segment, name) == 0)
                {
                    // Exact match: descend into this specific child
                    if (!build_path(current_path, name, path_buffer, sizeof(path_buffer)))
                        continue;

                    execute_plan_node(child, path_buffer, results, flags);
                    should_descend = true;
                    break;
                }
            }

            // If recursive, descend into all directories
            if (node->recursive && !should_descend)
            {
                if (!build_path(current_path, name, path_buffer, sizeof(path_buffer)))
                    continue;

                execute_plan_node(node, path_buffer, results, flags);
            }
        }
    }

    // Cleanup using MRI-style walker
    glob_dir_finish(gdir);
}

/**
 * @brief Execute the compiled execution plan
 */
char **rbc_glob_plan_execute(
    rbc_glob_plan_t *plan,
    const char *basedir,
    size_t *result_count)
{
    if (!plan || !result_count)
        return NULL;

    // Initialize context and results
    rbc_ctx_t ctx;
    rbc_results_t results;

    if (!rbc_glob_ctx_init(&ctx))
        return NULL;

    if (!rbc_glob_results_init(&results, &ctx))
    {
        rbc_glob_ctx_free(&ctx);
        return NULL;
    }

    // Execute the plan starting from root
    const char *base = basedir ? basedir : ".";

    // Optimization: If root has no patterns and only one literal child,
    // skip opening the base directory and go directly to the child
    if (plan->root->pattern_count == 0 &&
        plan->root->child_count == 1 &&
        plan->root->children[0]->is_literal)
    {
        rbc_plan_node_t *child = plan->root->children[0];
        char path_buffer[PATH_MAX];

        // Build path to child
        size_t base_len = strlen(base);
        size_t seg_len = strlen(child->path_segment);

        if (base_len == 0 || (base_len == 1 && base[0] == '.'))
        {
            // base is "." or "" -> use child segment directly
            snprintf(path_buffer, sizeof(path_buffer), "%s", child->path_segment);
        }
        else
        {
            // base is non-empty -> concatenate
            snprintf(path_buffer, sizeof(path_buffer), "%s/%s", base, child->path_segment);
        }

        execute_plan_node(child, path_buffer, &results, plan->flags);
    }
    else
    {
        // Normal execution from root
        execute_plan_node(plan->root, base, &results, plan->flags);
    }

    // Sort results if needed
    if (!(plan->flags & RBC_GLOB_NOSORT))
    {
        rbc_glob_results_sort(&results);
    }

    *result_count = results.count;
    return results.items;
}

/**
 * @brief Free the execution plan
 */
void rbc_glob_plan_free(rbc_glob_plan_t *plan)
{
    if (!plan)
        return;

    rbc_arena_destroy(&plan->arena);
    free(plan);
}

/* ========================================================================
 * MRI-style walker functions (copied from walker.c for plan.c)
 * ======================================================================== */

static int glob_sort_cmp(const void *a, const void *b)
{
    const rbc_dirent_t *ea = *(const rbc_dirent_t **)a;
    const rbc_dirent_t *eb = *(const rbc_dirent_t **)b;
    return strcmp(ea->d_name, eb->d_name);
}

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
        gdir->nosort = true;
        gdir->u.stream.dirp = dirp;
        return gdir;
    }

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
        if (dp->d_name[0] == '.' &&
            (dp->d_name[1] == '\0' ||
             (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
            continue;

        if (gdir->u.sorted.count >= capacity)
        {
            capacity *= 2;
            rbc_dirent_t **new_entries = realloc(gdir->u.sorted.entries,
                                                 sizeof(rbc_dirent_t *) * capacity);
            if (!new_entries)
            {
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

    closedir(dirp);

    if (gdir->u.sorted.count > 0)
    {
        qsort(gdir->u.sorted.entries, gdir->u.sorted.count,
              sizeof(rbc_dirent_t *), glob_sort_cmp);
    }

    return gdir;
}

static rbc_dirent_t *glob_getent(glob_dir_t *gdir)
{
    if (!gdir)
        return NULL;

    if (gdir->nosort)
    {
        struct dirent *dp = readdir(gdir->u.stream.dirp);
        if (!dp)
            return NULL;

        while (dp && dp->d_name[0] == '.' &&
               (dp->d_name[1] == '\0' ||
                (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
        {
            dp = readdir(gdir->u.stream.dirp);
        }

        if (!dp)
            return NULL;

        gdir->u.stream.temp_ent.d_name = dp->d_name;
        gdir->u.stream.temp_ent.d_namlen = strlen(dp->d_name);
#ifdef _DIRENT_HAVE_D_TYPE
        gdir->u.stream.temp_ent.d_type = dp->d_type;
#endif
        return &gdir->u.stream.temp_ent;
    }
    else
    {
        if (gdir->u.sorted.idx < gdir->u.sorted.count)
        {
            return gdir->u.sorted.entries[gdir->u.sorted.idx++];
        }
        return NULL;
    }
}

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

static bool is_directory(const char *path, unsigned char d_type)
{
#ifdef _DIRENT_HAVE_D_TYPE
    if (d_type == DT_DIR)
        return true;
    if (d_type != DT_UNKNOWN && d_type != DT_LNK)
        return false;
#endif

    struct stat st;
    if (stat(path, &st) == 0)
    {
        return S_ISDIR(st.st_mode);
    }
    return false;
}
