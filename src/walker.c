#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include "pattern.h"
#include "utils.h"
#include "rbc/rbc.h"

typedef struct fs_dir_iter_s fs_dir_iter_t;

typedef struct
{
    const char *name;
    bool is_dir;
    bool is_symlink;
    // Helper for d_type unknown cases
    bool type_unknown;
} fs_entry_t;

#if defined(_WIN32)
#include <windows.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Helper for UTF-8 <-> UTF-16 conversions
static wchar_t *utf8_to_wide(const char *utf8)
{
    if (!utf8)
        return NULL;
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (needed == 0)
        return NULL;
    wchar_t *wide = malloc(needed * sizeof(wchar_t));
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, needed);
    return wide;
}

static char *wide_to_utf8(const wchar_t *wide)
{
    if (!wide)
        return NULL;
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (needed == 0)
        return NULL;
    char *utf8 = malloc(needed);
    if (!utf8)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, needed, NULL, NULL);
    return utf8;
}

struct fs_dir_iter_s
{
    HANDLE handle;
    WIN32_FIND_DATAW find_data;
    bool first;
    bool finished;
    char *current_utf8_name;
};

static fs_dir_iter_t *fs_open(const char *path)
{
    const char *p = (path && *path) ? path : ".";
    // Build pattern: path\*
    size_t len = strlen(p);
    char *pattern = malloc(len + 3); // \ * \0
    if (!pattern)
        return NULL;

    strcpy(pattern, p);
    // Append separator if needed
    if (len > 0 && p[len - 1] != '/' && p[len - 1] != '\\')
    {
        strcat(pattern, "\\*");
    }
    else
    {
        strcat(pattern, "*");
    }

    wchar_t *wpattern = utf8_to_wide(pattern);
    free(pattern);
    if (!wpattern)
        return NULL;

    fs_dir_iter_t *iter = malloc(sizeof(fs_dir_iter_t));
    if (!iter)
    {
        free(wpattern);
        return NULL;
    }
    memset(iter, 0, sizeof(fs_dir_iter_t));

    iter->handle = FindFirstFileW(wpattern, &iter->find_data);
    free(wpattern);

    if (iter->handle == INVALID_HANDLE_VALUE)
    {
        free(iter);
        return NULL;
    }

    iter->first = true;
    iter->finished = false;
    return iter;
}

static void fs_close(fs_dir_iter_t *iter)
{
    if (iter)
    {
        if (iter->handle != INVALID_HANDLE_VALUE)
            FindClose(iter->handle);
        if (iter->current_utf8_name)
            free(iter->current_utf8_name);
        free(iter);
    }
}

