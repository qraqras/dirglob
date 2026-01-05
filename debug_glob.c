#include <dirglob/dirglob.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
    char **results;
    size_t count;
    if (dirglob((const char *[]){"**/{a,b}.txt"}, 1, 0, NULL, 1, &results, &count))
    {
        for (size_t i = 0; i < count; i++)
        {
            printf("%s\n", results[i]);
        }
        dirglob_free(results, count);
    }
    return 0;
}
