/**
 * @file trie.c
 * @brief Complete Trie-based multi-pattern glob execution
 *
 * This module implements a complete trie structure for optimizing
 * multiple glob patterns by:
 * 1. Merging common directory prefixes (literal segments)
 * 2. Grouping wildcard patterns at the same level
 * 3. Sharing recursive (**) traversals across patterns
 *
 * Example: ["src/ ** / *.c", "src/ ** / *.h", "lib/ ** / *.c"]
 *   (spaces added to avoid comment parsing issues)
 *
 * Trie structure:
 *   [ROOT]
 *   +-- [src] (literal)
 *   |   +-- [**] (recursive)
 *   |       +-- [MULTI: *.c, *.h] (grouped wildcards)
 *   +-- [lib] (literal)
 *       +-- [**] (recursive)
 *           +-- [*.c] (wildcard)
 *
 * Benefits:
 * - Each directory is opened exactly ONCE
 * - All patterns at a node are matched in a single pass
 * - Recursive patterns share traversal
 */

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "internal.h"
#include "rbc/rbc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

/* ========================================================================
 * Brace Expansion (Fixed-size array, max 64 options)
 * ======================================================================== */

bool rbc_brace_visit(const char *pattern, rbc_arena_t *arena, rbc_brace_visit_cb cb, void *arg)
{
    const char *p = pattern;
    bool in_brace = false;

    while (*p)
    {
        if (*p == '\\')
        {
            p += 2;
            continue;
        }
        if (*p == '{')
        {
            in_brace = true;
            break;
        }
        p++;
    }

    if (!in_brace)
        return cb(pattern, arg);

    /* Fixed-size array for brace options */
    const char *options[RBC_BRACE_MAX_OPTIONS];
    size_t option_count = 0;

    p = pattern;
    while (*p && *p != '{')
    {
        if (*p == '\\')
            p++;
        p++;
    }

    if (*p != '{')
    {
        return cb(pattern, arg);
    }

    size_t prefix_len = p - pattern;
    p++;
    int depth = 1;
    const char *chunk_start = p;
    bool valid_brace = false;

    while (*p)
    {
        if (*p == '\\')
        {
            p += 2;
            continue;
        }
        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            depth--;
            if (depth == 0)
            {
                if (option_count >= RBC_BRACE_MAX_OPTIONS)
                    return false;
                size_t len = p - chunk_start;
                char *chunk = arena ? rbc_arena_alloc(arena, len + 1) : malloc(len + 1);
                if (!chunk)
                    return false;
                memcpy(chunk, chunk_start, len);
                chunk[len] = 0;
                options[option_count++] = chunk;
                valid_brace = true;
                break;
            }
        }
        else if (*p == ',' && depth == 1)
        {
            if (option_count >= RBC_BRACE_MAX_OPTIONS)
                return false;
            size_t len = p - chunk_start;
            char *chunk = arena ? rbc_arena_alloc(arena, len + 1) : malloc(len + 1);
            if (!chunk)
                return false;
            memcpy(chunk, chunk_start, len);
            chunk[len] = 0;
            options[option_count++] = chunk;
            chunk_start = p + 1;
        }
        p++;
    }

    if (!valid_brace)
    {
        /* Free non-arena chunks */
        if (!arena)
        {
            for (size_t i = 0; i < option_count; i++)
                free((void *)options[i]);
        }
        return cb(pattern, arg);
    }

    const char *suffix = p + 1;
    bool success = true;
    for (size_t i = 0; i < option_count && success; i++)
    {
        size_t opt_len = strlen(options[i]);
        size_t suf_len = strlen(suffix);
        size_t needed = prefix_len + opt_len + suf_len + 1;

        if (needed < PATH_MAX)
        {
            char vla[needed];
            memcpy(vla, pattern, prefix_len);
            memcpy(vla + prefix_len, options[i], opt_len);
            memcpy(vla + prefix_len + opt_len, suffix, suf_len + 1);
            success = rbc_brace_visit(vla, arena, cb, arg);
        }
        else
        {
            char *next_buf = arena ? rbc_arena_alloc(arena, needed) : malloc(needed);
            if (!next_buf)
            {
                success = false;
                break;
            }
            memcpy(next_buf, pattern, prefix_len);
            memcpy(next_buf + prefix_len, options[i], opt_len);
            memcpy(next_buf + prefix_len + opt_len, suffix, suf_len + 1);
            success = rbc_brace_visit(next_buf, arena, cb, arg);
            if (!arena)
                free(next_buf);
        }
    }

    /* Free non-arena chunks */
    if (!arena)
    {
        for (size_t i = 0; i < option_count; i++)
            free((void *)options[i]);
    }
    return success;
}

