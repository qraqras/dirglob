#include <rbcglob/internal/compiler.h>
#include <rbcglob/internal/utils.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

bool rbcglob_has_glob_pattern(const char *str)
{
    if (!str)
        return false;

    while (*str)
    {
        switch (*str)
        {
        case '*':
        case '?':
        case '[':
        case '{':
            return true;
        case '\\':
            /* Skip escaped character */
            if (str[1])
                str++;
            break;
        }
        str++;
    }
    return false;
}

/* ========== Brace Expansion ========== */

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

/* ========== Pattern Compiler ========== */

static void rbcglob_compiler_parse_segment_tokens(rbcglob_segment_t *seg, unsigned flags)
{
    const char *p = seg->pattern;
    size_t capacity = 8;
    seg->tokens = malloc(sizeof(rbcglob_token_t) * capacity);
    seg->token_count = 0;

    while (*p)
    {
        if (seg->token_count >= capacity)
        {
            capacity *= 2;
            seg->tokens = realloc(seg->tokens, sizeof(rbcglob_token_t) * capacity);
        }
        rbcglob_token_t *tok = &seg->tokens[seg->token_count++];
        tok->ranges = NULL;
        tok->range_count = 0;

        switch (*p)
        {
        case '?':
            tok->token_type = RBCGLOB_TOKEN_ANY_CHAR;
            p++;
            break;
        case '*':
            tok->token_type = RBCGLOB_TOKEN_ANY_SEQUENCE;
            p++;
            break;
        case '[':
        {
            p++;
            if (*p == '!' || *p == '^')
            {
                tok->token_type = RBCGLOB_TOKEN_ANY_EXCEPT;
                p++;
            }
            else
            {
                tok->token_type = RBCGLOB_TOKEN_ANY_WITHIN;
            }

            size_t r_cap = 4;
            tok->ranges = malloc(sizeof(rbcglob_range_t) * r_cap);

            /* Special case: ] as first char */
            if (*p == ']')
            {
                tok->ranges[tok->range_count++] = (rbcglob_range_t){']', ']'};
                p++;
            }

            while (*p && *p != ']')
            {
                if (tok->range_count >= r_cap)
                {
                    r_cap *= 2;
                    tok->ranges = realloc(tok->ranges, sizeof(rbcglob_range_t) * r_cap);
                }
                char start = *p++;
                char end = start;
                if (*p == '-' && p[1] && p[1] != ']')
                {
                    p++;
                    end = *p++;
                }
                tok->ranges[tok->range_count++] = (rbcglob_range_t){start, end};
            }
            if (*p == ']')
                p++;
            break;
        }
        case '\\':
            if (!(flags & RBCGLOB_FNM_NOESCAPE))
            {
                tok->token_type = RBCGLOB_TOKEN_CHAR;
                p++;
                if (*p)
                    tok->c = *p++;
                else
                    tok->c = '\\';
            }
            else
            {
                tok->token_type = RBCGLOB_TOKEN_CHAR;
                tok->c = *p++;
            }
            break;
        default:
            tok->token_type = RBCGLOB_TOKEN_CHAR;
            tok->c = *p++;
            break;
        }
    }
}

static void rbcglob_compiler_extract_prefix(rbcglob_segment_t *seg)
{
    const char *p = seg->pattern;
    size_t len = 0;
    while (*p && *p != '*' && *p != '?' && *p != '[' && *p != '{')
    {
        if (*p == '\\' && p[1])
        {
            p += 2;
            len++;
        }
        else
        {
            p++;
            len++;
        }
    }
    if (len > 0)
    {
        seg->prefix = malloc(len + 1);
        if (seg->prefix)
        {
            const char *src = seg->pattern;
            char *dst = seg->prefix;
            while (dst < seg->prefix + len)
            {
                if (*src == '\\' && src[1])
                {
                    src++;
                    *dst++ = *src++;
                }
                else
                {
                    *dst++ = *src++;
                }
            }
            *dst = '\0';
            seg->prefix_len = len;
        }
    }
    else
    {
        seg->prefix = NULL;
        seg->prefix_len = 0;
    }
}

static void rbcglob_compiler_extract_suffix(rbcglob_segment_t *seg)
{
    size_t pattern_len = strlen(seg->pattern);
    if (pattern_len == 0)
    {
        seg->suffix = NULL;
        seg->suffix_len = 0;
        return;
    }

    const char *p = seg->pattern + pattern_len - 1;
    size_t len = 0;

    /* Scan backwards until we hit a metacharacter */
    while (p >= seg->pattern && *p != '*' && *p != '?' && *p != '[' && *p != '{' && *p != ']' && *p != '}')
    {
        /* Handle escaped characters */
        if (p > seg->pattern && p[-1] == '\\')
        {
            p -= 2;
            len++;
        }
        else
        {
            p--;
            len++;
        }
    }

    if (len > 0)
    {
        seg->suffix = malloc(len + 1);
        if (seg->suffix)
        {
            const char *src = seg->pattern + pattern_len - len;
            char *dst = seg->suffix;
            while (*src)
            {
                if (*src == '\\' && src[1])
                {
                    src++;
                }
                *dst++ = *src++;
            }
            *dst = '\0';
            seg->suffix_len = len;
        }
    }
    else
    {
        seg->suffix = NULL;
        seg->suffix_len = 0;
    }
}

