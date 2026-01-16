#include <stdio.h>
#include "rbc/rbc.h"

int main()
{
    const char *pattern = "**/.*";
    const char *path = "a/b/.hidden";
    unsigned flags = 0x02; // RBC_FNM_PATHNAME

    printf("Pattern: %s\n", pattern);
    printf("Path: %s\n", path);
    printf("Flags: 0x%02x\n", flags);

    bool result = rbc_fnmatch(pattern, path, flags);
    printf("Result: %s\n", result ? "MATCH" : "NOMATCH");
    printf("Expected: MATCH\n");

    return result ? 0 : 1;
}
