#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <limits.h>
#include <rbcglob/file.h>

#include "rbcglob/internal/graph.h"
#include "rbcglob/internal/traverse.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// --- State Management ---

typedef struct
{
    rbcglob_node_t *node;
    int lit_offset;
    bool is_literal_match;
} nfa_state_t;

typedef struct
{
    nfa_state_t *states;
    size_t count;
    size_t capacity;
} state_set_t;

static void state_set_init(state_set_t *set)
{
    set->states = NULL;
    set->count = 0;
    set->capacity = 0;
}

static void state_set_free(state_set_t *set)
{
    free(set->states);
}

static void state_set_add(state_set_t *set, rbcglob_node_t *node, int offset, bool is_literal_match)
{
    for (size_t i = 0; i < set->count; i++)
    {
        if (set->states[i].node == node && set->states[i].lit_offset == offset)
        {
            set->states[i].is_literal_match = set->states[i].is_literal_match || is_literal_match;
            return;
        }
    }

    if (set->count == set->capacity)
    {
        set->capacity = set->capacity ? set->capacity * 2 : 16;
        set->states = realloc(set->states, set->capacity * sizeof(nfa_state_t));
    }
    set->states[set->count].node = node;
    set->states[set->count].lit_offset = offset;
    set->states[set->count].is_literal_match = is_literal_match;
    set->count++;
}

// Compute Epsilon Closure (but don't expand ** to avoid losing it)
static void epsilon_closure(state_set_t *set)
{
    for (size_t i = 0; i < set->count; i++)
    {
        nfa_state_t s = set->states[i];
        rbcglob_node_t *n = s.node;
        if (!n)
            continue;

        if (n->type == OP_MATCH_LITERAL)
        {
            if (s.lit_offset == 0 && n->data.literal[0] == '\0')
            {
                state_set_add(set, n->next, 0, s.is_literal_match);
            }
            continue;
        }

        if (n->type == OP_JUMP)
        {
            state_set_add(set, n->next, 0, s.is_literal_match);
        }
        else if (n->type == OP_FORK)
        {
            if (n->data.branch.next)
                state_set_add(set, n->data.branch.next, 0, s.is_literal_match);
            if (n->data.branch.alt)
                state_set_add(set, n->data.branch.alt, 0, s.is_literal_match);
        }
        else if (n->type == OP_MATCH_STAR)
        {
            // Even if STAR matches empty string, the presence of STAR means
            // the pattern is not fully literal (it's a wildcard match of size 0).
            // This is crucial for 'parent_wild' logic in file recursion guard.
            state_set_add(set, n->next, 0, false);
        }
        // DON'T expand OP_MATCH_STAR2 here - we need to keep it for directory recursion
    }
}

static void step_char(state_set_t *current, char c, bool allow_wildcard, state_set_t *next_set)
{
    for (size_t i = 0; i < current->count; i++)
    {
        nfa_state_t s = current->states[i];
        rbcglob_node_t *n = s.node;
        if (!n)
            continue;

        if (n->type == OP_MATCH_LITERAL)
        {
            if (n->data.literal[s.lit_offset] == c)
            {
                if (n->data.literal[s.lit_offset + 1] == '\0')
                {
                    state_set_add(next_set, n->next, 0, s.is_literal_match);
                }
                else
                {
                    state_set_add(next_set, n, s.lit_offset + 1, s.is_literal_match);
                }
            }
        }
        else if (n->type == OP_MATCH_DOT)
        {
            if (c == '.')
            {
                state_set_add(next_set, n->next, 0, s.is_literal_match);
            }
        }
        else if (n->type == OP_MATCH_DOTDOT)
        {
            if (c == '.')
            {
                if (s.lit_offset == 0)
                {
                    state_set_add(next_set, n, 1, s.is_literal_match);
                }
                else if (s.lit_offset == 1)
                {
                    state_set_add(next_set, n->next, 0, s.is_literal_match);
                }
            }
        }
        else if (allow_wildcard)
        {
            if (n->type == OP_MATCH_STAR)
            {
                state_set_add(next_set, n, 0, false);
            }
            else if (n->type == OP_MATCH_QMARK)
            {
                state_set_add(next_set, n->next, 0, false);
            }
            else if (n->type == OP_MATCH_CLASS)
            {
                unsigned char uc = (unsigned char)c;
                bool match = (n->data.char_class.map[uc / 8] & (1 << (uc % 8))) != 0;
                if (n->data.char_class.is_negated)
                    match = !match;

                if (match)
                    state_set_add(next_set, n->next, 0, false);
            }
        }
    }
}

