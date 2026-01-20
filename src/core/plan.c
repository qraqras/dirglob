/**
 * @file plan.c
 * @brief Multi-pattern execution plan compiler and executor
 *
 * This module implements the execution plan strategy for optimizing
 * multiple glob patterns by merging common directory traversals.
 *
 * Design:
 * - Multiple patterns with common directory prefixes are merged into a tree
 * - Each directory is opened only once, and all patterns are checked
 * - Example: "src/*.c" and "src/*.h" share the "src" node
 *
 * Tree structure for ["src/*.c", "src/*.h", "include/*.h"]:
 *   root
 *   ├── src/           (opened once)
 *   │   ├── *.c        (pattern 0)
 *   │   └── *.h        (pattern 1)
 *   └── include/       (opened once)
 *       └── *.h        (pattern 2)
 */

#define _DEFAULT_SOURCE /* for DT_* constants */
#define _BSD_SOURCE

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

/* Fallback definitions for d_type */
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif
#ifndef DT_DIR
#define DT_DIR 4
#endif
#ifndef DT_LNK
#define DT_LNK 10
#endif

/* ========================================================================
 * Internal structures
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

/* Forward declarations */
static glob_dir_t *glob_opendir(const char *path, bool do_sort);
static rbc_dirent_t *glob_getent(glob_dir_t *gdir);
static void glob_dir_finish(glob_dir_t *gdir);
static bool is_directory(const char *path, unsigned char d_type);

/* ========================================================================
 * Plan node creation and management
 * ======================================================================== */

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
 * @brief Find existing child node with matching segment
 */
static rbc_plan_node_t *find_child(
    rbc_plan_node_t *parent,
    const char *segment_str)
{
    for (size_t i = 0; i < parent->child_count; i++)
    {
        rbc_plan_node_t *child = parent->children[i];
        if (child->is_literal && strcmp(child->path_segment, segment_str) == 0)
        {
            return child;
        }
    }
    return NULL;
}

/**
 * @brief Add a child node to parent
 */
static bool add_child(
    rbc_plan_node_t *parent,
    rbc_plan_node_t *child,
    rbc_arena_t *arena)
{
    size_t new_count = parent->child_count + 1;
    rbc_plan_node_t **new_children = rbc_arena_alloc(
        arena, sizeof(rbc_plan_node_t *) * new_count);
    if (!new_children)
        return false;

    if (parent->children && parent->child_count > 0)
    {
        memcpy(new_children, parent->children,
               sizeof(rbc_plan_node_t *) * parent->child_count);
    }

    new_children[parent->child_count] = child;
    parent->children = new_children;
    parent->child_count = new_count;

    return true;
}

/**
 * @brief Add a pattern to a node
 */
static bool add_pattern_to_node(
    rbc_plan_node_t *node,
    size_t pattern_id,
    rbc_segment_t *remaining_seg,
    rbc_arena_t *arena)
{
    size_t new_count = node->pattern_count + 1;
    rbc_plan_pattern_t *new_patterns = rbc_arena_alloc(
        arena, sizeof(rbc_plan_pattern_t) * new_count);
    if (!new_patterns)
        return false;

    if (node->patterns && node->pattern_count > 0)
    {
        memcpy(new_patterns, node->patterns,
               sizeof(rbc_plan_pattern_t) * node->pattern_count);
    }

    rbc_plan_pattern_t *ptn = &new_patterns[node->pattern_count];
    ptn->pattern_id = pattern_id;
    ptn->remaining_segs = remaining_seg;
    ptn->matcher = NULL;
    ptn->pattern_str = NULL;

    /* Pre-compile matcher for wildcard segments */
    if (remaining_seg && remaining_seg->type == RBC_SEGMENT_WILDCARD)
    {
        ptn->pattern_str = remaining_seg->data.glob.original_pattern;
        ptn->matcher = remaining_seg->data.glob.compiled;
    }

    node->patterns = new_patterns;
    node->pattern_count = new_count;

    return true;
}

/* ========================================================================
 * Pattern to tree conversion
 * ======================================================================== */

