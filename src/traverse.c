#include <rbcglob/internal/traverse.h>
#include <rbcglob/internal/dir.h>
#include <rbcglob/internal/file.h>
#include <rbcglob/internal/arena.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

int rbcglob_compare_paths(const char *s1_in, const char *s2_in)
{
    /* P12 Optimization: Inline fast path for common cases */
    const unsigned char *s1 = (const unsigned char *)s1_in;
    const unsigned char *s2 = (const unsigned char *)s2_in;

    if (!s1_in || !s2_in)
        return (s1_in == s2_in) ? 0 : (s1_in ? 1 : -1);

    /* Fast path: check first characters */
    if (*s1 != *s2)
        return (int)*s1 - (int)*s2;
    if (*s1 == '\0')
        return 0;

    /* Fall back to strcmp for rest */
    return strcmp(s1_in, s2_in);
}

/* P5: djb2 hash function */
static unsigned long rbcglob_traverse_hash_string(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash % RBCGLOB_HASH_TABLE_SIZE;
}

void rbcglob_ctx_init(rbcglob_ctx_t *ctx)
{
    rbcglob_arena_init(&ctx->arena, 0); /* Use default block size (128KB) */
    ctx->dir_cache = NULL;
    ctx->dir_cache_count = 0;
    for (size_t i = 0; i < RBCGLOB_HASH_TABLE_SIZE; i++)
    {
        ctx->cache_hash[i] = NULL;
    }
    ctx->discovery_counter = 0;
}

void rbcglob_results_clear_cache(rbcglob_ctx_t *ctx)
{
    if (!ctx->dir_cache)
        return;

    /* P13: Free non-arena memory (entries/d_types arrays and cache structure) */
    for (size_t i = 0; i < ctx->dir_cache_count; i++)
    {
        free(ctx->dir_cache[i].entries);
        free(ctx->dir_cache[i].entry_lens);
        free(ctx->dir_cache[i].d_types);
    }
    free(ctx->dir_cache);
    ctx->dir_cache = NULL;
    ctx->dir_cache_count = 0;

    /* P5: Clear hash table (structure only, strings are in arena) */
    for (size_t i = 0; i < RBCGLOB_HASH_TABLE_SIZE; i++)
    {
        ctx->cache_hash[i] = NULL;
    }

    /* P13: Re-initialize arena (frees all strings at once) */
    rbcglob_arena_destroy(&ctx->arena);
    rbcglob_arena_init(&ctx->arena, 0);
}

void rbcglob_ctx_free(rbcglob_ctx_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->dir_cache)
    {
        for (size_t i = 0; i < ctx->dir_cache_count; i++)
        {
            free(ctx->dir_cache[i].entries);
            free(ctx->dir_cache[i].entry_lens);
            free(ctx->dir_cache[i].d_types);
        }
        free(ctx->dir_cache);
    }
    rbcglob_arena_destroy(&ctx->arena);
}

