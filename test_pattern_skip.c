#include <stdio.h>
#include <string.h>

int main()
{
    const char *remaining_pattern = "/.* ";

    printf("remaining_pattern: '%s'\n", remaining_pattern);

    const char *next_pattern = remaining_pattern;
    if (*next_pattern == '/')
        next_pattern++;

    printf("next_pattern after skip: '%s'\n", next_pattern);

    if (*next_pattern != '\0')
    {
        printf("next_pattern is NOT empty, should call rbc_glob_match\n");
    }
    else
    {
        printf("next_pattern is empty, will NOT call rbc_glob_match\n");
    }

    return 0;
}