typedef struct
{
    rbc_str_list_t *list;
    rbc_arena_t *arena;
} brace_collect_ctx_t;

static bool brace_collect_cb(const char *pattern, void *arg)
{
    brace_collect_ctx_t *ctx = (brace_collect_ctx_t *)arg;
    if (ctx->list->count >= RBC_BRACE_MAX_OPTIONS)
        return false;
    /* Must copy pattern since it may be on stack (VLA) */
    ctx->list->items[ctx->list->count++] = rbc_arena_strdup(ctx->arena, pattern);
    return true;
}

rbc_str_list_t rbc_brace_collect(const char *pattern, rbc_arena_t *arena)
{
    rbc_str_list_t list = {.count = 0};
    brace_collect_ctx_t ctx = {.list = &list, .arena = arena};
    rbc_brace_visit(pattern, arena, brace_collect_cb, &ctx);
    return list;
}

/* ========================================================================
 * Trie Node Types
 * ======================================================================== */

typedef enum
{
    TRIE_NODE_ROOT,      // Root node
    TRIE_NODE_LITERAL,   // Literal directory name
    TRIE_NODE_WILDCARD,  // Single wildcard pattern (*, ?, [])
    TRIE_NODE_RECURSIVE, // Recursive wildcard (**)
    TRIE_NODE_TERMINAL,  // Terminal node (pattern ends here)
} rbc_trie_node_type_t;

/**
 * @brief Single matcher entry for grouped wildcards
 */
typedef struct rbc_trie_matcher_s
{
    size_t pattern_id;               // Original pattern index
    char *pattern_str;               // Pattern string (e.g., "*.c")
    rbc_fnmatch_pattern_t *compiled; // Pre-compiled matcher
    struct rbc_trie_matcher_s *next; // Next matcher in list
} rbc_trie_matcher_t;

/**
 * @brief Trie node structure
 */
typedef struct rbc_trie_node_s rbc_trie_node_t;
struct rbc_trie_node_s
{
    rbc_trie_node_type_t type;

    union
    {
        // TRIE_NODE_LITERAL
        struct
        {
            char *name; // Directory name
        } literal;

        // TRIE_NODE_WILDCARD
        struct
        {
            rbc_trie_matcher_t *matchers; // Linked list of matchers
            size_t matcher_count;
        } wildcard;

        // TRIE_NODE_TERMINAL
        struct
        {
            size_t *pattern_ids; // Array of pattern IDs that terminate here
            size_t count;
            bool match_dir; // True if trailing slash (match directories only)
        } terminal;
    } data;

    // Tree structure
    rbc_trie_node_t *children; // First child
    rbc_trie_node_t *sibling;  // Next sibling at same level

    // Flags
    bool is_recursive; // True if this node triggers ** recursion
    bool has_terminal; // True if any pattern terminates here
};

/**
 * @brief Complete trie structure
 */
typedef struct rbc_trie_s
{
    rbc_trie_node_t *root;
    rbc_arena_t *arena;
    size_t pattern_count;
    unsigned int flags;
    char **original_patterns;
} rbc_trie_t;

/* ========================================================================
 * Trie Node Creation
 * ======================================================================== */

static rbc_trie_node_t *trie_node_new(rbc_arena_t *arena, rbc_trie_node_type_t type)
{
    rbc_trie_node_t *node = rbc_arena_alloc(arena, sizeof(rbc_trie_node_t));
    if (!node)
        return NULL;

    memset(node, 0, sizeof(rbc_trie_node_t));
    node->type = type;
    return node;
}

static rbc_trie_matcher_t *trie_matcher_new(
    rbc_arena_t *arena,
    size_t pattern_id,
    const char *pattern_str,
    unsigned int flags)
{
    rbc_trie_matcher_t *m = rbc_arena_alloc(arena, sizeof(rbc_trie_matcher_t));
    if (!m)
        return NULL;

    memset(m, 0, sizeof(rbc_trie_matcher_t));
    m->pattern_id = pattern_id;
    m->pattern_str = rbc_arena_strdup(arena, pattern_str);
    m->compiled = rbc_fnmatch_compile(pattern_str, flags);

    return m;
}

