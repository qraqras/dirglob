#include <rbcglob/internal/arena.h>
#include <stdlib.h>
#include <string.h>

/* Default block size: 64KB */
#define DEFAULT_BLOCK_SIZE (64 * 1024)

/* Align allocations to 8 bytes for performance */
#define ALIGN_SIZE 8
#define ALIGN_UP(n) (((n) + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1))

void arena_init(arena_t *arena, size_t initial_size)
{
    if (initial_size == 0)
        initial_size = DEFAULT_BLOCK_SIZE;

    arena_block_t *block = malloc(sizeof(arena_block_t));
    if (!block)
    {
        arena->current = NULL;
        arena->first = NULL;
        arena->block_size = initial_size;
        return;
    }

    block->data = malloc(initial_size);
    if (!block->data)
    {
        free(block);
        arena->current = NULL;
        arena->first = NULL;
        arena->block_size = initial_size;
        return;
    }

    block->size = initial_size;
    block->used = 0;
    block->next = NULL;

    arena->current = block;
    arena->first = block;
    arena->block_size = initial_size;
}

void *arena_alloc(arena_t *arena, size_t size)
{
    if (!arena || !arena->current || size == 0)
        return NULL;

    /* Align size */
    size = ALIGN_UP(size);

    /* Check if current block has enough space */
    arena_block_t *block = arena->current;
    if (block->used + size <= block->size)
    {
        void *ptr = block->data + block->used;
        block->used += size;
        return ptr;
    }

    /* Need a new block */
    size_t new_block_size = arena->block_size;
    /* If requested size is larger than standard block, allocate larger block */
    if (size > new_block_size)
        new_block_size = size * 2;

    arena_block_t *new_block = malloc(sizeof(arena_block_t));
    if (!new_block)
        return NULL;

    new_block->data = malloc(new_block_size);
    if (!new_block->data)
    {
        free(new_block);
        return NULL;
    }

    new_block->size = new_block_size;
    new_block->used = size;
    new_block->next = NULL;

    /* Link new block */
    arena->current->next = new_block;
    arena->current = new_block;

    return new_block->data;
}

char *arena_strdup(arena_t *arena, const char *str)
{
    if (!str)
        return NULL;

    size_t len = strlen(str);
    char *dup = arena_alloc(arena, len + 1);
    if (!dup)
        return NULL;

    memcpy(dup, str, len + 1);
    return dup;
}

void arena_reset(arena_t *arena)
{
    if (!arena || !arena->first)
        return;

    /* Reset all blocks to unused state */
    arena_block_t *block = arena->first;
    while (block)
    {
        block->used = 0;
        block = block->next;
    }

    arena->current = arena->first;
}

void arena_destroy(arena_t *arena)
{
    if (!arena || !arena->first)
        return;

    arena_block_t *block = arena->first;
    while (block)
    {
        arena_block_t *next = block->next;
        free(block->data);
        free(block);
        block = next;
    }

    arena->current = NULL;
    arena->first = NULL;
}
