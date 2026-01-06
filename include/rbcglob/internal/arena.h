#ifndef RBCGLOB_INTERNAL_ARENA_H
#define RBCGLOB_INTERNAL_ARENA_H

#include <stddef.h>

/**
 * @brief Arena allocator for fast memory allocation without individual frees
 *
 * All allocations are freed together when arena is destroyed.
 * This eliminates malloc/free overhead for thousands of small allocations.
 */

typedef struct arena_block_s
{
    char *data;
    size_t size;
    size_t used;
    struct arena_block_s *next;
} arena_block_t;

typedef struct
{
    arena_block_t *current;
    arena_block_t *first;
    size_t block_size;
} arena_t;

/**
 * @brief Initialize an arena allocator
 * @param arena Arena to initialize
 * @param initial_size Initial block size (will grow as needed)
 */
void arena_init(arena_t *arena, size_t initial_size);

/**
 * @brief Allocate memory from arena
 * @param arena Arena to allocate from
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *arena_alloc(arena_t *arena, size_t size);

/**
 * @brief Duplicate string using arena memory
 * @param arena Arena to allocate from
 * @param str String to duplicate
 * @return Pointer to duplicated string, or NULL on failure
 */
char *arena_strdup(arena_t *arena, const char *str);

/**
 * @brief Reset arena (mark all memory as available without freeing)
 * @param arena Arena to reset
 */
void arena_reset(arena_t *arena);

/**
 * @brief Destroy arena and free all memory
 * @param arena Arena to destroy
 */
void arena_destroy(arena_t *arena);

#endif /* RBCGLOB_INTERNAL_ARENA_H */