/* ========================================================================
 * Trie Child Management
 * ======================================================================== */

/**
 * @brief Find a literal child node by name
 */
static rbc_trie_node_t *trie_find_literal_child(rbc_trie_node_t *parent, const char *name)
{
    for (rbc_trie_node_t *child = parent->children; child; child = child->sibling)
    {
        if (child->type == TRIE_NODE_LITERAL &&
            strcmp(child->data.literal.name, name) == 0)
        {
            return child;
        }
    }
    return NULL;
}

/**
 * @brief Find a wildcard child node (there can be only one per level)
 */
static rbc_trie_node_t *trie_find_wildcard_child(rbc_trie_node_t *parent)
{
    for (rbc_trie_node_t *child = parent->children; child; child = child->sibling)
    {
        if (child->type == TRIE_NODE_WILDCARD)
        {
            return child;
        }
    }
    return NULL;
}

/**
 * @brief Find a recursive child node
 */
static rbc_trie_node_t *trie_find_recursive_child(rbc_trie_node_t *parent)
{
    for (rbc_trie_node_t *child = parent->children; child; child = child->sibling)
    {
        if (child->type == TRIE_NODE_RECURSIVE || child->is_recursive)
        {
            return child;
        }
    }
    return NULL;
}

/**
 * @brief Add a child node to parent
 */
static void trie_add_child(rbc_trie_node_t *parent, rbc_trie_node_t *child)
{
    child->sibling = parent->children;
    parent->children = child;
}

/**
 * @brief Add a matcher to a wildcard node
 */
static void trie_add_matcher(
    rbc_trie_node_t *wildcard_node,
    rbc_trie_matcher_t *matcher)
{
    matcher->next = wildcard_node->data.wildcard.matchers;
    wildcard_node->data.wildcard.matchers = matcher;
    wildcard_node->data.wildcard.matcher_count++;
}

/**
 * @brief Add a pattern ID to a terminal node
 */
static bool trie_add_terminal_pattern(
    rbc_trie_node_t *node,
    size_t pattern_id,
    bool match_dir,
    rbc_arena_t *arena)
{
    size_t new_count = node->data.terminal.count + 1;
    size_t *new_ids = rbc_arena_alloc(arena, sizeof(size_t) * new_count);
    if (!new_ids)
        return false;

    if (node->data.terminal.pattern_ids && node->data.terminal.count > 0)
    {
        memcpy(new_ids, node->data.terminal.pattern_ids,
               sizeof(size_t) * node->data.terminal.count);
    }
    new_ids[node->data.terminal.count] = pattern_id;

    node->data.terminal.pattern_ids = new_ids;
    node->data.terminal.count = new_count;
    node->data.terminal.match_dir = match_dir;
    node->has_terminal = true;

    return true;
}

/* ========================================================================
 * Pattern Insertion into Trie
 * ======================================================================== */

/**
 * @brief Insert a compiled segment chain into the trie
 *
 * @param trie The trie structure
 * @param parent Current parent node
 * @param seg Current segment to insert
 * @param pattern_id Original pattern index
 */
