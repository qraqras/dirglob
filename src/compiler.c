#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <rbc/rbc.h>

#include "internal.h"
#include "utils.h"

/// @brief Check if the string is a pure recursive wildcard "**"
/// @param s String to check
/// @return true if it is "**", false otherwise
static bool is_recursive_wildcard(const char *s)
{
    return strcmp(s, "**") == 0;
}

/// @brief Check if the string contains any unescaped wildcard characters
/// @param str String to check
/// @return true if contains wildcard characters, false otherwise
static bool rbc_has_wildcard(const char *str)
{
    bool esc = false;
    for (const char *p = str; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '*' || *p == '?' || *p == '[')
            return true;
    }
    return false;
}

/// @brief Check if the string contains any unescaped brace characters
/// @param str String to check
/// @return true if contains brace characters, false otherwise
static bool rbc_has_brace(const char *str)
{
    bool esc = false;
    for (const char *p = str; *p; p++)
    {
        if (esc)
        {
            esc = false;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            continue;
        }
        if (*p == '{')
            return true;
    }
    return false;
}

/// @brief Find the end of the current segment in the pattern
/// @param str Pattern string
/// @return Pointer to the end of the segment (either '/' or '\0')
static const char *rbc_find_segment_end(const char *str)
{
    bool esc = false;
    int depth = 0;
    const char *p = str;
    while (*p)
    {
        if (esc)
        {
            esc = false;
            p++;
            continue;
        }
        if (*p == '\\')
        {
            esc = true;
            p++;
            continue;
        }

        if (*p == '{')
            depth++;
        else if (*p == '}')
        {
            if (depth > 0)
                depth--;
        }
        else if (*p == '/' && depth == 0)
            return p;

        p++;
    }
    return p;
}

/// @brief Create a new rbc_segment_t of the specified type
/// @param arena Arena to allocate from
/// @param type Segment type
/// @return Pointer to the new segment
static rbc_segment_t *rbc_segment_new(rbc_arena_t *arena, rbc_segment_type_t type)
{
    rbc_segment_t *seg = rbc_arena_alloc(arena, sizeof(rbc_segment_t));
    memset(seg, 0, sizeof(rbc_segment_t));
    seg->type = type;
    return seg;
}

