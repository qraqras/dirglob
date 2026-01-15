#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "rbc/rbc.h"
#include "internal.h"

/// @brief Precompiled fnmatch pattern structure
/// This is now a thin wrapper around the streaming implementation
struct rbc_fnmatch_pattern_s
{
    rbc_fnmatch_pattern_streaming_t *streaming;
};

/// @brief Precompile fnmatch pattern
/// @param pattern Pattern string to compile
/// @param flags Compilation flags
/// @return Pointer to precompiled pattern, or NULL on failure
rbc_fnmatch_pattern_t *rbc_fnmatch_compile(const char *pattern, unsigned int flags)
{
    if (!pattern)
    {
        return NULL;
    }

    rbc_fnmatch_pattern_t *p = malloc(sizeof(rbc_fnmatch_pattern_t));
    if (!p)
    {
        return NULL;
    }

    // Use streaming implementation for zero-allocation matching
    p->streaming = rbc_fnmatch_compile_streaming(pattern, flags);
    if (!p->streaming)
    {
        free(p);
        return NULL;
    }

    return p;
}

/// @brief Free precompiled fnmatch pattern
/// @param p Precompiled pattern to free
void rbc_fnmatch_pattern_free(rbc_fnmatch_pattern_t *p)
{
    if (!p)
    {
        return;
    }
    rbc_fnmatch_pattern_free_streaming(p->streaming);
    free(p);
}

/// @brief File::fnmatch implementation
/// @param pattern Pattern string to match
/// @param string String to match against
/// @param flags Matching flags
/// @return true if the string matches the pattern, false otherwise
/// @note Now uses zero-allocation streaming implementation
bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
    {
        return false;
    }

    // Use streaming implementation - zero heap allocation
    return rbc_fnmatch_streaming(pattern, string, flags);
}

/// @brief File::fnmatch implementation with precompiled pattern
/// @param p Precompiled pattern
/// @param string String to match against
/// @return true if the string matches the pattern, false otherwise
bool rbc_xfnmatch(const rbc_fnmatch_pattern_t *p, const char *string)
{
    if (!p || !string)
    {
        return false;
    }
    // Use streaming implementation with precompiled hints
    return rbc_xfnmatch_streaming(p->streaming, string);
}
