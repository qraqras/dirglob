#include <rbcglob/internal/arena.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Default block size: 128KB */
#define DEFAULT_BLOCK_SIZE (128 * 1024)

/* Align allocations to 8 bytes for performance */
#define ALIGN_SIZE 8
#define ALIGN_UP(n) (((n) + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1))

static rbcglob_arena_block_t *rbcglob_arena_new_block(size_t size)
{
    /* Single block allocation for header and data to improve cache locality */
    rbcglob_arena_block_t *block = malloc(sizeof(rbcglob_arena_block_t) + size);
    if (!block)
        return NULL;
    block->size = size;
    block->used = 0;
    block->next = NULL;
    return block;
}

void rbcglob_arena_init(rbcglob_arena_t *arena, size_t initial_size)
{
    if (!arena)
        return;
    if (initial_size == 0)
        initial_size = DEFAULT_BLOCK_SIZE;

    rbcglob_arena_block_t *block = rbcglob_arena_new_block(initial_size);
    arena->first = block;
    arena->current = block;
    arena->block_size = initial_size;
}

void *rbcglob_arena_alloc(rbcglob_arena_t *arena, size_t size)
{
    if (!arena || !arena->current || size == 0)
        return NULL;

    size = ALIGN_UP(size);

    /* Try current block */
    if (arena->current->used + size <= arena->current->size)
    {
        void *ptr = &arena->current->data[arena->current->used];
        arena->current->used += size;
        return ptr;
    }

    /* Current block exhausted, look for usable next block (from previous reset) */
    if (arena->current->next && size <= arena->current->next->size)
    {
        arena->current = arena->current->next;
        arena->current->used = size;
        return arena->current->data;
    }

    /* Allocate new block */
    size_t next_size = (size > arena->block_size) ? ALIGN_UP(size) : arena->block_size;
    rbcglob_arena_block_t *block = rbcglob_arena_new_block(next_size);
    if (!block)
        return NULL;

    block->used = size;
    /* Insert into chain after current block */
    block->next = arena->current->next;
    arena->current->next = block;
    arena->current = block;

    return block->data;
}

void *rbcglob_arena_memdup(rbcglob_arena_t *arena, const void *ptr, size_t size)
{
    if (!ptr || size == 0)
        return NULL;
    void *dup = rbcglob_arena_alloc(arena, size);
    if (dup)
        memcpy(dup, ptr, size);
    return dup;
}

char *rbcglob_arena_strdup(rbcglob_arena_t *arena, const char *str)
{
    if (!str)
        return NULL;
    size_t len = strlen(str);
    return (char *)rbcglob_arena_memdup(arena, str, len + 1);
}

char *rbcglob_arena_printf(rbcglob_arena_t *arena, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    /* Determine required size */
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (len < 0)
    {
        va_end(args);
        return NULL;
    }

    char *str = rbcglob_arena_alloc(arena, (size_t)len + 1);
    if (str)
    {
        vsnprintf(str, (size_t)len + 1, fmt, args);
    }

    va_end(args);
    return str;
}

void rbcglob_arena_reset(rbcglob_arena_t *arena)
{
    if (!arena)
        return;
    rbcglob_arena_block_t *block = arena->first;
    while (block)
    {
        block->used = 0;
        block = block->next;
    }
    arena->current = arena->first;
}

void rbcglob_arena_destroy(rbcglob_arena_t *arena)
{
    if (!arena)
        return;
    rbcglob_arena_block_t *block = arena->first;
    while (block)
    {
        rbcglob_arena_block_t *next = block->next;
        free(block);
        block = next;
    }
    arena->first = NULL;
    arena->current = NULL;
}
