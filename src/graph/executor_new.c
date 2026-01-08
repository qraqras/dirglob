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
                state_set_add(set, n->next, 0);
            }
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
            state_set_add(set, n->next, 0);
        }
        else if (n->type == OP_MATCH_STAR2)
        {
            // ** can match zero directories (skip to next)
            state_set_add(set, n->next, 0);
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
                if (n->data.literal[s.lit_offset + 1] == '\0')
                {
                    state_set_add(next_set, n->next, 0);
                }
                else
                {
                    state_set_add(next_set, n, s.lit_offset + 1);
                }
            }
        }
        else if (n->type == OP_MATCH_STAR)
        {
            state_set_add(next_set, n, 0);
        }
        else if (n->type == OP_MATCH_QMARK)
        {
            state_set_add(next_set, n->next, 0);
        }
        else if (n->type == OP_MATCH_CLASS)
        {
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
    // Compute epsilon closure
    epsilon_closure(current_states);

    // Check for accept state
    bool match_accept = false;
    for (size_t i = 0; i < current_states->count; i++)
    {
        if (current_states->states[i].node && current_states->states[i].node->type == OP_ACCEPT)
        {
            match_accept = true;
            break;
        }
    }

    if (match_accept)
    {
        ctx->cb(path, ctx->ud);
    }

    // Open directory
    DIR *d = opendir(path && *path ? path : ".");
    if (!d)
        return;

    // Read entries
    char **entries = NULL;
    size_t count = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

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

    qsort(entries, count, sizeof(char *), compare_entries);

    // Process each entry
    for (size_t i = 0; i < count; i++)
    {
        char *entry = entries[i];

        // Match entry name against current states
        state_set_t active, next_step;
        state_set_init(&active);
        state_set_init(&next_step);

        for (size_t k = 0; k < current_states->count; k++)
        {
            state_set_add(&active, current_states->states[k].node, current_states->states[k].lit_offset);
        }

        epsilon_closure(&active);

        // Step through entry name
        bool possible = true;
        for (char *p = entry; *p; p++)
        {
            if (active.count == 0)
            {
                possible = false;
                break;
            }

            step_char(&active, *p, &next_step);
            state_set_free(&active);
            active = next_step;
            state_set_init(&next_step);
            epsilon_closure(&active);
        }

        if (possible && active.count > 0)
        {
            char *next_path = path_join(path, entry);
            state_set_t passed_nodes;
            state_set_init(&passed_nodes);

            bool entry_match = false;

            for (size_t k = 0; k < active.count; k++)
            {
                nfa_state_t s = active.states[k];
                rbcglob_node_t *n = s.node;
                if (!n)
                    continue;

                // Check if this entry is a match
                if (n->type == OP_ACCEPT)
                {
                    entry_match = true;
                }

                // Check if we can descend into subdirectories
                // For **, continue with it
                if (n->type == OP_MATCH_STAR2)
                {
                    state_set_add(&passed_nodes, n, 0);
                }

                // For /, advance past it
                if (n->type == OP_MATCH_LITERAL && n->data.literal[s.lit_offset] == '/')
                {
                    if (n->data.literal[s.lit_offset + 1] == '\0')
                    {
                        state_set_add(&passed_nodes, n->next, 0);
                    }
                    else
                    {
                        state_set_add(&passed_nodes, n, s.lit_offset + 1);
                    }
                }
            }

            if (entry_match)
            {
                ctx->cb(next_path, ctx->ud);
            }

            // Recurse into subdirectory if it exists
            struct stat st;
            if (passed_nodes.count > 0 && stat(next_path, &st) == 0 && S_ISDIR(st.st_mode))
            {
                execute_recursive(&passed_nodes, next_path, ctx);
            }

            free(next_path);
            state_set_free(&passed_nodes);
        }

        state_set_free(&active);
        state_set_free(&next_step);
    }

    for (size_t i = 0; i < count; i++)
        free(entries[i]);
    free(entries);
}

void rbcglob_nfa_execute(
    rbcglob_node_t *root,
    const char *base_path,
    unsigned flags,
    rbcglob_match_callback_t callback,
    void *user_data)
{
    state_set_t initial;
    state_set_init(&initial);
    state_set_add(&initial, root, 0);

    exec_ctx_t ctx = {callback, user_data, flags};

    execute_recursive(&initial, base_path ? base_path : ".", &ctx);

    state_set_free(&initial);
}
