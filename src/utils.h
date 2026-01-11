#ifndef RBC_INTERNAL_UTILS_H
#define RBC_INTERNAL_UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "arena.h"

/**
 * @brief Duplicate a string
 */
char *rbc_strdup(const char *str);

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
    rbc_arena_t *arena; // Optional associated arena
} rbc_str_list_t;

// Initialize string list. If arena is provided, items array and strings will be allocated there.
void rbc_str_list_init(rbc_str_list_t *list, size_t initial_cap, rbc_arena_t *arena);
void rbc_str_list_add(rbc_str_list_t *list, const char *str);
void rbc_str_list_free(rbc_str_list_t *list);

// Check if string contains brace expression
bool rbc_has_brace(const char *str);

// Helper for fixed length pattern match (handles '?' but not '*')
bool rbc_match_fixed(const char *text, const char *pat, size_t len, bool casefold);

// Helper for finding fixed length pattern in text (like strstr but with '?')
const char *rbc_search_fixed(const char *text, const char *pat, const char *end_limit, bool casefold);

// Check if string contains wildcard characters (*, ?, [, ])
bool rbc_has_wildcard(const char *str);

/**
 * @brief Decode the next UTF-8 codepoint and advance the pointer
 *
 * @param p Pointer to the current character pointer. Will be updated.
 * @return The decoded Unicode code point, or the byte value if invalid UTF-8.
 *         Returns 0 at null terminator.
 */
uint32_t rbc_next_codepoint(const char **p);

// Brace expand using arena for allocations
rbc_str_list_t rbc_brace_expand(const char *pattern, rbc_arena_t *arena);

// Visitor callback for brace expansion
typedef void (*rbc_brace_visit_cb)(const char *path, void *arg);

// Expand braces and call callback for each result.
void rbc_brace_visit(const char *pattern, rbc_arena_t *arena, rbc_brace_visit_cb cb, void *arg);

// Find the end of the current logical segment (handles brace nesting)

// Returns pointer to the '/' separator or null terminator.
const char *rbc_find_segment_end(const char *str);

// Expand braces in a single string.
// Returns a list of expanded strings.
// e.g. "a{b,c}d" -> ["abd", "acd"]
// rbc_str_list_t rbc_brace_expand(const char *pattern);

// Calculate Longest Common Prefix/Suffix for a list of strings
#endif /* RBC_INTERNAL_UTILS_H */
