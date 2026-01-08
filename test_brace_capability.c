#include <stdio.h>
#include <glob.h>

int main()
{
    glob_t g;
    // Try without GLOB_BRACE
    if (glob("test_{a,b}", 0, NULL, &g) == 0)
    {
        printf("Without GLOB_BRACE: Matches found (Unexpected if no file named 'test_{a,b}')\n");
        globfree(&g);
    }
    else
    {
        printf("Without GLOB_BRACE: No match (Expected behavior)\n");
    }

    // Try with GLOB_BRACE
    if (glob("test_{a,b}", GLOB_BRACE, NULL, &g) == 0)
    {
        printf("With GLOB_BRACE: Found %zu matches\n", g.gl_pathc);
        for (size_t i = 0; i < g.gl_pathc; i++)
            printf(" - %s\n", g.gl_pathv[i]);
        globfree(&g);
    }
    else
    {
        printf("With GLOB_BRACE: No match (Check if files exist)\n");
    }
    return 0;
}
