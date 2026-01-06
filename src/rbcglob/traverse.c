#include <rbcglob/internal/traverse.h>
#include <rbcglob/internal/utils.h>
#include <rbcglob/internal/arena.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

/* P13 Optimization: Arena allocator for fast memory management */
static arena_t g_arena;
static bool g_arena_initialized = false;

/* P5 Optimization: Hash table for directory cache lookup */
#define HASH_TABLE_SIZE 1024

/* Simple Directory Cache */
typedef struct
{
    char *path;
    char **entries;
    size_t *entry_lens;     /* P18: Pre-calculate lengths to avoid strlen() */
    unsigned char *d_types; /* P3: d_type from dirent to avoid stat() */
    size_t count;
} dir_cache_node_t;

/* P5: Hash table entry for O(1) cache lookup */
typedef struct cache_hash_entry
{
    char *key;
    size_t cache_index;
    struct cache_hash_entry *next; /* Chaining for collision resolution */
} cache_hash_entry_t;

static dir_cache_node_t *g_dir_cache = NULL;
static size_t g_dir_cache_count = 0;
static cache_hash_entry_t *g_cache_hash[HASH_TABLE_SIZE] = {NULL}; /* P5: Hash table */

/* P5: djb2 hash function */
static unsigned long hash_string(const char *str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash % HASH_TABLE_SIZE;
}

void glob_results_clear_cache(void)
{
    if (!g_dir_cache)
        return;

    /* P13: Free non-arena memory (entries/d_types arrays and cache structure) */
    for (size_t i = 0; i < g_dir_cache_count; i++)
    {
        free(g_dir_cache[i].entries);
        free(g_dir_cache[i].entry_lens);
        free(g_dir_cache[i].d_types);
    }
    free(g_dir_cache);
    g_dir_cache = NULL;
    g_dir_cache_count = 0;

    /* P5: Clear hash table (structure only, strings are in arena) */
    for (size_t i = 0; i < HASH_TABLE_SIZE; i++)
    {
        g_cache_hash[i] = NULL;
    }

    /* P13: Destroy arena (frees all strings at once) */
    if (g_arena_initialized)
    {
        arena_destroy(&g_arena);
        g_arena_initialized = false;
    }
}

static ssize_t get_cached_dir_index(const char *path)
{
    /* P13: Initialize arena on first use */
    if (!g_arena_initialized)
    {
        arena_init(&g_arena, 128 * 1024); /* 128KB initial size */
        g_arena_initialized = true;
    }

    /* P5 Optimization: Hash table lookup O(1) instead of linear search O(n) */
    unsigned long h = hash_string(path);
    cache_hash_entry_t *entry = g_cache_hash[h];

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

    size_t idx = g_dir_cache_count++;
    dir_cache_node_t *new_cache = realloc(g_dir_cache, sizeof(dir_cache_node_t) * g_dir_cache_count);
    if (!new_cache)
    {
        closedir(dir);
        g_dir_cache_count--;
        return -1;
    }
    g_dir_cache = new_cache;

    /* P13: Use arena for path string */
    g_dir_cache[idx].path = arena_strdup(&g_arena, path);
    g_dir_cache[idx].entries = NULL;
    g_dir_cache[idx].entry_lens = NULL;
    g_dir_cache[idx].d_types = NULL;
    g_dir_cache[idx].count = 0;

    /* P4 Optimization: Pre-allocate space for entries */
    size_t entries_capacity = 64; /* Initial capacity */
    g_dir_cache[idx].entries = malloc(sizeof(char *) * entries_capacity);
    g_dir_cache[idx].entry_lens = malloc(sizeof(size_t) * entries_capacity);
    g_dir_cache[idx].d_types = malloc(sizeof(unsigned char) * entries_capacity);
    if (!g_dir_cache[idx].entries || !g_dir_cache[idx].entry_lens || !g_dir_cache[idx].d_types)
    {
        free(g_dir_cache[idx].entries);
        free(g_dir_cache[idx].entry_lens);
        free(g_dir_cache[idx].d_types);
        closedir(dir);
        g_dir_cache_count--;
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
        if (g_dir_cache[idx].count >= entries_capacity)
        {
            entries_capacity *= 2;
            char **new_entries = realloc(g_dir_cache[idx].entries, sizeof(char *) * entries_capacity);
            size_t *new_lens = realloc(g_dir_cache[idx].entry_lens, sizeof(size_t) * entries_capacity);
            unsigned char *new_types = realloc(g_dir_cache[idx].d_types, sizeof(unsigned char) * entries_capacity);
            if (!new_entries || !new_lens || !new_types)
            {
                free(new_entries);
                free(new_lens);
                free(new_types);
                break;
            }
            g_dir_cache[idx].entries = new_entries;
            g_dir_cache[idx].entry_lens = new_lens;
            g_dir_cache[idx].d_types = new_types;
        }

        /* P13: Use arena for entry names */
        size_t name_len = strlen(name);
        g_dir_cache[idx].entries[g_dir_cache[idx].count] = arena_alloc(&g_arena, name_len + 1);
        memcpy(g_dir_cache[idx].entries[g_dir_cache[idx].count], name, name_len + 1);
        g_dir_cache[idx].entry_lens[g_dir_cache[idx].count] = name_len;
        g_dir_cache[idx].d_types[g_dir_cache[idx].count] = entry_ptr->d_type;
        g_dir_cache[idx].count++;
    }
    closedir(dir);

    /* P5/P13: Add to hash table using arena memory */
    cache_hash_entry_t *new_entry = arena_alloc(&g_arena, sizeof(cache_hash_entry_t));
    if (new_entry)
    {
        new_entry->key = arena_strdup(&g_arena, path);
        new_entry->cache_index = idx;
        new_entry->next = g_cache_hash[h];
        g_cache_hash[h] = new_entry;
    }

    return (ssize_t)idx;
}

