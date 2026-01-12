#include <stdlib.h>
#include <string.h>
#include <rbc/rbc.h>
#include "internal.h"
#include "utils.h"

bool rbc_brace_visit(const char *pattern, rbc_arena_t *arena, rbc_brace_visit_cb cb, void *arg)
{
    const char *p = pattern;
    bool in_brace = false;

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
        return cb(pattern, arg);
    }

    rbc_str_list_t options;
    if (!rbc_str_list_init(&options, 4, arena))
        return false;

    p = pattern;
    while (*p && *p != '{')
    {
        if (*p == '\\')
            p++;
        p++;
    }

    if (*p != '{')
    {
        bool ret = cb(pattern, arg);
        rbc_str_list_free(&options);
        return ret;
    }

    size_t prefix_len = p - pattern;
    p++;
    int depth = 1;
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
                size_t len = p - chunk_start;
                char *chunk;
                if (arena)
                {
                    chunk = rbc_arena_alloc(arena, len + 1);
                    if (!chunk)
                    {
                        rbc_str_list_free(&options);
                        return false;
                    }
                    memcpy(chunk, chunk_start, len);
                    chunk[len] = 0;
                }
                else
                {
                    chunk = malloc(len + 1);
                    if (!chunk)
                    {
                        rbc_str_list_free(&options);
                        return false;
                    }
                    memcpy(chunk, chunk_start, len);
                    chunk[len] = 0;
                }
                if (!rbc_str_list_add(&options, chunk))
                {
                    if (!arena)
                        free(chunk);
                    rbc_str_list_free(&options);
                    return false;
                }
                if (!arena)
                    free(chunk);
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
                chunk = rbc_arena_alloc(arena, len + 1);
                if (!chunk)
                {
                    rbc_str_list_free(&options);
                    return false;
                }
                memcpy(chunk, chunk_start, len);
                chunk[len] = 0;
            }
            else
            {
                chunk = malloc(len + 1);
                if (!chunk)
                {
                    rbc_str_list_free(&options);
                    return false;
                }
                memcpy(chunk, chunk_start, len);
                chunk[len] = 0;
            }
            if (!rbc_str_list_add(&options, chunk))
            {
                if (!arena)
                    free(chunk);
                rbc_str_list_free(&options);
                return false;
            }
            if (!arena)
                free(chunk);
            chunk_start = p + 1;
        }
        p++;
    }

    if (!valid_brace)
    {
        bool ret = cb(pattern, arg);
        rbc_str_list_free(&options);
        return ret;
    }

    const char *suffix = p + 1;
    for (size_t i = 0; i < options.count; i++)
    {
        size_t opt_len = strlen(options.items[i]);
        size_t suf_len = strlen(suffix);
        size_t needed = prefix_len + opt_len + suf_len + 1;

        if (needed < 4096)
        {
            char vla[needed];
            memcpy(vla, pattern, prefix_len);
            memcpy(vla + prefix_len, options.items[i], opt_len);
            memcpy(vla + prefix_len + opt_len, suffix, suf_len + 1);
            if (!rbc_brace_visit(vla, arena, cb, arg))
            {
                rbc_str_list_free(&options);
                return false;
            }
        }
        else
        {
            char *next_buf = arena ? rbc_arena_alloc(arena, needed) : malloc(needed);
            if (!next_buf)
            {
                rbc_str_list_free(&options);
                return false;
            }
            memcpy(next_buf, pattern, prefix_len);
            memcpy(next_buf + prefix_len, options.items[i], opt_len);
            memcpy(next_buf + prefix_len + opt_len, suffix, suf_len + 1);

            bool ret = rbc_brace_visit(next_buf, arena, cb, arg);
            if (!arena)
                free(next_buf);
            if (!ret)
            {
                rbc_str_list_free(&options);
                return false;
            }
        }
    }

    rbc_str_list_free(&options);
    return true;
}

static bool rbc_brace_collect_cb(const char *pattern, void *arg)
{
    rbc_str_list_t *list = (rbc_str_list_t *)arg;
    return rbc_str_list_add(list, pattern);
}

rbc_str_list_t rbc_brace_collect(const char *pattern, rbc_arena_t *arena)
{
    rbc_str_list_t list;
    if (!rbc_str_list_init(&list, 8, arena))
    {
        list.items = NULL;
        list.count = 0;
        list.capacity = 0;
        list.arena = arena;
        return list;
    }
    rbc_brace_visit(pattern, arena, rbc_brace_collect_cb, &list);
    return list;
}