static ssize_t rbcglob_traverse_get_cached_dir_index(rbcglob_ctx_t *ctx, const char *path)
{
    /* P5 Optimization: Hash table lookup O(1) instead of linear search O(n) */
    unsigned long h = rbcglob_traverse_hash_string(path);
    rbcglob_cache_hash_entry_t *entry = ctx->cache_hash[h];

    while (entry)
    {
        if (strcmp(entry->key, path) == 0)
            return (ssize_t)entry->cache_index;
        entry = entry->next;
    }

    /* Cache miss - read directory and add to cache */
    DIR *dir = opendir(path);
    if (!dir)
        return -1;

    size_t idx = ctx->dir_cache_count++;
    rbcglob_dir_cache_node_t *new_cache = realloc(ctx->dir_cache, sizeof(rbcglob_dir_cache_node_t) * ctx->dir_cache_count);
    if (!new_cache)
    {
        closedir(dir);
        ctx->dir_cache_count--;
        return -1;
    }
    ctx->dir_cache = new_cache;

    /* P13: Use arena for path string */
    ctx->dir_cache[idx].path = rbcglob_arena_strdup(&ctx->arena, path);
    ctx->dir_cache[idx].entries = NULL;
    ctx->dir_cache[idx].entry_lens = NULL;
    ctx->dir_cache[idx].d_types = NULL;
    ctx->dir_cache[idx].count = 0;

    /* P4 Optimization: Pre-allocate space for entries */
    size_t entries_capacity = 64; /* Initial capacity */
    ctx->dir_cache[idx].entries = malloc(sizeof(char *) * entries_capacity);
    ctx->dir_cache[idx].entry_lens = malloc(sizeof(size_t) * entries_capacity);
    ctx->dir_cache[idx].d_types = malloc(sizeof(unsigned char) * entries_capacity);
    if (!ctx->dir_cache[idx].entries || !ctx->dir_cache[idx].entry_lens || !ctx->dir_cache[idx].d_types)
    {
        free(ctx->dir_cache[idx].entries);
        free(ctx->dir_cache[idx].entry_lens);
        free(ctx->dir_cache[idx].d_types);
        closedir(dir);
        ctx->dir_cache_count--;
        return -1;
    }

    struct dirent *entry_ptr;
    while ((entry_ptr = readdir(dir)) != NULL)
    {
        /* P7 Optimization: Skip "." and ".." early */
        const char *name = entry_ptr->d_name;
        if (name[0] == '.')
        {
            if (name[1] == '\0')
                continue; /* "." */
            if (name[1] == '.' && name[2] == '\0')
                continue; /* ".." */
        }

        /* P4: Grow capacity when needed */
        if (ctx->dir_cache[idx].count >= entries_capacity)
        {
            entries_capacity *= 2;
            char **new_entries = realloc(ctx->dir_cache[idx].entries, sizeof(char *) * entries_capacity);
            size_t *new_lens = realloc(ctx->dir_cache[idx].entry_lens, sizeof(size_t) * entries_capacity);
            unsigned char *new_types = realloc(ctx->dir_cache[idx].d_types, sizeof(unsigned char) * entries_capacity);
            if (!new_entries || !new_lens || !new_types)
            {
                if (new_entries)
                    ctx->dir_cache[idx].entries = new_entries;
                if (new_lens)
                    ctx->dir_cache[idx].entry_lens = new_lens;
                if (new_types)
                    ctx->dir_cache[idx].d_types = new_types;
                break;
            }
            ctx->dir_cache[idx].entries = new_entries;
            ctx->dir_cache[idx].entry_lens = new_lens;
            ctx->dir_cache[idx].d_types = new_types;
        }

        /* P13: Use arena for entry names */
        size_t name_len = strlen(name);
        ctx->dir_cache[idx].entries[ctx->dir_cache[idx].count] = rbcglob_arena_alloc(&ctx->arena, name_len + 1);
        memcpy(ctx->dir_cache[idx].entries[ctx->dir_cache[idx].count], name, name_len + 1);
        ctx->dir_cache[idx].entry_lens[ctx->dir_cache[idx].count] = name_len;
        ctx->dir_cache[idx].d_types[ctx->dir_cache[idx].count] = entry_ptr->d_type;
        ctx->dir_cache[idx].count++;
    }
    closedir(dir);

    /* P5/P13: Add to hash table using arena memory */
    rbcglob_cache_hash_entry_t *new_entry = rbcglob_arena_alloc(&ctx->arena, sizeof(rbcglob_cache_hash_entry_t));
    if (new_entry)
    {
        new_entry->key = rbcglob_arena_strdup(&ctx->arena, path);
        new_entry->cache_index = idx;
        new_entry->next = ctx->cache_hash[h];
        ctx->cache_hash[h] = new_entry;
    }

    return (ssize_t)idx;
}

