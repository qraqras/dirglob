#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/utils.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief Return library version string.
 */
const char *rbcglob_version(void)
{
    return RBCGLOB_VERSION;
}

char *rbcglob_strdup(const char *str)
{
    if (!str)
        return NULL;

    size_t len = strlen(str);
    char *dup = malloc(len + 1);
    if (!dup)
        return NULL;

    memcpy(dup, str, len + 1);
    return dup;
}
