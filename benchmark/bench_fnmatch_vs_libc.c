#include <rbcglob/rbcglob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include "../src/pattern.h" // For rbcglob_recursive_match

static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

void bench_pattern(const char *pat, const char *str, int iterations)
{
    printf("Pattern: [%s] String: [%s]\n", pat, str);

    // 1. Libc fnmatch
    double start = get_time_ms();
    int matches_libc = 0;
    for (int i = 0; i < iterations; i++)
    {
        if (fnmatch(pat, str, 0) == 0)
            matches_libc++;
    }
    double end = get_time_ms();
    printf("  libc fnmatch:    %8.4f ms (matches: %d)\n", end - start, matches_libc);

    // 2. rbcglob_recursive_match (Legacy)
    start = get_time_ms();
    int matches_legacy = 0;
    for (int i = 0; i < iterations; i++)
    {
        if (rbcglob_recursive_match(str, pat, 0))
            matches_legacy++;
    }
    end = get_time_ms();
    printf("  legacy rbcglob:  %8.4f ms (matches: %d)\n", end - start, matches_legacy);

    // 3. rbcglob_fnmatch (Optimized)
    start = get_time_ms();
    int matches_opt = 0;
    for (int i = 0; i < iterations; i++)
    {
        if (rbcglob_fnmatch(pat, str, 0))
            matches_opt++;
    }
    end = get_time_ms();
    printf("  opt rbcglob:     %8.4f ms (matches: %d)\n", end - start, matches_opt);
    printf("\n");
}

int main(void)
{
    int iter = 100000;
    printf("Iterations: %d\n\n", iter);

    // Case 1: Simple suffix
    bench_pattern("*.c", "super_long_file_name_checking_performance.c", iter);

    // Case 2: Matching middle
    bench_pattern("*foo*", "prefix_foo_suffix", iter);

    // Case 3: Pattern Chain
    bench_pattern("a?b*c", "aXb_something_c", iter);

    // Case 4: Long mismatch (Worst case for naive?)
    // "a*b*c*d" matching string that fails at d
    bench_pattern("a*b*c*d", "a_very_long_string_with_b_and_c_but_no_match_at_end_X", iter);

    // Case 5: Pathological backtracking case (if recursive is bad)
    // a*a*a*a*b matching aaaaa...aaaaa
    bench_pattern("a*a*a*a*b", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaa", iter);

    return 0;
}
