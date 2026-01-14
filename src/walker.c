#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include "internal.h"
#include "utils.h"
#include "rbc/rbc.h"
#include <stdio.h>

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

#ifdef __GNUC__
__attribute__((unused))
#endif
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
    char *name;
    bool is_dir;
    bool is_symlink;
    bool type_unknown;
} sorted_entry_t;

static int entry_cmp(const void *a, const void *b)
{
    const sorted_entry_t *ea = (const sorted_entry_t *)a;
    const sorted_entry_t *eb = (const sorted_entry_t *)b;
    return strcmp(ea->name, eb->name);
}

typedef struct
{
    rbc_match_callback_t cb;
    void *ud;
    unsigned flags;
    bool sort;
    rbc_arena_t *arena;
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
    ST_DIR_OPEN,
    ST_DIR_LOOP,
    ST_BRANCH_LOOP
};

// Stack frame
typedef struct
{
    rbc_segment_t *seg;
    segment_stack_t *stack_ptr;
    bool from_wildcard;
    bool post_recursive;
    char *path;
    size_t path_len;

    int state;
    fs_dir_iter_t *iter;
    sorted_entry_t *sorted_entries;
    size_t sorted_count;
    size_t sorted_idx;

    rbc_segment_t *alt;
    bool skipdot; // MRI: Skip "." in subdirectories (set after first iteration)
} frame_t;

typedef struct
{
    frame_t *items;
    size_t count;
    size_t capacity;
} exec_stack_t;

static void frame_cleanup(frame_t *f)
{
    if (f->iter)
    {
        fs_close(f->iter);
        f->iter = NULL;
    }
    if (f->sorted_entries)
    {
        for (size_t i = 0; i < f->sorted_count; i++)
            free(f->sorted_entries[i].name);
        free(f->sorted_entries);
        f->sorted_entries = NULL;
    }
}

static void stack_push(exec_stack_t *st, rbc_segment_t *seg, segment_stack_t *stack_ptr, bool from_wildcard, bool post_recursive, const char *path, size_t path_len, exec_ctx_t *ctx)
{
    if (st->count == st->capacity)
    {
        size_t new_cap = st->capacity ? st->capacity * 2 : 16;
        st->items = realloc(st->items, new_cap * sizeof(frame_t));
        st->capacity = new_cap;
    }
    frame_t *f = &st->items[st->count++];
    memset(f, 0, sizeof(frame_t));
    f->seg = seg;
    f->stack_ptr = stack_ptr;
    f->from_wildcard = from_wildcard;
    f->post_recursive = post_recursive;
    f->state = ST_INIT;
    
    // MRI: skipdot is false for root (first iteration), true for subdirectories
    // In MRI, this is done via FNM_GLOB_SKIPDOT flag
    // Here, we use post_recursive as a proxy: recursive calls have skipdot=true
    f->skipdot = post_recursive;

    if (path)
    {
        f->path = rbc_arena_alloc(ctx->arena, path_len + 1);
        memcpy(f->path, path, path_len);
        f->path[path_len] = '\0';
        f->path_len = path_len;
    }
}

#include <stdio.h>
#define DEBUG_WALKER 0

// Helper to append to buffer
// Helper to append to buffer
static size_t buf_append(char *buf, size_t *current_len, const char *str)
{
    if (!str)
        return 0;
    size_t len = strlen(str);

    // Simple rule: Add a separator if we have a path and it doesn't end in one.
    // Empty segments (str="") will result in a trailing slash being added.
    bool needs_sep = (*current_len > 0 && buf[*current_len - 1] != '/');
    if (*current_len == 0 && len == 0)
        needs_sep = true;

    if (*current_len + (needs_sep ? 1 : 0) + len + 1 >= PATH_MAX)
        return 0;

    size_t added = 0;
    if (needs_sep)
    {
        buf[(*current_len)++] = '/';
        added = 1;
    }

    if (len > 0)
    {
        memcpy(buf + *current_len, str, len);
        *current_len += len;
        added += len;
    }
    buf[*current_len] = '\0';
    return added;
}