static int rbcglob_traverse_execute_step(rbcglob_ctx_t *ctx, rbcglob_compiled_pattern_t *cp, size_t seg_idx, const char *rel_path, const char *search_base, bool is_after_wildcard, rbcglob_results_t *results)
{
    if (seg_idx >= cp->count)
    {
        return rbcglob_results_add(results, rel_path ? rel_path : ".");
    }

    rbcglob_segment_t *seg = &cp->segments[seg_idx];

    if (seg->type == RBCGLOB_SEGMENT_RECURSIVE)
    {
        /* Ruby ** matches zero or more directories.
           First, try skipping ** and moving to next instruction. */
        int ret = rbcglob_traverse_execute_step(ctx, cp, seg_idx + 1, rel_path, search_base, is_after_wildcard, results);
        if (ret != 0)
            return ret;

        /* Then, descend into directories. */
        char *full_dir_to_open = rbcglob_join_arena((const char *[]){search_base, rel_path}, 2, &ctx->arena);
        const char *dir_to_open = full_dir_to_open ? full_dir_to_open : (search_base ? search_base : ".");

        ssize_t cache_idx = rbcglob_traverse_get_cached_dir_index(ctx, dir_to_open);
        if (cache_idx < 0)
            return 0;

        for (size_t i = 0; i < ctx->dir_cache[cache_idx].count; i++)
        {
            const char *name = ctx->dir_cache[cache_idx].entries[i];
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            if (name[0] == '.' && !(cp->flags & RBCGLOB_FNM_DOTMATCH))
                continue;

            /* P3 Optimization: Use d_type to avoid stat() when possible */
            unsigned char d_type = ctx->dir_cache[cache_idx].d_types[i];
            bool is_dir = false;

            /* P4 Optimization: Build path once and reuse */
            char *next_rel = NULL;
            char *next_full = NULL;

            if (d_type == DT_DIR)
            {
                is_dir = true;
            }
            else if (d_type == DT_UNKNOWN || d_type == DT_LNK)
            {
                /* Fall back to stat() only for unknown types or symlinks */
                next_rel = rbcglob_join_arena((const char *[]){rel_path, name}, 2, &ctx->arena);
                if (!next_rel)
                    return -1;
                next_full = rbcglob_join_arena((const char *[]){search_base, next_rel}, 2, &ctx->arena);
                struct stat st;
                if (next_full && lstat(next_full, &st) == 0 && S_ISDIR(st.st_mode))
                {
                    is_dir = true;
                }
            }

            if (is_dir)
            {
                /* Reuse next_rel if already built */
                if (!next_rel)
                {
                    next_rel = rbcglob_join_arena((const char *[]){rel_path, name}, 2, &ctx->arena);
                    if (!next_rel)
                        return -1;
                }
                /* Stay on RBCGLOB_SEGMENT_RECURSIVE to find deeper matches.
                   Next instruction will still see is_after_wildcard=true because we moved through RBCGLOB_SEGMENT_RECURSIVE. */
                ret = rbcglob_traverse_execute_step(ctx, cp, seg_idx, next_rel, search_base, true, results);
                if (ret != 0)
                    return ret;
            }
        }
        return 0;
    }

    if (seg->type == RBCGLOB_SEGMENT_LITERAL)
    {
        char *next_rel = rbcglob_join_arena((const char *[]){rel_path, seg->pattern}, 2, &ctx->arena);
        if (!next_rel)
            return -1;
        char *next_full = rbcglob_join_arena((const char *[]){search_base, next_rel}, 2, &ctx->arena);
        struct stat st;
        if (next_full && stat(next_full, &st) == 0)
        {
            /* Check if this segment must be a directory */
            bool must_be_directory = (seg_idx + 1 < cp->count) || cp->has_trailing_slash;
            if (must_be_directory && !S_ISDIR(st.st_mode))
            {
                return 0;
            }
            /* Literals don't trigger is_after_wildcard, but they propagate it */
            int ret = rbcglob_traverse_execute_step(ctx, cp, seg_idx + 1, next_rel, search_base, is_after_wildcard, results);
            return ret;
        }
        return 0;
    }

    /* Wildcard match */
    char *full_dir_to_open = rbcglob_join_arena((const char *[]){search_base, rel_path}, 2, &ctx->arena);
    const char *dir_to_open = full_dir_to_open ? full_dir_to_open : (search_base ? search_base : ".");
    ssize_t cache_idx = rbcglob_traverse_get_cached_dir_index(ctx, dir_to_open);
    if (cache_idx < 0)
        return 0;

    for (size_t i = 0; i < ctx->dir_cache[cache_idx].count; i++)
    {
        const char *name = ctx->dir_cache[cache_idx].entries[i];

        /* Ruby parity: if we relate to a wildcard-matched directory,
           wildcard segments in the current level should not match "." or ".." */
        if (is_after_wildcard && (strcmp(name, ".") == 0 || strcmp(name, "..") == 0))
        {
            continue;
        }

        if (strcmp(name, "..") == 0)
            continue;

        /* P0 Optimization: Skip hidden files/directories early if not explicitly matching */
        if (name[0] == '.' && !(cp->flags & RBCGLOB_FNM_DOTMATCH))
        {
            /* Check if segment explicitly starts with '.' */
            bool explicit_dot = false;
            if (seg->type == RBCGLOB_SEGMENT_LITERAL && seg->pattern && seg->pattern[0] == '.')
            {
                explicit_dot = true;
            }
            else if (seg->token_count > 0 && seg->tokens[0].token_type == RBCGLOB_TOKEN_CHAR && seg->tokens[0].c == '.')
            {
                explicit_dot = true;
            }
            if (!explicit_dot)
            {
                continue;
            }
        }

        if (seg->prefix && strncmp(name, seg->prefix, seg->prefix_len) != 0)
            continue;

        if (seg->suffix)
        {
            size_t name_len = ctx->dir_cache[cache_idx].entry_lens[i];
            if (name_len < seg->suffix_len || strcmp(name + name_len - seg->suffix_len, seg->suffix) != 0)
            {
                continue;
            }
        }

        if (rbcglob_token_match_segment(seg, name, cp->flags))
        {
            char *next_rel = rbcglob_join_arena((const char *[]){rel_path, name}, 2, &ctx->arena);
            if (!next_rel)
                return -1;

            /* Check if this segment must be a directory */
            bool must_be_directory = (seg_idx + 1 < cp->count) || cp->has_trailing_slash;
            if (must_be_directory)
            {
                /* P3 Optimization: Use d_type first, fall back to stat() if needed */
                unsigned char d_type = ctx->dir_cache[cache_idx].d_types[i];
                bool is_dir = false;

                if (d_type == DT_DIR)
                {
                    is_dir = true;
                }
                else if (d_type == DT_UNKNOWN || d_type == DT_LNK)
                {
                    /* Fall back to stat() for unknown types or symlinks */
                    char *next_full = rbcglob_join_arena((const char *[]){search_base, next_rel}, 2, &ctx->arena);
                    struct stat st;
                    if (next_full && stat(next_full, &st) == 0 && S_ISDIR(st.st_mode))
                    {
                        is_dir = true;
                    }
                }

                if (!is_dir)
                {
                    continue;
                }
            }
            int ret = rbcglob_traverse_execute_step(ctx, cp, seg_idx + 1, next_rel, search_base, true, results);
            if (ret != 0)
                return ret;
        }
    }
    return 0;
}

