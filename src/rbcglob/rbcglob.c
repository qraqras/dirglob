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
typedef struct
{
  char *path;
  int brace_index;
  size_t original_index;
  size_t discovery_index;
} brace_match_t;

static int g_sort_flag = 0;

/**
 * @brief Comparison function for merging results (no wildcards before brace)
 */
static int compare_brace_matches_no_wildcards(const void *a, const void *b)
{
  const brace_match_t *m1 = (const brace_match_t *)a;
  const brace_match_t *m2 = (const brace_match_t *)b;

  if (m1->brace_index != m2->brace_index)
  {
    return (m1->brace_index < m2->brace_index) ? -1 : 1;
  }

  if (g_sort_flag)
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
static int compare_brace_matches_with_wildcards(const void *a, const void *b)
{
  const brace_match_t *m1 = (const brace_match_t *)a;
  const brace_match_t *m2 = (const brace_match_t *)b;

  if (g_sort_flag)
  {
    return rbcglob_compare_paths(m1->path, m2->path);
  }
  else
  {
    /* Use filesystem order comparison */
    int fs_cmp = rbcglob_compare_filesystem_order(m1->path, m2->path);
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
static int merge_ruby_style(const char *original_pattern,
                            glob_results_t *brace_results, size_t count,
                            int sort_flag, glob_results_t *final_results)
{
  size_t total_count = 0;
  for (size_t i = 0; i < count; i++)
  {
    total_count += brace_results[i].count;
  }

  if (total_count == 0)
    return 0;

  brace_match_t *all_matches = malloc(total_count * sizeof(brace_match_t));
  if (!all_matches)
  {
    errno = ENOMEM;
    return -1;
  }

  g_sort_flag = sort_flag;

  size_t idx = 0;
  for (size_t i = 0; i < count; i++)
  {
    for (size_t j = 0; j < brace_results[i].count; j++)
    {
      all_matches[idx].path = brace_results[i].items[j];
      all_matches[idx].brace_index = (int)i;
      all_matches[idx].original_index = j;
      all_matches[idx].discovery_index = brace_results[i].discovery_indices[j];
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

  g_sort_flag = sort_flag;
  if (prefix_has_wildcards)
  {
    qsort(all_matches, total_count, sizeof(brace_match_t), compare_brace_matches_with_wildcards);
  }
  else
  {
    qsort(all_matches, total_count, sizeof(brace_match_t), compare_brace_matches_no_wildcards);
  }

  for (size_t i = 0; i < total_count; i++)
  {
    glob_results_add_with_index(final_results, all_matches[i].path, all_matches[i].discovery_index);
  }

  free(all_matches);
  return 0;
}

/**
 * @brief Perform glob pattern matching
 */
bool dirglob(const char **patterns, size_t npatterns, unsigned flags,
             const char *base, int sort_flag, char ***out, size_t *count)
{
  if (!out || !count)
  {
    errno = EINVAL;
    return false;
  }

  if (!patterns || npatterns == 0)
  {
    *count = 0;
    *out = NULL;
    return true;
  }

  glob_results_reset_discovery_counter();

  /* Initialize result collector */
  glob_results_t results;
  glob_results_init(&results);

  /* Track whether any brace expansion occurred */
  bool has_brace_expansion = false;

  /* Process each pattern */
  for (size_t i = 0; i < npatterns; i++)
  {
    /* Expand braces first */
    char **expanded = NULL;
    size_t expanded_count = 0;

    if (expand_braces(patterns[i], &expanded, &expanded_count) != 0)
    {
      glob_results_clear(&results);
      glob_results_clear_cache();
      ;
      return false;
    }

    if (expanded_count > 1)
    {
      has_brace_expansion = true;

      /* Ruby-style merge for brace expansion */
      glob_results_t *brace_pattern_results = calloc(expanded_count, sizeof(glob_results_t));
      if (!brace_pattern_results)
      {
        for (size_t j = 0; j < expanded_count; j++)
          free(expanded[j]);
        free(expanded);
        glob_results_clear(&results);
        glob_results_clear_cache();
        ;
        return false;
      }

      for (size_t j = 0; j < expanded_count; j++)
      {
        glob_results_init(&brace_pattern_results[j]);
        rbcglob_compiled_pattern_t *cp = rbcglob_compile(expanded[j], flags);
        if (!cp || rbcglob_execute(cp, base, &brace_pattern_results[j]) != 0)
        {
          if (cp)
            rbcglob_compiled_pattern_free(cp);
          for (size_t k = 0; k <= j; k++)
            glob_results_clear(&brace_pattern_results[k]);
          free(brace_pattern_results);
          for (size_t k = 0; k < expanded_count; k++)
            free(expanded[k]);
          free(expanded);
          glob_results_clear(&results);
          glob_results_clear_cache();
          ;
          return false;
        }
        rbcglob_compiled_pattern_free(cp);
        if (sort_flag)
          glob_results_sort(&brace_pattern_results[j]);
      }

      if (merge_ruby_style(patterns[i], brace_pattern_results, expanded_count, sort_flag, &results) != 0)
      {
        for (size_t j = 0; j < expanded_count; j++)
          glob_results_clear(&brace_pattern_results[j]);
        free(brace_pattern_results);
        for (size_t j = 0; j < expanded_count; j++)
          free(expanded[j]);
        free(expanded);
        glob_results_clear(&results);
        glob_results_clear_cache();
        ;
        return false;
      }

      /* Cleanup brace results */
      for (size_t j = 0; j < expanded_count; j++)
      {
        glob_results_clear(&brace_pattern_results[j]);
      }
      free(brace_pattern_results);
    }
    else
    {
      /* No brace expansion */
      glob_results_t pattern_results;
      glob_results_init(&pattern_results);
      rbcglob_compiled_pattern_t *cp = rbcglob_compile(expanded[0], flags);
      if (!cp || rbcglob_execute(cp, base, &pattern_results) != 0)
      {
        if (cp)
          rbcglob_compiled_pattern_free(cp);
        glob_results_clear(&pattern_results);
        for (size_t j = 0; j < expanded_count; j++)
          free(expanded[j]);
        free(expanded);
        glob_results_clear(&results);
        glob_results_clear_cache();
        ;
        return false;
      }
      rbcglob_compiled_pattern_free(cp);

      if (sort_flag)
        glob_results_sort(&pattern_results);
      for (size_t k = 0; k < pattern_results.count; k++)
      {
        glob_results_add_with_index(&results, pattern_results.items[k], pattern_results.discovery_indices[k]);
      }
      glob_results_clear(&pattern_results);
    }

    /* Cleanup expanded patterns */
    for (size_t j = 0; j < expanded_count; j++)
      free(expanded[j]);
    free(expanded);
  }

  /* Final sort and deduplicate if no brace expansion occurred */
  if (sort_flag && !has_brace_expansion)
  {
    glob_results_sort(&results);
  }
  glob_results_deduplicate(&results);

  /* Return results - ensure non-NULL for empty results (Ruby compatibility) */
  *count = results.count;
  if (results.count == 0 && results.items == NULL)
  {
    /* Allocate empty array instead of returning NULL */
    *out = malloc(sizeof(char *));
    if (!*out)
    {
      glob_results_clear_cache();
      return false;
    }
  }
  else
  {
    *out = results.items;
  }
  glob_results_clear_cache();
  ;
  return true;
}

void rbcglob_free(char **list, size_t count)
{
  if (!list)
    return;
  for (size_t i = 0; i < count; i++)
    free(list[i]);
  free(list);
}

int rbcglob_match(const char *pattern, unsigned flags, const char *path)
{
  if (!pattern || !path)
    return -1;
  return rbcglob_fnmatch(pattern, path, flags);
}