// Helper to check if a name matches a segment
static bool segment_match(const rbc_segment_t *seg, const char *name, const exec_ctx_t *ctx)
{
    if (!seg || !name)
        return false;

    switch (seg->type)
    {
    case RBC_SEGMENT_LITERAL:
        return strcmp(seg->data.literal, name) == 0;

    case RBC_SEGMENT_WILDCARD:
        if (name[0] == '.')
        {
            if (!(ctx->flags & RBC_FNM_DOTMATCH))
            {
                if (seg->data.glob.original_pattern && seg->data.glob.original_pattern[0] != '.')
                    return false;
            }
        }
        return rbc_matcher_exec(&seg->data.glob.matcher, name);

    case RBC_SEGMENT_RECURSIVE:
        return true;

    default:
        return false;
    }
}

static bool push_next(exec_stack_t *st, char *path, size_t path_len, rbc_segment_t *current_seg, segment_stack_t *stack_ptr, exec_ctx_t *ctx, bool from_wildcard, bool post_recursive)
{
    if (current_seg && current_seg->next)
    {
        stack_push(st, current_seg->next, stack_ptr, from_wildcard, post_recursive, path, path_len, ctx);
        return true;
    }
    else if (stack_ptr)
    {
        stack_push(st, stack_ptr->seg, stack_ptr->next, from_wildcard, post_recursive, path, path_len, ctx);
        return true;
    }
    else
    {
        // Path is relative to the base_path
        const char *filename = path;
        const char *last_slash = strrchr(path, '/');
        if (last_slash)
            filename = last_slash + 1;

        // MRI Rule: ".." is never returned as a result matching a wildcard/recursion.
        if (from_wildcard && (strcmp(filename, "..") == 0))
            return false;

        ctx->cb(path, ctx->ud);
        return false;
    }
}

void rbc_walker_match_callback(const char *path, void *user_data)
{
    rbc_walker_ctx_t *ctx = (rbc_walker_ctx_t *)user_data;

    // MRI Parity Logic:
    // 1. Root match via "" (e.g. "**")
    if ((!path || path[0] == '\0') && !ctx->base)
    {
        if (!(ctx->flags & RBC_FNM_DOTMATCH))
            return;
    }

    // 2. Root match via "/" (e.g. "**/")
    // When base is NULL, this means we matched CWD root with trailing slash.
    // Ruby behavior: excludes "./" even with FNM_DOTMATCH for "**/".
    bool root_slash = (path && strcmp(path, "/") == 0);
    if (root_slash && !ctx->base)
    {
        return;
    }

    // MRI: Dir.glob("**", FNM_DOTMATCH) at root should return "."
    // If path is empty, it means current directory.
    const char *p = (path && path[0] != '\0') ? path : ".";

    rbc_glob_results_add(ctx->results, p);
}

static char *get_real_path(char *real_buf, const char *base, const char *rel)
{
    // If rel is absolute, or base is empty/NULL, just use rel.
    if ((rel && rel[0] == '/') || !base || !*base)
    {
        const char *p = (rel && *rel) ? rel : ".";
        strncpy(real_buf, p, PATH_MAX - 1);
        real_buf[PATH_MAX - 1] = '\0';
        return real_buf;
    }

    size_t blen = strlen(base);
    size_t rlen = rel ? strlen(rel) : 0;

    if (blen + 1 + rlen + 1 > PATH_MAX)
        return NULL;

    memcpy(real_buf, base, blen);
    size_t cur = blen;

    // Add separator if base doesn't end in one AND rel doesn't start with one
    // AND rel is not empty.
    if (blen > 0 && base[blen - 1] != '/' && rlen > 0 && rel[0] != '/')
    {
        real_buf[cur++] = '/';
    }

    if (rlen > 0)
    {
        memcpy(real_buf + cur, rel, rlen);
        cur += rlen;
    }
    real_buf[cur] = '\0';
    return real_buf;
}