static bool match_tokens(const rbcglob_segment_t *seg, const char *str, unsigned flags);

static bool match_recursive(const rbcglob_token_t *tokens, size_t t_idx, size_t t_count, const char *str, size_t s_idx, unsigned flags)
{
    if (t_idx == t_count)
        return str[s_idx] == '\0';

    const rbcglob_token_t *tok = &tokens[t_idx];
    if (tok->token_type == RBCGLOB_TOKEN_ANY_SEQUENCE)
    {
        /* Optimize: multiple * behave like one */
        while (t_idx + 1 < t_count && tokens[t_idx + 1].token_type == RBCGLOB_TOKEN_ANY_SEQUENCE)
            t_idx++;

        /* Try matching 0 to remaining chars */
        for (size_t i = s_idx; str[i] != '\0'; i++)
        {
            if (match_recursive(tokens, t_idx + 1, t_count, str, i, flags))
                return true;
        }
        return match_recursive(tokens, t_idx + 1, t_count, str, strlen(str), flags);
    }

    if (str[s_idx] == '\0')
        return false;

    char c = str[s_idx];
    switch (tok->token_type)
    {
    case RBCGLOB_TOKEN_CHAR:
    {
        char tc = tok->c;
        if (flags & 4)
        { /* RBCGLOB_FNM_CASEFOLD is 4 */
            if (c >= 'A' && c <= 'Z')
                c += 32;
            if (tc >= 'A' && tc <= 'Z')
                tc += 32;
        }
        if (c != tc)
            return false;
        break;
    }
    case RBCGLOB_TOKEN_ANY_CHAR:
        break;
    case RBCGLOB_TOKEN_ANY_WITHIN:
    case RBCGLOB_TOKEN_ANY_EXCEPT:
    {
        bool found = false;
        char lc = c;
        if (flags & 4 && lc >= 'A' && lc <= 'Z')
            lc += 32;
        for (size_t i = 0; i < tok->range_count; i++)
        {
            char start = tok->ranges[i].start;
            char end = tok->ranges[i].end;
            if (flags & 4)
            {
                if (start >= 'A' && start <= 'Z')
                    start += 32;
                if (end >= 'A' && end <= 'Z')
                    end += 32;
            }
            if (lc >= start && lc <= end)
            {
                found = true;
                break;
            }
        }
        if (tok->token_type == RBCGLOB_TOKEN_ANY_WITHIN && !found)
            return false;
        if (tok->token_type == RBCGLOB_TOKEN_ANY_EXCEPT && found)
            return false;
        break;
    }
    default:
        return false;
    }
    return match_recursive(tokens, t_idx + 1, t_count, str, s_idx + 1, flags);
}