rbcglob_compiled_pattern_t *rbcglob_compiler_compile(const char *pattern, unsigned flags)
{
    if (!pattern)
        return NULL;
    rbcglob_compiled_pattern_t *cp = malloc(sizeof(rbcglob_compiled_pattern_t));
    if (!cp)
        return NULL;
    cp->flags = flags;
    cp->is_absolute = (pattern[0] == '/');
    cp->sort_order = RBCGLOB_SORT_ASCENDING;

    /* Check for trailing slash */
    size_t pattern_len = strlen(pattern);
    cp->has_trailing_slash = (pattern_len > 0 && pattern[pattern_len - 1] == '/');

    size_t segment_count = 1;
    if (flags & RBCGLOB_FNM_PATHNAME)
    {
        for (const char *p = pattern; *p; p++)
            if (*p == '/')
                segment_count++;
    }
    cp->segments = calloc(segment_count, sizeof(rbcglob_segment_t));

    size_t idx = 0;
    const char *start = pattern;
    if (flags & RBCGLOB_FNM_PATHNAME)
    {
        if (cp->is_absolute)
        {
            while (*start == '/')
                start++; /* Skip leading slashes */
        }

        while (*start)
        {
            const char *end = strchr(start, '/');
            size_t len = end ? (size_t)(end - start) : strlen(start);
            if (len == 0 && end)
            {
                start = end + 1;
                continue;
            }

            rbcglob_segment_t *seg = &cp->segments[idx];
            seg->pattern = malloc(len + 1);
            memcpy(seg->pattern, start, len);
            seg->pattern[len] = '\0';

            if (strcmp(seg->pattern, "**") == 0)
            {
                seg->type = RBCGLOB_SEGMENT_RECURSIVE;
            }
            else if (rbcglob_has_glob_pattern(seg->pattern))
            {
                seg->type = RBCGLOB_SEGMENT_WILDCARD;
                rbcglob_compiler_parse_segment_tokens(seg, flags);
                rbcglob_compiler_extract_prefix(seg);
                rbcglob_compiler_extract_suffix(seg);
            }
            else
            {
                seg->type = RBCGLOB_SEGMENT_LITERAL;
            }
            idx++;
            if (!end)
                break;
            start = end + 1;
        }
    }
    else
    {
        /* Non-pathname: treat whole pattern as one wildcard segment */
        rbcglob_segment_t *seg = &cp->segments[0];
        seg->pattern = strdup(pattern);
        seg->type = RBCGLOB_SEGMENT_WILDCARD;
        rbcglob_compiler_parse_segment_tokens(seg, flags);
        seg->prefix = NULL;
        seg->prefix_len = 0;
        seg->suffix = NULL;
        seg->suffix_len = 0;
        idx = 1;
    }
    cp->count = idx;

    /* P2 Optimization: Analyze pattern for directory traversal pruning */
    cp->has_recursive_segment = false;
    cp->leading_literal_count = 0;
    for (size_t i = 0; i < cp->count; i++)
    {
        if (cp->segments[i].type == RBCGLOB_SEGMENT_RECURSIVE)
        {
            cp->has_recursive_segment = true;
        }
        if (i < cp->count && cp->segments[i].type == RBCGLOB_SEGMENT_LITERAL)
        {
            if (i == cp->leading_literal_count)
            {
                cp->leading_literal_count++;
            }
        }
    }

    return cp;
}

void rbcglob_compiler_compiled_pattern_free(rbcglob_compiled_pattern_t *cp)
{
    if (!cp)
        return;
    for (size_t i = 0; i < cp->count; i++)
    {
        free(cp->segments[i].pattern);
        free(cp->segments[i].prefix);
        free(cp->segments[i].suffix);
        if (cp->segments[i].tokens)
        {
            for (size_t j = 0; j < cp->segments[i].token_count; j++)
            {
                free(cp->segments[i].tokens[j].ranges);
            }
            free(cp->segments[i].tokens);
        }
    }
    free(cp->segments);
    free(cp);
}

rbcglob_compiled_glob_t *rbcglob_compile_glob(const char *pattern, unsigned flags)
{
    if (!pattern)
    {
        errno = EINVAL;
        return NULL;
    }

    rbcglob_compiled_glob_t *cg = malloc(sizeof(rbcglob_compiled_glob_t));
    if (!cg)
    {
        errno = ENOMEM;
        return NULL;
    }

    /* Expand braces */
    char **expanded = NULL;
    size_t expanded_count = 0;
    if (rbcglob_brace_expand(pattern, &expanded, &expanded_count) != 0)
    {
        free(cg);
        errno = ENOMEM;
        return NULL;
    }

    /* Compile each expanded pattern */
    cg->patterns = malloc(sizeof(rbcglob_compiled_pattern_t *) * expanded_count);
    if (!cg->patterns)
    {
        for (size_t i = 0; i < expanded_count; i++)
            free(expanded[i]);
        free(expanded);
        free(cg);
        errno = ENOMEM;
        return NULL;
    }

    cg->pattern_count = 0;
    for (size_t i = 0; i < expanded_count; i++)
    {
        rbcglob_compiled_pattern_t *cp = rbcglob_compiler_compile(expanded[i], flags);
        if (!cp)
        {
            /* Cleanup on error */
            for (size_t j = 0; j < cg->pattern_count; j++)
                rbcglob_compiler_compiled_pattern_free(cg->patterns[j]);
            free(cg->patterns);
            for (size_t j = i; j < expanded_count; j++)
                free(expanded[j]);
            free(expanded);
            free(cg);
            return NULL;
        }
        cg->patterns[cg->pattern_count++] = cp;
        free(expanded[i]);
    }
    free(expanded);

    return cg;
}

void rbcglob_compiled_glob_free(rbcglob_compiled_glob_t *cg)
{
    if (!cg)
        return;

    for (size_t i = 0; i < cg->pattern_count; i++)
    {
        rbcglob_compiler_compiled_pattern_free(cg->patterns[i]);
    }
    free(cg->patterns);
    free(cg);
}
