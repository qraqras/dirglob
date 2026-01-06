#include <rbcglob/internal/compiler.h>
#include <rbcglob/internal/utils.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void parse_segment_tokens(rbcglob_segment_t *seg)
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
            tok->token_type = RBCGLOB_TOKEN_CHAR;
            p++;
            if (*p)
                tok->c = *p++;
            else
                tok->c = '\\';
            break;
        default:
            tok->token_type = RBCGLOB_TOKEN_CHAR;
            tok->c = *p++;
            break;
        }
    }
}

static void extract_prefix(rbcglob_segment_t *seg)
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

static void extract_suffix(rbcglob_segment_t *seg)
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

rbcglob_compiled_pattern_t *rbcglob_compile(const char *pattern, unsigned flags)
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
    for (const char *p = pattern; *p; p++)
        if (*p == '/')
            segment_count++;
    cp->segments = calloc(segment_count + 1, sizeof(rbcglob_segment_t));

    size_t idx = 0;
    const char *start = pattern;
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
        else if (has_glob_pattern(seg->pattern))
        {
            seg->type = RBCGLOB_SEGMENT_WILDCARD;
            parse_segment_tokens(seg);
            extract_prefix(seg);
            extract_suffix(seg);
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
    cp->segments[idx].type = RBCGLOB_SEGMENT_END;
    cp->count = idx;
    return cp;
}

void rbcglob_compiled_pattern_free(rbcglob_compiled_pattern_t *cp)
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
    if (expand_braces(pattern, &expanded, &expanded_count) != 0)
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
        rbcglob_compiled_pattern_t *cp = rbcglob_compile(expanded[i], flags);
        if (!cp)
        {
            /* Cleanup on error */
            for (size_t j = 0; j < cg->pattern_count; j++)
                rbcglob_compiled_pattern_free(cg->patterns[j]);
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
        rbcglob_compiled_pattern_free(cg->patterns[i]);
    }
    free(cg->patterns);
    free(cg);
}
