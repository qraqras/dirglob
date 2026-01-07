#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/file.h>
#include <rbcglob/internal/compiler.h>

static bool rbcglob_token_match_internal(const rbcglob_token_t *tokens, size_t token_index, size_t token_count, const char *s, size_t pos, unsigned flags)
{
    if (token_index == token_count)
    {
        return s[pos] == '\0';
    }

    const rbcglob_token_t *token = &tokens[token_index];

    switch (token->token_type)
    {
    case RBCGLOB_TOKEN_CHAR:
    {
        char c1 = token->c;
        char c2 = s[pos];
        if (flags & RBCGLOB_FNM_CASEFOLD)
        {
            if (tolower((unsigned char)c1) != tolower((unsigned char)c2))
                return false;
        }
        else
        {
            if (c1 != c2)
                return false;
        }
        if (c2 == '\0')
            return false;
        return rbcglob_token_match_internal(tokens, token_index + 1, token_count, s, pos + 1, flags);
    }
    case RBCGLOB_TOKEN_ANY_CHAR:
        if (s[pos] == '\0')
            return false;
        return rbcglob_token_match_internal(tokens, token_index + 1, token_count, s, pos + 1, flags);
    case RBCGLOB_TOKEN_ANY_SEQUENCE:
        // Backtracking for *
        for (size_t i = 0;; i++)
        {
            if (rbcglob_token_match_internal(tokens, token_index + 1, token_count, s, pos + i, flags))
                return true;
            if (s[pos + i] == '\0')
                break;
        }
        return false;
    case RBCGLOB_TOKEN_ANY_WITHIN:
    case RBCGLOB_TOKEN_ANY_EXCEPT:
    {
        if (s[pos] == '\0')
            return false;
        bool match = false;
        char target = s[pos];
        if (flags & RBCGLOB_FNM_CASEFOLD)
            target = tolower((unsigned char)target);

        for (size_t i = 0; i < token->range_count; i++)
        {
            char start = token->ranges[i].start;
            char end = token->ranges[i].end;
            if (flags & RBCGLOB_FNM_CASEFOLD)
            {
                start = tolower((unsigned char)start);
                end = tolower((unsigned char)end);
            }
            if (target >= start && target <= end)
            {
                match = true;
                break;
            }
        }
        if (token->token_type == RBCGLOB_TOKEN_ANY_EXCEPT)
            match = !match;
        if (!match)
            return false;
        return rbcglob_token_match_internal(tokens, token_index + 1, token_count, s, pos + 1, flags);
    }
    }
    return false;
}

bool rbcglob_token_match_segment(const rbcglob_segment_t *seg, const char *str, unsigned flags)
{
    if (seg->type == RBCGLOB_SEGMENT_LITERAL)
    {
        return (flags & RBCGLOB_FNM_CASEFOLD) ? strcasecmp(seg->pattern, str) == 0 : strcmp(seg->pattern, str) == 0;
    }
    return rbcglob_token_match_internal(seg->tokens, 0, seg->token_count, str, 0, flags);
}

static bool rbcglob_fnmatch_compiled_pathname_internal(const rbcglob_segment_t *segments, size_t segment_index, size_t segment_count, const char *string, unsigned flags)
{
    if (segment_index == segment_count)
    {
        return *string == '\0';
    }

    const rbcglob_segment_t *seg = &segments[segment_index];

    if (seg->type == RBCGLOB_SEGMENT_RECURSIVE)
    {
        // 1. Zero match
        if (rbcglob_fnmatch_compiled_pathname_internal(segments, segment_index + 1, segment_count, string, flags))
        {
            return true;
        }
        // 2. Consume components
        const char *p = string;
        while (*p)
        {
            if (!(flags & RBCGLOB_FNM_DOTMATCH) && *p == '.')
            {
                break;
            }

            const char *slash = strchr(p, '/');
            const char *next = slash ? slash + 1 : p + strlen(p);

            if (rbcglob_fnmatch_compiled_pathname_internal(segments, segment_index + 1, segment_count, next, flags))
            {
                return true;
            }
            if (!slash)
                break;
            p = next;
        }
        return false;
    }
    else
    {
        const char *slash = strchr(string, '/');
        size_t len = slash ? (size_t)(slash - string) : strlen(string);
        char component[4096];
        if (len >= sizeof(component))
            return false;
        memcpy(component, string, len);
        component[len] = '\0';

        // Check FNM_DOTMATCH
        if (!(flags & RBCGLOB_FNM_DOTMATCH) && component[0] == '.')
        {
            if (seg->token_count > 0 && seg->tokens[0].token_type == RBCGLOB_TOKEN_CHAR && seg->tokens[0].c == '.')
            {
                // OK: explicitly starting with .
            }
            else if (seg->type == RBCGLOB_SEGMENT_LITERAL && seg->pattern[0] == '.')
            {
                // OK
            }
            else
            {
                return false;
            }
        }

        if (rbcglob_token_match_segment(seg, component, flags))
        {
            const char *next = string + len;
            if (*next == '/')
                next++;
            return rbcglob_fnmatch_compiled_pathname_internal(segments, segment_index + 1, segment_count, next, flags);
        }
        return false;
    }
}

bool rbcglob_fnmatch_pattern_compiled(const rbcglob_compiled_pattern_t *cp, const char *string)
{
    if (!cp || !string)
        return false;

    bool match;
    unsigned flags = cp->flags;

    if (!(flags & RBCGLOB_FNM_PATHNAME))
    {
        // Check dotmatch at start of entire string
        if (!(flags & RBCGLOB_FNM_DOTMATCH) && string[0] == '.')
        {
            const rbcglob_segment_t *seg = &cp->segments[0];
            if (seg->token_count > 0 && seg->tokens[0].token_type == RBCGLOB_TOKEN_CHAR && seg->tokens[0].c == '.')
            {
                // OK
            }
            else if (seg->type == RBCGLOB_SEGMENT_LITERAL && seg->pattern[0] == '.')
            {
                // OK
            }
            else
            {
                return false;
            }
        }
        match = rbcglob_token_match_segment(&cp->segments[0], string, flags);
    }
    else
    {
        match = rbcglob_fnmatch_compiled_pathname_internal(cp->segments, 0, cp->count, string, flags);
    }

    return match;
}

bool rbcglob_fnmatch_compiled(const rbcglob_compiled_glob_t *cg, const char *path)
{
    if (!cg || !path)
        return false;

    for (size_t i = 0; i < cg->pattern_count; i++)
    {
        if (rbcglob_fnmatch_pattern_compiled(cg->patterns[i], path))
        {
            return true;
        }
    }
    return false;
}

bool rbcglob_fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
        return false;

    rbcglob_compiled_glob_t *cg = rbcglob_compile_glob(pattern, flags);
    if (!cg)
        return false;

    bool res = rbcglob_fnmatch_compiled(cg, string);

    rbcglob_compiled_glob_free(cg);
    return res;
}
