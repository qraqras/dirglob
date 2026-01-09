#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/graph.h>
#include <rbcglob/internal/traverse.h>
#include <rbcglob/internal/utils.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h> // For snprintf if needed

/* Defines the opaque struct from types.h */
struct rbcglob_compiled_glob_s
{
    rbcglob_ctx_t *ctx;
    rbcglob_segment_t *graph;
    unsigned flags; /* Store flags during compilation if needed */
};

/* Result collection callback for NFA executor */
typedef struct
{
    rbcglob_results_t *results;
    const char *base_strip; /* If set, strip this prefix from results */
    size_t base_len;
} callback_ctx_t;

static void nfa_match_callback(const char *path, void *user_data)
{
    callback_ctx_t *ctx = (callback_ctx_t *)user_data;

    const char *add_path = path;

    // Handle stripping base path to match Ruby's 'base:' behavior
    // If base_strip is "foo", and path is "foo/bar", result should be "bar".
    if (ctx->base_strip && ctx->base_len > 0)
    {
        if (strncmp(path, ctx->base_strip, ctx->base_len) == 0)
        {
            if (path[ctx->base_len] == '/')
            {
                add_path = path + ctx->base_len + 1;
            }
            else if (path[ctx->base_len] == '\0')
            {
                add_path = "."; // base itself
            }
        }
    }

    rbcglob_results_add(ctx->results, add_path);
}

rbcglob_compiled_glob_t *rbcglob_compile_glob(const char *pattern, unsigned flags)
{
    if (!pattern)
        return NULL;

    rbcglob_compiled_glob_t *cg = malloc(sizeof(rbcglob_compiled_glob_t));
    if (!cg)
        return NULL;

    cg->ctx = malloc(sizeof(rbcglob_ctx_t));
    if (!cg->ctx)
    {
        free(cg);
        return NULL;
    }

    rbcglob_ctx_init(cg->ctx);
    cg->flags = flags;

    cg->graph = rbcglob_compile_segments(&cg->ctx->arena, pattern);

    if (!cg->graph)
    {
        rbcglob_compiled_glob_free(cg);
        return NULL;
    }

    return cg;
}

void rbcglob_compiled_glob_free(rbcglob_compiled_glob_t *cg)
{
    if (!cg)
        return;
    if (cg->ctx)
    {
        rbcglob_ctx_free(cg->ctx);
        free(cg->ctx);
    }
    free(cg);
}

bool rbcglob_dirglob_compiled(const rbcglob_compiled_glob_t *cg, const char *base, bool sort,
                              char ***out, size_t *count, size_t **lengths)
{
    if (!cg || !out || !count)
        return false;

    rbcglob_ctx_t *run_ctx = malloc(sizeof(rbcglob_ctx_t));
    if (!run_ctx)
        return false;
    rbcglob_ctx_init(run_ctx);

    rbcglob_results_t results;
    rbcglob_results_init(&results, run_ctx);

    callback_ctx_t cb_ctx;
    cb_ctx.results = &results;

    // Always set base_strip. Even for ".", we want to strip logical prefix "./"
    // which is now produced by executor.
    cb_ctx.base_strip = base;
    cb_ctx.base_len = base ? strlen(base) : 0;

    rbcglob_execute_segments(cg->graph, base, cg->flags, sort, nfa_match_callback, &cb_ctx);

    if (sort)
    {
        rbcglob_results_sort(&results);
    }
    rbcglob_results_deduplicate(&results);

    // Packaging
    *count = results.count;
    void **package = malloc(sizeof(void *) + (results.count + 1) * sizeof(char *));
    if (!package)
    {
        rbcglob_results_clear(&results);
        rbcglob_ctx_free(run_ctx);
        free(run_ctx);
        return false;
    }

    package[0] = run_ctx;
    char **pkg_items = (char **)&package[1];
    if (results.count > 0)
    {
        memcpy(pkg_items, results.items, results.count * sizeof(char *));
    }
    pkg_items[results.count] = NULL;

    *out = pkg_items;
    if (lengths)
        *lengths = results.lengths;
    else if (results.lengths)
        free(results.lengths);

    if (results.discovery_indices)
        free(results.discovery_indices);

    return true;
}

/* Context for fast path visitor */
typedef struct
{
    rbcglob_results_t *results;
    rbcglob_ctx_t *ctx;
    const char *base;
    unsigned flags;
    bool sort;
    callback_ctx_t *cb_ctx;
} fast_path_ctx_t;