/**
 * @brief Insert a single pattern into the plan tree
 *
 * Walks through literal segments to find/create the deepest common node,
 * then attaches the remaining pattern (wildcard/recursive) at that node.
 */
static bool insert_pattern_into_tree(
    rbc_plan_node_t *root,
    size_t pattern_id,
    rbc_segment_t *segments,
    rbc_arena_t *arena)
{
    if (!segments)
        return false;

    rbc_plan_node_t *current = root;
    rbc_segment_t *seg = segments;

    /* Walk through consecutive literal segments, merging into tree */
    while (seg && seg->type == RBC_SEGMENT_LITERAL)
    {
        const char *lit = seg->data.literal;

        /* Find existing child or create new one */
        rbc_plan_node_t *child = find_child(current, lit);
        if (!child)
        {
            child = create_plan_node(arena);
            if (!child)
                return false;

            child->path_segment = rbc_arena_strdup(arena, lit);
            child->is_literal = true;

            if (!add_child(current, child, arena))
                return false;
        }

        current = child;
        seg = seg->next;
    }

    /* Handle recursive segment (**) */
    if (seg && seg->type == RBC_SEGMENT_RECURSIVE)
    {
        current->recursive = true;
        seg = seg->next;
    }

    /* Attach remaining pattern (wildcard, branch, or end) to current node */
    return add_pattern_to_node(current, pattern_id, seg, arena);
}

/* ========================================================================
 * Plan compilation
 * ======================================================================== */

/**
 * @brief Calculate sharing score to determine if plan execution is beneficial
 *
 * Returns true if plan execution is beneficial:
 * - If any node has 2+ patterns → sharing exists → use plan
 * - If all patterns are in different directories → use individual
 */
static bool should_use_plan(rbc_plan_node_t *root, size_t total_patterns)
{
    if (!root || total_patterns <= 1)
        return false;

    /* Count nodes with multiple patterns (sharing) */
    size_t nodes_with_sharing = 0;
    size_t total_nodes_with_patterns = 0;

    void count_recursive(rbc_plan_node_t * node)
    {
        if (!node)
            return;

        if (node->pattern_count > 0)
        {
            total_nodes_with_patterns++;
            if (node->pattern_count >= 2)
            {
                nodes_with_sharing++;
            }
        }

        for (size_t i = 0; i < node->child_count; i++)
        {
            count_recursive(node->children[i]);
        }
    }

    count_recursive(root);

    /* If we have at least one node with 2+ patterns, there's sharing benefit */
    if (nodes_with_sharing > 0)
        return true;

    /* If all patterns are in separate nodes, no sharing benefit */
    /* But if there are many patterns, plan might still be faster for cache locality */
    return total_patterns >= 5;
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

    rbc_glob_plan_t *plan = malloc(sizeof(rbc_glob_plan_t));
    if (!plan)
        return NULL;

    memset(plan, 0, sizeof(rbc_glob_plan_t));

    if (!rbc_arena_init(&plan->arena, 4096))
    {
        free(plan);
        return NULL;
    }

    plan->pattern_count = count;
    plan->flags = flags;

    /* Store original patterns */
    plan->original_patterns = rbc_arena_alloc(
        &plan->arena, sizeof(char *) * count);
    for (size_t i = 0; i < count; i++)
    {
        plan->original_patterns[i] = rbc_arena_strdup(&plan->arena, patterns[i]);
    }

    /* Create root node */
    plan->root = create_plan_node(&plan->arena);
    if (!plan->root)
    {
        rbc_glob_plan_free(plan);
        return NULL;
    }

    plan->root->path_segment = rbc_arena_strdup(&plan->arena, "");
    plan->root->is_literal = true;

    /* Parse each pattern and insert into tree */
    for (size_t i = 0; i < count; i++)
    {
        const char *pattern = patterns[i];

        /* Check if pattern contains braces and needs expansion */
        if (rbc_has_brace(pattern))
        {
            /* Expand braces and insert each expanded pattern */
            rbc_str_list_t expanded = rbc_brace_collect(pattern, &plan->arena);

            for (size_t j = 0; j < expanded.count; j++)
            {
                rbc_segment_t *segments = rbc_glob_segment_compile(
                    &plan->arena, expanded.items[j], flags);

                if (segments)
                {
                    insert_pattern_into_tree(plan->root, i, segments, &plan->arena);
                }
            }
        }
        else
        {
            /* No braces: compile and insert directly */
            rbc_segment_t *segments = rbc_glob_segment_compile(
                &plan->arena, pattern, flags);

            if (segments)
            {
                insert_pattern_into_tree(plan->root, i, segments, &plan->arena);
            }
        }
    }

    /* Calculate if plan execution is beneficial */
    bool use_plan = should_use_plan(plan->root, plan->pattern_count);

    /* Invert logic: use_individual_execution = !use_plan */
    plan->use_individual_execution = !use_plan;

    return plan;
}

