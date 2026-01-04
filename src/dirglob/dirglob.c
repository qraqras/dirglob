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

  /* For now, handle simple patterns (no directory components) */
  /* TODO: Handle patterns with / separators */
  if (strchr(pattern, '/') == NULL)
  {
    /* Simple filename pattern */
    return traverse_directory(pattern, base, flags, results);
  }

  /* Complex patterns with directories - not yet implemented */
  /* For now, return no matches */
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

  /* Initialize result collector */
  glob_results_t results;
  glob_results_init(&results);

  /* Process each pattern */
  for (size_t i = 0; i < npatterns; i++)
  {
    if (process_pattern(patterns[i], base, flags, &results) != 0)
    {
      glob_results_clear(&results);
      return false;
    }
  }

  /* Sort if requested */
  if (sort_flag)
  {
    glob_results_sort(&results);
  }

  /* Remove duplicates */
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
