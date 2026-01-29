#include <stdio.h>
#include "rbc/rbc.h"

int main()
{
    const char *pattern = "*";
    const char *text = ".hidden";
    unsigned flags = 0;

    bool result = rbc_fnmatch(pattern, text, flags);
    printf("rbc_fnmatch(\"%s\", \"%s\", %u) = %s\n",
           pattern, text, flags, result ? "true" : "false");
    printf("Expected: false\n");

    return 0;
}
