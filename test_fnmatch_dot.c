#include <stdio.h>
#include <stdbool.h>
#include "rbc.h"

int main(void)
{
    printf("Testing fnmatch directly:\n");
    printf("fnmatch(\"*\", \".\", 0) = %s\n",
           rbc_fnmatch("*", ".", 0) ? "true" : "false");
    printf("fnmatch(\"*\", \".\", DOTMATCH) = %s\n",
           rbc_fnmatch("*", ".", RBC_FNM_DOTMATCH) ? "true" : "false");
    printf("fnmatch(\".*\", \".\", 0) = %s\n",
           rbc_fnmatch(".*", ".", 0) ? "true" : "false");

    return 0;
}
