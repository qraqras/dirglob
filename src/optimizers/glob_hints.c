/**
 * @file glob_v2_hints.c
 * @brief Hint generation for glob v2
 *
 * Lightweight pattern analysis (20-100ns).
 * 1-pass scan to determine pattern complexity and extract optimization hints.
 */

#include "rbc/glob_hints.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Helper: Extract brace expansion information */
static void extract_brace_info(const char *pattern, glob_brace_info_t *info)
{
    const char *brace_open = strchr(pattern, '{');
    if (!brace_open)
        return;

    /* Prefix: everything before '{' */
    info->prefix = pattern;
    info->prefix_len = brace_open - pattern;

    /* Find matching '}' */
    const char *brace_close = strchr(brace_open, '}');
    if (!brace_close)
        return;

    /* Suffix: everything after '}' */
    info->suffix = brace_close + 1;
    info->suffix_len = strlen(info->suffix);

    /* Parse choices: split by ',' */
    const char *choice_start = brace_open + 1;
    const char *p = choice_start;
    int choice_idx = 0;
    int depth = 0;

    while (*p && *p != '}' && choice_idx < 32)
    {
        if (*p == '{')
        {
            depth++;
        }
        else if (*p == '}')
        {
            if (depth > 0)
            {
                depth--;
            }
            else
            {
                break;
            }
        }
        else if (*p == ',' && depth == 0)
        {
            /* End of choice */
            info->choices[choice_idx].start = choice_start;
            info->choices[choice_idx].len = p - choice_start;
            choice_idx++;
            choice_start = p + 1;
        }
        p++;
    }

    /* Last choice */
    if (choice_idx < 32)
    {
        info->choices[choice_idx].start = choice_start;
        info->choices[choice_idx].len = p - choice_start;
        choice_idx++;
    }

    info->choice_count = choice_idx;

    /* Optimization hints */
    info->can_use_hashset = (info->choice_count >= 4);
    info->all_single_char = true;

    for (int i = 0; i < info->choice_count; i++)
    {
        if (info->choices[i].len != 1)
        {
            info->all_single_char = false;
            break;
        }
    }
}

/* Main hint generation function */
rbc_glob_hints_t rbc_glob_hints_generate(const char *pattern)
{
    rbc_glob_hints_t hints = {0};

    if (!pattern || !*pattern)
    {
        hints.type = GLOB_HINT_LITERAL;
        return hints;
    }

    /* Phase 1: Single-pass scan */
    const char *p = pattern;
    const char *segment_starts[16] = {0};
    int segment_idx = 0;

    segment_starts[0] = pattern;

    while (*p)
    {
        switch (*p)
        {
        case '{':
            hints.flags.has_brace = true;
            hints.brace_depth++;
            break;

        case '}':
            hints.brace_depth--;
            break;

        case '*':
            hints.flags.has_wildcard = true;
            if (p[1] == '*')
            {
                hints.flags.has_doublestar = true;
                p++; /* Skip second '*' */
            }
            break;

        case '?':
            hints.flags.has_wildcard = true;
            break;

        case '[':
            hints.flags.has_bracket = true;
            /* Skip bracket expression */
            p++;
            if (*p == '!' || *p == '^')
                p++;
            if (*p == ']')
                p++; /* Literal ']' at start */
            while (*p && *p != ']')
            {
                if (*p == '\\' && p[1])
                    p++;
                p++;
            }
            break;

        case '/':
            hints.segment_count++;
            if (segment_idx < 15)
            {
                segment_starts[++segment_idx] = p + 1;
            }
            break;

        case '\\':
            hints.flags.has_escape = true;
            if (p[1])
                p++; /* Skip escaped character */
            break;
        }
        p++;
    }

    hints.segment_count++; /* Count includes last segment */

    /* Store segment information */
    hints.segment_info.count = hints.segment_count < 16 ? hints.segment_count : 16;
    for (int i = 0; i < hints.segment_info.count; i++)
    {
        hints.segment_info.segments[i] = segment_starts[i];
        /* Calculate length */
        const char *next = (i + 1 < segment_idx + 1) ? segment_starts[i + 1] - 1 : p;
        hints.segment_info.lengths[i] = next - segment_starts[i];
    }

    /* Phase 2: Determine hint type */

    /* Literal path (no metacharacters) */
    if (!hints.flags.has_wildcard &&
        !hints.flags.has_brace &&
        !hints.flags.has_bracket)
    {
        hints.type = GLOB_HINT_LITERAL;
        hints.cost.estimated_dirs = 0; /* stat() only */
        hints.cost.estimated_io_cost = 1;
        return hints;
    }

    /* Simple pattern (single segment, no brace) */
    if (hints.segment_count == 1 && !hints.flags.has_brace)
    {
        hints.type = GLOB_HINT_SIMPLE_PATTERN;
        hints.cost.estimated_dirs = 1;
        hints.cost.estimated_io_cost = 1;
        return hints;
    }

    /* Multi-segment (no brace, no **) */
    if (!hints.flags.has_brace && !hints.flags.has_doublestar)
    {
        hints.type = GLOB_HINT_MULTI_SEGMENT;
        hints.cost.estimated_dirs = hints.segment_count;
        hints.cost.estimated_io_cost = hints.segment_count;
        return hints;
    }

    /* Recursive pattern */
    if (hints.flags.has_doublestar && !hints.flags.has_brace)
    {
        hints.type = GLOB_HINT_RECURSIVE;
        hints.cost.estimated_dirs = 100; /* Heuristic */
        hints.cost.estimated_io_cost = 100;
        return hints;
    }

    /* Brace expansion */
    if (hints.flags.has_brace)
    {
        /* Extract brace information */
        extract_brace_info(pattern, &hints.brace_info);

        /* Simple brace (single level) */
        if (hints.brace_depth <= 1 && hints.brace_info.choice_count > 0)
        {
            hints.type = GLOB_HINT_BRACE_SINGLE_DIR;

            /* With optimization: 1 scan instead of N scans */
            hints.cost.estimated_dirs = 1;
            hints.cost.estimated_io_cost = 1;
            return hints;
        }

        /* Nested brace */
        if (hints.brace_depth > 1)
        {
            hints.type = GLOB_HINT_BRACE_NESTED;
            hints.cost.estimated_dirs = hints.brace_info.choice_count * 2;
            hints.cost.estimated_io_cost = hints.brace_info.choice_count * 2;
            return hints;
        }
    }

    /* Complex pattern (requires full AST) */
    hints.type = GLOB_HINT_COMPLEX;
    hints.cost.estimated_dirs = 10; /* Heuristic */
    hints.cost.estimated_io_cost = 10;
    return hints;
}

