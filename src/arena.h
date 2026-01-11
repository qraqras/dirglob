#ifndef RBC_INTERNAL_ARENA_H
#define RBC_INTERNAL_ARENA_H

#include <stddef.h>
#include <stdarg.h>

/**
 * @brief Arena allocator for fast memory allocation without individual frees
 *
 * All allocations are freed together when arena is destroyed.
 * This eliminates malloc/free overhead for thousands of small allocations.
 */
typedef struct rbc_arena_block_s
{
    struct rbc_arena_block_s *next;
    size_t size;
    size_t used;
    unsigned char data[]; /* C99 Flexible Array Member */
} rbc_arena_block_t;

typedef struct rbc_arena_s
{
    rbc_arena_block_t *current;
    rbc_arena_block_t *first;
    size_t block_size;
    unsigned int flags; // 1 = using static buffer matching first block
} rbc_arena_t;

/**
 * @brief Initialize an rbc_arena allocator
 * @param arena rbc_arena to initialize
 * @param initial_size Initial block size (will grow as needed)
 */
void rbc_arena_init(rbc_arena_t *arena, size_t initial_size);

/**
 * @brief Initialize an rbc_arena allocator with a static buffer (no malloc if fits)
 * @param arena rbc_arena to initialize
 * @param buffer Pointer to static buffer
 * @param size Size of the buffer
 */
void rbc_arena_init_static(rbc_arena_t *arena, void *buffer, size_t size);

/**
 * @brief Allocate memory from rbc_arena
 * @param arena rbc_arena to allocate from
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *rbc_arena_alloc(rbc_arena_t *arena, size_t size);

/**
 * @brief Duplicate memory using rbc_arena memory
 * @param arena rbc_arena to allocate from
 * @param ptr Pointer to memory to duplicate
 * @param size Size of memory to duplicate
 * @return Pointer to duplicated memory, or NULL on failure
 */
void *rbc_arena_memdup(rbc_arena_t *arena, const void *ptr, size_t size);

/**
 * @brief Duplicate string using rbc_arena memory
 * @param arena rbc_arena to allocate from
 * @param str String to duplicate
 * @return Pointer to duplicated string, or NULL on failure
 */
char *rbc_arena_strdup(rbc_arena_t *arena, const char *str);

/**
 * @brief Format string using rbc_arena memory
 * @param arena rbc_arena to allocate from
 * @param fmt Format string
 * @return Pointer to formatted string, or NULL on failure
 */
char *rbc_arena_printf(rbc_arena_t *arena, const char *fmt, ...);

/**
 * @brief Reset rbc_arena (mark all memory as available without freeing)
 * @param arena rbc_arena to reset
 */
void rbc_arena_reset(rbc_arena_t *arena);

/**
 * @brief Destroy rbc_arena and free all memory
 * @param arena rbc_arena to destroy
 */
void rbc_arena_destroy(rbc_arena_t *arena);

#endif /* RBC_INTERNAL_ARENA_H */
