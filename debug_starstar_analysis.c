#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int rbc_glob(const char **patterns, size_t npatterns, unsigned flags, const char *base, int sort, char ***out, size_t *count, size_t **lengths);
extern void rbc_glob_free(char **list, size_t count, size_t *lengths);

int main()
{
    printf("=== Analyzing **/.* pattern ===\n\n");

    // Test with different flag combinations
    struct
    {
        const char *name;
        unsigned flags;
    } tests[] = {
        {"No flags (0)", 0},
        {"FNM_DOTMATCH (0x04)", 0x04},
        {"FNM_PATHNAME (0x02)", 0x02},
        {"FNM_PATHNAME | FNM_DOTMATCH", 0x02 | 0x04},
    };

    for (int i = 0; i < 4; i++)
    {
        char **results = NULL;
        size_t count = 0;
        size_t *lengths = NULL;
        const char *pattern = "**/.*";

        rbc_glob(&pattern, 1, tests[i].flags, NULL, 1, &results, &count, &lengths);

        printf("Test %d: %s\n", i + 1, tests[i].name);
        printf("  Matches: %zu\n", count);
        if (count > 0 && count <= 5)
        {
            printf("  First few: ");
            for (size_t j = 0; j < count && j < 5; j++)
            {
                printf("%s%s", results[j], j < count - 1 && j < 4 ? ", " : "");
            }
            printf("\n");
        }

        // Check if "." is in results
        int has_dot = 0;
        for (size_t j = 0; j < count; j++)
        {
            if (strcmp(results[j], ".") == 0)
            {
                has_dot = 1;
                printf("  Contains '.': YES (at index %zu)\n", j);
                break;
            }
        }
        if (!has_dot && count > 0)
        {
            printf("  Contains '.': NO\n");
        }
        printf("\n");

        rbc_glob_free(results, count, lengths);
    }

    // Now trace what happens
    printf("=== Expected behavior analysis ===\n\n");
    printf("Pattern: **/.*\n");
    printf("  Segment 1: '**' (RECURSIVE, starts_with_dot=false)\n");
    printf("  Separator: '/'\n");
    printf("  Segment 2: '.*' (MAGICAL, starts_with_dot=true)\n");
    printf("\nRuby behavior (41 matches, NO '.'):\n");
    printf("  - ** doesn't descend into dot-directories (no DOTMATCH)\n");
    printf("  - .* matches dotfiles in current directory only\n");
    printf("  - '.' entry is never matched in glob operations\n");
    printf("\nCurrent behavior:\n");
    printf("  - See above test results\n");

    return 0;
}
