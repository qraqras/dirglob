#ifndef RBCGLOB_INTERNAL_DIR_H
#define RBCGLOB_INTERNAL_DIR_H

#include <stdbool.h>
#include <stddef.h>
#include <rbcglob/internal/common.h>

/**
 * @file dir.h
 * @brief Internal APIs for Dir class methods
 */

/**
 * @brief Get home directory path (Ruby Dir.home equivalent)
 *
 * Returns the home directory of the specified user, or the current user if NULL.
 * This is an internal API not exposed in the public headers.
 *
 * @param user Username (NULL or empty string for current user)
 * @return Newly allocated string, caller must free with free(), or NULL on error
 */
char *rbcglob_home_dir(const char *user);

#endif /* RBCGLOB_INTERNAL_DIR_H */
