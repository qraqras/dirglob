#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/dir.h>
#include <rbcglob/internal/file.h>
#include <rbcglob/internal/traverse.h>
#include <rbcglob/internal/compiler.h>
#include <rbcglob/internal/utils.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#ifdef _WIN32
#include <sys/stat.h>
#define stat _stat
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

/**
 * @brief Merge results from multiple expanded patterns using Ruby-style rules
 *
 * Ruby simply concatenates results in brace expansion order.
 * For example, *.{c,h} returns all *.c results first, then all *.h results.
 */
static int rbcglob_merge_ruby_style(rbcglob_results_t *brace_results,
                                    size_t count,
                                    rbcglob_results_t *final_results)
{
  /* Simply concatenate results in brace expansion order */
  for (size_t i = 0; i < count; i++)
  {
    for (size_t j = 0; j < brace_results[i].count; j++)
    {
      if (rbcglob_results_add_with_index(final_results,
                                         brace_results[i].items[j],
                                         brace_results[i].discovery_indices[j]) != 0)
      {
        return -1;
      }
    }
  }
  return 0;
}

/**
 * @brief Execute a compiled glob pattern (internal helper)
 */
static bool rbcglob_dirglob_compiled_internal(const rbcglob_compiled_glob_t *cg,
                                              const char *base,
                                              bool sort_flag,
                                              rbcglob_ctx_t *ctx,
                                              rbcglob_results_t *results,
                                              bool *has_brace_expansion)
{
  if (cg->pattern_count > 1)
  {
    *has_brace_expansion = true;

    /* Ruby-style merge for brace expansion: simply concatenate in expansion order */
    rbcglob_results_t *brace_pattern_results = calloc(cg->pattern_count, sizeof(rbcglob_results_t));
    if (!brace_pattern_results)
    {
      errno = ENOMEM;
      return false;
    }

    for (size_t j = 0; j < cg->pattern_count; j++)
    {
      rbcglob_results_init(&brace_pattern_results[j], ctx);
      if (rbcglob_execute(ctx, cg->patterns[j], base, &brace_pattern_results[j]) != 0)
      {
        for (size_t k = 0; k <= j; k++)
          rbcglob_results_clear(&brace_pattern_results[k]);
        free(brace_pattern_results);
        return false;
      }
      if (sort_flag)
        rbcglob_results_sort(&brace_pattern_results[j]);
    }

    if (rbcglob_merge_ruby_style(brace_pattern_results, cg->pattern_count, results) != 0)
    {
      for (size_t j = 0; j < cg->pattern_count; j++)
        rbcglob_results_clear(&brace_pattern_results[j]);
      free(brace_pattern_results);
      return false;
    }

    /* Cleanup brace results */
    for (size_t j = 0; j < cg->pattern_count; j++)
    {
      rbcglob_results_clear(&brace_pattern_results[j]);
    }
    free(brace_pattern_results);
  }
  else
  {
    /* No brace expansion */
    rbcglob_results_t pattern_results;
    rbcglob_results_init(&pattern_results, ctx);
    if (rbcglob_execute(ctx, cg->patterns[0], base, &pattern_results) != 0)
    {
      rbcglob_results_clear(&pattern_results);
      return false;
    }

    if (sort_flag)
      rbcglob_results_sort(&pattern_results);
    for (size_t k = 0; k < pattern_results.count; k++)
    {
      rbcglob_results_add_with_index(results, pattern_results.items[k], pattern_results.discovery_indices[k]);
    }
    rbcglob_results_clear(&pattern_results);
  }

  return true;
}

/**
 * @brief Perform glob matching with a precompiled pattern
 */
bool rbcglob_dirglob_compiled(const rbcglob_compiled_glob_t *cg, const char *base, bool sort_flag,
                              char ***out, size_t *count, size_t **lengths)
{
  if (!cg || !out || !count)
  {
    errno = EINVAL;
    return false;
  }

  /* Allocate and initialize context for thread-safety */
  rbcglob_ctx_t *ctx = malloc(sizeof(rbcglob_ctx_t));
  if (!ctx)
  {
    errno = ENOMEM;
    return false;
  }
  rbcglob_ctx_init(ctx);

  rbcglob_results_reset_discovery_counter(ctx);

  /* Initialize result collector */
  rbcglob_results_t results;
  rbcglob_results_init(&results, ctx);

  bool has_brace_expansion = false;

  if (!rbcglob_dirglob_compiled_internal(cg, base, sort_flag, ctx, &results, &has_brace_expansion))
  {
    rbcglob_results_clear(&results);
    rbcglob_ctx_free(ctx);
    free(ctx);
    return false;
  }

  /* Final sort and deduplicate if no brace expansion occurred */
  if (sort_flag && !has_brace_expansion)
  {
    rbcglob_results_sort(&results);
  }
  rbcglob_results_deduplicate(&results);

  /* Packaging for the caller */
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
  pkg_items[results.count] = NULL; /* Null-terminate for safety */

  *out = pkg_items;

  if (lengths)
  {
    *lengths = results.lengths;
  }
  else
  {
    if (results.lengths)
      free(results.lengths);
  }

  /* Free temporary result collector buffers (but not the items or lengths we've moved/copied) */
  if (results.items)
    free(results.items);
  if (results.discovery_indices)
    free(results.discovery_indices);

  return true;
}