// --- Execution ---

static char *path_join(const char *dir, const char *file)
{
    size_t dlen = dir ? strlen(dir) : 0;
    size_t flen = strlen(file);

    char *res = malloc(dlen + flen + 2);
    if (!res)
        return NULL;
    if (dlen > 0)
    {
        strcpy(res, dir);
        if (dir[dlen - 1] != '/')
            strcat(res, "/");
        strcat(res, file);
    }
    else
    {
        strcpy(res, file);
    }
    return res;
}

static int compare_entries(const void *a, const void *b)
{
    return rbcglob_compare_paths(*(const char **)a, *(const char **)b);
}

typedef struct
{
    rbcglob_match_callback_t cb;
    void *ud;
    unsigned flags;
    bool sort;
} exec_ctx_t;

static void execute_recursive(state_set_t *current_states, const char *path, exec_ctx_t *ctx, int depth, bool parent_wild)
{
    // Limit recursion depth
    if (depth > 100)
        return;

    // Identify ** states before expanding
    bool has_globstar = false;
    for (size_t i = 0; i < current_states->count; i++)
    {
        if (current_states->states[i].node &&
            current_states->states[i].node->type == OP_MATCH_STAR2)
        {
            has_globstar = true;
            break;
        }
    }

    // Expand ** for matching in current directory (can match 0 directories)
    state_set_t expanded;
    state_set_init(&expanded);

    for (size_t i = 0; i < current_states->count; i++)
    {
        nfa_state_t s = current_states->states[i];
        state_set_add(&expanded, s.node, s.lit_offset, s.is_literal_match);

        // If it's **, also add the next state (** matches 0 directories)
        if (s.node && s.node->type == OP_MATCH_STAR2)
        {
            state_set_add(&expanded, s.node->next, 0, s.is_literal_match);
        }
    }

    // Compute epsilon closure
    epsilon_closure(&expanded);

    // Check for accept state - but don't match the base directory itself
    bool match_accept = false;
    for (size_t i = 0; i < expanded.count; i++)
    {
        if (expanded.states[i].node && expanded.states[i].node->type == OP_ACCEPT)
        {
            match_accept = true;
            break;
        }
    }

    // Only report match if it's not just the base directory
    if (match_accept && path && *path)
    {
        // Check if this is a directory - only match if pattern ends with explicit directory match
        struct stat st;
        bool is_match_dir = (stat(path, &st) == 0 && S_ISDIR(st.st_mode));

        // For directories, only match if we're at a terminal state after matching directory name
        if (!is_match_dir)
        {
            ctx->cb(path, ctx->ud);
        }
    }

    // Open directory
    DIR *d = opendir(path && *path ? path : ".");
    if (!d)
    {
        state_set_free(&expanded);
        return;
    }

    // Read entries
    char **entries = NULL;
    size_t count = 0, cap = 0;
    struct dirent *de;

    // Determine if we should allow dotfiles
    // Rule: Allow if DOTMATCH flag OR if we have ONLY a literal starting with '.' (no wildcards before it)
    bool allow_dotfiles = (ctx->flags & RBCGLOB_FNM_DOTMATCH);

    if (!allow_dotfiles)
    {
        // Check if the FIRST matching node in expanded states is a literal starting with '.'
        for (size_t k = 0; k < expanded.count; k++)
        {
            rbcglob_node_t *node = expanded.states[k].node;
            if (!node)
                continue;

            // Skip control flow nodes
            if (node->type == OP_JUMP || node->type == OP_FORK || node->type == OP_ACCEPT ||
                node->type == OP_MATCH_STAR2)
                continue;

            // If we hit a wildcard first, don't allow dotfiles
            if (node->type == OP_MATCH_STAR || node->type == OP_MATCH_QMARK || node->type == OP_MATCH_CLASS)
            {
                break;
            }

            // If we hit a literal, check if it starts with '.'
            if (node->type == OP_MATCH_LITERAL)
            {
                if (node->data.literal && node->data.literal[0] == '.')
                {
                    allow_dotfiles = true;
                }
                break;
            }
            // Explicit dot/dotdot
            if (node->type == OP_MATCH_DOT || node->type == OP_MATCH_DOTDOT)
            {
                allow_dotfiles = true;
                break;
            }
        }
    }

    while ((de = readdir(d)) != NULL)
    {
        bool is_hidden = (de->d_name[0] == '.');

        if (is_hidden && !allow_dotfiles)
            continue;

        if (count == cap)
        {
            cap = cap ? cap * 2 : 16;
            entries = realloc(entries, cap * sizeof(char *));
        }
        entries[count++] = strdup(de->d_name);
    }
    closedir(d);

    // Always sort directory entries during traversal for deterministic behavior.
    // Ruby's Dir.glob behavior with sort: false is technically filesystem dependent.
    // However, for consistency and to avoid flaky tests due to readdir order,
    // we sort the entries at each level.
    // Note: This might make "sort: false" actually sorted in our implementation,
    // which is compliant since "unsorted" implies "no specific order guaranteed".
    if (ctx->sort)
        qsort(entries, count, sizeof(char *), compare_entries);

    for (size_t i = 0; i < count; i++)
    {
        char *entry = entries[i];
        bool is_dot = !strcmp(entry, ".");
        bool is_dotdot = !strcmp(entry, "..");

        if ((is_dot || is_dotdot) && parent_wild)
            continue;

        char *next_path = path_join(path, entry);
        struct stat st;
        bool is_dir = (stat(next_path, &st) == 0 && S_ISDIR(st.st_mode));

        // Prepare for matches
        bool entry_match = false;
        bool entry_was_fully_literal = false;
        state_set_t passed_nodes;
        state_set_init(&passed_nodes);

        // Unified Sequence Matching for all files (including . and ..)
        {
            state_set_t active, next_step;
            state_set_init(&active);
            state_set_init(&next_step);

            // Import expanded states
            for (size_t k = 0; k < expanded.count; k++)
            {
                rbcglob_node_t *n = expanded.states[k].node;
                if (!n)
                    continue;

                // Skip non-consuming control nodes.
                if (n->type == OP_FORK || n->type == OP_JUMP)
                    continue;

                // SPECIAL FILTER for . and ..
                // Normally . and .. are NOT matched by *, ?, [..]
                // Exception: FNM_DOTMATCH allows . to be matched by wildcards
                // BUT .. is NEVER matched by wildcards even with DOTMATCH (in typical Ruby/Shell usage)
                if (is_dot || is_dotdot)
                {
                    bool is_wildcard = (n->type == OP_MATCH_STAR ||
                                        n->type == OP_MATCH_STAR2 ||
                                        n->type == OP_MATCH_QMARK ||
                                        n->type == OP_MATCH_CLASS);

                    if (is_wildcard)
                    {
                        if (is_dot && (ctx->flags & RBCGLOB_FNM_DOTMATCH))
                        {
                            // Allow match
                        }
                        else
                        {
                            // Filter out
                            continue;
                        }
                    }
                }

                state_set_add(&active, n, expanded.states[k].lit_offset, expanded.states[k].is_literal_match);
            }

            // NOTE: Do NOT call epsilon_closure(&active) here.
            // We just constructed 'active' from 'expanded' which was ALREADY closed.
            // If we call closure again, it might traverse from a consuming node's epsilon transition
            // (like STAR -> next) back to a node we wanted to filter out (if graph loop? unlikley)
            // OR more importantly, if we had kept FORK nodes, it would re-expand them.
            // Since we filtered FORK/JUMP, closure wouldn't help unless consuming nodes have epsilons.
            // Consuming nodes with epsilons (like STAR) have their targets in 'expanded' already.

            // Step through entry name
            bool possible = true;
            for (char *p = entry; *p; p++)
            {
                if (active.count == 0)
                {
                    possible = false;
                    break;
                }

                bool allow_wildcard = true;
                // Standard dotfile rule: if strictly starting with '.', wildcard not allowed unless DOTMATCH
                if (p == entry && *p == '.' && !(ctx->flags & RBCGLOB_FNM_DOTMATCH))
                {
                    allow_wildcard = false;
                }

                // .. should NEVER be matched by wildcards (even with DOTMATCH).
                // It can only be matched by literal ".." or DOTDOT node.
                if (is_dotdot)
                {
                    allow_wildcard = false;
                }

                step_char(&active, *p, allow_wildcard, &next_step);
                state_set_free(&active);
                active = next_step;
                state_set_init(&next_step);
                epsilon_closure(&active);
            }

            // Check if we reached a separator or accept state
            if (possible && active.count > 0)
            {
                for (size_t k = 0; k < active.count; k++)
                {
                    nfa_state_t s = active.states[k];
                    rbcglob_node_t *n = s.node;
                    if (!n)
                        continue;

                    // If we reached ACCEPT, it's a match
                    if (n->type == OP_ACCEPT)
                    {
                        entry_match = true;
                        if (s.is_literal_match)
                            entry_was_fully_literal = true;
                    }
                    // If we reached a SEP, we cross it
                    else if (n->type == OP_MATCH_SEP)
                    {
                        state_set_add(&passed_nodes, n->next, 0, true);
                        if (s.is_literal_match)
                            entry_was_fully_literal = true;
                    }
                }
            }
            state_set_free(&active);
            state_set_free(&next_step); // Safety
        }

        // Report match
        if (entry_match)
        {
            ctx->cb(next_path, ctx->ud);
        }

        // For directories with **, always descend (micromatch-style)
        // BUT NEVER descend into . or .. via globstar to avoid infinite loops
        bool is_dot_dir = !strcmp(entry, ".") || !strcmp(entry, "..");
        if (is_dir && has_globstar && !is_dot_dir)
        {
            for (size_t k = 0; k < current_states->count; k++)
            {
                rbcglob_node_t *n = current_states->states[k].node;
                if (n && n->type == OP_MATCH_STAR2)
                {
                    state_set_add(&passed_nodes, n, 0, false);
                }
            }
        }

        // Recurse into subdirectory
        if (is_dir && passed_nodes.count > 0)
        {
            execute_recursive(&passed_nodes, next_path, ctx, depth + 1, !entry_was_fully_literal);
        }

        state_set_free(&passed_nodes);
        free(next_path);
    }

    for (size_t i = 0; i < count; i++)
        free(entries[i]);
    free(entries);
    state_set_free(&expanded);
}

void rbcglob_nfa_execute(
    rbcglob_node_t *root,
    const char *base_path,
    unsigned flags,
    bool sort,
    rbcglob_match_callback_t callback,
    void *user_data)
{
    state_set_t initial;
    state_set_init(&initial);
    state_set_add(&initial, root, 0, true);

    exec_ctx_t ctx = {callback, user_data, flags, sort};

    // fprintf(stderr, "[DEBUG] rbcglob_nfa_execute base_path='%s'\n", base_path ? base_path : "NULL");
    // execute_recursive handles NULL path by treating it as "." for opendir,
    // but preserving NULL for path construction (path_join) to avoid "./" prefixes.
    const char *start_path = base_path;
    if (start_path && strcmp(start_path, ".") == 0)
        start_path = NULL; // effectively empty prefix

    execute_recursive(&initial, start_path, &ctx, 0, false);

    state_set_free(&initial);
}
