#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>

#include "rbcglob/rbcglob.h"
#include "rbcglob/internal/pattern.h"
#include "rbcglob/internal/utils.h"

bool rbcglob_fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
        return false;

    // Call the shared (recursive) matching engine
    return rbcglob_vm_match(string, pattern, flags);
}