void rbc_segment_exec(
    rbc_segment_t *root,
    const char *base_path,
    unsigned flags,
    bool sort,
    rbc_match_callback_t callback,
    void *user_data,
    rbc_arena_t *arena)
{
    exec_ctx_t ctx = {callback, user_data, flags, sort, arena};

    char path_buf[PATH_MAX];
    char real_path_buf[PATH_MAX];
    path_buf[0] = '\0';

    // Initialize Stack
    exec_stack_t st = {NULL, 0, 0};

    // Push initial task with EMPTY path_buf.
    // base_path is only used for syscalls via get_real_path.
    stack_push(&st, root, NULL, false, false, "", 0, &ctx);

    while (st.count > 0)
    {
        size_t cur_idx = st.count - 1;
        frame_t *f = &st.items[cur_idx];
        size_t path_len = f->path_len;
        if (f->path)
        {
            memcpy(path_buf, f->path, path_len);
        }
        path_buf[path_len] = '\0';

        // Capture state to survive reallocs during push
        rbc_segment_t *seg = f->seg;
        segment_stack_t *stack_ptr = f->stack_ptr;
        bool from_wildcard = f->from_wildcard;
        // If current segment is Recursive (**), it acts as a wildcard predecessor for matches within this loop
        if (seg && seg->type == RBC_SEGMENT_RECURSIVE)
            from_wildcard = true;
        bool post_recursive = f->post_recursive;

        switch (f->state)
        {
        case ST_INIT:
            if (!seg)
            {
                st.count--;
                push_next(&st, path_buf, path_len, NULL, stack_ptr, &ctx, from_wildcard, post_recursive);
                continue;
            }

            if (seg->type == RBC_SEGMENT_LITERAL)
            {
                // MRI Rule: post-recursive match must not be "." or ".."
                if (post_recursive && (strcmp(seg->data.literal, ".") == 0 || strcmp(seg->data.literal, "..") == 0))
                {
                    frame_cleanup(f);
                    st.count--;
                    continue;
                }

                if (buf_append(path_buf, &path_len, seg->data.literal) > 0 || (seg->data.literal && seg->data.literal[0] == '\0'))
                {
                    struct stat s_lit;
                    char *rp = get_real_path(real_path_buf, base_path, path_buf);
                    if (stat(rp, &s_lit) == 0)
                    {
                        bool is_dir = S_ISDIR(s_lit.st_mode);
                        bool has_more = (seg->next != NULL) || (stack_ptr != NULL);
                        if (!has_more || is_dir)
                        {
                            st.count--; // Pop current frame (LITERAL)
                            push_next(&st, path_buf, path_len, seg, stack_ptr, &ctx, from_wildcard, false);
                            continue; // Run next frame immediately
                        }
                    }
                }
                frame_cleanup(f);
                st.count--;
                continue;
            }
            else if (seg->type == RBC_SEGMENT_BRANCH)
            {
                f->alt = seg->data.branch.head;
                f->state = ST_BRANCH_LOOP;
            }
            else if (seg->type == RBC_SEGMENT_RECURSIVE)
            {
                // RBC_SEGMENT_RECURSIVE (**):
                // Logic:
                // 1. Check if current path effectively matches the recursive part (empty match).
                //    This covers "**/foo" matching "foo" (via empty ** + match foo).
                //    We do this via Match Zero logic on Current Path.

                // Match Zero (Current Path):
                // If we match here, we must output/push immediately to preserve Pre-Order (Parent First).
                // We use push_next (Top) to execute Match Zero logic.
                // BUT we also need to Recurse (Iterate) afterwards.

                // If we push MatchZero (Top), it runs and pops.
                // Then we assume we are still in "Recurse" mode?
                // But Recurse frame needs to change state to ST_DIR_OPEN.

                // Problem: If we change state to ST_DIR_OPEN, we lose the chance to do MatchZero?
                // Solution: Do MatchZero logic manually here, or spawn a frame.

                // If we spawn MatchZero frame:
                // push_next(MatchZero).
                // It sits on Top.
                // The current frame (Recurse) stays at ST_INIT?
                // If ST_INIT, it will loop forever spawning MatchZero.

                // We need to advance state.

                // Note: We used to push MatchZero here via push_next(seg->next).
                // But this causes DUPLICATION because ST_DIR_LOOP also checks for matches.
                // We rely on ST_DIR_LOOP to check matches on CHILDREN (interleaved).
                // However, we MUST check match on SELF (Empty Recursion) here?
                // Example: `**/` matches `root` (Self).
                // ST_DIR_LOOP only iterates children, so it misses Self.

                // So we push MatchZero ONLY if `seg->next` matches "Empty String" (Self).
                // e.g. NULL (Match All) or LITERAL "" (Trailing Slash).
                // e.g. LITERAL "a" does NOT match Empty String.

                // IMPORTANT: Advance state to prevent infinite loop of ST_INIT
                f->state = ST_DIR_OPEN;

                // Note: We do NOT output "." here for match_self cases
                // "." will be processed naturally in ST_DIR_LOOP if it matches the pattern
                // This avoids duplication and ensures correct skipdot behavior

                // We MUST skip re-using 'f' here, as push_next might realloc.
                // Instead of continue block logic, we can just break/continue with the knowledge that state is updated.
                // But we must NOT use 'f' after this.
                // And we must ensure loop continues to pick up new Top (MatchZero).
                continue;
            }

            else // WILDCARD
            {
                f->state = ST_DIR_OPEN;
            }
            break;

        case ST_BRANCH_LOOP:
            if (f->alt)
            {
                rbc_segment_t *curr = f->alt;
                f->alt = curr->next_alt;

                segment_stack_t *cont = NULL;
                if (seg->next || stack_ptr)
                {
                    cont = rbc_arena_alloc(ctx.arena, sizeof(segment_stack_t));
                    cont->seg = seg->next;
                    cont->next = stack_ptr;
                }
                stack_push(&st, curr, cont, from_wildcard, post_recursive, path_buf, path_len, &ctx);
            }
            else
            {
                frame_cleanup(f);
                st.count--;
            }
            break;

        case ST_DIR_OPEN:
        {
            char *rp = get_real_path(real_path_buf, base_path, path_buf);
            f->iter = fs_open(rp);
            if (!f->iter)
            {
                frame_cleanup(f);
                st.count--;
                continue;
            }

            if (ctx.sort)
            {
                size_t cap = 16;
                f->sorted_entries = malloc(sizeof(sorted_entry_t) * cap);
                if (f->sorted_entries)
                {
                    f->sorted_count = 0;
                    fs_entry_t entry;
                    while (fs_next(f->iter, &entry))
                    {
                        if (f->sorted_count == cap)
                        {
                            size_t new_cap = cap * 2;
                            sorted_entry_t *new_entries = realloc(f->sorted_entries, sizeof(sorted_entry_t) * new_cap);
                            if (!new_entries)
                                break;
                            f->sorted_entries = new_entries;
                            cap = new_cap;
                        }
                        f->sorted_entries[f->sorted_count].name = strdup(entry.name);
                        f->sorted_entries[f->sorted_count].is_dir = entry.is_dir;
                        f->sorted_entries[f->sorted_count].is_symlink = entry.is_symlink;
                        f->sorted_entries[f->sorted_count].type_unknown = entry.type_unknown;
                        f->sorted_count++;
                    }
                    if (f->sorted_count > 0)
                    {
                        qsort(f->sorted_entries, f->sorted_count, sizeof(sorted_entry_t), entry_cmp);
                    }
                }
                fs_close(f->iter);
                f->iter = NULL;
                f->sorted_idx = 0;
            }
            f->state = ST_DIR_LOOP;
        }
        break;

        case ST_DIR_LOOP:
        {
            // Get next entry FIRST and advance index/iterator
            const char *name = NULL;
            bool is_dir = false;
            bool is_symlink = false;
            bool type_unknown = false;
            bool has_entry = false;

            if (f->sorted_entries)
            {
                if (f->sorted_idx < f->sorted_count)
                {
                    sorted_entry_t *se = &f->sorted_entries[f->sorted_idx];
                    name = se->name;
                    is_dir = se->is_dir;
                    is_symlink = se->is_symlink;
                    type_unknown = se->type_unknown;
                    f->sorted_idx++; // Increment BEFORE processing
                    has_entry = true;
                }
            }
            else
            {
                fs_entry_t entry;
                if (fs_next(f->iter, &entry))
                {
                    name = entry.name;
                    is_dir = entry.is_dir;
                    is_symlink = entry.is_symlink;
                    type_unknown = entry.type_unknown;
                    has_entry = true;
                }
            }

            // If no more entries, cleanup and pop
            if (!has_entry)
            {
                frame_cleanup(f);
                st.count--;
                continue;
            }

            if (type_unknown)
            {
                size_t saved = path_len;
                if (buf_append(path_buf, &path_len, name) > 0)
                {
                    char *rp = get_real_path(real_path_buf, base_path, path_buf);
                    struct stat sb;
                    if (lstat(rp, &sb) == 0)
                    {
                        is_dir = S_ISDIR(sb.st_mode);
                        is_symlink = S_ISLNK(sb.st_mode);
                    }
                }
                path_len = saved;
                path_buf[path_len] = '\0';
            }

            bool is_dot = (name[0] == '.' && (name[1] == '\0'));
            bool is_dotdot = (name[0] == '.' && name[1] == '.' && name[2] == '\0');
            int dotfile = 0;
            if (name[0] == '.') {
                dotfile++;
                if (is_dot) {
                    dotfile++;
                }
            }

            // MRI: Always skip ".." to prevent infinite recursion
            if (is_dotdot)
                continue;

            // MRI: Handle "." (current directory)
            if (is_dot) {
                // Skip if recursive && !DOTMATCH (prevent infinite recursion)
                if (seg->type == RBC_SEGMENT_RECURSIVE && !(ctx.flags & RBC_FNM_DOTMATCH))
                    continue;
                // Skip if skipdot is set (subdirectory iteration)
                if (f->skipdot)
                    continue;
                dotfile = 2; // Mark as "." for later checks
            }

            // MRI: If we arrived here via a recursive "nothing" match, skip "."

            if (seg->type == RBC_SEGMENT_RECURSIVE)
            {
               // Allow fallthrough to buf_append to check for explicit match in next segment
            }
            else // WILDCARD
            {
                bool dotmatch = (ctx.flags & RBC_FNM_DOTMATCH) != 0;
                bool dotglob = (seg->data.glob.original_pattern && seg->data.glob.original_pattern[0] == '.');

                if (name[0] == '.')
                {
                    // If it's a hidden dot file, but not matched by DOTMATCH or explicit dot
                    if (!dotmatch && !dotglob)
                        continue;

                    // If it's a special dot entry (. or ..)
                    if (is_dot)
                    {
                        // MRI: . is matched by a wildcard only if (dotglob OR dotmatch)
                        // AND there were no preceding wildcards (from_wildcard is false)
                        // to avoid infinite loops (e.g. */* Matching ./* -> ././a)
                        // UNLESS DOTMATCH is explicitly set to allow dot matching.
                        if (from_wildcard)
                        {
                            continue;
                        }
                    }
                }
            }

            size_t saved_len = path_len;
            if (buf_append(path_buf, &path_len, name) > 0)
            {
                // Note: is_dot and is_dotdot already defined above - do not redefine
                bool is_hidden = (name[0] == '.');

                if (seg->type == RBC_SEGMENT_WILDCARD)
                {
                    if (is_dotdot) // Handled above, but belt and suspenders
                    {
                        path_len = saved_len;
                        path_buf[path_len] = '\0';
                        continue;
                    }

                    if (is_hidden)
                    {
                        bool visible = (ctx.flags & RBC_FNM_DOTMATCH);
                        // Explicit dot check: if pattern starts with '.', it matches hidden
                        if (!visible && seg->data.glob.original_pattern && seg->data.glob.original_pattern[0] == '.')
                        {
                            visible = true;
                        }
                        if (!visible)
                        {
                            path_len = saved_len;
                            path_buf[path_len] = '\0';
                            continue;
                        }
                    }

                    if (is_dot)
                    {
                        // MRI Rule: '.*' matches '.', but '*' matches '.' only if DOTMATCH.
                        // AND: A wildcard matches '.' only if no preceding wildcard matches '.' in the immediate chain
                        // to avoid infinite loops (e.g. */* Matching ./* -> ././a)
                        // UNLESS DOTMATCH is explicitly set to allow dot matching.
                        if (from_wildcard && !(ctx.flags & RBC_FNM_DOTMATCH))
                        {
                            path_len = saved_len;
                            path_buf[path_len] = '\0';
                            continue;
                        }
                    }

                    if (rbc_matcher_exec(&seg->data.glob.matcher, name))
                    {
                        push_next(&st, path_buf, path_len, seg, stack_ptr, &ctx, true, false);
                        // Don't break - continue to process this frame and move to next entry
                    }
                }
                else if (seg->type == RBC_SEGMENT_RECURSIVE)
                {
                    bool recurse_allowed = true;
                    if (name[0] == '.' && !(ctx.flags & RBC_FNM_DOTMATCH))
                        recurse_allowed = false;

                    // Logic: Match Zero (Current Entry) AND Recurse (Current Entry)
                    // Push Recurse (Bottom), then MatchZero (Top) to ensure MatchZero runs first (Parent First).

                    // 1. RECURSE (Enter directory)
                    // We must push the CURRENT segment ('**') to continue recursing inside the subdirectory.
                    // MRI: Check dotfile condition before recursing
                    if (recurse_allowed && is_dir && !is_symlink && !is_dot)
                    {
                        // MRI: For recursive patterns, check dotfile condition
                        // dotfile < 2: not \".\", dotfile < 1: not any dotfile
                        // DOTMATCH: allow dotfiles but not \".\", !DOTMATCH: no dotfiles
                        bool allow_recursion = false;
                        if (ctx.flags & RBC_FNM_DOTMATCH) {
                            allow_recursion = (dotfile < 2); // Allow dotfiles but not \".\"
                        } else {
                            allow_recursion = (dotfile < 1); // No dotfiles at all
                        }
                        
                        if (allow_recursion) {
                            // We are recursing from a wildcard (**), so from_wildcard must be true
                            stack_push(&st, seg, stack_ptr, true, true, path_buf, path_len, &ctx);
                        }
                    }

                    // 2. MATCH ZERO (Check if entry matches REST of pattern)
                    bool matched = false;
                    bool push_current_next = false;
                    bool next_matches_empty = false;

                    if (!seg->next)
                    {
                        // ** at end. Matches everything.
                        matched = true;
                        next_matches_empty = true;
                    }
                    else if (seg->next->type == RBC_SEGMENT_LITERAL && seg->next->data.literal[0] == '\0')
                    {
                        // ** followed by empty literal (trailing slash).
                        // Matches directories.
                        if (is_dir)
                        {
                            matched = true;
                            push_current_next = true;
                        }
                        // Even if not dir, we treat it as "matches empty" pattern for duplication check logic?
                        // If it's a specific requirement (Trailing Slash), it implies Directory.
                        next_matches_empty = true;
                    }
                    else if (seg->next->type == RBC_SEGMENT_BRANCH)
                    {
                        // Handle Branching in MatchZero
                        if (!(is_dot && path_len > 0)) {
                            rbc_segment_t *alt = seg->next->data.branch.head;
                            while (alt)
                            {
                                if (segment_match(alt, name, &ctx))
                                {
                                    segment_stack_t *cont = NULL;
                                    if (seg->next->next || stack_ptr)
                                    {
                                        cont = rbc_arena_alloc(ctx.arena, sizeof(segment_stack_t));
                                        cont->seg = seg->next->next;
                                        cont->next = stack_ptr;
                                    }
                                    push_next(&st, path_buf, path_len, alt, cont, &ctx, from_wildcard, false);
                                }
                                alt = alt->next_alt;
                            }
                        }
                    }
                    else if (segment_match(seg->next, name, &ctx))
                    {
                        // Normal match (Literal 'name' or Wildcard matching 'name').
                        matched = true;

                        if (is_dot)
                        {
                            // MRI: Skip . in subdirectories to avoid redundancy/duplicates  
                            // For patterns like **/.*  the . should match at root only
                            // For patterns like **/?  the . should match at root only
                            // Use skipdot flag to determine if we're in a recursive call
                            if (f->skipdot)
                                matched = false;
                        }
                    }

                    // Duplication Prevention:
                    // If is_dir AND next_matches_empty (e.g. `**` or `**/`).
                    // The Recursion Step (1) + ST_INIT inside it will handle the match properly (Self Match).
                    // If we also match here, we produce a duplicate.
                    // So we SKIP match here.
                    // EXCEPT if not is_dir?
                    // If not is_dir. Step 1 didn't recurse. ST_INIT won't run.
                    // So we MUST match here.
                    // So condition: if (is_dir && next_matches_empty) SKIP.

                    if (matched)
                    {
                        // MRI: Avoid duplicates when is_dir && next_matches_empty
                        // The recursive step will handle directory matches
                        bool skip_for_recursion = false;
                        if (is_dir && next_matches_empty) {
                            skip_for_recursion = true;
                        }
                        
                        if (!skip_for_recursion)
                        {
                            if (push_current_next)
                            {
                                stack_push(&st, seg->next, stack_ptr, from_wildcard, false, path_buf, path_len, &ctx);
                            }
                            else
                            {
                                stack_push(&st, seg->next ? seg->next->next : NULL, stack_ptr, from_wildcard, false, path_buf, path_len, &ctx);
                            }
                        }
                    }

                    // Break to refresh 'f'
                    break;
                }
            }
            path_len = saved_len;
            path_buf[path_len] = '\0';
            // Continue to next entry in this directory
            continue;
        }
        break;
        }
    }

    if (st.items)
        free(st.items);
}

