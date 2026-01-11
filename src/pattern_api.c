#include <stdlib.h>
#include <rbc/rbc.h>
#include "internal.h"
#include "arena.h"

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

    rbc_arena_init(&p->arena, 0); // Default block size

    rbc_matcher_build(&p->arena, &p->matcher, pattern, flags);
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
