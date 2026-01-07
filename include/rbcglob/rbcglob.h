#ifndef RBCGLOB_RBCGLOB_H
#define RBCGLOB_RBCGLOB_H

/**
 * @file rbcglob.h
 * @brief Ruby-compatible file system glob library
 *
 * This header re-exports all public APIs from dir.h and file.h.
 * You can include this single header to access all functionality,
 * or include dir.h/file.h individually for specific modules.
 */

/**
 * @brief Library version string
 */
#define RBCGLOB_VERSION "0.1.0"

/**
 * @brief Get the library version string
 *
 * @return A null-terminated string containing the library version
 */
const char *rbcglob_version(void);

/* Re-export Dir class methods */
#include <rbcglob/dir.h>

/* Re-export File class methods */
#include <rbcglob/file.h>

#endif /* RBCGLOB_RBCGLOB_H */