static bool rbc_walker_dispatch_literal(const char *p, rbc_walker_ctx_t *ctx)
{
    char full_path[PATH_MAX];
    int needed;
    const char *base = ctx->base;

    if (base && strcmp(base, ".") != 0)
    {
        needed = snprintf(full_path, sizeof(full_path), "%s/%s", base, p);
    }
    else
    {
        needed = snprintf(full_path, sizeof(full_path), "%s", p);
    }

    if (needed < 0 || (size_t)needed >= sizeof(full_path))
    {
        return true; // Too long, continue
    }

    struct stat st;
    if (stat(full_path, &st) == 0)
    {
        size_t plen = strlen(p);
        if (plen > 0 && p[plen - 1] == '/')
        {
            if (!S_ISDIR(st.st_mode))
                return true;
        }
        if (!rbc_glob_results_add(ctx->results, p))
            return false;
    }
    return true;
}

static bool rbc_walker_dispatch_segments(const char *p, rbc_walker_ctx_t *ctx)
{
    rbc_segment_t *segments = rbc_glob_segment_compile(&ctx->ctx->arena, p, ctx->flags);
    if (segments)
    {
        rbc_segment_exec(segments, ctx->base, ctx->flags, ctx->sort, rbc_walker_match_callback, ctx, &ctx->ctx->arena);
    }
    return true;
}

