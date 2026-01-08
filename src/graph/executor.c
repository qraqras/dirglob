#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <limits.h>
#include <rbcglob/file.h> /* FNM flags */

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

static void state_set_add(state_set_t *set, rbcglob_node_t *node, int offset)
{
    // Deduplication check (linear scan is fine for small NFA wavefronts usually)
    for (size_t i = 0; i < set->count; i++)
    {
        if (set->states[i].node == node && set->states[i].lit_offset == offset)
            return;
    }

    if (set->count == set->capacity)
    {
        set->capacity = set->capacity ? set->capacity * 2 : 16;
        set->states = realloc(set->states, set->capacity * sizeof(nfa_state_t));
    }
    set->states[set->count].node = node;
    set->states[set->count].lit_offset = offset;
    set->count++;
}

// Compute Epsilon Closure
static void epsilon_closure(state_set_t *set)
{
    // Process new additions.
    // Since we append, we can iterate.
    // But adding might realloc, so use indices.

    for (size_t i = 0; i < set->count; i++)
    {
        nfa_state_t s = set->states[i];
        rbcglob_node_t *n = s.node;
        if (!n)
            continue;

        // Literal logic: if offset < len, not epsilon.
        if (n->type == OP_MATCH_LITERAL)
        {
            if (s.lit_offset == 0 && n->data.literal[0] == '\0')
            {
                // Empty literal is epsilon?
                state_set_add(set, n->next, 0);
            }
            // Non-empty literal is NOT epsilon unless it's fully consumed,
            // but here state is (node, offset). Epsilon move is only from End of Node to Next Node.
            // If we are at end of literal (offset == len), we should have moved to next node already.
            // So here we assume offset < len implies waiting for char.
            continue;
        }

        if (n->type == OP_JUMP)
        {
            state_set_add(set, n->next, 0);
        }
        else if (n->type == OP_FORK)
        {
            if (n->data.branch.next)
                state_set_add(set, n->data.branch.next, 0);
            if (n->data.branch.alt)
                state_set_add(set, n->data.branch.alt, 0);
        }
        else if (n->type == OP_MATCH_STAR)
        {
            // STAR matches empty sequence, so can move to next
            state_set_add(set, n->next, 0);
        }
        else if (n->type == OP_MATCH_STAR2)
        {
            // STAR2 matches empty
            state_set_add(set, n->next, 0);
            // STAR2 also matches directories (self loop handled in recursion/executor main loop)
        }
    }
}

static void step_char(state_set_t *current, char c, state_set_t *next_set)
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
                // Matched char
                if (n->data.literal[s.lit_offset + 1] == '\0')
                {
                    // Finished literal
                    state_set_add(next_set, n->next, 0);
                }
                else
                {
                    // Continue literal
                    state_set_add(next_set, n, s.lit_offset + 1);
                }
            }
        }
        else if (n->type == OP_MATCH_STAR)
        {
            // STAR consumes c and stays
            state_set_add(next_set, n, 0);

            // Note: STAR's epsilon transition to next is handled by epsilon_closure called AFTER this step.
            // Wait, standard NFA:
            // S1 --c--> S2.
            // Closure(S2).
            // STAR --c--> STAR.
            // STAR --epsilon--> Next.
            // So if we add STAR to next_set, Closure(next_set) will add Next. Correct.
        }
        else if (n->type == OP_MATCH_QMARK)
        {
            state_set_add(next_set, n->next, 0);
        }
        else if (n->type == OP_MATCH_CLASS)
        {
            // TODO: char class check
            // For now assume match all
            state_set_add(next_set, n->next, 0);
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
} exec_ctx_t;

