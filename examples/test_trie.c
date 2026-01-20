/**
 * @file test_trie.c
 * @brief Test program for complete trie-based glob
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <rbc/rbc.h>

// Create test directory structure
static void create_test_structure(void)
{
    system("rm -rf /tmp/trie_test && mkdir -p /tmp/trie_test");
    
    // Create directories
    mkdir("/tmp/trie_test/src", 0755);
    mkdir("/tmp/trie_test/src/core", 0755);
    mkdir("/tmp/trie_test/src/utils", 0755);
    mkdir("/tmp/trie_test/lib", 0755);
    mkdir("/tmp/trie_test/lib/common", 0755);
    mkdir("/tmp/trie_test/include", 0755);
    
    // Create files
    system("touch /tmp/trie_test/src/main.c");
    system("touch /tmp/trie_test/src/main.h");
    system("touch /tmp/trie_test/src/core/glob.c");
    system("touch /tmp/trie_test/src/core/glob.h");
    system("touch /tmp/trie_test/src/core/fnmatch.c");
    system("touch /tmp/trie_test/src/utils/arena.c");
    system("touch /tmp/trie_test/src/utils/arena.h");
    system("touch /tmp/trie_test/lib/libfoo.c");
    system("touch /tmp/trie_test/lib/libfoo.h");
    system("touch /tmp/trie_test/lib/common/helper.c");
    system("touch /tmp/trie_test/lib/common/helper.h");
    system("touch /tmp/trie_test/include/rbc.h");
    system("touch /tmp/trie_test/README.md");
}

static void print_results(const char *label, char **results, size_t count)
{
    printf("\n%s (%zu results):\n", label, count);
    for (size_t i = 0; i < count; i++) {
        printf("  [%zu] %s\n", i, results[i]);
    }
}

// Compare regular glob vs trie glob
static void compare_glob_trie(const char **patterns, size_t npatterns, const char *base)
{
    char **results_glob = NULL;
    char **results_trie = NULL;
    size_t count_glob = 0;
    size_t count_trie = 0;
    
    printf("\n========================================\n");
    printf("Patterns (%zu):\n", npatterns);
    for (size_t i = 0; i < npatterns; i++) {
        printf("  [%zu] %s\n", i, patterns[i]);
    }
    printf("Base: %s\n", base ? base : "(null)");
    printf("========================================\n");
    
    // Run regular glob
    if (rbc_glob(patterns, npatterns, 0, base, true, &results_glob, &count_glob, NULL)) {
        print_results("rbc_glob (regular)", results_glob, count_glob);
        rbc_glob_free(results_glob, count_glob, NULL);
    } else {
        printf("rbc_glob failed!\n");
    }
    
    // Run trie glob
    if (rbc_glob_trie(patterns, npatterns, 0, base, true, &results_trie, &count_trie, NULL)) {
        print_results("rbc_glob_trie", results_trie, count_trie);
        rbc_glob_free(results_trie, count_trie, NULL);
    } else {
        printf("rbc_glob_trie failed!\n");
    }
}

int main(void)
{
    printf("=== Complete Trie Glob Test ===\n");
    
    create_test_structure();
    
    // Test 1: Shared prefix patterns
    {
        const char *patterns[] = {
            "src/*.c",
            "src/*.h"
        };
        compare_glob_trie(patterns, 2, "/tmp/trie_test");
    }
    
    // Test 2: Multiple directories with same suffix
    {
        const char *patterns[] = {
            "src/*.c",
            "lib/*.c"
        };
        compare_glob_trie(patterns, 2, "/tmp/trie_test");
    }
    
    // Test 3: Recursive patterns with shared prefix
    {
        const char *patterns[] = {
            "src/**/*.c",
            "src/**/*.h"
        };
        compare_glob_trie(patterns, 2, "/tmp/trie_test");
    }
    
    // Test 4: Brace expansion (should be optimized by trie)
    {
        const char *patterns[] = {
            "{src,lib}/**/*.c"
        };
        compare_glob_trie(patterns, 1, "/tmp/trie_test");
    }
    
    // Test 5: Complex multi-pattern
    {
        const char *patterns[] = {
            "src/**/*.c",
            "src/**/*.h",
            "lib/**/*.c",
            "lib/**/*.h",
            "include/*.h"
        };
        compare_glob_trie(patterns, 5, "/tmp/trie_test");
    }
    
    // Cleanup
    system("rm -rf /tmp/trie_test");
    
    printf("\n=== Test Complete ===\n");
    return 0;
}
