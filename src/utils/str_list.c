#include <stdlib.h>
#include <string.h>
#include <rbc/rbc.h>
#include "internal.h"
#include "utils.h"

bool rbc_str_list_init(rbc_str_list_t *list, size_t initial_cap, rbc_arena_t *arena)
{
    list->arena = arena;
    list->count = 0;
    list->capacity = initial_cap;
    if (arena)
    {
        list->items = rbc_arena_alloc(arena, initial_cap * sizeof(char *));
    }
    else
    {
        list->items = malloc(initial_cap * sizeof(char *));
    }

    if (initial_cap > 0 && !list->items)
        return false;
    return true;
}

bool rbc_str_list_add(rbc_str_list_t *list, const char *str)
{
    if (list->count == list->capacity)
    {
        size_t new_cap = list->capacity ? list->capacity * 2 : 4;
        if (list->arena)
        {
            char **new_items = rbc_arena_alloc(list->arena, new_cap * sizeof(char *));
            if (!new_items)
                return false;
            if (list->count > 0)
                memcpy(new_items, list->items, list->count * sizeof(char *));
            list->items = new_items;
            list->capacity = new_cap;
        }
        else
        {
            char **new_items = realloc(list->items, new_cap * sizeof(char *));
            if (!new_items)
                return false;
            list->items = new_items;
            list->capacity = new_cap;
        }
    }

    if (list->arena)
    {
        list->items[list->count] = rbc_arena_strdup(list->arena, str);
        if (!list->items[list->count])
            return false;
        list->count++;
    }
    else
    {
        list->items[list->count] = rbc_strdup(str);
        if (!list->items[list->count])
            return false;
        list->count++;
    }
    return true;
}

void rbc_str_list_free(rbc_str_list_t *list)
{
    if (list->arena)
        return;

    for (size_t i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
}
