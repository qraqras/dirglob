#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define RBC_GLOB_MAX_PATH 4096

/// @brief Normalize glob pattern by folding continuous ** patterns
/// Ruby Dir.glob behavior: **/**/ → **/ (fold continuous recursive patterns)
/// Note: .**/ patterns are NOT normalized (.**/ ≠ .**/**/)
/// This prevents duplicate results in patterns like **/**/ or **/**/
static void rbc_normalize_pattern(const char *pattern, char *normalized, size_t max_len)
{
    const char *src = pattern;
    char *dst = normalized;
    char *end = normalized + max_len - 1;

    while (*src && dst < end)
    {
        // Check for **/ followed by another **/
        // But skip if it's a .**/ pattern (dot-prefixed recursive)
        bool is_doublestar_slash = (src[0] == '*' && src[1] == '*' && src[2] == '/');
        bool is_dot_doublestar = (dst > normalized && *(dst - 1) == '.' && is_doublestar_slash);

        if (is_doublestar_slash && !is_dot_doublestar)
        {
            // Found **/ (not .**/): copy it
            *dst++ = '*';
            *dst++ = '*';
            *dst++ = '/';
            src += 3;

            // Skip any following **/ patterns (fold continuous **)
            // But stop if we encounter .**/
            while (src[0] == '*' && src[1] == '*' && src[2] == '/' && dst < end)
            {
                // Check if this is .**/ (don't fold it)
                if (src > pattern && *(src - 1) == '.')
                    break;

                src += 3; // Skip the redundant **/
            }
        }
        else
        {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

int main(void)
{
    struct test_case
    {
        const char *input;
        const char *expected;
        const char *description;
    } tests[] = {
        // Should normalize (regular ** patterns)
        {"**/foo", "**/foo", "Single **/ - no change"},
        {"**/**/foo", "**/foo", "Double **/ - should normalize"},
        {"**/**/**/foo", "**/foo", "Triple **/ - should normalize"},
        {"**/dir1/**/foo", "**/dir1/**/foo", "** with literal - single ** each"},
        {"**/dir1/**/**/foo", "**/dir1/**/foo", "** with literal - normalize second **"},

        // Should NOT normalize (.**/ patterns)
        {".**/foo", ".**/foo", ".** pattern - no change"},
        {".**/**/foo", ".**/**/foo", ".** + ** - should NOT normalize"},
        {".**/.**/foo", ".**/.**/foo", "Double .** - should NOT normalize"},

        // Mixed patterns
        {"**/.hidden/**/foo", "**/.hidden/**/foo", "** + .hidden + ** - normalize first **"},
        {".hidden/**/**/foo", ".hidden/**/foo", ".hidden + ** - normalize (not .**)"},

        // Edge cases
        {"foo/**/bar", "foo/**/bar", "Simple ** in middle"},
        {"foo/**/**/bar", "foo/**/bar", "Double ** in middle - normalize"},
        {"**/**/**/**/**/foo", "**/foo", "Many ** - fold all"},
    };

    int passed = 0;
    int failed = 0;

    printf("Testing rbc_normalize_pattern()\n");
    printf("=================================\n\n");

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        char result[RBC_GLOB_MAX_PATH];
        rbc_normalize_pattern(tests[i].input, result, sizeof(result));

        bool success = (strcmp(result, tests[i].expected) == 0);

        printf("Test %zu: %s\n", i + 1, tests[i].description);
        printf("  Input:    %s\n", tests[i].input);
        printf("  Expected: %s\n", tests[i].expected);
        printf("  Got:      %s\n", result);
        printf("  Status:   %s\n", success ? "✅ PASS" : "❌ FAIL");
        printf("\n");

        if (success)
        {
            passed++;
        }
        else
        {
            failed++;
        }
    }

    printf("=================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);

    return (failed > 0) ? 1 : 0;
}
