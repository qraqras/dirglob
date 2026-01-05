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
 * @brief Process a single pattern and collect matches
 */
static int process_pattern(const char *pattern, const char *base,
                           unsigned flags, glob_results_t *results)
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
  if (has_glob_pattern(first_component))
  {
    /* First component has wildcards - need to match directories */
    ret = traverse_directory_recursive(first_component, rest_pattern, base, flags, results);
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

      ret = process_pattern(rest_pattern, new_base, flags, &subresults);

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

  /* Initialize result collector */
  glob_results_t results;
  glob_results_init(&results);

  /* Process each pattern */
  for (size_t i = 0; i < npatterns; i++)
  {
    /* Expand braces first */
    char **expanded = NULL;
    size_t expanded_count = 0;

    if (expand_braces(patterns[i], &expanded, &expanded_count) != 0)
    {
      glob_results_clear(&results);
      return false;
    }

    /* Process each expanded pattern */
    for (size_t j = 0; j < expanded_count; j++)
    {
      /* Collect results for this specific expanded pattern */
      glob_results_t pattern_results;
      glob_results_init(&pattern_results);

      if (process_pattern(expanded[j], base, flags, &pattern_results) != 0)
      {
        /* Cleanup */
        glob_results_clear(&pattern_results);
        for (size_t k = 0; k < expanded_count; k++)
          free(expanded[k]);
        free(expanded);
        glob_results_clear(&results);
        return false;
      }

      /* Sort this pattern's results if requested */
      if (sort_flag)
      {
        glob_results_sort(&pattern_results);
      }

      /* Merge into main results */
      for (size_t k = 0; k < pattern_results.count; k++)
      {
        glob_results_add(&results, pattern_results.items[k]);
        free(pattern_results.items[k]);
      }
      free(pattern_results.items);
    }

    /* Cleanup expanded patterns */
    for (size_t j = 0; j < expanded_count; j++)
      free(expanded[j]);
    free(expanded);
  }

  /* Remove duplicates (do not sort again - already sorted per pattern) */
  glob_results_deduplicate(&results);

  /* Return results */
  *count = results.count;
  *out = results.items;

  /* Don't call glob_results_clear() - caller owns the items array */
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
