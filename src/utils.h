#ifndef RBCGLOB_INTERNAL_UTILS_H
#define RBCGLOB_INTERNAL_UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "arena.h"

/**
 * @brief Duplicate a string
 */
char *rbcglob_strdup(const char *str);

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
    rbcglob_arena_t *arena; // Optional associated arena
} rbcglob_str_list_t;

// Initialize string list. If arena is provided, items array and strings will be allocated there.
void rbcglob_str_list_init(rbcglob_str_list_t *list, size_t initial_cap, rbcglob_arena_t *arena);
void rbcglob_str_list_add(rbcglob_str_list_t *list, const char *str);
void rbcglob_str_list_free(rbcglob_str_list_t *list);

// Check if string contains brace expression
bool rbcglob_has_brace(const char *str);

// Check if string contains wildcard characters (*, ?, [, ])
bool rbcglob_has_wildcard(const char *str);

/**
 * @brief Decode the next UTF-8 codepoint and advance the pointer
 *
 * @param p Pointer to the current character pointer. Will be updated.
 * @return The decoded Unicode code point, or the byte value if invalid UTF-8.
 *         Returns 0 at null terminator.
 */
uint32_t rbcglob_next_codepoint(const char **p);

// Brace expand using arena for allocations
rbcglob_str_list_t rbcglob_brace_expand(const char *pattern, rbcglob_arena_t *arena);

// Visitor callback for brace expansion
typedef void (*rbcglob_brace_visit_cb)(const char *path, void *arg);

// Expand braces and call callback for each result.
void rbcglob_brace_visit(const char *pattern, rbcglob_arena_t *arena, rbcglob_brace_visit_cb cb, void *arg);

// Find the end of the current logical segment (handles brace nesting)

// Returns pointer to the '/' separator or null terminator.
const char *rbcglob_find_segment_end(const char *str);

// Expand braces in a single string.
// Returns a list of expanded strings.
// e.g. "a{b,c}d" -> ["abd", "acd"]
// rbcglob_str_list_t rbcglob_brace_expand(const char *pattern);

// Calculate Longest Common Prefix/Suffix for a list of strings
#endif /* RBCGLOB_INTERNAL_UTILS_H */