int rbcglob_execute(rbcglob_ctx_t *ctx, rbcglob_compiled_pattern_t *cp, const char *base, rbcglob_results_t *results)
{
    if (!cp || !ctx)
        return -1;

    const char *search_base = (base && base[0] != '\0') ? base : NULL;
    if (cp->is_absolute)
        search_base = "/";

    /* P2 Optimization: Skip directly to first wildcard segment if pattern starts with literals */
    if (cp->leading_literal_count > 0)
    {
        /* Build path from leading literal segments */
        char *literal_path = NULL;
        for (size_t i = 0; i < cp->leading_literal_count; i++)
        {
            char *next_path = rbcglob_join_arena((const char *[]){literal_path ? literal_path : (search_base ? search_base : "."), cp->segments[i].pattern}, 2, &ctx->arena);
            literal_path = next_path;
            if (!literal_path)
                return -1;
        }

        /* Check if this is the final segment (complete pattern is all literals) */
        bool is_final = (cp->leading_literal_count >= cp->count);

        /* Verify the literal path exists */
        struct stat st;
        if (stat(literal_path, &st) != 0)
        {
            return 0; /* Path doesn't exist */
        }

        /* If this is the final segment and it's a file (or we're matching directories too), add it */
        if (is_final)
        {
            /* Check trailing slash requirement */
            if (cp->has_trailing_slash && !S_ISDIR(st.st_mode))
            {
                return 0;
            }

            /* Extract relative path for results */
            const char *rel_start = literal_path;
            if (search_base && strncmp(literal_path, search_base, strlen(search_base)) == 0)
            {
                rel_start = literal_path + strlen(search_base);
                while (*rel_start == '/')
                    rel_start++;
            }
            else if (!search_base || search_base[0] == '\0')
            {
                if (literal_path[0] == '.' && literal_path[1] == '/')
                {
                    rel_start = literal_path + 2;
                }
            }

            int ret = rbcglob_results_add(results, rel_start);
            return ret;
        }

        /* Not final segment - must be a directory to continue */
        if (!S_ISDIR(st.st_mode))
        {
            return 0;
        }

        /* Extract the relative path portion for results */
        const char *rel_start = literal_path;
        if (search_base && strncmp(literal_path, search_base, strlen(search_base)) == 0)
        {
            rel_start = literal_path + strlen(search_base);
            while (*rel_start == '/')
                rel_start++;
        }
        else if (!search_base || search_base[0] == '\0')
        {
            if (literal_path[0] == '.' && literal_path[1] == '/')
            {
                rel_start = literal_path + 2;
            }
        }

        /* Continue from first wildcard segment */
        int ret = rbcglob_traverse_execute_step(ctx, cp, cp->leading_literal_count, rel_start, search_base, false, results);
        return ret;
    }

    return rbcglob_traverse_execute_step(ctx, cp, 0, NULL, search_base, false, results);
}

