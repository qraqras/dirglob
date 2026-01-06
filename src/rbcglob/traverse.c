#include <rbcglob/internal/traverse.h>
#include <rbcglob/internal/utils.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

/* Simple Directory Cache */
typedef struct
{
    char *path;
    char **entries;
    size_t count;
} dir_cache_node_t;

static dir_cache_node_t *g_dir_cache = NULL;
static size_t g_dir_cache_count = 0;

void glob_results_clear_cache(void)
{
    if (!g_dir_cache)
        return;
    for (size_t i = 0; i < g_dir_cache_count; i++)
    {
        free(g_dir_cache[i].path);
        for (size_t j = 0; j < g_dir_cache[i].count; j++)
        {
            free(g_dir_cache[i].entries[j]);
        }
        free(g_dir_cache[i].entries);
    }
    free(g_dir_cache);
    g_dir_cache = NULL;
    g_dir_cache_count = 0;
}

static ssize_t get_cached_dir_index(const char *path)
{
    for (size_t i = 0; i < g_dir_cache_count; i++)
    {
        if (strcmp(g_dir_cache[i].path, path) == 0)
            return (ssize_t)i;
    }

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

    g_dir_cache[idx].path = strdup(path);
    g_dir_cache[idx].entries = NULL;
    g_dir_cache[idx].count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        char **new_entries = realloc(g_dir_cache[idx].entries, sizeof(char *) * (g_dir_cache[idx].count + 1));
        if (!new_entries)
            break;
        g_dir_cache[idx].entries = new_entries;
        g_dir_cache[idx].entries[g_dir_cache[idx].count++] = strdup(entry->d_name);
    }
    closedir(dir);
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
        char *full_dir_to_open = path_join(search_base, rel_path);
        const char *dir_to_open = full_dir_to_open ? full_dir_to_open : (search_base ? search_base : ".");

        ssize_t cache_idx = get_cached_dir_index(dir_to_open);
        free(full_dir_to_open);
        if (cache_idx < 0)
            return 0;

        for (size_t i = 0; i < g_dir_cache[cache_idx].count; i++)
        {
            const char *name = g_dir_cache[cache_idx].entries[i];
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                continue;
            if (name[0] == '.' && !(cp->flags & 8))
                continue; /* 8 is RBCGLOB_FNM_DOTMATCH */

            char *next_rel = path_join(rel_path, name);
            if (!next_rel)
                return -1;

            char *next_full = path_join(search_base, next_rel);
            struct stat st;
            if (next_full && lstat(next_full, &st) == 0 && S_ISDIR(st.st_mode))
            {
                /* Stay on RBCGLOB_SEGMENT_RECURSIVE to find deeper matches.
                   Next instruction will still see is_after_wildcard=true because we moved through RBCGLOB_SEGMENT_RECURSIVE. */
                ret = execute_step(cp, seg_idx, next_rel, search_base, true, results);
                free(next_rel);
                free(next_full);
                if (ret != 0)
                    return ret;
            }
            else
            {
                free(next_rel);
                free(next_full);
            }
        }
        return 0;
    }

    if (seg->type == RBCGLOB_SEGMENT_LITERAL)
    {
        char *next_rel = path_join(rel_path, seg->pattern);
        if (!next_rel)
            return -1;
        char *next_full = path_join(search_base, next_rel);
        struct stat st;
        if (next_full && stat(next_full, &st) == 0)
        {
            /* Check if this segment must be a directory */
            bool must_be_directory = (seg_idx + 1 < cp->count) || cp->has_trailing_slash;
            if (must_be_directory && !S_ISDIR(st.st_mode))
            {
                free(next_rel);
                free(next_full);
                return 0;
            }
            /* Literals don't trigger is_after_wildcard, but they propagate it */
            int ret = execute_step(cp, seg_idx + 1, next_rel, search_base, is_after_wildcard, results);
            free(next_rel);
            free(next_full);
            return ret;
        }
        free(next_rel);
        free(next_full);
        return 0;
    }

    /* Wildcard match */
    char *full_dir_to_open = path_join(search_base, rel_path);
    const char *dir_to_open = full_dir_to_open ? full_dir_to_open : (search_base ? search_base : ".");
    ssize_t cache_idx = get_cached_dir_index(dir_to_open);
    free(full_dir_to_open);
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

        if (seg->prefix && strncmp(name, seg->prefix, seg->prefix_len) != 0)
            continue;

        if (seg->suffix)
        {
            size_t name_len = strlen(name);
            if (name_len < seg->suffix_len || strcmp(name + name_len - seg->suffix_len, seg->suffix) != 0)
            {
                continue;
            }
        }

        if (match_tokens(seg, name, cp->flags))
        {
            char *next_rel = path_join(rel_path, name);
            if (!next_rel)
                return -1;

            /* Check if this segment must be a directory */
            bool must_be_directory = (seg_idx + 1 < cp->count) || cp->has_trailing_slash;
            if (must_be_directory)
            {
                char *next_full = path_join(search_base, next_rel);
                struct stat st;
                if (!next_full || stat(next_full, &st) != 0 || !S_ISDIR(st.st_mode))
                {
                    free(next_rel);
                    free(next_full);
                    continue;
                }
                free(next_full);
            }
            int ret = execute_step(cp, seg_idx + 1, next_rel, search_base, true, results);
            free(next_rel);
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
    const char *search_base = (base && base[0] != '\0') ? base : NULL;
    if (cp->is_absolute)
        search_base = "/";
    return execute_step(cp, 0, NULL, search_base, false, results);
}

