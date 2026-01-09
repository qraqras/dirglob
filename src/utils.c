#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/utils.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void rbcglob_str_list_init(rbcglob_str_list_t *list, size_t initial_cap, rbcglob_arena_t *arena)
{
    list->arena = arena;
    list->count = 0;
    list->capacity = initial_cap;
    if (arena)
        list->items = rbcglob_arena_alloc(arena, initial_cap * sizeof(char *));
    else
        list->items = malloc(initial_cap * sizeof(char *));
}

void rbcglob_str_list_add(rbcglob_str_list_t *list, const char *str)
{
    if (list->count == list->capacity)
    {
        list->capacity *= 2;
        if (list->arena)
        {
            // Arena can't easily realloc, so we allocate new and copy
            char **new_items = rbcglob_arena_alloc(list->arena, list->capacity * sizeof(char *));
            if (list->count > 0)
                memcpy(new_items, list->items, list->count * sizeof(char *));
            list->items = new_items;
        }
        else
        {
            list->items = realloc(list->items, list->capacity * sizeof(char *));
        }
    }

    if (list->arena)
    {
        list->items[list->count++] = rbcglob_arena_strdup(list->arena, str);
    }
    else
    {
        list->items[list->count++] = rbcglob_strdup(str);
    }
}

void rbcglob_str_list_free(rbcglob_str_list_t *list)
{
    if (list->arena)
    {
        // Arena managed, no need to free individual items
        // We also don't free list->items as it belongs to arena
        return;
    }

    for (size_t i = 0; i < list->count; i++)
    {
        free(list->items[i]);
    }
    free(list->items);
}

bool rbcglob_has_brace(const char *str)
{
    bool esc = false;
    for (const char *p = str; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '{')
            return true;
    }
    return false;
}

bool rbcglob_has_wildcard(const char *str)
{
    bool esc = false;
    for (const char *p = str; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '*' || *p == '?' || *p == '[')
            return true;
    }
    return false;
}

const char *rbcglob_find_segment_end(const char *str)
{
    bool esc = false;
    int depth = 0;
    const char *p = str;
    while (*p)
    {
        if (esc)
        {
            esc = false;
            p++;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            p++;
            continue;
        }

        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            if (depth > 0)
                depth--;
        }
        else if (*p == '/' && depth == 0)
            return p;

        p++;
    }
    return p;
}

// Simple implementations for LCP/LCS
char *rbcglob_compute_lcp(const char **strs, size_t count)
{
    if (count == 0 || !strs)
        return NULL;
    if (count == 1)
        return rbcglob_strdup(strs[0]);

    size_t len = strlen(strs[0]);
    for (size_t i = 1; i < count; i++)
    {
        size_t j = 0;
        while (j < len && strs[i][j] && strs[0][j] == strs[i][j])
        {
            j++;
        }
        len = j;
    }

    if (len == 0)
        return NULL;
    char *res = malloc(len + 1);
    strncpy(res, strs[0], len);
    res[len] = '\0';
    return res;
}

char *rbcglob_compute_lcs(const char **strs, size_t count)
{
    if (count == 0 || !strs)
        return NULL;
    if (count == 1)
        return rbcglob_strdup(strs[0]);

    size_t min_len = strlen(strs[0]);
    for (size_t i = 1; i < count; i++)
    {
        size_t l = strlen(strs[i]);
        if (l < min_len)
            min_len = l;
    }

    size_t common = 0;
    while (common < min_len)
    {
        char c = strs[0][strlen(strs[0]) - 1 - common];
        bool all_match = true;
        for (size_t i = 1; i < count; i++)
        {
            char this_c = strs[i][strlen(strs[i]) - 1 - common];
            if (this_c != c)
            {
                all_match = false;
                break;
            }
        }
        if (!all_match)
            break;
        common++;
    }

    if (common == 0)
        return NULL;
    char *res = malloc(common + 1);
    // Copy the suffix
    const char *src = strs[0] + strlen(strs[0]) - common;
    strcpy(res, src);
    return res;
}

