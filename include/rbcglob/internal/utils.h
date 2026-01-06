#ifndef RBCGLOB_INTERNAL_UTILS_H
#define RBCGLOB_INTERNAL_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#include <rbcglob/internal/arena.h>

/**
 * @brief Check if pattern contains any glob metacharacters
 */
bool rbcglob_has_glob_pattern(const char *str);

/**
 * @brief Duplicate a string
 */
char *rbcglob_strdup(const char *str);

/**
 * @brief Join two path components using arena allocation
 */
char *rbcglob_path_join_arena(rbcglob_arena_t *arena, const char *base, const char *name);

/**
 * @brief Join two path components
 * @return Newly allocated string, caller must free
 */
char *rbcglob_path_join(const char *base, const char *name);

/**
 * @brief Expand brace expressions in a pattern
 * @param pattern Input pattern with braces like "{a,b}.txt"
 * @param expanded Output array of expanded patterns
 * @param count Number of expanded patterns
 * @return 0 on success, -1 on error
 */
int rbcglob_brace_expand(const char *pattern, char ***expanded, size_t *count);

/**
 * @brief Compare two paths using Ruby-style sorting rules
 * @return <0 if s1 < s2, 0 if s1 == s2, >0 if s1 > s2
 */
int rbcglob_compare_paths(const char *s1, const char *s2);

#endif /* RBCGLOB_INTERNAL_UTILS_H */

/**
 * @brief Expand tilde (~) in path using arena allocation
 * Supports ~/ and ~user/ formats.
 */
char *rbcglob_expand_tilde_arena(rbcglob_arena_t *arena, const char *path);
