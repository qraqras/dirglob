#ifndef RBCGLOB_UTILS_H
#define RBCGLOB_UTILS_H

#include <stddef.h>
#include <stdbool.h>

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} rbcglob_str_list_t;

void rbcglob_str_list_init(rbcglob_str_list_t *list, size_t initial_cap);
void rbcglob_str_list_add(rbcglob_str_list_t *list, const char *str);
void rbcglob_str_list_free(rbcglob_str_list_t *list);

// Check if string contains brace expression
bool rbcglob_has_brace(const char *str);

// Check if string contains wildcard characters (*, ?, [, ])
bool rbcglob_has_wildcard(const char *str);

// Find the end of the current logical segment (handles brace nesting)
// Returns pointer to the '/' separator or null terminator.
const char *rbcglob_find_segment_end(const char *str);

// Expand braces in a single string.
// Returns a list of expanded strings.
// e.g. "a{b,c}d" -> ["abd", "acd"]
rbcglob_str_list_t rbcglob_brace_expand(const char *pattern);

// Calculate Longest Common Prefix/Suffix for a list of strings
char *rbcglob_compute_lcp(const char **strs, size_t count);
char *rbcglob_compute_lcs(const char **strs, size_t count);

#endif
