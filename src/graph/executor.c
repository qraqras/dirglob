#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include "rbcglob/internal/graph.h"
#include "rbcglob/internal/traverse.h"
#include "rbcglob/file.h"

/******************************************************************************
 * Local NFA Executor (for matching filenames within SEG_WILDCARD)
 ******************************************************************************/

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
    nfa_state_t *heap_ptr;
} state_set_t;

static void state_set_init(state_set_t *set, nfa_state_t *buf, size_t cap)
{
    set->states = buf;
    set->count = 0;
    set->capacity = cap;
    set->heap_ptr = NULL;
}

static void state_set_free(state_set_t *set)
{
    if (set->heap_ptr)
        free(set->heap_ptr);
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
        size_t new_cap = set->capacity ? set->capacity * 2 : 16;
        nfa_state_t *new_ptr = malloc(new_cap * sizeof(nfa_state_t));
        if (!new_ptr)
            return; // Silent fail on OOM

        memcpy(new_ptr, set->states, set->count * sizeof(nfa_state_t));
        if (set->heap_ptr)
            free(set->heap_ptr);

        set->states = new_ptr;
        set->heap_ptr = new_ptr;
        set->capacity = new_cap;
    }
    set->states[set->count].node = node;
    set->states[set->count].lit_offset = offset;
    set->count++;
}

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
        }
        else if (n->type == OP_JUMP)
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
    }
}

static bool local_nfa_match(const char *text, rbcglob_node_t *start_node)
{
    nfa_state_t buf1[32];
    nfa_state_t buf2[32];
    state_set_t current, next;
    state_set_init(&current, buf1, 32);
    state_set_init(&next, buf2, 32);

    state_set_add(&current, start_node, 0);
    epsilon_closure(&current);

    for (const char *p = text; *p; p++)
    {
        char c = *p;
        next.count = 0;

        for (size_t i = 0; i < current.count; i++)
        {
            nfa_state_t s = current.states[i];
            rbcglob_node_t *n = s.node;
            if (!n)
                continue;

            if (n->type == OP_MATCH_LITERAL)
            {
                if (n->data.literal[s.lit_offset] == c)
                {
                    if (n->data.literal[s.lit_offset + 1] == '\0')
                        state_set_add(&next, n->next, 0);
                    else
                        state_set_add(&next, n, s.lit_offset + 1);
                }
            }
            else if (n->type == OP_MATCH_STAR)
            {
                state_set_add(&next, n, 0);
            }
            else if (n->type == OP_MATCH_QMARK)
            {
                state_set_add(&next, n->next, 0);
            }
            else if (n->type == OP_MATCH_CLASS)
            {
                unsigned char uc = (unsigned char)c;
                bool match = (n->data.char_class.map[uc / 8] & (1 << (uc % 8))) != 0;
                if (n->data.char_class.is_negated)
                    match = !match;
                if (match)
                    state_set_add(&next, n->next, 0);
            }
        }

        state_set_t temp = current;
        current = next;
        next = temp;

        epsilon_closure(&current);
        if (current.count == 0)
            break;
    }

    bool matched = false;
    for (size_t i = 0; i < current.count; i++)
    {
        if (current.states[i].node && current.states[i].node->type == OP_ACCEPT)
        {
            matched = true;
            break;
        }
    }

    state_set_free(&current);
    state_set_free(&next);
    return matched;
}

/******************************************************************************
 * Segment Executor
 ******************************************************************************/

typedef struct
{
    rbcglob_match_callback_t cb;
    void *ud;
    unsigned flags;
    bool sort;
} exec_ctx_t;

