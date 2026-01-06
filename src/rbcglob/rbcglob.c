#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/fnmatch.h>
#include <rbcglob/internal/traverse.h>
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
 * @brief Return library version string.
 */
const char *
rbcglob_version(void)
{
  return RBCGLOB_VERSION;
}

/**
 * @brief Match entry for merging results from multiple expanded patterns
 */
typedef struct rbcglob_brace_match
{
  char *path;
  int brace_index;
  size_t original_index;
  size_t discovery_index;
  rbcglob_ctx_t *ctx;
  int sort_flag;
} rbcglob_brace_match_t;

/**
 * @brief Comparison function for merging results (no wildcards before brace)
 */
static int rbcglob_compare_brace_matches_no_wildcards(const void *a, const void *b)
{
  const rbcglob_brace_match_t *m1 = (const rbcglob_brace_match_t *)a;
  const rbcglob_brace_match_t *m2 = (const rbcglob_brace_match_t *)b;

  if (m1->brace_index != m2->brace_index)
  {
    return (m1->brace_index < m2->brace_index) ? -1 : 1;
  }

  if (m1->sort_flag)
  {
    return rbcglob_compare_paths(m1->path, m2->path);
  }
  else
  {
    return (m1->original_index < m2->original_index) ? -1 : 1;
  }
}

/**
 * @brief Comparison function for merging results (wildcards before brace)
 */
static int rbcglob_compare_brace_matches_with_wildcards(const void *a, const void *b)
{
  const rbcglob_brace_match_t *m1 = (const rbcglob_brace_match_t *)a;
  const rbcglob_brace_match_t *m2 = (const rbcglob_brace_match_t *)b;

  if (m1->sort_flag)
  {
    return rbcglob_compare_paths(m1->path, m2->path);
  }
  else
  {
    /* Use filesystem order comparison */
    int fs_cmp = rbcglob_compare_filesystem_order(m1->ctx, m1->path, m2->path);
    if (fs_cmp != 0)
      return fs_cmp;

    if (m1->brace_index != m2->brace_index)
    {
      return (m1->brace_index < m2->brace_index) ? -1 : 1;
    }
    return (m1->original_index < m2->original_index) ? -1 : 1;
  }
}

/**
 * @brief Merge results from multiple expanded patterns using Ruby-style rules
 */
static int rbcglob_merge_ruby_style(rbcglob_ctx_t *ctx,
                                    const char *original_pattern,
                                    rbcglob_results_t *brace_results, size_t count,
                                    int sort_flag, rbcglob_results_t *final_results)
{
  size_t total_count = 0;
  for (size_t i = 0; i < count; i++)
  {
    total_count += brace_results[i].count;
  }

  if (total_count == 0)
    return 0;

  rbcglob_brace_match_t *all_matches = malloc(total_count * sizeof(rbcglob_brace_match_t));
  if (!all_matches)
  {
    errno = ENOMEM;
    return -1;
  }

  size_t idx = 0;
  for (size_t i = 0; i < count; i++)
  {
    for (size_t j = 0; j < brace_results[i].count; j++)
    {
      all_matches[idx].path = brace_results[i].items[j];
      all_matches[idx].brace_index = (int)i;
      all_matches[idx].original_index = j;
      all_matches[idx].discovery_index = brace_results[i].discovery_indices[j];
      all_matches[idx].ctx = ctx;
      all_matches[idx].sort_flag = sort_flag;
      idx++;
    }
  }

  /* Check if there are wildcards before the first brace */
  int prefix_has_wildcards = 0;
  const char *first_brace = strchr(original_pattern, '{');
  if (first_brace)
  {
    for (const char *p = original_pattern; p < first_brace; p++)
    {
      if (*p == '*' || *p == '?' || *p == '[')
      {
        prefix_has_wildcards = 1;
        break;
      }
    }
  }

  if (prefix_has_wildcards)
  {
    qsort(all_matches, total_count, sizeof(rbcglob_brace_match_t), rbcglob_compare_brace_matches_with_wildcards);
  }
  else
  {
    qsort(all_matches, total_count, sizeof(rbcglob_brace_match_t), rbcglob_compare_brace_matches_no_wildcards);
  }

  for (size_t i = 0; i < total_count; i++)
  {
    rbcglob_results_add_with_index(final_results, all_matches[i].path, all_matches[i].discovery_index);
  }

  free(all_matches);
  return 0;
}

