/**
 * @file test_walker_v2.c
 * @brief Test recursive walker (v2) implementation
 */

#include "internal.h"
#include "rbc/rbc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TEST_DIR "test_walker_v2"

static int test_count = 0;
static int pass_count = 0;

typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
} result_collector_t;

static void collect_result(const char *path, void *userdata)
{
    result_collector_t *collector = (result_collector_t *)userdata;
    
    if (collector->count >= collector->capacity) {
        collector->capacity = collector->capacity ? collector->capacity * 2 : 16;
        collector->paths = realloc(collector->paths, 
                                   collector->capacity * sizeof(char *));
    }
    
    collector->paths[collector->count++] = strdup(path);
}

static void free_results(result_collector_t *collector)
{
    for (size_t i = 0; i < collector->count; i++) {
        free(collector->paths[i]);
    }
    free(collector->paths);
    collector->paths = NULL;
    collector->count = 0;
    collector->capacity = 0;
}

static void setup_test_files(void)
{
    (void)system("rm -rf " TEST_DIR);
    (void)system("mkdir -p " TEST_DIR "/src/core");
    (void)system("mkdir -p " TEST_DIR "/src/util");
    (void)system("mkdir -p " TEST_DIR "/test");
    (void)system("mkdir -p " TEST_DIR "/docs");
    
    (void)system("touch " TEST_DIR "/file1.txt");
    (void)system("touch " TEST_DIR "/file2.c");
    (void)system("touch " TEST_DIR "/file3.h");
    (void)system("touch " TEST_DIR "/src/main.c");
    (void)system("touch " TEST_DIR "/src/utils.c");
    (void)system("touch " TEST_DIR "/src/core/engine.c");
    (void)system("touch " TEST_DIR "/src/core/parser.c");
    (void)system("touch " TEST_DIR "/src/util/helper.c");
    (void)system("touch " TEST_DIR "/test/test1.c");
    (void)system("touch " TEST_DIR "/test/test2.c");
    (void)system("touch " TEST_DIR "/docs/readme.txt");
}

static void cleanup_test_files(void)
{
    (void)system("rm -rf " TEST_DIR);
}

static rbc_segment_t* create_literal_segment(const char *literal, rbc_segment_t *next)
{
    rbc_segment_t *seg = malloc(sizeof(rbc_segment_t));
    memset(seg, 0, sizeof(rbc_segment_t));
    seg->type = RBC_SEGMENT_LITERAL;
    seg->data.literal = strdup(literal);
    seg->next = next;
    return seg;
}

static rbc_segment_t* create_wildcard_segment(const char *pattern, rbc_segment_t *next)
{
    rbc_segment_t *seg = malloc(sizeof(rbc_segment_t));
    memset(seg, 0, sizeof(rbc_segment_t));
    seg->type = RBC_SEGMENT_WILDCARD;
    seg->data.glob.original_pattern = strdup(pattern);
    seg->data.glob.compiled = NULL;
    seg->data.glob.alternatives = NULL;
    seg->next = next;
    return seg;
}

static rbc_segment_t* create_recursive_segment(rbc_segment_t *next)
{
    rbc_segment_t *seg = malloc(sizeof(rbc_segment_t));
    memset(seg, 0, sizeof(rbc_segment_t));
    seg->type = RBC_SEGMENT_RECURSIVE;
    seg->next = next;
    return seg;
}

static void free_segments(rbc_segment_t *seg)
{
    while (seg) {
        rbc_segment_t *next = seg->next;
        
        if (seg->type == RBC_SEGMENT_LITERAL) {
            free(seg->data.literal);
        } else if (seg->type == RBC_SEGMENT_WILDCARD) {
            free(seg->data.glob.original_pattern);
        }
        
        free(seg);
        seg = next;
    }
}

static bool contains_path(result_collector_t *collector, const char *path)
{
    for (size_t i = 0; i < collector->count; i++) {
        if (strcmp(collector->paths[i], path) == 0) {
            return true;
        }
    }
    return false;
}

static void test_literal_path(void)
{
    test_count++;
    printf("Test %d: Literal path...", test_count);
    
    /* test_walker_v2/file1.txt */
    rbc_segment_t *seg = create_literal_segment(TEST_DIR, 
                            create_literal_segment("file1.txt", NULL));
    
    result_collector_t collector = {0};
    bool ok = rbc_glob_walk(seg, collect_result, &collector, 0, false);
    
    if (ok && collector.count == 1 && 
        strcmp(collector.paths[0], TEST_DIR "/file1.txt") == 0) {
        printf(" PASS\n");
        pass_count++;
    } else {
        printf(" FAIL (got %zu results)\n", collector.count);
    }
    
    free_results(&collector);
    free_segments(seg);
}

static void test_simple_wildcard(void)
{
    test_count++;
    printf("Test %d: Simple wildcard (*.txt)...", test_count);
    
    /* test_walker_v2/*.txt */
    rbc_segment_t *seg = create_literal_segment(TEST_DIR,
                            create_wildcard_segment("*.txt", NULL));
    
    result_collector_t collector = {0};
    bool ok = rbc_glob_walk(seg, collect_result, &collector, 0, true);
    
    if (ok && collector.count == 1 &&
        contains_path(&collector, TEST_DIR "/file1.txt")) {
        printf(" PASS\n");
        pass_count++;
    } else {
        printf(" FAIL (got %zu results)\n", collector.count);
        for (size_t i = 0; i < collector.count; i++) {
            printf("  [%zu] %s\n", i, collector.paths[i]);
        }
    }
    
    free_results(&collector);
    free_segments(seg);
}

