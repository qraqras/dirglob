#ifndef DIRGLOB_INTERNAL_UTILS_H
#define DIRGLOB_INTERNAL_UTILS_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Check if pattern contains any glob metacharacters
 */
bool has_glob_pattern(const char *str);

/**
 * @brief Duplicate a string
 */
char *dirglob_strdup(const char *str);

/**
 * @brief Join two path components
 * @return Newly allocated string, caller must free
 */
char *path_join(const char *base, const char *name);

/**
 * @brief Expand brace expressions in a pattern
 * @param pattern Input pattern with braces like "{a,b}.txt"
 * @param expanded Output array of expanded patterns
 * @param count Number of expanded patterns
 * @return 0 on success, -1 on error
 */
int expand_braces(const char *pattern, char ***expanded, size_t *count);

#endif /* DIRGLOB_INTERNAL_UTILS_H */