static void trie_insert_segment(
    rbc_trie_t *trie,
    rbc_trie_node_t *parent,
    rbc_segment_t *seg,
    size_t pattern_id)
{
    if (!seg)
    {
        // Pattern ends here - mark terminal
        rbc_trie_node_t *term = trie_node_new(trie->arena, TRIE_NODE_TERMINAL);
        if (term)
        {
            trie_add_terminal_pattern(term, pattern_id, false, trie->arena);
            trie_add_child(parent, term);
            parent->has_terminal = true;
        }
        return;
    }

    switch (seg->type)
    {
    case RBC_SEGMENT_LITERAL:
    {
        const char *name = seg->data.literal;

        // Empty literal = trailing slash (directory match)
        if (!name || name[0] == '\0')
        {
            // This is a terminal with directory requirement
            rbc_trie_node_t *term = trie_node_new(trie->arena, TRIE_NODE_TERMINAL);
            if (term)
            {
                trie_add_terminal_pattern(term, pattern_id, true, trie->arena);
                trie_add_child(parent, term);
                parent->has_terminal = true;
            }
            return;
        }

        // Find or create literal child
        rbc_trie_node_t *child = trie_find_literal_child(parent, name);
        if (!child)
        {
            child = trie_node_new(trie->arena, TRIE_NODE_LITERAL);
            if (!child)
                return;
            child->data.literal.name = rbc_arena_strdup(trie->arena, name);
            trie_add_child(parent, child);
        }

        // Continue with next segment
        trie_insert_segment(trie, child, seg->next, pattern_id);
        break;
    }

    case RBC_SEGMENT_WILDCARD:
    {
        const char *pattern = seg->data.glob.original_pattern;

        // Find or create wildcard child
        rbc_trie_node_t *wc_child = trie_find_wildcard_child(parent);
        if (!wc_child)
        {
            wc_child = trie_node_new(trie->arena, TRIE_NODE_WILDCARD);
            if (!wc_child)
                return;
            trie_add_child(parent, wc_child);
        }

        // Add matcher to wildcard node
        rbc_trie_matcher_t *matcher = trie_matcher_new(
            trie->arena, pattern_id, pattern, trie->flags);
        if (matcher)
        {
            trie_add_matcher(wc_child, matcher);
        }

        // Continue with next segment if exists
        if (seg->next)
        {
            trie_insert_segment(trie, wc_child, seg->next, pattern_id);
        }
        else
        {
            // Pattern ends after wildcard
            wc_child->has_terminal = true;
        }
        break;
    }

    case RBC_SEGMENT_RECURSIVE:
    {
        // Find or create recursive child
        rbc_trie_node_t *rec_child = trie_find_recursive_child(parent);
        if (!rec_child)
        {
            rec_child = trie_node_new(trie->arena, TRIE_NODE_RECURSIVE);
            if (!rec_child)
                return;
            rec_child->is_recursive = true;
            trie_add_child(parent, rec_child);
        }

        // Continue with next segment
        trie_insert_segment(trie, rec_child, seg->next, pattern_id);
        break;
    }

    case RBC_SEGMENT_BRANCH:
    {
        // For branches, insert each alternative
        rbc_segment_t *alt = seg->data.branch.head;
        while (alt)
        {
            trie_insert_segment(trie, parent, alt, pattern_id);
            alt = alt->next_alt;
        }
        break;
    }
    }
}

/**
 * @brief Insert a pattern string into the trie
 */
static void trie_insert_pattern(
    rbc_trie_t *trie,
    const char *pattern,
    size_t pattern_id)
{
    if (!pattern || !pattern[0])
        return;

    // Compile pattern into segments
    rbc_segment_t *segments = rbc_glob_segment_compile(
        trie->arena, pattern, trie->flags);

    if (!segments)
        return;

    // Insert into trie
    trie_insert_segment(trie, trie->root, segments, pattern_id);
}

/* ========================================================================
 * Trie Compilation
 * ======================================================================== */

/**
 * @brief Create a complete trie from multiple patterns
 */
static rbc_trie_t *trie_compile(
    const char **patterns,
    size_t count,
    unsigned int flags,
    rbc_arena_t *arena)
{
    rbc_trie_t *trie = rbc_arena_alloc(arena, sizeof(rbc_trie_t));
    if (!trie)
        return NULL;

    memset(trie, 0, sizeof(rbc_trie_t));
    trie->arena = arena;
    trie->pattern_count = count;
    trie->flags = flags;

    // Store original patterns
    trie->original_patterns = rbc_arena_alloc(arena, sizeof(char *) * count);
    for (size_t i = 0; i < count; i++)
    {
        trie->original_patterns[i] = rbc_arena_strdup(arena, patterns[i]);
    }

    // Create root node
    trie->root = trie_node_new(arena, TRIE_NODE_ROOT);
    if (!trie->root)
        return NULL;

    // Insert all patterns
    for (size_t i = 0; i < count; i++)
    {
        const char *pattern = patterns[i];

        // Handle brace expansion
        if (rbc_has_brace(pattern))
        {
            rbc_str_list_t expanded = rbc_brace_collect(pattern, arena);
            for (size_t j = 0; j < expanded.count; j++)
            {
                trie_insert_pattern(trie, expanded.items[j], i);
            }
        }
        else
        {
            trie_insert_pattern(trie, pattern, i);
        }
    }

    return trie;
}