static void test_wildcard_all_c_files(void)
{
    test_count++;
    printf("Test %d: Wildcard (*.c)...", test_count);
    
    /* test_walker_v2/*.c */
    rbc_segment_t *seg = create_literal_segment(TEST_DIR,
                            create_wildcard_segment("*.c", NULL));
    
    result_collector_t collector = {0};
    bool ok = rbc_glob_walk(seg, collect_result, &collector, 0, true);
    
    if (ok && collector.count == 1 &&
        contains_path(&collector, TEST_DIR "/file2.c")) {
        printf(" PASS\n");
        pass_count++;
    } else {
        printf(" FAIL (got %zu results)\n", collector.count);
    }
    
    free_results(&collector);
    free_segments(seg);
}

static void test_nested_wildcard(void)
{
    test_count++;
    printf("Test %d: Nested wildcard (src/ *.c)...", test_count);
    
    /* test_walker_v2/src/*.c */
    rbc_segment_t *seg = create_literal_segment(TEST_DIR,
                            create_literal_segment("src",
                                create_wildcard_segment("*.c", NULL)));
    
    result_collector_t collector = {0};
    bool ok = rbc_glob_walk(seg, collect_result, &collector, 0, true);
    
    if (ok && collector.count == 2 &&
        contains_path(&collector, TEST_DIR "/src/main.c") &&
        contains_path(&collector, TEST_DIR "/src/utils.c")) {
        printf(" PASS\n");
        pass_count++;
    } else {
        printf(" FAIL (got %zu results, expected 2)\n", collector.count);
        for (size_t i = 0; i < collector.count; i++) {
            printf("  [%zu] %s\n", i, collector.paths[i]);
        }
    }
    
    free_results(&collector);
    free_segments(seg);
}

static void test_recursive_glob(void)
{
    test_count++;
    printf("Test %d: Recursive glob (**/ *.c)...", test_count);
    
    /* test_walker_v2 / ** / *.c */
    rbc_segment_t *seg3 = create_literal_segment(TEST_DIR,
                            create_recursive_segment(
                                create_wildcard_segment("*.c", NULL)));
    
    result_collector_t collector = {0};
    bool ok = rbc_glob_walk(seg3, collect_result, &collector, 0, true);
    
    /* Should find: file2.c, src/main.c, src/utils.c, src/core/engine.c, 
       src/core/parser.c, src/util/helper.c, test/test1.c, test/test2.c */
    if (ok && collector.count == 8) {
        printf(" PASS (found %zu files)\n", collector.count);
        pass_count++;
    } else {
        printf(" FAIL (got %zu results, expected 8)\n", collector.count);
        for (size_t i = 0; i < collector.count; i++) {
            printf("  [%zu] %s\n", i, collector.paths[i]);
        }
    }
    
    free_results(&collector);
    free_segments(seg3);
}

static void test_prefix_pattern(void)
{
    test_count++;
    printf("Test %d: Prefix pattern (test*)...", test_count);
    
    /* test_walker_v2/test/test* */
    rbc_segment_t *seg = create_literal_segment(TEST_DIR,
                            create_literal_segment("test",
                                create_wildcard_segment("test*", NULL)));
    
    result_collector_t collector = {0};
    bool ok = rbc_glob_walk(seg, collect_result, &collector, 0, true);
    
    if (ok && collector.count == 2 &&
        contains_path(&collector, TEST_DIR "/test/test1.c") &&
        contains_path(&collector, TEST_DIR "/test/test2.c")) {
        printf(" PASS\n");
        pass_count++;
    } else {
        printf(" FAIL (got %zu results)\n", collector.count);
    }
    
    free_results(&collector);
    free_segments(seg);
}

static void test_suffix_pattern(void)
{
    test_count++;
    printf("Test %d: Suffix pattern (*.txt)...", test_count);
    
    /* test_walker_v2 / ** / *.txt */
    rbc_segment_t *seg7 = create_literal_segment(TEST_DIR,
                            create_recursive_segment(
                                create_wildcard_segment("*.txt", NULL)));
    
    result_collector_t collector = {0};
    bool ok = rbc_glob_walk(seg7, collect_result, &collector, 0, true);
    
    /* Should find: file1.txt, docs/readme.txt */
    if (ok && collector.count == 2) {
        printf(" PASS\n");
        pass_count++;
    } else {
        printf(" FAIL (got %zu results, expected 2)\n", collector.count);
    }
    
    free_results(&collector);
    free_segments(seg7);
}

int main(void)
{
    printf("========================================\n");
    printf("Walker v2 (Recursive) Tests\n");
    printf("========================================\n\n");
    
    setup_test_files();
    
    test_literal_path();
    test_simple_wildcard();
    test_wildcard_all_c_files();
    test_nested_wildcard();
    test_recursive_glob();
    test_prefix_pattern();
    test_suffix_pattern();
    
    cleanup_test_files();
    
    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", pass_count, test_count);
    printf("========================================\n");
    
    return (pass_count == test_count) ? 0 : 1;
}