// Minimal brace expansion (recursive)
static void expand_recursive(rbcglob_str_list_t *results, const char *prefix, const char *pattern, rbcglob_arena_t *arena)
{
    const char *brace_start = NULL;
    bool esc = false;

    // Search for first unescaped opening brace
    for (const char *p = pattern; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '{')
        {
            brace_start = p;
            break;
        }
    }

    if (!brace_start)
    {
        // No braces, append prefix + pattern
        if (arena)
        {
            char *full = rbcglob_arena_printf(arena, "%s%s", prefix, pattern);
            rbcglob_str_list_add(results, full);
            // No free
        }
        else
        {
            size_t total_len = strlen(prefix) + strlen(pattern) + 1;
            char *full = malloc(total_len);
            sprintf(full, "%s%s", prefix, pattern);
            rbcglob_str_list_add(results, full);
            free(full);
        }
        return;
    }

    // Extract prefix before brace
    size_t pre_len = brace_start - pattern;
    char *pre;
    if (arena)
    {
        pre = rbcglob_arena_alloc(arena, pre_len + 1);
        memcpy(pre, pattern, pre_len);
        pre[pre_len] = '\0';
    }
    else
    {
        pre = malloc(pre_len + 1);
        strncpy(pre, pattern, pre_len);
        pre[pre_len] = '\0';
    }

    // Combine current prefix with new pre (e.g., "dir/" + "file")
    char *new_prefix_base;
    if (arena)
    {
        new_prefix_base = rbcglob_arena_printf(arena, "%s%s", prefix, pre);
    }
    else
    {
        new_prefix_base = malloc(strlen(prefix) + pre_len + 1);
        sprintf(new_prefix_base, "%s%s", prefix, pre);
        free(pre);
    }

    const char *p = brace_start + 1;
    const char *chunk_start = p;
    int depth = 1;
    esc = false;

    rbcglob_str_list_t options;
    rbcglob_str_list_init(&options, 4, arena);

    // Parse brace content
    while (*p)
    {
        if (esc)
        {
            esc = false;
            p++;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            p++;
            continue;
        }

        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            depth--;
            if (depth == 0)
            {
                // Found option end
                size_t chunk_len = p - chunk_start;
                char *chunk;
                if (arena)
                {
                    chunk = rbcglob_arena_alloc(arena, chunk_len + 1);
                    memcpy(chunk, chunk_start, chunk_len);
                    chunk[chunk_len] = '\0';
                }
                else
                {
                    chunk = malloc(chunk_len + 1);
                    strncpy(chunk, chunk_start, chunk_len);
                    chunk[chunk_len] = '\0';
                }
                rbcglob_str_list_add(&options, chunk);
                if (!arena)
                    free(chunk);
                break;
            }
        }
        else if (*p == ',' && depth == 1)
        {
            // Found comma at top level
            size_t chunk_len = p - chunk_start;
            char *chunk;
            if (arena)
            {
                chunk = rbcglob_arena_alloc(arena, chunk_len + 1);
                memcpy(chunk, chunk_start, chunk_len);
                chunk[chunk_len] = '\0';
            }
            else
            {
                chunk = malloc(chunk_len + 1);
                strncpy(chunk, chunk_start, chunk_len);
                chunk[chunk_len] = '\0';
            }
            rbcglob_str_list_add(&options, chunk);
            if (!arena)
                free(chunk);
            chunk_start = p + 1;
        }
        p++;
    }

    const char *suffix = p;
    if (*p == '}')
        suffix = p + 1;

    for (size_t i = 0; i < options.count; i++)
    {
        // Recursive call with new prefix + option + suffix
        char *next_pattern;
        if (arena)
        {
            // Optimization: avoid snprintf format overhead for simple concat
            size_t opt_len = strlen(options.items[i]);
            size_t suf_len = strlen(suffix);
            size_t total = opt_len + suf_len + 1;
            next_pattern = rbcglob_arena_alloc(arena, total);
            memcpy(next_pattern, options.items[i], opt_len);
            memcpy(next_pattern + opt_len, suffix, suf_len + 1); // +1 copies null terminator

            expand_recursive(results, new_prefix_base, next_pattern, arena);
        }
        else
        {
            size_t opt_len = strlen(options.items[i]);
            size_t suf_len = strlen(suffix);
            next_pattern = malloc(opt_len + suf_len + 1);
            sprintf(next_pattern, "%s%s", options.items[i], suffix);
            expand_recursive(results, new_prefix_base, next_pattern, arena);
            free(next_pattern);
        }
    }

    rbcglob_str_list_free(&options);
    if (!arena)
        free(new_prefix_base);
}