static bool rbc_walker_dispatch_visitor(const char *p, void *arg)
{
    rbc_walker_ctx_t *ctx = (rbc_walker_ctx_t *)arg;
    if (strpbrk(p, "*?[]\\") == NULL)
    {
        return rbc_walker_dispatch_literal(p, ctx);
    }
    return rbc_walker_dispatch_segments(p, ctx);
}

bool rbc_walker_run(const char *pattern, rbc_walker_ctx_t *ctx)
{
    if (!pattern)
    {
        return false;
    }

    // Fast-path 1: Pure literal
    if (strpbrk(pattern, "*?[]\\{}") == NULL)
    {
        return rbc_walker_dispatch_literal(pattern, ctx);
    }

    // Fast-path 2: Pure braces (no wildcards)
    if (strchr(pattern, '{') != NULL && strpbrk(pattern, "*?[]\\") == NULL)
    {
        return rbc_brace_visit(pattern, &ctx->ctx->arena, rbc_walker_dispatch_visitor, ctx);
    }

    // Default: General case (handles general wildcards and nested braces)
    return rbc_walker_dispatch_segments(pattern, ctx);
}

bool rbc_walker_run_compiled(const rbc_glob_pattern_t *cg, rbc_walker_ctx_t *ctx)
{
    if (!cg)
    {
        return false;
    }

    switch (cg->type)
    {
    case RBC_PATTERN_LITERAL:
        return rbc_walker_dispatch_literal(cg->original_pattern, ctx);
    case RBC_PATTERN_BRACE_LITERAL:
        return rbc_brace_visit(cg->original_pattern, &ctx->ctx->arena, rbc_walker_dispatch_visitor, ctx);
    case RBC_PATTERN_GENERAL:
        // Use the pre-compiled segments
        rbc_segment_exec(cg->segments, ctx->base, cg->flags, ctx->sort, rbc_walker_match_callback, ctx, &ctx->ctx->arena);
        return true;
    }
    return false;
}
