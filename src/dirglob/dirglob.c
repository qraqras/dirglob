#include <dirglob/dirglob.h>
#include <stdlib.h>
#include <errno.h>

/**
 * @brief Return library version string.
 */
const char *
dirglob_version(void)
{
  return "0.1.0";
}

/**
 * @brief Perform glob pattern matching (stub implementation)
 */
bool dirglob(const char **patterns, size_t npatterns, unsigned flags,
             const char *base, int sort, char ***out, size_t *count)
{
  (void)patterns;
  (void)npatterns;
  (void)flags;
  (void)base;
  (void)sort;

  if (!out || !count)
  {
    errno = EINVAL;
    return false;
  }

  /* Stub: return empty result */
  *count = 0;
  *out = calloc(1, sizeof(char *));
  if (!*out)
  {
    errno = ENOMEM;
    return false;
  }

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
 * @brief Test if a path matches a glob pattern (stub implementation)
 */
int dirglob_match(const char *pattern, unsigned flags, const char *path)
{
  (void)pattern;
  (void)flags;
  (void)path;

  /* Stub: always return no match */
  return 1;
}