/// @brief Compile pattern into segments
/// @param arena Arena to allocate from
/// @param pattern Pattern string
/// @param flags Compilation flags
/// @return Pointer to the head segment
rbc_segment_t *rbc_compile_segments(rbc_arena_t *arena, const char *pattern, unsigned int flags)
{
    if (!pattern || !*pattern)
        return NULL;

    rbc_segment_t *head = NULL;
    rbc_segment_t *curr = NULL;

    const char *p = pattern;
    while (*p)
    {
        const char *end = rbc_find_segment_end(p);
        size_t len = end - p;

        if (len == 0 && *end == '/')
        {
            if (head != NULL)
            {
                // Collapse multiple slashes or leading slash in the middle
                p = end + 1;
                continue;
            }
        }

        if (len == 0 && *end != '/')
        {
            p = end;
            continue;
        }

        char *component = rbc_arena_alloc(arena, len + 1);
        memcpy(component, p, len);
        component[len] = '\0';

        // printf("DEBUG: Component='%s' len=%zu\n", component, len);

        bool is_sep = (*end == '/');
        p = is_sep ? end + 1 : end;
        const char *rest = p;

        if (is_sep && !*p)
        {
            // Trailing slash: add an empty literal segment
            // Note: If previous was **, do we collapse? No. **/ matches directories.
            // ** matches everything.
            rbc_segment_t *trail = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
            trail->data.literal = "";
            if (!head)
                head = trail;
            else
                curr->next = trail;
            curr = trail;
            continue;
        }

        rbc_segment_t *seg = NULL;
        // Use the compiler's arena for brace expansion.
        // This avoids malloc/free overhead for temporary strings.
        // The expanded strings will persist in the arena for the lifetime of the compiled glob, which is acceptable.
        rbc_str_list_t expansions = rbc_brace_collect(component, arena);

        // Special Case: Pure Recursive Wildcard
        if (!rbc_has_brace(component) && is_recursive_wildcard(component))
        {
            // Collapse consecutive recursive wildcards
            if (curr && curr->type == RBC_SEGMENT_RECURSIVE)
            {
                printf("DEBUG: Collapsing ** (prev type=%d)\n", curr->type);
                rbc_str_list_free(&expansions);
                // We consume this component but do NOT create a new segment.
                // However, we must ensure that if this was followed by a slash, the previous one handles it?
                // The previous segment is already created.
                // This component is skipped.
                continue;
            }

            printf("DEBUG: Created ** segment\n");
            seg = rbc_segment_new(arena, RBC_SEGMENT_RECURSIVE);
            rbc_str_list_free(&expansions);
            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            continue;
        }

        // Special Case: Simple Literal (No braces, no wildcards)
        if (!rbc_has_brace(component) && !rbc_has_wildcard(component))
        {
            seg = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
            seg->data.literal = component;
            rbc_str_list_free(&expansions);
            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            continue;
        }

        bool any_slash = false;
        bool all_literals = true;
        for (size_t i = 0; i < expansions.count; i++)
        {
            if (strchr(expansions.items[i], '/'))
                any_slash = true;
            if (rbc_has_wildcard(expansions.items[i]))
                all_literals = false;
        }

        if (any_slash || all_literals || expansions.count > 1)
        {
            // Case A: SEG_BRANCH (Stat Optimization, Topology Split, or Brace Branching)
            seg = rbc_segment_new(arena, RBC_SEGMENT_BRANCH);
            rbc_segment_t *last_alt = NULL;

            for (size_t i = 0; i < expansions.count; i++)
            {
                char *full_pattern;
                // If is_sep, we need to append "/" + rest.
                // If rest is empty, we just append "/"?
                // Wait, if pattern ended with /, "dir/" -> component "dir". rest "".
                // If we expand dir -> "dir". full_pattern = "dir/".

                if (is_sep)
                {
                    full_pattern = rbc_arena_printf(arena, "%s/%s", expansions.items[i], rest);
                }
                else if (*rest)
                {
                    // Should technically not happen if we parsed correctly up to separator,
                    // unless separator was implicit or missing?
                    // Just append.
                    full_pattern = rbc_arena_printf(arena, "%s%s", expansions.items[i], rest);
                }
                else
                {
                    full_pattern = rbc_arena_strdup(arena, expansions.items[i]);
                }

                rbc_segment_t *alt_chain = NULL;

                // Optimization: Short-circuit recursion for simple literal leaves (Pattern 1)
                // If we know this branch is a simple literal (no slash append, no rest, and from all_literals set),
                // we can create the node directly without re-parsing.
                if (all_literals && !is_sep && !*rest)
                {
                    alt_chain = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
                    alt_chain->data.literal = full_pattern;
                }
                else
                {
                    alt_chain = rbc_compile_segments(arena, full_pattern, flags);
                }

                // Handle empty expansion case?
                if (!alt_chain && !*full_pattern)
                {
                    alt_chain = rbc_segment_new(arena, RBC_SEGMENT_LITERAL);
                    alt_chain->data.literal = "";
                }

                if (alt_chain)
                {
                    if (!seg->data.branch.head)
                        seg->data.branch.head = alt_chain;
                    else if (last_alt)
                        last_alt->next_alt = alt_chain;
                    last_alt = alt_chain;
                }
            }
            rbc_str_list_free(&expansions);

            // SEG_BRANCH consumes the rest using recursion. Break loop.
            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            break;
        }
        else
        {
            // Case B: SEG_WILDCARD (Optimization Strategy)
            seg = rbc_segment_new(arena, RBC_SEGMENT_WILDCARD);
            seg->data.glob.original_pattern = rbc_arena_strdup(arena, expansions.items[0]);

            // Analyze pattern and select strategy
            rbc_matcher_build(arena, &seg->data.glob.matcher, seg->data.glob.original_pattern, flags);

            rbc_str_list_free(&expansions);

            if (!head)
                head = seg;
            else
                curr->next = seg;
            curr = seg;
            // Do NOT break here. Continue parsing next component.
        }
    }
    return head;
}
