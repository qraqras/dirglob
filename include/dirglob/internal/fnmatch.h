#ifndef DIRGLOB_INTERNAL_FNMATCH_H
#define DIRGLOB_INTERNAL_FNMATCH_H

#include <dirglob/dirglob.h>

/**
 * @brief Pattern matching function compatible with fnmatch(3)
 *
 * @param pattern Glob pattern
 * @param string String to match against pattern
 * @param flags FNM_* flags
 * @return 0 if match, non-zero if no match
 */
int dirglob_fnmatch(const char *pattern, const char *string, unsigned flags);

#endif /* DIRGLOB_INTERNAL_FNMATCH_H */