/* ========================================================================
 * Plan execution
 * ======================================================================== */

/**
 * @brief Build full path from directory and name
 */
static bool build_path(
    const char *dir,
    const char *name,
    char *buffer,
    size_t bufsize)
{
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);

    if (dir_len + name_len + 2 > bufsize)
        return false;

    if (dir_len == 0)
    {
        memcpy(buffer, name, name_len + 1);
        return true;
    }

    memcpy(buffer, dir, dir_len);
    if (dir[dir_len - 1] != '/')
    {
        buffer[dir_len++] = '/';
    }
    memcpy(buffer + dir_len, name, name_len + 1);

    return true;
}

/**
 * @brief Check if entry matches a pattern segment
 */
static bool matches_segment(
    rbc_plan_pattern_t *ptn,
    const char *name,
    unsigned int flags)
{
    rbc_segment_t *seg = ptn->remaining_segs;

    if (!seg)
    {
        /* No remaining segment = pattern ended at directory level */
        return false;
    }

    if (seg->type == RBC_SEGMENT_WILDCARD)
    {
        /* Use pre-compiled matcher if available */
        if (ptn->matcher)
        {
            return rbc_xfnmatch(ptn->matcher, name, flags);
        }
        return rbc_segment_match(seg, name, flags);
    }
    else if (seg->type == RBC_SEGMENT_LITERAL)
    {
        return strcmp(seg->data.literal, name) == 0;
    }
    else if (seg->type == RBC_SEGMENT_BRANCH)
    {
        return rbc_segment_match(seg, name, flags);
    }

    return false;
}

/**
 * @brief Check if pattern is complete (no more segments after current match)
 */
static bool is_pattern_terminal(rbc_plan_pattern_t *ptn)
{
    if (!ptn->remaining_segs)
        return true;
    return ptn->remaining_segs->next == NULL;
}

/**
 * @brief Execute plan at a specific node
 *
 * Core execution logic:
 * 1. Open directory ONCE
 * 2. For each entry, check ALL patterns at this node
 * 3. Recursively descend into child nodes (literal directories)
 */
static void execute_node(
    rbc_plan_node_t *node,
    const char *current_path,
    rbc_results_t *results,
    unsigned int flags)
{
    if (!node)
        return;

    bool do_sort = !(flags & RBC_GLOB_NOSORT);
    glob_dir_t *gdir = glob_opendir(current_path, do_sort);
    if (!gdir)
        return;

    rbc_dirent_t *dp;
    char path_buffer[PATH_MAX];

    while ((dp = glob_getent(gdir)) != NULL)
    {
        const char *name = dp->d_name;

#ifdef _DIRENT_HAVE_D_TYPE
        unsigned char d_type = dp->d_type;
#else
        unsigned char d_type = DT_UNKNOWN;
#endif

        /* Build full path for this entry */
        if (!build_path(current_path, name, path_buffer, sizeof(path_buffer)))
            continue;

        /* Check all patterns registered at this node */
        for (size_t i = 0; i < node->pattern_count; i++)
        {
            rbc_plan_pattern_t *ptn = &node->patterns[i];

            if (matches_segment(ptn, name, flags))
            {
                if (is_pattern_terminal(ptn))
                {
                    /* Pattern complete: add to results */
                    rbc_glob_results_add_with_index(results, path_buffer, ptn->pattern_id);
                }
                else
                {
                    /* Pattern continues: need to descend (TODO: multi-level patterns) */
                    /* For now, handle single remaining wildcard only */
                }
            }
        }

        /* Check if this entry is a directory for descending */
        bool is_dir = is_directory(path_buffer, d_type);

        if (is_dir)
        {
            /* Check if any child node matches this directory name */
            rbc_plan_node_t *child = find_child(node, name);
            if (child)
            {
                /* Descend into matched child node */
                execute_node(child, path_buffer, results, flags);
            }

            /* Handle recursive (**) patterns */
            if (node->recursive)
            {
                /* For recursive, we re-execute the same node on subdirectory */
                execute_node(node, path_buffer, results, flags);
            }
        }
    }

    glob_dir_finish(gdir);
}

