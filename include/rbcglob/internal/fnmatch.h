#ifndef DIRGLOB_INTERNAL_FNMATCH_H
#define DIRGLOB_INTERNAL_FNMATCH_H

#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/compiler.h>

/**
 * @brief Pattern matching function compatible with fnmatch(3)
 *
 * @param pattern Glob pattern
 * @param string String to match against pattern
 * @param flags RBCGLOB_FNM_* flags
 * @return true if match, false if no match
 */
bool rbcglob_fnmatch(const char *pattern, const char *string, unsigned flags);

/**
 * @brief Match a string against a compiled segment's tokens
 *
 * @param seg Compiled segment
 * @param str String to match
 * @param flags RBCGLOB_FNM_* flags
 * @return true if match, false if no match
 */
bool rbcglob_token_match_segment(const rbcglob_segment_t *seg, const char *str, unsigned flags);

/**
 * @brief Match a string against a single compiled pattern
 *
 * @param cp Compiled pattern
 * @param string String to match
 * @return true if match, false if no match
 */
bool rbcglob_fnmatch_pattern_compiled(const rbcglob_compiled_pattern_t *cp, const char *string);

#endif /* DIRGLOB_INTERNAL_FNMATCH_H */