/**
 * @brief Perform glob pattern matching
 */
bool rbcglob_dirglob(const char **patterns,
                     size_t npatterns,
                     unsigned flags,
                     const char *base,
                     bool sort_flag,
                     char ***out,
                     size_t *count,
                     size_t **lengths)
{
  if (!out || !count)
  {
    errno = EINVAL;
    return false;
  }

  /* Allocate and initialize context for thread-safety */
  rbcglob_ctx_t *ctx = malloc(sizeof(rbcglob_ctx_t));
  if (!ctx)
  {
    errno = ENOMEM;
    return false;
  }
  rbcglob_ctx_init(ctx);

  /* Dir.glob always operates in pathname mode */
  flags |= RBCGLOB_FNM_PATHNAME;

  if (!patterns || npatterns == 0)
  {
    *count = 0;
    void **package = malloc(sizeof(void *) + sizeof(char *));
    if (!package)
    {
      rbcglob_ctx_free(ctx);
      free(ctx);
      errno = ENOMEM;
      return false;
    }
    package[0] = ctx;
    package[1] = NULL;
    *out = (char **)&package[1];
    if (lengths)
      *lengths = NULL;
    return true;
  }

  rbcglob_results_reset_discovery_counter(ctx);

  /* Initialize result collector */
  rbcglob_results_t results;
  rbcglob_results_init(&results, ctx);

  /* Track whether any brace expansion occurred */
  bool has_brace_expansion = false;

  /* Process each pattern */
  for (size_t i = 0; i < npatterns; i++)
  {
    /* Tilde expansion (Ruby Dir.glob behavior) */
    const char *p = patterns[i];
    if (p[0] == '~')
    {
      p = rbcglob_expand_path_arena(p, NULL, &ctx->arena);
    }

    /* Compile with brace expansion */
    rbcglob_compiled_glob_t *cg = rbcglob_compile_glob(p, flags);
    if (!cg)
    {
      rbcglob_results_clear(&results);
      rbcglob_ctx_free(ctx);
      free(ctx);
      return false;
    }

    /* Execute compiled pattern */
    if (!rbcglob_dirglob_compiled_internal(cg, base, sort_flag, ctx, &results, &has_brace_expansion))
    {
      rbcglob_compiled_glob_free(cg);
      rbcglob_results_clear(&results);
      rbcglob_ctx_free(ctx);
      free(ctx);
      return false;
    }

    /* Cleanup compiled glob */
    rbcglob_compiled_glob_free(cg);
  }

  /* Final sort and deduplicate if no brace expansion occurred */
  if (sort_flag && !has_brace_expansion)
  {
    rbcglob_results_sort(&results);
  }
  rbcglob_results_deduplicate(&results);

  /* Packaging for the caller */
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
  pkg_items[results.count] = NULL; /* Null-terminate for safety */

  *out = pkg_items;

  if (lengths)
  {
    *lengths = results.lengths;
  }
  else
  {
    if (results.lengths)
      free(results.lengths);
  }

  /* Free temporary result collector buffers (but not the items or lengths we've moved/copied) */
  if (results.items)
    free(results.items);
  if (results.discovery_indices)
    free(results.discovery_indices);

  return true;
}

void rbcglob_free(char **list, size_t count, size_t *lengths)
{
  (void)count;
  (void)lengths;
  if (!list)
    return;

  /* Extract context from package */
  void **package = (void **)list - 1;
  rbcglob_ctx_t *ctx = (rbcglob_ctx_t *)package[0];

  /* Free context and its associated memory (arena, cache) */
  rbcglob_ctx_free(ctx);
  free(ctx);

  /* Free the results package and optional lengths array */
  free(package);
  if (lengths)
    free(lengths);
}
