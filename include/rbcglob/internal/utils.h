#ifndef RBCGLOB_INTERNAL_UTILS_H
#define RBCGLOB_INTERNAL_UTILS_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @file utils.h
 * @brief General-purpose internal utilities
 */

/**
 * @brief Duplicate a string
 *
 * General-purpose string duplication utility used throughout the library.
 *
 * @param str String to duplicate
 * @return Newly allocated string, or NULL on error (caller must free)
 */
char *rbcglob_strdup(const char *str);

#endif /* RBCGLOB_INTERNAL_UTILS_H */