typedef struct segment_stack_s
{
    rbcglob_segment_t *seg;
    struct segment_stack_s *next;
} segment_stack_t;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Helper to append to buffer
static size_t buf_append(char *buf, size_t *current_len, const char *str)
{
    size_t len = strlen(str);
    if (*current_len + len + 1 >= PATH_MAX)
        return 0; // Overflow check

    // Add separator if needed
    size_t added = 0;
    if (*current_len > 0 && buf[*current_len - 1] != '/')
    {
        if (*current_len + 1 >= PATH_MAX)
            return 0;
        buf[(*current_len)++] = '/';
        added++;
    }

    strcpy(buf + *current_len, str);
    *current_len += len;
    return added + len;
}

static void segment_run_recursive(char *path_buf, size_t *path_len, rbcglob_segment_t *seg, segment_stack_t *stack, exec_ctx_t *ctx, bool from_wildcard);

static void run_next_recursive(char *path_buf, size_t *path_len, rbcglob_segment_t *current_seg, segment_stack_t *stack, exec_ctx_t *ctx, bool from_wildcard)
{
    if (current_seg && current_seg->next)
    {
        segment_run_recursive(path_buf, path_len, current_seg->next, stack, ctx, from_wildcard);
    }
    else if (stack)
    {
        segment_run_recursive(path_buf, path_len, stack->seg, stack->next, ctx, from_wildcard);
    }
    else
    {
        ctx->cb(path_buf, ctx->ud);
    }
}