/* ========================================================================
 * Trie Execution
 * ======================================================================== */

// Forward declaration for recursion
static void trie_execute_node(
    rbc_trie_node_t *node,
    const char *current_path,
    rbc_results_t *results,
    unsigned int flags,
    rbc_arena_t *arena);

/**
 * @brief Check if entry matches any wildcard matchers
 */
static void trie_match_wildcards(
    rbc_trie_node_t *wc_node,
    const char *name,
    const char *full_path,
    rbc_results_t *results,
    unsigned int flags,
    rbc_arena_t *arena)
{
    for (rbc_trie_matcher_t *m = wc_node->data.wildcard.matchers; m; m = m->next)
    {
        bool matched = false;

        if (m->compiled)
        {
            matched = rbc_xfnmatch(m->compiled, name, flags);
        }
        else
        {
            matched = rbc_fnmatch(m->pattern_str, name, flags);
        }

        if (matched)
        {
            // Check if pattern ends at this wildcard node
            if (wc_node->has_terminal)
            {
                // Pattern ends here - add result with the matcher's pattern_id
                rbc_glob_results_add_with_index(results, full_path, m->pattern_id);
            }
        }
    }

    // If wildcard node has non-terminal children, continue matching for directories
    if (wc_node->children)
    {
        // For each matched file that is a directory, continue descent
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
        {
            for (rbc_trie_matcher_t *m = wc_node->data.wildcard.matchers; m; m = m->next)
            {
                bool matched = m->compiled ? rbc_xfnmatch(m->compiled, name, flags) : rbc_fnmatch(m->pattern_str, name, flags);

                if (matched)
                {
                    trie_execute_node(wc_node, full_path, results, flags, arena);
                }
            }
        }
    }
}

/**
 * @brief Execute trie traversal at a node
 */