static void expand_recursive_visitor(const char *pattern, rbcglob_arena_t *arena, rbcglob_brace_visit_cb cb, void *arg, char *scratch_buf, size_t buf_size)
{
    // Simplified logic for visitor: no full allocation if possible.
    // For now, minimal implementation reusing some logic but using scratch buffer logic implicitly via recursion.
    // Actually, to implement "stack buffer" logic correctly without recursion allocation,
    // we would need to pass (buffer, pos) down.
    // But since `option` part needs parsing, we do:

    // Find first brace
    const char *p = pattern;
    bool in_brace = false;
    int depth = 0;

    // Quick scan for top-level brace
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
    {
        // No expansion needed. Just callback.
        cb(pattern, arg);
        return;
    }

    // Brace found. We must parse it.
    // Use the same robust parsing logic as before but iterate differently?
    // To strictly avoid allocation for the *full path*, we can construct it on stack.
    // But we still need to parse options into a list? Not necessarily, we can iterate them.

    // To keep it simple and robust (reusing tested logic), we use `expand_recursive` logic
    // but instead of `results` list, we pass `cb`.
    // And for the memory, we use `alloca` for the intermediate pattern strings to keep it on stack.

    rbcglob_str_list_t options;
    rbcglob_str_list_init(&options, 4, arena);

    // Parse Logic (Duplicated for now, or consider refactoring common parse logic)
    p = pattern;
    depth = 0;
    while (*p && *p != '{')
    {
        if (*p == '\\')
            p++;
        p++;
    }

    if (*p != '{')
    {
        cb(pattern, arg);
        rbcglob_str_list_free(&options);
        return;
    } // Should match quick scan

    // Prefix is pattern .. p
    size_t prefix_len = p - pattern;
    // We can't easily copy prefix because we are recursively expanding.
    // So 'pattern' contains the full string so far.
    // We strip it: prefix = [pattern, p)

    const char *brace_start = p;
    p++; // Skip {
    depth = 1;
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
                // End option
                size_t len = p - chunk_start;
                char *chunk;
                if (arena)
                {
                    chunk = rbcglob_arena_alloc(arena, len + 1);
                    memcpy(chunk, chunk_start, len);
                    chunk[len] = 0;
                }
                else
                {
                    chunk = malloc(len + 1);
                    memcpy(chunk, chunk_start, len);
                    chunk[len] = 0;
                }
                rbcglob_str_list_add(&options, chunk);
                if (!arena)
                    free(chunk); // list copies it if needed? No, list stores pointer.
                // Wait, if !arena, list stores the pointer. If we free it, list has dangling.
                // rbcglob_str_list_add copies if it manages own memory?
                // `rbcglob_str_list_add` implementation: copies string if arena is NULL usually?
                // Let's check `rbcglob_str_list_add`.
                // Actually `rbcglob_str_list_add` usually takes ownership or strdups?
                // Assuming `rbcglob_str_list_add` duplicates if needed.
                // In `expand_recursive` above:
                // if (!arena) free(chunk); -> This implies `add` duplicates.

                valid_brace = true;
                break;
            }
        }
        else if (*p == ',' && depth == 1)
        {
            size_t len = p - chunk_start;
            char *chunk;
            if (arena)
            {
                chunk = rbcglob_arena_alloc(arena, len + 1);
                memcpy(chunk, chunk_start, len);
                chunk[len] = 0;
            }
            else
            {
                chunk = malloc(len + 1);
                memcpy(chunk, chunk_start, len);
                chunk[len] = 0;
            }
            rbcglob_str_list_add(&options, chunk);
            if (!arena)
                free(chunk);
            chunk_start = p + 1;
        }
        p++;
    }

    if (!valid_brace)
    {
        cb(pattern, arg);
        rbcglob_str_list_free(&options);
        return;
    }

    const char *suffix = p + 1;

    // Now iterate and Recurse
    for (size_t i = 0; i < options.count; i++)
    {
        size_t opt_len = strlen(options.items[i]);
        size_t suf_len = strlen(suffix);
        size_t needed = prefix_len + opt_len + suf_len + 1;

        char *next_buf = NULL;
        // Use stack if small enough
        if (needed < 4096)
        {
            // alloca is standard enough? Or just recursive safe VLA?
            // Use scratch buffer if depth is 0? No, depth increases.
            // We can use a VLA here.
            char vla[needed];
            memcpy(vla, pattern, prefix_len);
            memcpy(vla + prefix_len, options.items[i], opt_len);
            memcpy(vla + prefix_len + opt_len, suffix, suf_len + 1);
            expand_recursive_visitor(vla, arena, cb, arg, NULL, 0);
        }
        else
        {
            // Fallback to heap/arena
            if (arena)
                next_buf = rbcglob_arena_alloc(arena, needed);
            else
                next_buf = malloc(needed);

            memcpy(next_buf, pattern, prefix_len);
            memcpy(next_buf + prefix_len, options.items[i], opt_len);
            memcpy(next_buf + prefix_len + opt_len, suffix, suf_len + 1);

            expand_recursive_visitor(next_buf, arena, cb, arg, NULL, 0);
            if (!arena)
                free(next_buf);
        }
    }

    rbcglob_str_list_free(&options);
}

void rbcglob_brace_visit(const char *pattern, rbcglob_arena_t *arena, rbcglob_brace_visit_cb cb, void *arg)
{
    expand_recursive_visitor(pattern, arena, cb, arg, NULL, 0);
}

rbcglob_str_list_t rbcglob_brace_expand(const char *pattern, rbcglob_arena_t *arena)
{
    rbcglob_str_list_t list;
    rbcglob_str_list_init(&list, 8, arena);
    expand_recursive(&list, "", pattern, arena);
    return list;
}

/**
 * @brief Return library version string.
 */
const char *rbcglob_version(void)
{
    return RBCGLOB_VERSION;
}

char *rbcglob_strdup(const char *str)
{
    if (!str)
        return NULL;

    size_t len = strlen(str);
    char *dup = malloc(len + 1);
    if (!dup)
        return NULL;

    memcpy(dup, str, len + 1);
    return dup;
}