static bool match_tokens(const rbcglob_segment_t *seg, const char *str, unsigned flags)
{
    /* P14 Optimization: Fast path for common "*" case */
    if (seg->token_count == 1 && seg->tokens[0].token_type == RBCGLOB_TOKEN_ANY_SEQUENCE)
    {
        if (str[0] == '.')
        {
            return (flags & RBCGLOB_FNM_DOTMATCH) != 0;
        }
        return true;
    }

    /* P0 Optimization: Fast path for literal segments */
    if (seg->type == RBCGLOB_SEGMENT_LITERAL)
    {
        if (flags & RBCGLOB_FNM_CASEFOLD)
        {
            /* Case-insensitive comparison */
            const char *p = seg->pattern;
            const char *s = str;
            while (*p && *s)
            {
                char pc = *p;
                char sc = *s;
                if (pc >= 'A' && pc <= 'Z')
                    pc += 32;
                if (sc >= 'A' && sc <= 'Z')
                    sc += 32;
                if (pc != sc)
                    return false;
                p++;
                s++;
            }
            return (*p == '\0' && *s == '\0');
        }
        else
        {
            /* Case-sensitive: direct strcmp */
            return strcmp(seg->pattern, str) == 0;
        }
    }

    if (str[0] == '.')
    {
        if (seg->token_count > 0 && seg->tokens[0].token_type == RBCGLOB_TOKEN_CHAR && seg->tokens[0].c == '.')
        {
            /* OK: explicitly matching dot */
        }
        else if (!(flags & RBCGLOB_FNM_DOTMATCH))
        {
            return false;
        }
    }
    return match_recursive(seg->tokens, 0, seg->token_count, str, 0, flags);
}

static int execute_step(rbcglob_compiled_pattern_t *cp, size_t seg_idx, const char *rel_path, const char *search_base, bool is_after_wildcard, glob_results_t *results)
{
    if (seg_idx >= cp->count)
    {
        return glob_results_add(results, rel_path ? rel_path : ".");
    }

    rbcglob_segment_t *seg = &cp->segments[seg_idx];

    if (seg->type == RBCGLOB_SEGMENT_RECURSIVE)
    {
        /* Ruby ** matches zero or more directories.
           First, try skipping ** and moving to next instruction. */
        int ret = execute_step(cp, seg_idx + 1, rel_path, search_base, is_after_wildcard, results);
        if (ret != 0)
            return ret;

        /* Then, descend into directories. */
        char *full_dir_to_open = rbcglob_path_join_arena(&g_arena, search_base, rel_path);
        const char *dir_to_open = full_dir_to_open ? full_dir_to_open : (search_base ? search_base : ".");

        ssize_t cache_idx = get_cached_dir_index(dir_to_open);
        if (cache_idx < 0)
            return 0;

        for (size_t i = 0; i < g_dir_cache[cache_idx].count; i++)
        {
            const char *name = g_dir_cache[cache_idx].entries[i];
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            if (name[0] == '.' && !(cp->flags & 8))
                continue; /* 8 is RBCGLOB_FNM_DOTMATCH */

            /* P3 Optimization: Use d_type to avoid stat() when possible */
            unsigned char d_type = g_dir_cache[cache_idx].d_types[i];
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
                next_rel = rbcglob_path_join_arena(&g_arena, rel_path, name);
                if (!next_rel)
                    return -1;
                next_full = rbcglob_path_join_arena(&g_arena, search_base, next_rel);
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
                    next_rel = rbcglob_path_join_arena(&g_arena, rel_path, name);
                    if (!next_rel)
                        return -1;
                }
                /* Stay on RBCGLOB_SEGMENT_RECURSIVE to find deeper matches.
                   Next instruction will still see is_after_wildcard=true because we moved through RBCGLOB_SEGMENT_RECURSIVE. */
                ret = execute_step(cp, seg_idx, next_rel, search_base, true, results);
                if (ret != 0)
                    return ret;
            }
        }
        return 0;
    }

    if (seg->type == RBCGLOB_SEGMENT_LITERAL)
    {
        char *next_rel = rbcglob_path_join_arena(&g_arena, rel_path, seg->pattern);
        if (!next_rel)
            return -1;
        char *next_full = rbcglob_path_join_arena(&g_arena, search_base, next_rel);
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
            int ret = execute_step(cp, seg_idx + 1, next_rel, search_base, is_after_wildcard, results);
            return ret;
        }
        return 0;
    }

    /* Wildcard match */
    char *full_dir_to_open = rbcglob_path_join_arena(&g_arena, search_base, rel_path);
    const char *dir_to_open = full_dir_to_open ? full_dir_to_open : (search_base ? search_base : ".");
    ssize_t cache_idx = get_cached_dir_index(dir_to_open);
    if (cache_idx < 0)
        return 0;

    for (size_t i = 0; i < g_dir_cache[cache_idx].count; i++)
    {
        const char *name = g_dir_cache[cache_idx].entries[i];

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
            size_t name_len = g_dir_cache[cache_idx].entry_lens[i];
            if (name_len < seg->suffix_len || strcmp(name + name_len - seg->suffix_len, seg->suffix) != 0)
            {
                continue;
            }
        }

        if (match_tokens(seg, name, cp->flags))
        {
            char *next_rel = rbcglob_path_join_arena(&g_arena, rel_path, name);
            if (!next_rel)
                return -1;

            /* Check if this segment must be a directory */
            bool must_be_directory = (seg_idx + 1 < cp->count) || cp->has_trailing_slash;
            if (must_be_directory)
            {
                /* P3 Optimization: Use d_type first, fall back to stat() if needed */
                unsigned char d_type = g_dir_cache[cache_idx].d_types[i];
                bool is_dir = false;

                if (d_type == DT_DIR)
                {
                    is_dir = true;
                }
                else if (d_type == DT_UNKNOWN || d_type == DT_LNK)
                {
                    /* Fall back to stat() for unknown types or symlinks */
                    char *next_full = rbcglob_path_join_arena(&g_arena, search_base, next_rel);
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
            int ret = execute_step(cp, seg_idx + 1, next_rel, search_base, true, results);
            if (ret != 0)
                return ret;
        }
    }
    return 0;
}