static void trie_execute_node(
    rbc_trie_node_t *node,
    const char *current_path,
    rbc_results_t *results,
    unsigned int flags,
    rbc_arena_t *arena)
{
    if (!node)
        return;

    // Open directory ONCE
    DIR *dir = opendir(current_path[0] ? current_path : ".");
    if (!dir)
        return;

    struct dirent *dp;
    char path_buf[PATH_MAX];

    while ((dp = readdir(dir)) != NULL)
    {
        const char *name = dp->d_name;

        // Skip . and ..
        if (name[0] == '.' && (name[1] == '\0' ||
                               (name[1] == '.' && name[2] == '\0')))
        {
            continue;
        }

        // Skip dotfiles unless FNM_DOTMATCH
        if (name[0] == '.' && !(flags & RBC_FNM_DOTMATCH))
        {
            continue;
        }

        // Build full path
        size_t path_len = strlen(current_path);
        size_t name_len = strlen(name);
        if (path_len + name_len + 2 > PATH_MAX)
            continue;

        if (path_len == 0 || (path_len == 1 && current_path[0] == '.'))
        {
            memcpy(path_buf, name, name_len + 1);
        }
        else
        {
            memcpy(path_buf, current_path, path_len);
            path_buf[path_len] = '/';
            memcpy(path_buf + path_len + 1, name, name_len + 1);
        }

        // Determine if entry is a directory
        bool is_dir = false;
#ifdef _DIRENT_HAVE_D_TYPE
        if (dp->d_type == DT_DIR)
        {
            is_dir = true;
        }
        else if (dp->d_type == DT_UNKNOWN || dp->d_type == DT_LNK)
        {
            struct stat st;
            if (stat(path_buf, &st) == 0)
            {
                is_dir = S_ISDIR(st.st_mode);
            }
        }
#else
        struct stat st;
        if (stat(path_buf, &st) == 0)
        {
            is_dir = S_ISDIR(st.st_mode);
        }
#endif

        // Special handling if current node is RECURSIVE
        // For directories, we need to recurse with the RECURSIVE node itself
        if (node->type == TRIE_NODE_RECURSIVE)
        {
            // Match children of RECURSIVE node against current entry
            for (rbc_trie_node_t *rec_child = node->children; rec_child; rec_child = rec_child->sibling)
            {
                if (rec_child->type == TRIE_NODE_WILDCARD)
                {
                    trie_match_wildcards(rec_child, name, path_buf, results, flags, arena);
                }
                else if (rec_child->type == TRIE_NODE_TERMINAL)
                {
                    for (size_t i = 0; i < rec_child->data.terminal.count; i++)
                    {
                        if (!rec_child->data.terminal.match_dir || is_dir)
                        {
                            rbc_glob_results_add_with_index(
                                results, path_buf, rec_child->data.terminal.pattern_ids[i]);
                        }
                    }
                }
            }

            // If directory, recurse with same RECURSIVE node
            if (is_dir)
            {
                trie_execute_node(node, path_buf, results, flags, arena);
            }
            continue; // Skip the normal child processing below
        }

        // Process each child node
        for (rbc_trie_node_t *child = node->children; child; child = child->sibling)
        {
            switch (child->type)
            {
            case TRIE_NODE_LITERAL:
                // Check if name matches literal
                if (strcmp(child->data.literal.name, name) == 0)
                {
                    if (child->has_terminal && !child->children)
                    {
                        // Pattern ends at this literal
                        for (rbc_trie_node_t *term = child->children; term; term = term->sibling)
                        {
                            if (term->type == TRIE_NODE_TERMINAL)
                            {
                                for (size_t i = 0; i < term->data.terminal.count; i++)
                                {
                                    if (!term->data.terminal.match_dir || is_dir)
                                    {
                                        rbc_glob_results_add_with_index(
                                            results, path_buf, term->data.terminal.pattern_ids[i]);
                                    }
                                }
                            }
                        }
                    }
                    // Descend if directory and has children
                    if (is_dir && child->children)
                    {
                        trie_execute_node(child, path_buf, results, flags, arena);
                    }
                }
                break;

            case TRIE_NODE_WILDCARD:
                // Match against all wildcard patterns at this level
                trie_match_wildcards(child, name, path_buf, results, flags, arena);
                break;

            case TRIE_NODE_RECURSIVE:
                // ** matches zero or more directories
                // First: try matching children without consuming directories (zero match)
                for (rbc_trie_node_t *rec_child = child->children; rec_child; rec_child = rec_child->sibling)
                {
                    if (rec_child->type == TRIE_NODE_WILDCARD)
                    {
                        trie_match_wildcards(rec_child, name, path_buf, results, flags, arena);
                    }
                    else if (rec_child->type == TRIE_NODE_LITERAL)
                    {
                        if (strcmp(rec_child->data.literal.name, name) == 0)
                        {
                            if (is_dir && rec_child->children)
                            {
                                trie_execute_node(rec_child, path_buf, results, flags, arena);
                            }
                        }
                    }
                    else if (rec_child->type == TRIE_NODE_TERMINAL)
                    {
                        for (size_t i = 0; i < rec_child->data.terminal.count; i++)
                        {
                            if (!rec_child->data.terminal.match_dir || is_dir)
                            {
                                rbc_glob_results_add_with_index(
                                    results, path_buf, rec_child->data.terminal.pattern_ids[i]);
                            }
                        }
                    }
                }

                // Second: if directory, recursively descend with same ** node
                if (is_dir)
                {
                    trie_execute_node(child, path_buf, results, flags, arena);
                }
                break;

            case TRIE_NODE_TERMINAL:
                // Terminal node at this level - add result
                for (size_t i = 0; i < child->data.terminal.count; i++)
                {
                    if (!child->data.terminal.match_dir || is_dir)
                    {
                        rbc_glob_results_add_with_index(
                            results, path_buf, child->data.terminal.pattern_ids[i]);
                    }
                }
                break;

            default:
                break;
            }
        }
    }

    closedir(dir);
}

/**
 * @brief Start trie execution from root
 */
