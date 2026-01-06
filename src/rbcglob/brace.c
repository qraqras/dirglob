#include <rbcglob/internal/utils.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * @brief Find matching closing brace, handling nesting
 * @return Pointer to closing brace, or NULL if not found
 */
static const char *rbcglob_brace_find_closing_brace(const char *str)
{
    int depth = 1;
    const char *p = str;

    while (*p && depth > 0)
    {
        if (*p == '\\' && p[1])
        {
            p += 2;
            continue;
        }
        if (*p == '{')
            depth++;
        else if (*p == '}')
            depth--;

        if (depth > 0)
            p++;
    }

    return (depth == 0) ? p : NULL;
}

/**
 * @brief Expand brace expressions in a pattern
 * @param pattern Input pattern with braces
 * @param expanded Output array of expanded patterns
 * @param count Number of expanded patterns
 * @return 0 on success, -1 on error
 */
int rbcglob_brace_expand(const char *pattern, char ***expanded, size_t *count)
{
    if (!pattern || !expanded || !count)
    {
        return -1;
    }

    *expanded = NULL;
    *count = 0;

    /* Find first unescaped opening brace */
    const char *open = NULL;
    const char *p = pattern;
    while (*p)
    {
        if (*p == '\\' && p[1])
        {
            p += 2;
            continue;
        }
        if (*p == '{')
        {
            open = p;
            break;
        }
        p++;
    }

    /* No braces found - return original pattern */
    if (!open)
    {
        *expanded = malloc(sizeof(char *));
        if (!*expanded)
            return -1;

        (*expanded)[0] = rbcglob_strdup(pattern);
        if (!(*expanded)[0])
        {
            free(*expanded);
            *expanded = NULL;
            return -1;
        }
        *count = 1;
        return 0;
    }

    /* Find matching closing brace */
    const char *close = rbcglob_brace_find_closing_brace(open + 1);
    if (!close)
    {
        /* No matching brace - return original pattern */
        *expanded = malloc(sizeof(char *));
        if (!*expanded)
            return -1;

        (*expanded)[0] = rbcglob_strdup(pattern);
        if (!(*expanded)[0])
        {
            free(*expanded);
            *expanded = NULL;
            return -1;
        }
        *count = 1;
        return 0;
    }

    /* Extract prefix, alternatives, and suffix */
    size_t prefix_len = open - pattern;
    const char *alts_start = open + 1;
    const char *suffix = close + 1;

    /* Count alternatives (split by comma) */
    size_t alt_count = 1;
    for (const char *c = alts_start; c < close; c++)
    {
        if (*c == '\\' && c + 1 < close)
        {
            c++;
            continue;
        }
        if (*c == ',')
            alt_count++;
    }

    /* Allocate result array */
    *expanded = malloc(alt_count * sizeof(char *));
    if (!*expanded)
        return -1;

    *count = 0;

    /* Process each alternative */
    const char *alt_start = alts_start;

    for (const char *c = alts_start; c <= close; c++)
    {
        if (c < close && *c == '\\' && c + 1 < close)
        {
            c++;
            continue;
        }

        if (c == close || *c == ',')
        {
            size_t alt_len = c - alt_start;

            /* Build expanded pattern: prefix + alternative + suffix */
            size_t total_len = prefix_len + alt_len + strlen(suffix) + 1;
            char *result = malloc(total_len);
            if (!result)
            {
                /* Cleanup on error */
                for (size_t i = 0; i < *count; i++)
                    free((*expanded)[i]);
                free(*expanded);
                *expanded = NULL;
                return -1;
            }

            /* Copy prefix */
            memcpy(result, pattern, prefix_len);
            /* Copy alternative */
            memcpy(result + prefix_len, alt_start, alt_len);
            /* Copy suffix */
            strcpy(result + prefix_len + alt_len, suffix);

            /* Recursively expand in case there are more braces */
            char **sub_expanded = NULL;
            size_t sub_count = 0;
            if (rbcglob_brace_expand(result, &sub_expanded, &sub_count) == 0)
            {
                size_t new_capacity = *count + sub_count;
                char **new_expanded = realloc(*expanded, new_capacity * sizeof(char *));
                if (!new_expanded)
                {
                    for (size_t i = 0; i < sub_count; i++)
                        free(sub_expanded[i]);
                    free(sub_expanded);
                    for (size_t i = 0; i < *count; i++)
                        free((*expanded)[i]);
                    free(*expanded);
                    *expanded = NULL;
                    return -1;
                }
                *expanded = new_expanded;

                for (size_t i = 0; i < sub_count; i++)
                {
                    (*expanded)[*count] = sub_expanded[i];
                    (*count)++;
                }
                free(sub_expanded);
            }
            else
            {
                (*expanded)[*count] = result;
                (*count)++;
            }

            alt_start = c + 1;
        }
    }

    return 0;
}