/**
 * @brief Start execution from root, handling base directory
 */
static void execute_from_root(
    rbc_plan_node_t *root,
    const char *basedir,
    rbc_results_t *results,
    unsigned int flags)
{
    if (!root)
        return;

    const char *base = (basedir && basedir[0]) ? basedir : ".";
    char path_buffer[PATH_MAX];

    /* If root has no patterns, directly execute children */
    if (root->pattern_count == 0 && root->child_count > 0)
    {
        /* Execute each child node from its literal path */
        for (size_t i = 0; i < root->child_count; i++)
        {
            rbc_plan_node_t *child = root->children[i];
            if (child->is_literal)
            {
                /* Build path: base/child_segment */
                if (strcmp(base, ".") == 0)
                {
                    snprintf(path_buffer, sizeof(path_buffer), "%s", child->path_segment);
                }
                else
                {
                    snprintf(path_buffer, sizeof(path_buffer), "%s/%s", base, child->path_segment);
                }

                execute_node(child, path_buffer, results, flags);
            }
        }
    }
    else
    {
        /* Root has patterns: execute from base directory */
        execute_node(root, base, results, flags);
    }
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

    /* Check if we should use individual execution instead */
    if (plan->use_individual_execution)
    {
        /* Fallback to individual rbc_glob execution */
        rbc_ctx_t ctx;
        rbc_results_t results;

        if (!rbc_glob_ctx_init(&ctx))
            return NULL;

        if (!rbc_glob_results_init(&results, &ctx))
        {
            rbc_glob_ctx_free(&ctx);
            return NULL;
        }

        /* Execute each pattern individually */
        for (size_t i = 0; i < plan->pattern_count; i++)
        {
            char **pattern_results = NULL;
            size_t count = 0;
            size_t *lengths = NULL;
            const char *pattern = plan->original_patterns[i];

            if (rbc_glob(&pattern, 1, plan->flags,
                         basedir, false, &pattern_results, &count, &lengths))
            {
                /* Add results with pattern index */
                for (size_t j = 0; j < count; j++)
                {
                    rbc_glob_results_add_with_index(&results, pattern_results[j], i);
                }

                rbc_glob_free(pattern_results, count, lengths);
            }
        }

        /* Sort and deduplicate */
        if (!(plan->flags & RBC_GLOB_NOSORT))
        {
            rbc_glob_results_sort(&results);
        }

        *result_count = results.count;
        return results.items;
    }

    /* Use plan-based execution */
    rbc_ctx_t ctx;
    rbc_results_t results;

    if (!rbc_glob_ctx_init(&ctx))
        return NULL;

    if (!rbc_glob_results_init(&results, &ctx))
    {
        rbc_glob_ctx_free(&ctx);
        return NULL;
    }

    /* Execute the plan */
    execute_from_root(plan->root, basedir, &results, plan->flags);

    /* Sort and deduplicate results */
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
 * MRI-style directory walker (internal)
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
        /* Skip . and .. */
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
#else
    (void)d_type;
#endif

    struct stat st;
    if (stat(path, &st) == 0)
    {
        return S_ISDIR(st.st_mode);
    }
    return false;
}