void rbcglob_results_reset_discovery_counter(rbcglob_ctx_t *ctx) { ctx->discovery_counter = 0; }

/* P1 Optimization: Initial capacity for result array */
#define INITIAL_RESULT_CAPACITY 64

void rbcglob_results_init(rbcglob_results_t *results, rbcglob_ctx_t *ctx)
{
    /* P1-1: Pre-allocate result array to reduce realloc() calls */
    results->capacity = INITIAL_RESULT_CAPACITY;
    results->items = malloc(sizeof(char *) * results->capacity);
    results->lengths = malloc(sizeof(size_t) * results->capacity);
    results->discovery_indices = malloc(sizeof(size_t) * results->capacity);
    results->count = 0;
    results->ctx = ctx;

    /* Handle allocation failure gracefully */
    if (!results->items || !results->lengths || !results->discovery_indices)
    {
        free(results->items);
        free(results->lengths);
        free(results->discovery_indices);
        results->items = NULL;
        results->lengths = NULL;
        results->discovery_indices = NULL;
        results->capacity = 0;
    }
}

void rbcglob_results_clear(rbcglob_results_t *results)
{
    if (!results)
        return;
    /* P13: Strings are in arena, no need to free individually */
    /* Only free the arrays themselves */
    free(results->items);
    free(results->lengths);
    free(results->discovery_indices);
    results->items = NULL;
    results->lengths = NULL;
    results->discovery_indices = NULL;
    results->count = 0;
    results->capacity = 0;
}

