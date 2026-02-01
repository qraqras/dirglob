#ifndef RBC_INTERNAL_UTILS_H
#define RBC_INTERNAL_UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "rbc/rbc.h"

char *rbc_strdup(const char *str);

uint32_t rbc_next_codepoint(const char **p);

/// @brief Compare two characters with optional case folding
/// @param c1 character 1
/// @param c2 character 2
/// @param flags Matching flags (RBC_FNM_CASEFOLD)
/// @return true if characters match
static inline bool rbc_char_match(int c1, int c2, unsigned flags)
{
    if (c1 == c2)
        return true;

    if (!(flags & RBC_FNM_CASEFOLD))
        return false;

    if (c1 >= 'A' && c1 <= 'Z')
        c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
        c2 += 32;

    return c1 == c2;
}

#endif /* RBC_INTERNAL_UTILS_H */
