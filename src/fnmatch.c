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
    if (!rbc_matcher_build(&arena, &m, pattern, flags))
    {
        rbc_arena_destroy(&arena);
        return false;
    }

    bool result = rbc_matcher_exec(&m, string, flags);

    rbc_arena_destroy(&arena);
    return result;
}

/* --- Pre-compiled fnmatch API --- */

struct rbc_fnmatch_pattern_s
{
    rbc_arena_t arena;
    rbc_matcher_t matcher;
    unsigned int flags;
};

rbc_fnmatch_pattern_t *rbc_fnmatch_compile(const char *pattern, unsigned int flags)
{
    if (!pattern)
        return NULL;

    rbc_fnmatch_pattern_t *p = malloc(sizeof(rbc_fnmatch_pattern_t));
    if (!p)
        return NULL;

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

bool rbc_xfnmatch(const rbc_fnmatch_pattern_t *p, const char *string)
{
    if (!p || !string)
        return false;
    return rbc_matcher_exec(&p->matcher, string, p->flags);
}

void rbc_fnmatch_pattern_free(rbc_fnmatch_pattern_t *p)
{
    if (!p)
        return;
    rbc_arena_destroy(&p->arena);
    free(p);
}