static size_t g_discovery_counter = 0;
void glob_results_reset_discovery_counter(void) { g_discovery_counter = 0; }
void glob_results_init(glob_results_t *results)
{
    results->items = NULL;
    results->discovery_indices = NULL;
    results->count = 0;
    results->capacity = 0;
}
void glob_results_clear(glob_results_t *results)
{
    if (!results)
        return;
    for (size_t i = 0; i < results->count; i++)
        free(results->items[i]);
    free(results->items);
    free(results->discovery_indices);
    results->items = NULL;
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
        size_t *new_indices = realloc(results->discovery_indices, sizeof(size_t) * new_cap);
        if (!new_indices)
            return -1;
        results->discovery_indices = new_indices;
        results->capacity = new_cap;
    }
    results->items[results->count] = strdup(path ? path : ".");
    results->discovery_indices[results->count] = index;
    results->count++;
    return 0;
}
int glob_results_add(glob_results_t *results, const char *path)
{
    return glob_results_add_with_index(results, path, g_discovery_counter++);
}
void glob_results_sort(glob_results_t *results)
{
    for (size_t i = 0; i < results->count; i++)
    {
        for (size_t j = i + 1; j < results->count; j++)
        {
            if (rbcglob_compare_paths(results->items[i], results->items[j]) > 0)
            {
                char *tmp = results->items[i];
                results->items[i] = results->items[j];
                results->items[j] = tmp;
                size_t itmp = results->discovery_indices[i];
                results->discovery_indices[i] = results->discovery_indices[j];
                results->discovery_indices[j] = itmp;
            }
        }
    }
}
void glob_results_deduplicate(glob_results_t *results)
{
    if (results->count <= 1)
        return;
    size_t write_idx = 1;
    for (size_t read_idx = 1; read_idx < results->count; read_idx++)
    {
        bool duplicate = false;
        for (size_t k = 0; k < write_idx; k++)
        {
            if (strcmp(results->items[read_idx], results->items[k]) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
        {
            results->items[write_idx] = results->items[read_idx];
            results->discovery_indices[write_idx] = results->discovery_indices[read_idx];
            write_idx++;
        }
        else
        {
            free(results->items[read_idx]);
        }
    }
    results->count = write_idx;
}
int rbcglob_compare_filesystem_order(const char *a, const char *b) { return strcmp(a, b); }
