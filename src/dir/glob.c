#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/graph.h>
#include <rbcglob/internal/traverse.h>
#include <rbcglob/internal/utils.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Defines the opaque struct from types.h */
struct rbcglob_compiled_glob_s
{
    rbcglob_ctx_t *ctx;
    rbcglob_node_t *graph;
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

    cg->graph = rbcglob_nfa_compile(&cg->ctx->arena, pattern);

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
    // Special handling for ".": treat it as empty base for stripping purposes.
    // If base is ".", executor treats it as NULL/current dir, so generated paths
    // don't have "." prefix unless matched. Thus we shouldn't strip it.
    if (base && strcmp(base, ".") == 0)
    {
        cb_ctx.base_strip = NULL;
        cb_ctx.base_len = 0;
    }
    else
    {
        cb_ctx.base_strip = base;
        cb_ctx.base_len = base ? strlen(base) : 0;
    }

    rbcglob_nfa_execute(cg->graph, base, cg->flags, sort, nfa_match_callback, &cb_ctx);

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

    if (results.items)
        free(results.items);
    if (results.discovery_indices)
        free(results.discovery_indices);

    return true;
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

    if (base && strcmp(base, ".") == 0)
    {
        cb_ctx.base_strip = NULL;
        cb_ctx.base_len = 0;
    }
    else
    {
        cb_ctx.base_strip = base;
        cb_ctx.base_len = base ? strlen(base) : 0;
    }

    for (size_t i = 0; i < npatterns; i++)
    {
        if (!patterns[i])
            continue;
        rbcglob_node_t *graph = rbcglob_nfa_compile(&ctx->arena, patterns[i]);
        if (graph)
        {
            rbcglob_nfa_execute(graph, base, flags, sort, nfa_match_callback, &cb_ctx);
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
