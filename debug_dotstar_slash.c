#include <stdio.h>
#include <stdlib.h>
#include "rbc/rbc.h"

int main()
{
    printf("Testing pattern: .**/.*/\n");
    printf("Flags: 0 (no DOTMATCH)\n\n");

    const char *patterns[] = {".**/.*/"};
    ;
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    bool ret = rbc_glob(patterns, 1, 0, NULL, true, &results, &count, &lengths);

    if (ret)
    {
        printf("Found %zu matches:\n", count);
        for (size_t i = 0; i < count; i++)
        {
            printf("  %s\n", results[i]);
        }
        rbc_glob_free(results, count, lengths);
    }
    else
    {
        printf("Error in glob\n");
    }

    printf("\n\nExpected (Ruby output):\n");
    printf("  ./.hidden/\n");
    printf("  .hidden/.subhidden0/\n");
    printf("  .hidden/.subhidden1/\n");
    printf("  .hidden/.subhidden2/\n");

    return 0;
}
