#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "rbc/rbc.h"
#include "internal.h"

// Optimized implementation using compiler and strategy-based matcher
bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
        return false;

    // Use stack memory for arena to avoid malloc overhead for most patterns
    char stack_buf[4096];
    rbc_arena_t arena;
    rbc_arena_init_static(&arena, stack_buf, sizeof(stack_buf));

    rbc_matcher_t m;

    // rbc_matcher_build handles parsing strategy (CHAIN, SUFFIX etc)
    rbc_matcher_build(&arena, &m, pattern, flags);

    bool result = rbc_matcher_exec(&m, string, flags);

    rbc_arena_destroy(&arena);
    return result;
}