int rbcglob_execute(rbcglob_compiled_pattern_t *cp, const char *base, glob_results_t *results)
{
    if (!cp)
        return -1;

    /* P13: Initialize arena at the start of execution if not already done.
     * This is needed because path joining and result adding use the arena. */
    if (!g_arena_initialized)
    {
        arena_init(&g_arena, 128 * 1024); /* 128KB initial size */
        g_arena_initialized = true;
    }

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
            char *next_path = rbcglob_path_join_arena(&g_arena,
                                                      literal_path ? literal_path : (search_base ? search_base : "."),
                                                      cp->segments[i].pattern);
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
            /* DEBUG: printf("Stat failed for: %s (base: %s)\n", literal_path, search_base ? search_base : "NULL"); */
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

            int ret = glob_results_add(results, rel_start);
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
        int ret = execute_step(cp, cp->leading_literal_count, rel_start, search_base, false, results);
        return ret;
    }

    return execute_step(cp, 0, NULL, search_base, false, results);
}

static size_t g_discovery_counter = 0;
void glob_results_reset_discovery_counter(void) { g_discovery_counter = 0; }

/* P1 Optimization: Initial capacity for result array */
#define INITIAL_RESULT_CAPACITY 64

void glob_results_init(glob_results_t *results)
{
    /* P1-1: Pre-allocate result array to reduce realloc() calls */
    results->capacity = INITIAL_RESULT_CAPACITY;
    results->items = malloc(sizeof(char *) * results->capacity);
    results->lengths = malloc(sizeof(size_t) * results->capacity);
    results->discovery_indices = malloc(sizeof(size_t) * results->capacity);
    results->count = 0;

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
void glob_results_clear(glob_results_t *results)
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
int glob_results_add_with_index(glob_results_t *results, const char *path, size_t index)
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
    results->items[results->count] = arena_alloc(&g_arena, len + 1);
    memcpy(results->items[results->count], p, len + 1);
    results->lengths[results->count] = len;
    results->discovery_indices[results->count] = index;
    results->count++;
    return 0;
}
int glob_results_add(glob_results_t *results, const char *path)
{
    return glob_results_add_with_index(results, path, g_discovery_counter++);
}

/* P10 Optimization: Helper structure for qsort() */
typedef struct
{
    char *path;
    size_t length;
    size_t discovery_index;
} sort_pair_t;

/* P10: Comparison function for qsort() */
static int compare_sort_pairs(const void *a, const void *b)
{
    const sort_pair_t *pa = (const sort_pair_t *)a;
    const sort_pair_t *pb = (const sort_pair_t *)b;
    return rbcglob_compare_paths(pa->path, pb->path);
}

void glob_results_sort(glob_results_t *results)
{
    if (results->count <= 1)
        return;

    /* P10: Use qsort() instead of O(n²) bubble sort */
    sort_pair_t *pairs = malloc(sizeof(sort_pair_t) * results->count);
    if (!pairs)
        return; /* Fallback: keep unsorted */

    for (size_t i = 0; i < results->count; i++)
    {
        pairs[i].path = results->items[i];
        pairs[i].length = results->lengths[i];
        pairs[i].discovery_index = results->discovery_indices[i];
    }

    qsort(pairs, results->count, sizeof(sort_pair_t), compare_sort_pairs);

    for (size_t i = 0; i < results->count; i++)
    {
        results->items[i] = pairs[i].path;
        results->lengths[i] = pairs[i].length;
        results->discovery_indices[i] = pairs[i].discovery_index;
    }

    free(pairs);
}
void glob_results_deduplicate(glob_results_t *results)
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
        else
        {
            /* P13: Duplicate - skip (arena will free all at once) */
            /* No need to free(results->items[read_idx]); */
        }
    }
    results->count = write_idx;
}
int rbcglob_compare_filesystem_order(const char *a, const char *b) { return strcmp(a, b); }