static void trie_execute(
    rbc_trie_t *trie,
    const char *basedir,
    rbc_results_t *results)
{
    if (!trie || !trie->root)
        return;

    const char *base = (basedir && basedir[0]) ? basedir : ".";
    char path_buf[PATH_MAX];

    // Check if root has direct literal children (optimized path)
    bool has_literal_children = false;
    for (rbc_trie_node_t *child = trie->root->children; child; child = child->sibling)
    {
        if (child->type == TRIE_NODE_LITERAL)
        {
            has_literal_children = true;
            break;
        }
    }

    if (has_literal_children)
    {
        // Start from each literal child directly
        for (rbc_trie_node_t *child = trie->root->children; child; child = child->sibling)
        {
            if (child->type == TRIE_NODE_LITERAL)
            {
                // Build path to literal directory
                if (strcmp(base, ".") == 0)
                {
                    snprintf(path_buf, sizeof(path_buf), "%s", child->data.literal.name);
                }
                else
                {
                    snprintf(path_buf, sizeof(path_buf), "%s/%s", base, child->data.literal.name);
                }

                // Check if directory exists
                struct stat st;
                if (stat(path_buf, &st) == 0 && S_ISDIR(st.st_mode))
                {
                    // Handle terminal patterns (pattern is just the literal)
                    if (child->has_terminal)
                    {
                        for (rbc_trie_node_t *term = child->children; term; term = term->sibling)
                        {
                            if (term->type == TRIE_NODE_TERMINAL)
                            {
                                for (size_t i = 0; i < term->data.terminal.count; i++)
                                {
                                    rbc_glob_results_add_with_index(
                                        results, path_buf, term->data.terminal.pattern_ids[i]);
                                }
                            }
                        }
                    }

                    // Continue descent
                    if (child->children)
                    {
                        trie_execute_node(child, path_buf, results, trie->flags, trie->arena);
                    }
                }
            }
            else if (child->type == TRIE_NODE_RECURSIVE)
            {
                // ** at root - scan from base
                trie_execute_node(child, base, results, trie->flags, trie->arena);
            }
            else if (child->type == TRIE_NODE_WILDCARD)
            {
                // Wildcard at root - scan base directory
                trie_execute_node(trie->root, base, results, trie->flags, trie->arena);
            }
        }
    }
    else
    {
        // No literal children - start from base
        trie_execute_node(trie->root, base, results, trie->flags, trie->arena);
    }
}

/* ========================================================================
 * Public API: Complete Trie-based Glob
 * ======================================================================== */

/**
 * @brief Execute multiple glob patterns using complete trie optimization
 *
 * This is the main entry point for trie-based multi-pattern globbing.
 * It provides the best performance when multiple patterns share:
 * - Common directory prefixes
 * - Recursive wildcards (**)
 * - Same parent directories with different file patterns
 *
 * @param patterns Array of pattern strings
 * @param npatterns Number of patterns
 * @param flags Glob flags
 * @param base Base directory (NULL or "." for current)
 * @param sort Whether to sort results
 * @param out Output: array of matched paths
 * @param count Output: number of matches
 * @param lengths Output: lengths of matched paths (optional)
 * @return true on success, false on failure
 */
bool rbc_glob_trie(
    const char **patterns,
    size_t npatterns,
    unsigned flags,
    const char *base,
    bool sort,
    char ***out,
    size_t *count,
    size_t **lengths)
{
    if (!out || !count || !patterns || npatterns == 0)
    {
        return false;
    }

    // Initialize context
    rbc_ctx_t *ctx = malloc(sizeof(rbc_ctx_t));
    if (!ctx)
        return false;

    if (!rbc_glob_ctx_init(ctx))
    {
        free(ctx);
        return false;
    }

    rbc_results_t results;
    if (!rbc_glob_results_init(&results, ctx))
    {
        rbc_glob_ctx_free(ctx);
        free(ctx);
        return false;
    }

    // Compile trie (handles single pattern, brace expansion, etc.)
    rbc_trie_t *trie = trie_compile(patterns, npatterns, flags, &ctx->arena);
    if (!trie)
    {
        rbc_glob_results_clear(&results);
        rbc_glob_ctx_free(ctx);
        free(ctx);
        return false;
    }

    // Execute trie
    trie_execute(trie, base, &results);

    // Sort if requested
    if (sort)
    {
        rbc_glob_results_sort(&results);
    }

    // Package results
    *count = results.count;
    void **package = malloc(sizeof(void *) + (results.count + 1) * sizeof(char *));
    if (!package)
    {
        rbc_glob_results_clear(&results);
        rbc_glob_ctx_free(ctx);
        free(ctx);
        return false;
    }

    package[0] = ctx;
    char **pkg_items = (char **)&package[1];
    if (results.count > 0)
    {
        memcpy(pkg_items, results.items, results.count * sizeof(char *));
    }
    pkg_items[results.count] = NULL;

    *out = pkg_items;
    if (lengths)
    {
        *lengths = results.lengths;
    }
    else if (results.lengths)
    {
        free(results.lengths);
    }

    free(results.items);
    free(results.discovery_indices);

    return true;
}
