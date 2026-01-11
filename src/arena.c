#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define RBC_BLOCK_SIZE (128 * 1024)
#define RBC_ALIGN_SIZE 8
#define RBC_ALIGN_UP(n) (((n) + RBC_ALIGN_SIZE - 1) & ~(RBC_ALIGN_SIZE - 1))
#define RBC_BLOCK_HEADER_SIZE RBC_ALIGN_UP(sizeof(rbc_arena_block_t))

/// @brief Create a new arena block
/// @param size Size of the block's data area
/// @return Pointer to new block, or NULL on failure
static rbc_arena_block_t *rbc_arena_new_block(size_t size)
{
    rbc_arena_block_t *block = malloc(RBC_BLOCK_HEADER_SIZE + size);
    if (!block)
    {
        return NULL;
    }
    block->size = size;
    block->used = 0;
    block->next = NULL;
    return block;
}

/// @brief Initialize an rbc_arena allocator
/// @param arena rbc_arena to initialize
/// @param initial_size Initial block size (will grow as needed)
/// @return true on success, false on failure
bool rbc_arena_init(rbc_arena_t *arena, size_t initial_size)
{
    if (!arena)
    {
        return false;
    }
    if (initial_size < RBC_BLOCK_SIZE)
    {
        initial_size = RBC_BLOCK_SIZE;
    }

    rbc_arena_block_t *block = rbc_arena_new_block(initial_size);
    if (!block)
    {
        arena->first = NULL;
        arena->current = NULL;
        return false;
    }
    arena->first = block;
    arena->current = block;
    arena->block_size = initial_size;
    arena->is_static = false;
    return true;
}

/// @brief Initialize an rbc_arena allocator with a static buffer (no malloc if fits)
/// @param arena rbc_arena to initialize
/// @param buffer Pointer to static buffer
/// @param size Size of the buffer
/// @return true on success, false on failure
bool rbc_arena_init_static(rbc_arena_t *arena, void *buffer, size_t size)
{
    if (!arena)
    {
        return false;
    }

    if (!buffer || size < RBC_BLOCK_HEADER_SIZE)
    {
        arena->first = NULL;
        arena->current = NULL;
        return false;
    }

    rbc_arena_block_t *block = (rbc_arena_block_t *)buffer;
    block->size = size - RBC_BLOCK_HEADER_SIZE;
    block->used = 0;
    block->next = NULL;

    arena->first = block;
    arena->current = block;
    arena->block_size = RBC_BLOCK_SIZE;
    arena->is_static = true;
    return true;
}

/// @brief Allocate memory from rbc_arena
/// @param arena rbc_arena to allocate from
/// @param size Number of bytes to allocate
/// @return Pointer to allocated memory, or NULL on failure
void *rbc_arena_alloc(rbc_arena_t *arena, size_t size)
{
    if (!arena || !arena->current || size == 0)
    {
        return NULL;
    }

    size = RBC_ALIGN_UP(size);
    unsigned char *block_data = (unsigned char *)arena->current + RBC_BLOCK_HEADER_SIZE;

    // Try current block
    if (arena->current->used + size <= arena->current->size)
    {
        void *ptr = &block_data[arena->current->used];
        arena->current->used += size;
        return ptr;
    }

    // Current block exhausted, look for usable next block (from previous reset)
    if (arena->current->next && size <= arena->current->next->size)
    {
        arena->current = arena->current->next;
        arena->current->used = size;
        return (unsigned char *)arena->current + RBC_BLOCK_HEADER_SIZE;
    }

    // Allocate new block
    size_t next_size = (size > arena->block_size) ? size : arena->block_size;
    rbc_arena_block_t *block = rbc_arena_new_block(next_size);
    if (!block)
        return NULL;

    block->used = size;
    block->next = arena->current->next;
    arena->current->next = block;
    arena->current = block;
    return (unsigned char *)block + RBC_BLOCK_HEADER_SIZE;
}

/// @brief Duplicate memory using rbc_arena memory
/// @param arena rbc_arena to allocate from
/// @param ptr Pointer to memory to duplicate
/// @param size Size of memory to duplicate
/// @return Pointer to duplicated memory, or NULL on failure
void *rbc_arena_memdup(rbc_arena_t *arena, const void *ptr, size_t size)
{
    if (!ptr || size == 0)
    {
        return NULL;
    }
    void *dup = rbc_arena_alloc(arena, size);
    if (dup)
    {
        memcpy(dup, ptr, size);
    }
    return dup;
}

/// @brief Duplicate string using rbc_arena memory
/// @param arena rbc_arena to allocate from
/// @param str String to duplicate
/// @return Pointer to duplicated string, or NULL on failure
char *rbc_arena_strdup(rbc_arena_t *arena, const char *str)
{
    if (!str)
    {
        return NULL;
    }
    size_t len = strlen(str);
    return (char *)rbc_arena_memdup(arena, str, len + 1);
}

/// @brief Format string using rbc_arena memory
/// @param arena rbc_arena to allocate from
/// @param fmt Format string
/// @return Pointer to formatted string, or NULL on failure
char *rbc_arena_printf(rbc_arena_t *arena, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    // Determine required size
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (len < 0)
    {
        va_end(args);
        return NULL;
    }

    char *str = rbc_arena_alloc(arena, (size_t)len + 1);
    if (str)
    {
        vsnprintf(str, (size_t)len + 1, fmt, args);
    }

    va_end(args);
    return str;
}

/// @brief Reset the arena, freeing all allocations
/// @param arena rbc_arena to reset
void rbc_arena_reset(rbc_arena_t *arena)
{
    if (!arena)
    {
        return;
    }
    rbc_arena_block_t *block = arena->first;
    while (block)
    {
        block->used = 0;
        block = block->next;
    }
    arena->current = arena->first;
}

/// @brief Destroy the rbc_arena and free all memory
/// @param arena rbc_arena to destroy
void rbc_arena_destroy(rbc_arena_t *arena)
{
    if (!arena)
    {
        return;
    }

    rbc_arena_block_t *block = arena->first;

    // If it's a static arena, the first block is user-provided (stack/static) and shouldn't be freed.
    if (arena->is_static && block)
    {
        block = block->next;
    }

    while (block)
    {
        rbc_arena_block_t *next = block->next;
        free(block);
        block = next;
    }

    arena->first = NULL;
    arena->current = NULL;
    arena->is_static = false;
}
