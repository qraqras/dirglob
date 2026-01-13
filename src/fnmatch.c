#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "rbc/rbc.h"
#include "internal.h"

/// @brief Precompiled fnmatch pattern structure
struct rbc_fnmatch_pattern_s
{
    rbc_arena_t arena;
    rbc_matcher_t matcher;
    unsigned int flags;
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

    if (!rbc_arena_init(&p->arena, 0))
    {
        free(p);
        return NULL;
    }

    if (!rbc_matcher_build(&p->arena, &p->matcher, pattern, flags))
    {
        rbc_arena_destroy(&p->arena);
        free(p);
        return NULL;
    }
    p->flags = flags;

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
    rbc_arena_destroy(&p->arena);
    free(p);
}

/// @brief File::fnmatch implementation
/// @param pattern Pattern string to match
/// @param string String to match against
/// @param flags Matching flags
/// @return true if the string matches the pattern, false otherwise
bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
    {
        return false;
    }

    char stack_buf[PATH_MAX];
    rbc_arena_t arena;
    rbc_arena_init_static(&arena, stack_buf, sizeof(stack_buf));

    rbc_matcher_t matcher;
    if (!rbc_matcher_build(&arena, &matcher, pattern, flags))
    {
        rbc_arena_destroy(&arena);
        return false;
    }

    bool result = rbc_matcher_exec(&matcher, string);

    rbc_arena_destroy(&arena);
    return result;
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
    return rbc_matcher_exec(&p->matcher, string);
}
