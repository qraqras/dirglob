#include <dirglob/dirglob.h>
#include <dirglob/internal/fnmatch.h>
#include <dirglob/internal/traverse.h>
#include <dirglob/internal/utils.h>
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
dirglob_version(void)
{
  return "0.1.0";
}

/**
 * @brief Check if a path exists (file or directory)
 */
static bool path_exists(const char *path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

/**
 * @brief Extract directory part from a path (e.g., "a/b.txt" -> "a", "./b.txt" -> ".")
 */
static char *get_directory_part(const char *path)
{
  const char *slash = strrchr(path, '/');
  if (!slash)
  {
    /* No slash - current directory */
    return strdup(".");
  }

  size_t dirlen = slash - path;
  if (dirlen == 0)
  {
    /* Leading slash */
    return strdup("/");
  }

  char *dir = malloc(dirlen + 1);
  if (dir)
  {
    memcpy(dir, path, dirlen);
    dir[dirlen] = '\0';
  }
  return dir;
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
    return dirglob_compare_paths(m1->path, m2->path);
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
    return dirglob_compare_paths(m1->path, m2->path);
  }
  else
  {
    /* Use filesystem order comparison */
    int fs_cmp = dirglob_compare_filesystem_order(m1->path, m2->path);
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
 * @brief Process a single pattern and collect matches
 */
static int process_pattern(const char *pattern, const char *base,
                           unsigned flags, glob_results_t *results, int sort_flag);

/**
 * @brief Process a single pattern and collect matches
 */
static int process_pattern(const char *pattern, const char *base,
                           unsigned flags, glob_results_t *results, int sort_flag)
{
  if (!pattern)
  {
    errno = EINVAL;
    return -1;
  }

  /* Handle empty pattern */
  if (pattern[0] == '\0')
  {
    return 0;
  }

  /* Check if pattern contains glob metacharacters */
  if (!has_glob_pattern(pattern))
  {
    /* Literal path - check if it exists */
    char *full_path = path_join(base, pattern);
    if (!full_path)
    {
      errno = ENOMEM;
      return -1;
    }

    if (path_exists(full_path))
    {
      /* For base != NULL, result should be relative to base */
      const char *result_path = (base && base[0] != '\0') ? pattern : full_path;
      int ret = glob_results_add(results, result_path);
      free(full_path);
      return ret;
    }

    free(full_path);
    return 0; /* No match is not an error */
  }

  /* Handle patterns with directory components */
  const char *slash = strchr(pattern, '/');
  if (slash == NULL)
  {
    /* Simple filename pattern - no directory component */
    return traverse_directory(pattern, base, flags, results);
  }

  /* Pattern has directory component: dir/file or dir/wildcard/file etc. */
  /* Split pattern into first component and rest */
  size_t first_len = slash - pattern;
  char *first_component = malloc(first_len + 1);
  if (!first_component)
  {
    errno = ENOMEM;
    return -1;
  }
  memcpy(first_component, pattern, first_len);
  first_component[first_len] = '\0';

  /* Rest of pattern (after the slash) */
  const char *rest_pattern = slash + 1;

  /* Skip multiple slashes */
  while (*rest_pattern == '/')
  {
    rest_pattern++;
  }

  int ret;

  /* Check for ** (recursive glob) */
  if (strcmp(first_component, "**") == 0)
  {
    /* Recursive directory traversal */
    ret = traverse_recursive_glob(rest_pattern, base, flags, results, sort_flag, true);
    free(first_component);
    return ret;
  }

  if (has_glob_pattern(first_component))
  {
    /* First component has wildcards - need to match directories */
    ret = traverse_directory_recursive(first_component, rest_pattern, base, flags, results, sort_flag);
  }
  else
  {
    /* First component is literal - check if directory exists and recurse */
    char *new_base = path_join(base, first_component);
    if (!new_base)
    {
      free(first_component);
      errno = ENOMEM;
      return -1;
    }

    /* Check if directory exists */
    struct stat st;
    if (stat(new_base, &st) == 0 && S_ISDIR(st.st_mode))
    {
      /* Collect results from subdirectory and prepend directory name */
      glob_results_t subresults;
      glob_results_init(&subresults);

      ret = process_pattern(rest_pattern, new_base, flags, &subresults, sort_flag);

      if (ret == 0)
      {
        /* Prepend directory name to all results */
        for (size_t i = 0; i < subresults.count; i++)
        {
          char *prefixed_path = path_join(first_component, subresults.items[i]);
          if (prefixed_path)
          {
            glob_results_add(results, prefixed_path);
            free(prefixed_path);
          }
          free(subresults.items[i]);
        }
        free(subresults.items);
      }
      else
      {
        glob_results_clear(&subresults);
      }
    }
    else
    {
      /* Directory doesn't exist - no matches */
      ret = 0;
    }
    free(new_base);
  }

  free(first_component);
  return ret;
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
        return false;
      }

      for (size_t j = 0; j < expanded_count; j++)
      {
        glob_results_init(&brace_pattern_results[j]);
        if (process_pattern(expanded[j], base, flags, &brace_pattern_results[j], sort_flag) != 0)
        {
          for (size_t k = 0; k <= j; k++)
            glob_results_clear(&brace_pattern_results[k]);
          free(brace_pattern_results);
          for (size_t k = 0; k < expanded_count; k++)
            free(expanded[k]);
          free(expanded);
          glob_results_clear(&results);
          glob_results_clear_cache();
          return false;
        }
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
        return false;
      }

      /* Cleanup brace results */
      for (size_t j = 0; j < expanded_count; j++)
      {
        for (size_t k = 0; k < brace_pattern_results[j].count; k++)
          free(brace_pattern_results[j].items[k]);
        free(brace_pattern_results[j].items);
      }
      free(brace_pattern_results);
    }
    else
    {
      /* No brace expansion */
      glob_results_t pattern_results;
      glob_results_init(&pattern_results);
      if (process_pattern(expanded[0], base, flags, &pattern_results, sort_flag) != 0)
      {
        glob_results_clear(&pattern_results);
        free(expanded[0]);
        free(expanded);
        glob_results_clear(&results);
        glob_results_clear_cache();
        return false;
      }
      if (sort_flag)
        glob_results_sort(&pattern_results);
      for (size_t k = 0; k < pattern_results.count; k++)
      {
        glob_results_add_with_index(&results, pattern_results.items[k], pattern_results.discovery_indices[k]);
        free(pattern_results.items[k]);
      }
      free(pattern_results.items);
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

  /* Return results */
  *count = results.count;
  *out = results.items;
  glob_results_clear_cache();
  return true;
}

/**
 * @brief Free memory allocated by dirglob
 */
void dirglob_free(char **list, size_t count)
{
  if (!list)
  {
    return;
  }

  for (size_t i = 0; i < count; i++)
  {
    free(list[i]);
  }
  free(list);
}

/**
 * @brief Test if a path matches a glob pattern
 */
int dirglob_match(const char *pattern, unsigned flags, const char *path)
{
  if (!pattern || !path)
  {
    return -1;
  }

  return dirglob_fnmatch(pattern, path, flags);
}
