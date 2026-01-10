#ifndef RBCGLOB_INTERNAL_ARENA_H
#define RBCGLOB_INTERNAL_ARENA_H

#include <stddef.h>
#include <stdarg.h>

/**
 * @brief Arena allocator for fast memory allocation without individual frees
 *
 * All allocations are freed together when arena is destroyed.
 * This eliminates malloc/free overhead for thousands of small allocations.
 */
typedef struct rbcglob_arena_block_s
{
    struct rbcglob_arena_block_s *next;
    size_t size;
    size_t used;
    unsigned char data[]; /* C99 Flexible Array Member */
} rbcglob_arena_block_t;

typedef struct rbcglob_arena_s
{
    rbcglob_arena_block_t *current;
    rbcglob_arena_block_t *first;
    size_t block_size;
} rbcglob_arena_t;

/**
 * @brief Initialize an rbcglob_arena allocator
 * @param arena rbcglob_arena to initialize
 * @param initial_size Initial block size (will grow as needed)
 */
void rbcglob_arena_init(rbcglob_arena_t *arena, size_t initial_size);

/**
 * @brief Allocate memory from rbcglob_arena
 * @param arena rbcglob_arena to allocate from
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *rbcglob_arena_alloc(rbcglob_arena_t *arena, size_t size);

/**
 * @brief Duplicate memory using rbcglob_arena memory
 * @param arena rbcglob_arena to allocate from
 * @param ptr Pointer to memory to duplicate
 * @param size Size of memory to duplicate
 * @return Pointer to duplicated memory, or NULL on failure
 */
void *rbcglob_arena_memdup(rbcglob_arena_t *arena, const void *ptr, size_t size);

/**
 * @brief Duplicate string using rbcglob_arena memory
 * @param arena rbcglob_arena to allocate from
 * @param str String to duplicate
 * @return Pointer to duplicated string, or NULL on failure
 */
char *rbcglob_arena_strdup(rbcglob_arena_t *arena, const char *str);

/**
 * @brief Format string using rbcglob_arena memory
 * @param arena rbcglob_arena to allocate from
 * @param fmt Format string
 * @return Pointer to formatted string, or NULL on failure
 */
char *rbcglob_arena_printf(rbcglob_arena_t *arena, const char *fmt, ...);

/**
 * @brief Reset rbcglob_arena (mark all memory as available without freeing)
 * @param arena rbcglob_arena to reset
 */
void rbcglob_arena_reset(rbcglob_arena_t *arena);

/**
 * @brief Destroy rbcglob_arena and free all memory
 * @param arena rbcglob_arena to destroy
 */
void rbcglob_arena_destroy(rbcglob_arena_t *arena);

#endif /* RBCGLOB_INTERNAL_ARENA_H */