/**
 * @brief Perform glob pattern matching
 */
bool rbcglob_dirglob(const char **patterns, size_t npatterns, unsigned flags,
                     const char *base, bool sort_flag, char ***out, size_t *count, size_t **lengths)
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

    /* Expand braces first */
    char **expanded = NULL;
    size_t expanded_count = 0;

    if (rbcglob_brace_expand(p, &expanded, &expanded_count) != 0)
    {
      rbcglob_results_clear(&results);
      rbcglob_ctx_free(ctx);
      free(ctx);
      return false;
    }

    if (expanded_count > 1)
    {
      has_brace_expansion = true;

      /* Ruby-style merge for brace expansion */
      rbcglob_results_t *brace_pattern_results = calloc(expanded_count, sizeof(rbcglob_results_t));
      if (!brace_pattern_results)
      {
        for (size_t j = 0; j < expanded_count; j++)
          free(expanded[j]);
        free(expanded);
        rbcglob_results_clear(&results);
        rbcglob_ctx_free(ctx);
        free(ctx);
        return false;
      }

      for (size_t j = 0; j < expanded_count; j++)
      {
        rbcglob_results_init(&brace_pattern_results[j], ctx);
        rbcglob_compiled_pattern_t *cp = rbcglob_compiler_compile(expanded[j], flags);
        if (!cp || rbcglob_execute(ctx, cp, base, &brace_pattern_results[j]) != 0)
        {
          if (cp)
            rbcglob_compiler_compiled_pattern_free(cp);
          for (size_t k = 0; k <= j; k++)
            rbcglob_results_clear(&brace_pattern_results[k]);
          free(brace_pattern_results);
          for (size_t k = 0; k < expanded_count; k++)
            free(expanded[k]);
          free(expanded);
          rbcglob_results_clear(&results);
          rbcglob_ctx_free(ctx);
          free(ctx);
          return false;
        }
        rbcglob_compiler_compiled_pattern_free(cp);
        if (sort_flag)
          rbcglob_results_sort(&brace_pattern_results[j]);
      }

      if (rbcglob_merge_ruby_style(ctx, p, brace_pattern_results, expanded_count, sort_flag, &results) != 0)
      {
        for (size_t j = 0; j < expanded_count; j++)
          rbcglob_results_clear(&brace_pattern_results[j]);
        free(brace_pattern_results);
        for (size_t j = 0; j < expanded_count; j++)
          free(expanded[j]);
        free(expanded);
        rbcglob_results_clear(&results);
        rbcglob_ctx_free(ctx);
        free(ctx);
        return false;
      }

      /* Cleanup brace results */
      for (size_t j = 0; j < expanded_count; j++)
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
      rbcglob_compiled_pattern_t *cp = rbcglob_compiler_compile(expanded[0], flags);
      if (!cp || rbcglob_execute(ctx, cp, base, &pattern_results) != 0)
      {
        if (cp)
          rbcglob_compiler_compiled_pattern_free(cp);
        rbcglob_results_clear(&pattern_results);
        for (size_t j = 0; j < expanded_count; j++)
          free(expanded[j]);
        free(expanded);
        rbcglob_results_clear(&results);
        rbcglob_ctx_free(ctx);
        free(ctx);
        return false;
      }
      rbcglob_compiler_compiled_pattern_free(cp);

      if (sort_flag)
        rbcglob_results_sort(&pattern_results);
      for (size_t k = 0; k < pattern_results.count; k++)
      {
        rbcglob_results_add_with_index(&results, pattern_results.items[k], pattern_results.discovery_indices[k]);
      }
      rbcglob_results_clear(&pattern_results);
    }

    /* Cleanup expanded patterns */
    for (size_t j = 0; j < expanded_count; j++)
      free(expanded[j]);
    free(expanded);
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