static void segment_run_recursive(char *path_buf, size_t *path_len, rbcglob_segment_t *seg, segment_stack_t *stack, exec_ctx_t *ctx, bool from_wildcard)
{
    if (!seg)
    {
        run_next_recursive(path_buf, path_len, NULL, stack, ctx, from_wildcard);
        return;
    }

    if (seg->type == SEG_LITERAL)
    {
        size_t original_len = *path_len;
        if (buf_append(path_buf, path_len, seg->data.literal) == 0)
            return; // Error or overflow

        struct stat st;
        if (stat(path_buf, &st) == 0)
        {
            bool is_dir = S_ISDIR(st.st_mode);
            // Check if we need to continue
            bool has_more = (seg->next != NULL) || (stack != NULL && stack->seg != NULL);

            if (!has_more || is_dir)
            {
                run_next_recursive(path_buf, path_len, seg, stack, ctx, false);
            }
        }

        // Restore
        *path_len = original_len;
        path_buf[*path_len] = '\0';
    }
    else if (seg->type == SEG_WILDCARD || seg->type == SEG_RECURSIVE)
    {
        // Prepare to read current directory
        const char *open_path = (*path_len == 0) ? "." : path_buf;
        DIR *d = opendir(open_path);
        if (!d)
            return;

        struct dirent *entry;
        while ((entry = readdir(d)) != NULL)
        {
            const char *name = entry->d_name;
            if (strcmp(name, "..") == 0)
                continue;
            if (strcmp(name, ".") == 0)
            {
                if (seg->type == SEG_RECURSIVE)
                    continue;
                if (from_wildcard)
                    continue;
            }

            // Check d_type to avoid unnecessary recursion or stats
            unsigned char dtype = entry->d_type;
            bool is_dir_known = (dtype == DT_DIR);
            bool is_not_dir_known = (dtype != DT_DIR && dtype != DT_UNKNOWN && dtype != DT_LNK);

            // If next segment requires a directory (and strictly so), we can skip non-dirs.
            // But next segment might be NULL (file match).
            // Logic:
            // - If seg->type is SEG_RECURSIVE (doublestar), we ONLY recurse into directories.
            if (seg->type == SEG_RECURSIVE)
            {
                if (is_not_dir_known)
                    continue; // Skip files for recursive descent
                // Note: Symlinks to dirs need to serve as dirs? valid glob usually follows symlinks in recursion?
                // Ruby's Dir.glob defaults to NOT following symlinks for **?
                // Actually Ruby Dir.glob follows symlinks if they are explicitly listed but ** recursion usually checks is_directory.
            }

            bool is_hidden = (name[0] == '.');
            if (is_hidden && !(ctx->flags & RBCGLOB_FNM_DOTMATCH))
            {
                if (seg->type == SEG_WILDCARD)
                {
                    if (seg->data.glob.original_pattern[0] != '.')
                    {
                        continue;
                    }
                }
                else
                {
                    continue;
                }
            }

            if (seg->type == SEG_WILDCARD)
            {
                if (seg->data.glob.must_start)
                {
                    if (strncmp(name, seg->data.glob.must_start, seg->data.glob.start_len) != 0)
                        continue;
                }
                if (seg->data.glob.must_end)
                {
                    size_t name_len = strlen(name);
                    if (name_len < seg->data.glob.end_len)
                        continue;
                    if (strcmp(name + name_len - seg->data.glob.end_len, seg->data.glob.must_end) != 0)
                        continue;
                }

                if (local_nfa_match(name, seg->data.glob.local_nfa_root))
                {
                    size_t original_len = *path_len;
                    if (buf_append(path_buf, path_len, name) > 0)
                    {
                        run_next_recursive(path_buf, path_len, seg, stack, ctx, true);

                        // Restore
                        *path_len = original_len;
                        path_buf[*path_len] = '\0';
                    }
                }
            }
            else if (seg->type == SEG_RECURSIVE)
            {
                // Try recursive step
                size_t original_len = *path_len;
                if (buf_append(path_buf, path_len, name) > 0)
                {
                    bool can_recurse = is_dir_known;
                    if (!can_recurse && dtype == DT_UNKNOWN)
                    {
                        struct stat st;
                        if (lstat(path_buf, &st) == 0 && S_ISDIR(st.st_mode))
                        { // Use lstat to avoid following symlinks in ** recursion?
                            can_recurse = true;
                        }
                    }
                    else if (!can_recurse && dtype == DT_LNK)
                    {
                        // For recursive **, usually we do NOT follow symlinks unless FNM_EXTMATCH?
                        // Ruby documentation: "** matches directories recursively."
                        // It usually follows physical structure.
                        // But commonly ** does not traverse symlinks to avoid infinite loops.
                        // Let's check with stat (follows link) or lstat?
                        // Ruby File.directory? follows links.
                        struct stat st;
                        if (stat(path_buf, &st) == 0 && S_ISDIR(st.st_mode))
                        {
                            // Check for infinite loop?
                            // For now, let's allow it to match behavior of simple recursion
                            can_recurse = true;
                        }
                    }
                    else if (dtype == DT_DIR)
                    {
                        can_recurse = true;
                    }

                    if (can_recurse)
                    {
                        segment_run_recursive(path_buf, path_len, seg, stack, ctx, true);
                    }

                    // Restore
                    *path_len = original_len;
                    path_buf[*path_len] = '\0';
                }
            }
        }
        closedir(d);

        if (seg->type == SEG_RECURSIVE)
        {
            run_next_recursive(path_buf, path_len, seg, stack, ctx, from_wildcard);
        }
    }
    else if (seg->type == SEG_BRANCH)
    {
        rbcglob_segment_t *alt = seg->data.branch.head;
        segment_stack_t new_frame = {seg->next, stack};

        while (alt)
        {
            segment_run_recursive(path_buf, path_len, alt, &new_frame, ctx, from_wildcard);
            alt = alt->next_alt;
        }
    }
}

void rbcglob_execute_segments(
    rbcglob_segment_t *root,
    const char *base_path,
    unsigned flags,
    bool sort,
    rbcglob_match_callback_t callback,
    void *user_data)
{
    exec_ctx_t ctx = {callback, user_data, flags, sort};

    char *path_buf = malloc(PATH_MAX);
    if (!path_buf)
        return;

    size_t path_len = 0;
    path_buf[0] = '\0';

    if (base_path && *base_path)
    {
        strncpy(path_buf, base_path, PATH_MAX - 1);
        path_buf[PATH_MAX - 1] = '\0';
        path_len = strlen(path_buf);
    }

    segment_run_recursive(path_buf, &path_len, root, NULL, &ctx, false);

    free(path_buf);
}
