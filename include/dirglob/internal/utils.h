#ifndef DIRGLOB_INTERNAL_UTILS_H
#define DIRGLOB_INTERNAL_UTILS_H

#include <stdbool.h>

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

#endif /* DIRGLOB_INTERNAL_UTILS_H */
