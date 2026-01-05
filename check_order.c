
#include <stdio.h>
#include <stdlib.h>
#include <dirglob/dirglob.h>

int main()
{
    dirglob_t results;
    if (dirglob("*/file.txt", 0, &results) == 0)
    {
        for (size_t i = 0; i < results.gl_pathc; i++)
        {
            printf("%s\n", results.gl_pathv[i]);
        }
        dirglob_free(&results);
    }
    return 0;
}