static bool fs_next(fs_dir_iter_t *iter, fs_entry_t *out_entry)
{
    if (!iter || iter->finished)
        return false;

    if (iter->first)
    {
        iter->first = false;
    }
    else
    {
        if (!FindNextFileW(iter->handle, &iter->find_data))
        {
            iter->finished = true;
            return false;
        }
    }

    if (iter->current_utf8_name)
        free(iter->current_utf8_name);
    iter->current_utf8_name = wide_to_utf8(iter->find_data.cFileName);
    out_entry->name = iter->current_utf8_name;

    out_entry->is_dir = (iter->find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out_entry->is_symlink = (iter->find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    out_entry->type_unknown = false;
    return true;
}

static bool fs_is_dir(const char *path)
{
    if (!path)
        return false;
    wchar_t *wpath = utf8_to_wide(path);
    if (!wpath)
        return false;
    DWORD attrs = GetFileAttributesW(wpath);
    free(wpath);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool fs_is_dir_nofollow(const char *path)
{
    return fs_is_dir(path);
}
#else
#include <dirent.h>

struct fs_dir_iter_s
{
    DIR *d;
};

static fs_dir_iter_t *fs_open(const char *path)
{
    const char *p = (path && *path) ? path : ".";
    DIR *d = opendir(p);
    if (!d)
        return NULL;
    fs_dir_iter_t *iter = malloc(sizeof(fs_dir_iter_t));
    if (iter)
        iter->d = d;
    else
        closedir(d);
    return iter;
}

static void fs_close(fs_dir_iter_t *iter)
{
    if (iter)
    {
        if (iter->d)
            closedir(iter->d);
        free(iter);
    }
}

static bool fs_next(fs_dir_iter_t *iter, fs_entry_t *out_entry)
{
    if (!iter || !iter->d)
        return false;
    struct dirent *ent = readdir(iter->d);
    if (!ent)
        return false;

    out_entry->name = ent->d_name;

#ifdef _DIRENT_HAVE_D_TYPE
    out_entry->is_dir = (ent->d_type == DT_DIR);
    out_entry->is_symlink = (ent->d_type == DT_LNK);
    out_entry->type_unknown = (ent->d_type == DT_UNKNOWN);
#else
    out_entry->is_dir = false;
    out_entry->is_symlink = false;
    out_entry->type_unknown = true;
#endif
    return true;
}

static bool fs_is_dir(const char *path)
{ // Follows symlink
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static bool fs_is_dir_nofollow(const char *path)
{ // Does not follow symlink
    struct stat st;
    return (lstat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

#endif

typedef struct
{
    rbc_match_callback_t cb;

    void *ud;
    unsigned flags;
    bool sort;
} exec_ctx_t;

typedef struct segment_stack_s
{
    rbc_segment_t *seg;
    struct segment_stack_s *next;
} segment_stack_t;

// Frame states
enum
{
    ST_INIT,
    ST_LITERAL_DONE,
    ST_DIR_LOOP,
    ST_DIR_LOOP_RESTORE,
    ST_RECURSIVE_CHECK_CHILD,
    ST_RECURSIVE_NEXT,
    ST_BRANCH_LOOP
};

// Stack frame
typedef struct
{
    // Arguments
    rbc_segment_t *seg;
    segment_stack_t *stack_ptr;
    bool from_wildcard;

    // Local state variables
    int state;
    size_t entry_len; // To restore path_buf length
    fs_dir_iter_t *iter;
    rbc_segment_t *alt;      // For BRANCH
    segment_stack_t branch_node; // For BRANCH storage

    // Cached entry info for fused recursion
    bool current_is_dir;
    bool current_is_symlink;
    bool current_type_unknown;
    char current_name[256];
} frame_t;

typedef struct
{
    frame_t *items;
    size_t count;
    size_t capacity;
} exec_stack_t;

static void stack_push(exec_stack_t *st, rbc_segment_t *seg, segment_stack_t *stack_ptr, bool from_wildcard)
{
    if (st->count == st->capacity)
    {
        size_t new_cap = st->capacity ? st->capacity * 2 : 16;
        st->items = realloc(st->items, new_cap * sizeof(frame_t));
        st->capacity = new_cap;
    }
    frame_t *f = &st->items[st->count++];
    f->seg = seg;
    f->stack_ptr = stack_ptr;
    f->from_wildcard = from_wildcard;
    f->state = ST_INIT;
    f->iter = NULL;
    // other fields initialized when used
}

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

// Logic to determine next segment helper
// Returns true if it pushed a new frame, false if it executed callback (leaf)
static bool push_next(exec_stack_t *st, char *path, rbc_segment_t *current_seg, segment_stack_t *stack_ptr, exec_ctx_t *ctx, bool from_wildcard)
{
    if (current_seg && current_seg->next)
    {
        stack_push(st, current_seg->next, stack_ptr, from_wildcard);
        return true;
    }
    else if (stack_ptr)
    {
        stack_push(st, stack_ptr->seg, stack_ptr->next, from_wildcard);
        return true;
    }
    else
    {
        ctx->cb(path, ctx->ud);
        return false;
    }
}

void rbc_execute_segments(
    rbc_segment_t *root,
    const char *base_path,
    unsigned flags,
    bool sort,
    rbc_match_callback_t callback,
    void *user_data)
{
    exec_ctx_t ctx = {callback, user_data, flags, sort};

    char stack_buf[PATH_MAX];
    char *path_buf = stack_buf;

    size_t path_len = 0;
    path_buf[0] = '\0';

    if (base_path && *base_path)
    {
        strncpy(path_buf, base_path, PATH_MAX - 1);
        path_buf[PATH_MAX - 1] = '\0';
        path_len = strlen(path_buf);
    }

    // Initialize Stack
    exec_stack_t st = {NULL, 0, 0};

    // Push initial task (similar to run_next_recursive logic for NULL, but here we start with root)
    // Actually the recursive entry point was segment_run_recursive(root, NULL...)
    // But segment_run_recursive handles NULL seg by calling run_next_recursive(NULL...) which handles stack.
    // If root is valid:
    stack_push(&st, root, NULL, false);

    while (st.count > 0)
    {
        frame_t *f = &st.items[st.count - 1]; // Top

        // Load context from frame
        rbc_segment_t *seg = f->seg;

        switch (f->state)
        {
        case ST_INIT:
            f->entry_len = path_len; // Save current length

            if (!seg)
            {
                // Equivalent to: if (!seg) run_next_recursive(NULL...)
                // We pop this frame and push the "next" logic?
                // Actually push_next(NULL...) will push the next frame ON TOP.
                st.count--; // Pop current "empty" frame effectively (tail call optimization)
                push_next(&st, path_buf, NULL, f->stack_ptr, &ctx, f->from_wildcard);
                continue;
            }

            if (seg->type == RBC_SEGMENT_LITERAL)
            {
                if (buf_append(path_buf, &path_len, seg->data.literal) == 0)
                {
                    st.count--; // Overflow/Error
                    continue;
                }

                if (fs_is_dir(path_buf))
                {
                    // Check if we need to continue
                    bool is_dir = true; // Confirmed by fs_is_dir
                    bool has_more = (seg->next != NULL) || (f->stack_ptr != NULL && f->stack_ptr->seg != NULL);

                    if (!has_more || is_dir)
                    {
                        // Recurse: run_next_recursive
                        f->state = ST_LITERAL_DONE;
                        // Instead of pop, we pause this frame and push child
                        push_next(&st, path_buf, seg, f->stack_ptr, &ctx, false);
                    }
                    else
                    {
                        // End of this path
                        path_len = f->entry_len;
                        path_buf[path_len] = '\0';
                        st.count--;
                    }
                }
                else
                {
                    // Maybe file?
                    // Original logic used `stat` then checked `S_ISDIR`.
                    // If simply file exists:
                    struct stat s;
                    if (stat(path_buf, &s) == 0)
                    {
                        bool is_dir = S_ISDIR(s.st_mode);
                        bool has_more = (seg->next != NULL) || (f->stack_ptr != NULL && f->stack_ptr->seg != NULL);
                        if (!has_more || is_dir)
                        {
                            f->state = ST_LITERAL_DONE;
                            push_next(&st, path_buf, seg, f->stack_ptr, &ctx, false);
                        }
                        else
                        {
                            path_len = f->entry_len;
                            path_buf[path_len] = '\0';
                            st.count--;
                        }
                    }
                    else
                    {
                        // Not found
                        path_len = f->entry_len;
                        path_buf[path_len] = '\0';
                        st.count--;
                    }
                }
            }
            else if (seg->type == RBC_SEGMENT_WILDCARD || seg->type == RBC_SEGMENT_RECURSIVE)
            {
                const char *open_path = (path_len == 0) ? "." : path_buf;
                f->iter = fs_open(open_path);
                if (!f->iter)
                {
                    st.count--;
                    continue;
                }
                f->state = ST_DIR_LOOP;
            }
            else if (seg->type == RBC_SEGMENT_BRANCH)
            {
                f->alt = seg->data.branch.head;
                // Prepare the stack node for children
                f->branch_node.seg = seg->next;
                f->branch_node.next = f->stack_ptr;

                f->state = ST_BRANCH_LOOP;
                // Go to loop immediately
            }
            break;

        case ST_LITERAL_DONE:
            // Came back from child
            path_len = f->entry_len;
            path_buf[path_len] = '\0';
            st.count--;
            break;

        case ST_DIR_LOOP:
            // Loop over directory entries
            // We are "paused" inside the loop.
            {
                fs_entry_t entry;
                if (!fs_next(f->iter, &entry))
                {
                    fs_close(f->iter);
                    f->iter = NULL;
                    st.count--;
                    continue;
                }

                // Process Entry
                const char *name = entry.name;
                if (strcmp(name, "..") == 0)
                    continue; // Loop again
                if (strcmp(name, ".") == 0)
                {
                    if (seg->type == RBC_SEGMENT_RECURSIVE)
                        continue;
                    if (f->from_wildcard)
                        continue;
                }

                // Check d_type to avoid unnecessary recursion or stats
                bool is_dir_known = entry.is_dir;
                // bool is_not_dir_known = !entry.is_dir && !entry.type_unknown && !entry.is_symlink;

                // REMOVED Optimization: We must process files for Fused Loop matching (and pure ** matches files too)
                /*
                if (seg->type == SEG_RECURSIVE)
                {
                    if (is_not_dir_known)
                        continue;
                }
                */

                bool is_hidden = (name[0] == '.');
                if (is_hidden && !(ctx.flags & RBC_FNM_DOTMATCH))
                {
                    if (seg->type == RBC_SEGMENT_WILDCARD)
                    {
                        if (seg->data.glob.original_pattern[0] != '.')
                            continue;
                    }
                    else
                    {
                        continue;
                    }
                }

                bool should_recurse = false;
                bool next_from_wildcard = false;

                if (seg->type == RBC_SEGMENT_WILDCARD)
                {
                    bool matched = rbc_matcher_exec(&seg->data.glob.matcher, name, ctx.flags);

                    if (matched)
                    {
                        should_recurse = true;
                        next_from_wildcard = true;
                    }
                }
                else if (seg->type == RBC_SEGMENT_RECURSIVE)
                {
                    // logic for ** recursion
                    bool can_recurse = is_dir_known;
                    if (!can_recurse && entry.type_unknown)
                    {
                        // struct stat st;
                        size_t old_len = path_len;
                        if (buf_append(path_buf, &path_len, name) > 0)
                        {
                            if (fs_is_dir_nofollow(path_buf)) // lstat default
                                can_recurse = true;
                            path_len = old_len;
                            path_buf[path_len] = '\0'; // Restore
                        }
                    }
                    else if (!can_recurse && entry.is_symlink)
                    {
                        // struct stat st;
                        size_t old_len = path_len;
                        if (buf_append(path_buf, &path_len, name) > 0)
                        {
                            if (fs_is_dir(path_buf)) // stat (follows link) for explicit check?
                                can_recurse = true;
                            path_len = old_len;
                            path_buf[path_len] = '\0';
                        }
                    }
                    else if (entry.is_dir)
                    {
                        can_recurse = true;
                    }

                    // Store directory status in frame for state machine resumption
                    f->current_is_dir = can_recurse; // We trust the logic above
                    // We don't strictly need symlink specific flags if we trust can_recurse result,
                    // but if we needed to re-stat later we might. Ideally we don't re-stat.

                    // --- OPTIMIZATION: Fused Loop (Single Pass) ---
                    // Instead of just recursing, we ALSO check if this entry matches the *next* segment.
                    // If it does, we spawn a task for that match.
                    // AFTER that task completes, we come back (ST_RECURSIVE_CHECK_CHILD) and do the recursion.

                    rbc_segment_t *next_seg = seg->next;
                    segment_stack_t *next_stack = f->stack_ptr;

                    // Resolve next segment across stack boundaries if needed
                    if (!next_seg && next_stack)
                    {
                        next_seg = next_stack->seg;
                        next_stack = next_stack->next;
                    }

                    // DEBUG
                    // printf("DEBUG: Path='%.*s' Item='%s' NextSegType=%d\n", (int)path_len, path_buf, name, next_seg ? next_seg->type : -1);

                    bool matched_next = false;
                    if (next_seg)
                    {
                        if (next_seg->type == RBC_SEGMENT_WILDCARD)
                        {
                            matched_next = rbc_matcher_exec(&next_seg->data.glob.matcher, name, ctx.flags);
                        }
                        else if (next_seg->type == RBC_SEGMENT_LITERAL)
                        {
                            if (strcmp(name, next_seg->data.literal) == 0)
                            {
                                matched_next = true;
                            }
                        }
                    }

                    if (matched_next)
                    {
                        // We have a match for the NEXT segment (e.g. found 'foo.c' matching '*.c' while in '**').
                        // We must process this match.
                        size_t old_len = path_len;
                        if (buf_append(path_buf, &path_len, name) > 0)
                        {
                            // If this was the last segment, emit callback immediately
                            if (!next_seg->next && !next_stack)
                            {
                                ctx.cb(path_buf, ctx.ud);
                                path_len = old_len;
                                path_buf[path_len] = '\0';
                            }
                            else
                            {
                                // Otherwise push a task for the REST of the path
                                push_next(&st, path_buf, next_seg, next_stack, &ctx, true);

                                // CRITICAL: We pause *this* frame to let the child run.
                                // When child returns, we resume at ST_RECURSIVE_CHECK_CHILD to do the recursion part.
                                f->state = ST_RECURSIVE_CHECK_CHILD;
                                f->entry_len = old_len; // Save fallback length
                                // Cache name for the second pass (Recursion)
                                strncpy(f->current_name, name, 255);
                                f->current_name[255] = '\0';
                                goto loop_continue;
                            }
                        }
                    }

                    // If no match (or match emitted immediately), fall through to Recursion Check
                    goto check_recursion;
                }

                // --- End SEG_RECURSIVE block ---

                // Standard processing for WILDCARD/LITERAL (non-recursive segments)
                // (This part of logic remains from original loop, ensure variables align)
                if (should_recurse)
                {
                    // Optimization: If we have a next segment (which implies we need to enter a directory),
                    // but we know this entry is NOT a directory (and not a symlink that might point to one),
                    // we can skip the expensive push_next -> opendir failure cycle.
                    // Only apply this checks if there really IS a next segment.
                    // If seg->next is NULL, it's a leaf match, so files are valid!
                    if (seg->next && !entry.type_unknown && !entry.is_dir && !entry.is_symlink)
                    {
                        continue;
                    }

                    size_t old_len = path_len;
                    if (buf_append(path_buf, &path_len, name) > 0)
                    {
                        push_next(&st, path_buf, seg, f->stack_ptr, &ctx, next_from_wildcard);
                        f->state = ST_DIR_LOOP_RESTORE;
                        f->entry_len = old_len;
                        goto loop_continue;
                    }
                }

                // If we are here, we are looping to next entry normally.
                continue;

            // --- Shared Recursion Check Block ---
            // Reached via goto or state transition
            check_recursion:
                if (f->current_is_dir)
                {
                    // Perform Recursion: push SEG_RECURSIVE (self) for this directory
                    size_t old_len = path_len;
                    // 'name' is valid here because we are still in the entry processing block
                    if (buf_append(path_buf, &path_len, name) > 0)
                    {
                        stack_push(&st, seg, f->stack_ptr, true);
                        f->state = ST_DIR_LOOP_RESTORE;
                        f->entry_len = old_len;
                        goto loop_continue;
                    }
                }
                // If not recursing, continue to next entry
                continue;
            } // End of entry processing block
            break;

        case ST_RECURSIVE_CHECK_CHILD:
            // Returning from "Match Next" task. Now we must check if we should Recurse (the "**" part).
            // path_buf currently contains "path/to/dir/file". We must restore it to "path/to/dir".
            // f->entry_len held the length before appending the name.
            path_len = f->entry_len;
            path_buf[path_len] = '\0';

            // Now perform the recursion check using cached name
            if (f->current_is_dir)
            {
                const char *chk_name = f->current_name;
                size_t old_len = path_len;
                if (buf_append(path_buf, &path_len, chk_name) > 0)
                {
                    // Push SEG_RECURSIVE (self)
                    stack_push(&st, f->seg, f->stack_ptr, true);
                    f->state = ST_DIR_LOOP_RESTORE;
                    f->entry_len = old_len;
                    goto loop_continue;
                }
            }

            // Done processing this entry (both match and recurse attempt finished).
            // Resume directory iteration.
            f->state = ST_DIR_LOOP;
            break; // Break switch to re-enter loop which enters ST_DIR_LOOP which calls fs_next()
            // `fs_dir_iter_t` implementation usually keeps the current name in buffers.
            // Let's assume `f->iter` has it. `fs_next` returns bool.
            // We can add `name_ptr` to `frame_t`.

            // Let's assume for now we need to fix this.
            // I'll grab the name from the iterator if possible, or save it.
            // But first, let's complete the `replace_string` to fix the mess.
            // I will use a placeholder `name = f->iter->current_name` or similar if I can find it,
            // OR I will simply rely on the fact that `buf_append` needs it.
            // If I can't get it, I can't append it again.

            // Wait, we appended it for the *Previous* task. Then we stripped it.
            // Now we need to append it *Again* for the recursion task.
            // So we definitely need the name.

            // Plan: Add `char current_name[256]` or `char *current_name` to `frame_t`?
            // Or just `strdup` it? memory management...
            // `readdir` strings are static/thread-local or in the DIR struct.
            // On Linux `readdir` returns pointer to statics or buffer in DIR.
            // It should remain valid as long as we don't call `readdir` (fs_next) again.
            // Since we pause processing of *this* directory to process a *sub* directory (or match),
            // and we rely on the stack, we are NOT iterating this directory further yet.
            // So the `dirent` should still be valid!
            // Depending on implementation of `fs_next` (wrapper).
            // Let's verify `fs_next`.
            break;

        case ST_DIR_LOOP_RESTORE:
            // Returning from child
            path_len = f->entry_len;
            path_buf[path_len] = '\0';
            f->state = ST_DIR_LOOP; // Go back to loop
            // We need to re-execute the loop logic to get NEXT entry.
            // But verify: we just finished processing ONE entry.
            // So loops check condition?
            // The `ST_DIR_LOOP` case will call `readdir` again. Correct.
            break;

        case ST_RECURSIVE_NEXT:
            // Returning from "run_next_recursive" call at end of RECURSIVE
            st.count--;
            break;

        case ST_BRANCH_LOOP:
            // f->alt is current alternative
            if (f->alt)
            {
                rbc_segment_t *curr = f->alt;
                f->alt = f->alt->next_alt; // Advance for next time

                f->state = ST_BRANCH_LOOP; // Come back here

                // Push child: segment_run_recursive(alt, &new_frame...)
                stack_push(&st, curr, &f->branch_node, f->from_wildcard);
            }
            else
            {
                st.count--; // Done
            }
            break;
        }
    loop_continue:;
    }

    if (st.items)
        free(st.items);
}