int rbcglob_results_add_with_index(rbcglob_results_t *results, const char *path, size_t index)
{
    if (results->count >= results->capacity)
    {
        size_t new_cap = results->capacity ? results->capacity * 2 : 16;
        char **new_items = realloc(results->items, sizeof(char *) * new_cap);
        if (!new_items)
            return -1;
        results->items = new_items;
        size_t *new_lens = realloc(results->lengths, sizeof(size_t) * new_cap);
        if (!new_lens)
            return -1;
        results->lengths = new_lens;
        size_t *new_indices = realloc(results->discovery_indices, sizeof(size_t) * new_cap);
        if (!new_indices)
            return -1;
        results->discovery_indices = new_indices;
        results->capacity = new_cap;
    }
    /* P13: Use arena for result strings */
    const char *p = path ? path : ".";
    size_t len = strlen(p);
    results->items[results->count] = rbcglob_arena_alloc(&results->ctx->arena, len + 1);
    memcpy(results->items[results->count], p, len + 1);
    results->lengths[results->count] = len;
    results->discovery_indices[results->count] = index;
    results->count++;
    return 0;
}

int rbcglob_results_add(rbcglob_results_t *results, const char *path)
{
    return rbcglob_results_add_with_index(results, path, results->ctx->discovery_counter++);
}

/* P10 Optimization: Helper structure for qsort() */
typedef struct rbcglob_traverse_sort_pair_s
{
    char *path;
    size_t length;
    size_t discovery_index;
} rbcglob_traverse_sort_pair_t;

/* P10: Comparison function for qsort() */
static int rbcglob_traverse_compare_sort_pairs(const void *a, const void *b)
{
    const rbcglob_traverse_sort_pair_t *pa = (const rbcglob_traverse_sort_pair_t *)a;
    const rbcglob_traverse_sort_pair_t *pb = (const rbcglob_traverse_sort_pair_t *)b;
    return rbcglob_compare_paths(pa->path, pb->path);
}

void rbcglob_results_sort(rbcglob_results_t *results)
{
    if (results->count <= 1)
        return;

    /* P10: Use qsort() instead of O(n²) bubble sort */
    rbcglob_traverse_sort_pair_t *pairs = malloc(sizeof(rbcglob_traverse_sort_pair_t) * results->count);
    if (!pairs)
        return; /* Fallback: keep unsorted */

    for (size_t i = 0; i < results->count; i++)
    {
        pairs[i].path = results->items[i];
        pairs[i].length = results->lengths[i];
        pairs[i].discovery_index = results->discovery_indices[i];
    }

    qsort(pairs, results->count, sizeof(rbcglob_traverse_sort_pair_t), rbcglob_traverse_compare_sort_pairs);

    for (size_t i = 0; i < results->count; i++)
    {
        results->items[i] = pairs[i].path;
        results->lengths[i] = pairs[i].length;
        results->discovery_indices[i] = pairs[i].discovery_index;
    }

    free(pairs);
}

void rbcglob_results_deduplicate(rbcglob_results_t *results)
{
    if (results->count <= 1)
        return;

    /* P11 Optimization: O(n) deduplication for sorted array
     * Only compare adjacent elements instead of O(n²) full scan */
    size_t write_idx = 1;
    for (size_t read_idx = 1; read_idx < results->count; read_idx++)
    {
        /* Compare only with previous element (array is sorted) */
        if (strcmp(results->items[read_idx], results->items[write_idx - 1]) != 0)
        {
            /* Different from previous - keep it */
            if (write_idx != read_idx)
            {
                results->items[write_idx] = results->items[read_idx];
                results->lengths[write_idx] = results->lengths[read_idx];
                results->discovery_indices[write_idx] = results->discovery_indices[read_idx];
            }
            write_idx++;
        }
    }
    results->count = write_idx;
}

int rbcglob_compare_filesystem_order(rbcglob_ctx_t *ctx, const char *a, const char *b)
{
    (void)ctx;
    return strcmp(a, b);
}
