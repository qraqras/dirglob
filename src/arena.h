#ifndef RBC_INTERNAL_ARENA_H
#define RBC_INTERNAL_ARENA_H

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

typedef struct rbc_arena_block_s
{
    struct rbc_arena_block_s *next;
    size_t size;
    size_t used;
} rbc_arena_block_t;

typedef struct rbc_arena_s
{
    rbc_arena_block_t *current;
    rbc_arena_block_t *first;
    size_t block_size;
    bool is_static;
} rbc_arena_t;

bool rbc_arena_init(rbc_arena_t *arena, size_t initial_size);
bool rbc_arena_init_static(rbc_arena_t *arena, void *buffer, size_t size);
void *rbc_arena_alloc(rbc_arena_t *arena, size_t size);
void *rbc_arena_memdup(rbc_arena_t *arena, const void *ptr, size_t size);
char *rbc_arena_strdup(rbc_arena_t *arena, const char *str);
char *rbc_arena_printf(rbc_arena_t *arena, const char *fmt, ...);
void rbc_arena_reset(rbc_arena_t *arena);
void rbc_arena_destroy(rbc_arena_t *arena);

#endif /* RBC_INTERNAL_ARENA_H */