/* Debug helpers */
const char *rbc_glob_hint_type_name(glob_hint_type_t type)
{
    switch (type)
    {
    case GLOB_HINT_LITERAL:
        return "LITERAL";
    case GLOB_HINT_SIMPLE_PATTERN:
        return "SIMPLE_PATTERN";
    case GLOB_HINT_MULTI_SEGMENT:
        return "MULTI_SEGMENT";
    case GLOB_HINT_BRACE_SINGLE_DIR:
        return "BRACE_SINGLE_DIR";
    case GLOB_HINT_BRACE_NESTED:
        return "BRACE_NESTED";
    case GLOB_HINT_RECURSIVE:
        return "RECURSIVE";
    case GLOB_HINT_COMPLEX:
        return "COMPLEX";
    default:
        return "UNKNOWN";
    }
}

void rbc_glob_hints_dump(const rbc_glob_hints_t *hints)
{
    printf("Glob Hints:\n");
    printf("  Type: %s\n", rbc_glob_hint_type_name(hints->type));
    printf("  Flags: brace=%d doublestar=%d wildcard=%d bracket=%d escape=%d\n",
           hints->flags.has_brace,
           hints->flags.has_doublestar,
           hints->flags.has_wildcard,
           hints->flags.has_bracket,
           hints->flags.has_escape);
    printf("  Segments: %d\n", hints->segment_count);
    printf("  Brace depth: %d\n", hints->brace_depth);

    if (hints->type == GLOB_HINT_BRACE_SINGLE_DIR)
    {
        printf("  Brace info:\n");
        printf("    Prefix: '%.*s'\n",
               (int)hints->brace_info.prefix_len,
               hints->brace_info.prefix);
        printf("    Suffix: '%.*s'\n",
               (int)hints->brace_info.suffix_len,
               hints->brace_info.suffix);
        printf("    Choices: %d\n", hints->brace_info.choice_count);
        for (int i = 0; i < hints->brace_info.choice_count; i++)
        {
            printf("      [%d] '%.*s'\n", i,
                   (int)hints->brace_info.choices[i].len,
                   hints->brace_info.choices[i].start);
        }
        printf("    Can use hashset: %d\n", hints->brace_info.can_use_hashset);
        printf("    All single char: %d\n", hints->brace_info.all_single_char);
    }

    printf("  Cost estimate:\n");
    printf("    Directories: %zu\n", hints->cost.estimated_dirs);
    printf("    I/O cost: %zu\n", hints->cost.estimated_io_cost);
}
