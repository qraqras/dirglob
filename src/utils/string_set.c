/**
 * @file string_set.c
 * @brief Simple string set implementation
 *
 * Hash set optimized for small string sets (typical brace expansions).
 */

#include "string_set.h"
#include <stdlib.h>
#include <string.h>

/* Hash table entry */
typedef struct entry_s
{
    char *key;
    struct entry_s *next;
} entry_t;

/* String set structure */
struct rbc_string_set_s
{
    entry_t **buckets;
    size_t bucket_count;
    size_t size;
};

/* Simple hash function (djb2) */
static unsigned long hash_string(const char *str, size_t len)
{
    unsigned long hash = 5381;
    for (size_t i = 0; i < len; i++)
    {
        hash = ((hash << 5) + hash) + (unsigned char)str[i];
    }
    return hash;
}

rbc_string_set_t *rbc_string_set_create(size_t expected_size)
{
    rbc_string_set_t *set = calloc(1, sizeof(rbc_string_set_t));
    if (!set)
        return NULL;

    /* Use next power of 2 >= expected_size */
    size_t bucket_count = 16;
    while (bucket_count < expected_size && bucket_count < 1024)
    {
        bucket_count *= 2;
    }

    set->bucket_count = bucket_count;
    set->buckets = calloc(bucket_count, sizeof(entry_t *));
    if (!set->buckets)
    {
        free(set);
        return NULL;
    }

    return set;
}

bool rbc_string_set_add_n(rbc_string_set_t *set, const char *str, size_t len)
{
    if (!set || !str)
        return false;

    /* Check if already exists */
    unsigned long hash = hash_string(str, len);
    size_t bucket = hash % set->bucket_count;

    for (entry_t *e = set->buckets[bucket]; e; e = e->next)
    {
        if (strncmp(e->key, str, len) == 0 && e->key[len] == '\0')
        {
            return true; /* Already exists */
        }
    }

    /* Add new entry */
    entry_t *entry = malloc(sizeof(entry_t));
    if (!entry)
        return false;

    entry->key = malloc(len + 1);
    if (!entry->key)
    {
        free(entry);
        return false;
    }

    memcpy(entry->key, str, len);
    entry->key[len] = '\0';

    /* Prepend to bucket */
    entry->next = set->buckets[bucket];
    set->buckets[bucket] = entry;
    set->size++;

    return true;
}

bool rbc_string_set_add(rbc_string_set_t *set, const char *str)
{
    if (!str)
        return false;
    return rbc_string_set_add_n(set, str, strlen(str));
}

bool rbc_string_set_contains(const rbc_string_set_t *set, const char *str)
{
    if (!set || !str)
        return false;

    size_t len = strlen(str);
    unsigned long hash = hash_string(str, len);
    size_t bucket = hash % set->bucket_count;

    for (entry_t *e = set->buckets[bucket]; e; e = e->next)
    {
        if (strcmp(e->key, str) == 0)
        {
            return true;
        }
    }

    return false;
}

void rbc_string_set_free(rbc_string_set_t *set)
{
    if (!set)
        return;

    for (size_t i = 0; i < set->bucket_count; i++)
    {
        entry_t *e = set->buckets[i];
        while (e)
        {
            entry_t *next = e->next;
            free(e->key);
            free(e);
            e = next;
        }
    }

    free(set->buckets);
    free(set);
}