static void fast_path_visitor(const char *p, void *arg)
{
    fast_path_ctx_t *fp_ctx = (fast_path_ctx_t *)arg;

    // Check if remaining string has wildcards (escaped braces? etc)
    if (strpbrk(p, "*?[]{}\\") == NULL)
    {
        // Pure literal
        char full_path[4096];
        int needed;
        const char *base = fp_ctx->base;

        if (base && strcmp(base, ".") != 0)
        {
            needed = snprintf(full_path, sizeof(full_path), "%s/%s", base, p);
        }
        else
        {
            needed = snprintf(full_path, sizeof(full_path), "%s", p);
        }

        if (needed < 0 || (size_t)needed >= sizeof(full_path))
        {
            // Too long, fallback to graph? Or just abort.
            // If too long for stack buffer, NFA won't help much with stat unless it handles long paths logic differently.
            return;
        }

        struct stat st;
        if (stat(full_path, &st) == 0)
        {
            size_t plen = strlen(p);
            if (plen > 0 && p[plen - 1] == '/')
            {
                if (!S_ISDIR(st.st_mode))
                    return;
            }
            rbcglob_results_add(fp_ctx->results, p);
        }
    }
    else
    {
        // Still has wildcards? Compile and run graph.
        rbcglob_segment_t *graph = rbcglob_compile_segments(&fp_ctx->ctx->arena, p);
        if (graph)
        {
            rbcglob_execute_segments(graph, fp_ctx->base, fp_ctx->flags, fp_ctx->sort, nfa_match_callback, fp_ctx->cb_ctx);
        }
    }
}

bool rbcglob_dirglob(const char **patterns, size_t npatterns, unsigned flags,
                     const char *base, bool sort, char ***out, size_t *count, size_t **lengths)
{

    if (!out || !count)
        return false;

    rbcglob_ctx_t *ctx = malloc(sizeof(rbcglob_ctx_t));
    if (!ctx)
        return false;
    rbcglob_ctx_init(ctx);

    rbcglob_results_t results;
    rbcglob_results_init(&results, ctx);

    callback_ctx_t cb_ctx;
    cb_ctx.results = &results;

    cb_ctx.base_strip = base;
    cb_ctx.base_len = base ? strlen(base) : 0;

    for (size_t i = 0; i < npatterns; i++)
    {
        if (!patterns[i])
            continue;

        const char *current_pattern = patterns[i];

        if (strpbrk(current_pattern, "*?[]{}\\") == NULL)
        {
            // Pure literal: Fast path
            fast_path_ctx_t fp_ctx = {&results, ctx, base, flags, sort, &cb_ctx};
            fast_path_visitor(current_pattern, &fp_ctx);
        }
        else if (strchr(current_pattern, '{') != NULL && strpbrk(current_pattern, "*?[]") == NULL)
        {
            // Pure braces: Use visitor
            fast_path_ctx_t fp_ctx = {&results, ctx, base, flags, sort, &cb_ctx};
            rbcglob_brace_visit(current_pattern, &ctx->arena, fast_path_visitor, &fp_ctx);
        }
        else
        {
            // Standard path or mixed wildcard-brace
            // If mixed, we pass directly to compile segments which handles braces via graph or expansion internally
            rbcglob_segment_t *graph = rbcglob_compile_segments(&ctx->arena, current_pattern);
            if (graph)
            {
                rbcglob_execute_segments(graph, base, flags, sort, nfa_match_callback, &cb_ctx);
            }
        }
    }

    if (sort)
    {
        rbcglob_results_sort(&results);
    }
    rbcglob_results_deduplicate(&results);

    // Packaging
    *count = results.count;
    void **package = malloc(sizeof(void *) + (results.count + 1) * sizeof(char *));
    if (!package)
    {
        rbcglob_results_clear(&results);
        rbcglob_ctx_free(ctx);
        free(ctx);
        return false;
    }

    package[0] = ctx;
    char **pkg_items = (char **)&package[1];
    if (results.count > 0)
    {
        memcpy(pkg_items, results.items, results.count * sizeof(char *));
    }
    pkg_items[results.count] = NULL;

    *out = pkg_items;
    if (lengths)
        *lengths = results.lengths;
    else if (results.lengths)
        free(results.lengths);

    if (results.items)
        free(results.items);
    if (results.discovery_indices)
        free(results.discovery_indices);

    return true;
}

void rbcglob_free(char **list, size_t count, size_t *lengths)
{
    (void)count;
    if (!list)
        return;
    void **package = (void **)list - 1;
    rbcglob_ctx_t *ctx = (rbcglob_ctx_t *)package[0];
    rbcglob_ctx_free(ctx);
    free(ctx);
    free(package);
    if (lengths)
        free(lengths);
}