static void execute_recursive(state_set_t *current_states, const char *path, exec_ctx_t *ctx)
{
    fprintf(stderr, "execute_recursive called: path=%s, state_count=%zu\n", path, current_states->count);
    fflush(stderr);

    static int depth = 0;
    static int call_count = 0;
    depth++;
    call_count++;

    if (call_count % 1000 == 0)
    {
        fprintf(stderr, "execute_recursive call #%d, depth=%d, path=%s\n", call_count, depth, path);
    }

    if (depth > 100)
    {
        fprintf(stderr, "RECURSION DEPTH EXCEEDED at path: %s\n", path);
        depth--;
        return;
    }

    // 1. Compute Closure
    epsilon_closure(current_states);

    // 2. Identify Terminals (Accept) and Separators (Transitions to next dir)
    state_set_t next_layer_nodes; // Nodes to traverse in SUB-directories
    state_set_init(&next_layer_nodes);

    bool match_accept = false;

    for (size_t i = 0; i < current_states->count; i++)
    {
        nfa_state_t s = current_states->states[i];
        rbcglob_node_t *n = s.node;
        if (!n)
            continue;

        if (n->type == OP_ACCEPT)
        {
            match_accept = true;
        }
        else if (n->type == OP_MATCH_LITERAL && n->data.literal[s.lit_offset] == '/')
        {
            // Logic: If we are at a '/', we consume it and move to next component.
            // This means 'path' is a directory aligned with this '/'
            // Node state moves to 'next char of literal' or 'next node'.
            if (n->data.literal[s.lit_offset + 1] == '\0')
            {
                state_set_add(&next_layer_nodes, n->next, 0);
            }
            else
            {
                state_set_add(&next_layer_nodes, n, s.lit_offset + 1);
            }
        }

        // Handle STAR2: **
        // ** can traverse into any directory depth.
        // However, we don't add it to next_layer_nodes because that would cause
        // infinite recursion on the same directory.
        // Instead, ** is handled via epsilon closure (which adds it to subdirectories)
        // and when matching subdirectories below.
    }

    // If ACCEPT reached, trigger callback
    if (match_accept)
    {
        ctx->cb(path, ctx->ud);
    }

    // If no possible deeper traversal, stop
    if (next_layer_nodes.count == 0 && !match_accept)
    {
        // Optimization: if we have active STAR/STAR2, we might still match files in THIS dir.
        // We need to check if any node accepts non-separator chars.
        // If all nodes are blocked (waiting for / but found none?), we stop?
        // No, current_states contains nodes that match chars in THIS directory (files).
    }

    // 3. Scan Directory
    // Only if we have active states that are NOT just waiting for '/'?
    // Or if we have states that can consume chars.

    fprintf(stderr, "Opening directory: '%s'\n", path && *path ? path : ".");

    DIR *d = opendir(path && *path ? path : ".");
    if (!d)
    {
        fprintf(stderr, "Failed to open directory: '%s'\n", path);
        state_set_free(&next_layer_nodes);
        depth--;
        return;
    }

    // Read all entries
    char **entries = NULL;
    size_t count = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        // Handle FNM_DOTMATCH (hidden files)
        // Default (no flag) -> skip dotfiles
        bool is_hidden = (de->d_name[0] == '.');
        if (is_hidden && !(ctx->flags & RBCGLOB_FNM_DOTMATCH))
            continue;

        if (count == cap)
        {
            cap = cap ? cap * 2 : 16;
            entries = realloc(entries, cap * sizeof(char *));
        }
        entries[count++] = strdup(de->d_name);
    }
    closedir(d);

    fprintf(stderr, "Found %zu entries in '%s'\n", count, path);

    // Sort
    qsort(entries, count, sizeof(char *), compare_entries);

    // For each entry, run NFA simulation
    for (size_t i = 0; i < count; i++)
    {
        char *entry = entries[i];

        // Sim: Match 'entry' against 'current_states'
        // We need a specific walker
        state_set_t active, next_step;
        state_set_init(&active);
        state_set_init(&next_step);

        // Copy current base states to active
        for (size_t k = 0; k < current_states->count; k++)
        {
            // Only add if not waiting for '/' (those are for next layer)
            // Actually, waiting for '/' means we expect '/' NOW. But we serve 'entry' (chars).
            // So logic: if literal[offset] == '/', we FAIL matching 'entry' (unless entry has / which it doesn't).
            state_set_add(&active, current_states->states[k].node, current_states->states[k].lit_offset);
        }

        epsilon_closure(&active);

        bool possible = true;
        for (char *p = entry; *p; p++)
        {
            if (active.count == 0)
            {
                possible = false;
                break;
            }

            step_char(&active, *p, &next_step);

            // Swap
            state_set_free(&active);
            active = next_step;
            state_set_init(&next_step);

            epsilon_closure(&active);
        }

        // After matching entry string:
        // Identify states that are ready to accept '/' or are terminating
        if (possible && active.count > 0)
        {
            state_set_t passed_nodes; // Nodes valid for recursing into 'path/entry'
            state_set_init(&passed_nodes);

            bool entry_creates_match = false; // Is 'path/entry' a match?

            for (size_t k = 0; k < active.count; k++)
            {
                nfa_state_t s = active.states[k];
                rbcglob_node_t *n = s.node;
                if (!n)
                    continue;

                // If we reached ACCEPT
                if (n->type == OP_ACCEPT)
                    entry_creates_match = true;

                // If we reached Separator
                if (n->type == OP_MATCH_LITERAL && n->data.literal[s.lit_offset] == '/')
                {
                    // Consumes '/' implicitly by moving into directory 'entry'
                    if (n->data.literal[s.lit_offset + 1] == '\0')
                    {
                        state_set_add(&passed_nodes, n->next, 0);
                    }
                    else
                    {
                        state_set_add(&passed_nodes, n, s.lit_offset + 1);
                    }
                }
                // Handle STAR2: If active state is STAR2, it effectively matches "current dir",
                // so it can continue matching inside.
                // STAR2 -> STAR2 (consume dir)
                if (n->type == OP_MATCH_STAR2)
                {
                    state_set_add(&passed_nodes, n, 0);
                }
            }

            char *next_path = path_join(path, entry);

            if (entry_creates_match)
            {
                ctx->cb(next_path, ctx->ud);
            }

            if (passed_nodes.count > 0)
            {
                execute_recursive(&passed_nodes, next_path, ctx);
            }

            free(next_path);
            state_set_free(&passed_nodes);
        }

        state_set_free(&active);
        state_set_free(&next_step);
    }

    // Clean up
    for (size_t i = 0; i < count; i++)
        free(entries[i]);
    free(entries);

    // Also Recurse for the 'next_layer_nodes' (Empty string match for separators)
    // E.g. 'src/' -> 'src' matched in previous layer. Now just '/' pending matches.
    // In current structure, 'execute_recursive' is called AFTER consuming a path component.
    // So 'next_layer_nodes' collected at START of this function are actually
    // paths that matched NOTHING in this directory (e.g. `//` or `src/` where src is empty? No.)
    // Wait, initial logic:
    // `current_states` are states valid BEFORE consuming `entry`.
    // We scan `current_states` for `/`.
    // If found, these are transitions that expect `/` immediately.
    // But `readdir` gives us "entries".
    // Does `/` match an entry? No.
    // `/` matches a *directory boundary*.
    // But we are ALREADY at a directory boundary (inside `execute_recursive`).
    // So if a state expects `/`, and we are at a directory, does it consume it?
    // My compiler separates components with `/`.
    // `src/` -> `src`, `/`.
    // We match `src`. Resulting state is at `/`.
    // Recurse `execute_recursive(states_at_slash, "src")`.
    // Inside: we check if states match `/`.
    // Yes. They transition to next component.
    // But we haven't consumed a directory entry for *that* slash.
    // We just descend.

    if (next_layer_nodes.count > 0)
    {
        // This represents matching `.` (current dir) effectively as a directory step?
        // No, this handles the explicit `/`.
        // If we have nodes waiting for `/`, and we are here, we verify path is dir? (We did opendir).
        // Then we Recurse with Same Path, but advanced nodes (skipped /).
        // BUT we must avoid infinite recursion if no consumption.
        // Nodes advanced past `/`. So progress made.
        execute_recursive(&next_layer_nodes, path, ctx);
    }

    state_set_free(&next_layer_nodes);
    depth--;
}

void rbcglob_nfa_execute(
    rbcglob_node_t *root,
    const char *base_path,
    unsigned flags,
    rbcglob_match_callback_t callback,
    void *user_data)
{
    fprintf(stderr, "rbcglob_nfa_execute: root=%p, base_path=%s\n", (void *)root, base_path ? base_path : "NULL");

    state_set_t initial;
    state_set_init(&initial);
    state_set_add(&initial, root, 0);

    exec_ctx_t ctx = {callback, user_data, flags};

    execute_recursive(&initial, base_path ? base_path : ".", &ctx);

    state_set_free(&initial);

    fprintf(stderr, "rbcglob_nfa_execute: complete\n");
}
