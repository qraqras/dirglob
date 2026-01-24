#include <stdio.h>
#include <rbc/rbc.h>

int main()
{
    // Test fnmatch with DOTMATCH
    const char *patterns[] = {
        "*",  // Should match .hidden with DOTMATCH
        ".*", // Should always match .hidden
    };

    const char *names[] = {
        ".hidden",
        "visible",
    };

    printf("Testing fnmatch:\n");
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            bool match_no_dot = rbc_fnmatch(patterns[i], names[j], RBC_FNM_PATHNAME);
            bool match_with_dot = rbc_fnmatch(patterns[i], names[j], RBC_FNM_PATHNAME | RBC_FNM_DOTMATCH);
            printf("  '%s' vs '%s': no_dotmatch=%d, with_dotmatch=%d\n",
                   patterns[i], names[j], match_no_dot, match_with_dot);
        }
    }

    printf("\nTesting glob:\n");
    char **results = NULL;
    size_t count = 0;
    size_t *lengths = NULL;

    // Test glob with DOTMATCH
    const char *glob_patterns[] = {"*/"};
    bool ret = rbc_glob(glob_patterns, 1, RBC_FNM_DOTMATCH, NULL, true, &results, &count, &lengths);

    printf("  glob('*/', DOTMATCH) returned %zu results\n", count);
    for (size_t i = 0; i < count && i < 5; i++)
    {
        printf("    %zu: %s\n", i, results[i]);
    }

    rbc_glob_free(results, count, lengths);
    return 0;
}
