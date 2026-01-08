#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>

#include "rbcglob/rbcglob.h"
#include "rbcglob/internal/graph.h"
#include "rbcglob/internal/utils.h"

static bool is_separator(char c)
{
    return c == '/';
}

static bool nfa_match_recursive(rbcglob_node_t *node, const char *str, unsigned flags, bool start_of_component)
{
    if (!node)
        return false;

    if (node->type == OP_ACCEPT)
    {
        return *str == '\0';
    }

    switch (node->type)
    {
    case OP_MATCH_LITERAL:
    {
        const char *lit = node->data.literal;
        size_t lit_len = strlen(lit);
        if (lit_len == 0)
            return nfa_match_recursive(node->next, str, flags, start_of_component);

        // Check if string matches literal
        for (size_t i = 0; i < lit_len; i++)
        {
            if (*str == '\0')
                return false;

            char c_str = *str;
            char c_lit = lit[i];

            if (is_separator(c_str))
            {
                // If we hit a separator, update start_of_component for remainder?
                // But literal matching implies continuous sequence.
                // If LITERAL is "a/b", we consume slash.
                // However, the rule for leading dot check only applies at start of component.
                // If LITERAL starts with '.', we must check if allowed.
                if (i == 0 && start_of_component && c_lit == '.' && !(flags & RBCGLOB_FNM_DOTMATCH))
                    return false;
            }
            else if (i == 0 && start_of_component && c_lit == '.' && !(flags & RBCGLOB_FNM_DOTMATCH))
            {
                return false;
            }

            if (flags & RBCGLOB_FNM_CASEFOLD)
            {
                if (tolower((unsigned char)c_str) != tolower((unsigned char)c_lit))
                    return false;
            }
            else
            {
                if (c_str != c_lit)
                    return false;
            }
            str++;
        }

        // After matching literal "foo/", the next component starts if literal ended with /
        bool new_start = false;
        if (lit_len > 0 && is_separator(lit[lit_len - 1]))
            new_start = true;

        // If literal did NOT contain slash, we carry over 'false' (because we are in mid-component)
        // Unless we just started? No, if we consumed chars, we are not at start anymore.

        return nfa_match_recursive(node->next, str, flags, new_start);
    }
    case OP_MATCH_QMARK:
    {
        if (*str == '\0')
            return false;

        // Dot check
        if (!(flags & RBCGLOB_FNM_DOTMATCH) && start_of_component && *str == '.')
            return false;

        // Separator check
        if ((flags & RBCGLOB_FNM_PATHNAME) && is_separator(*str))
            return false;

        return nfa_match_recursive(node->next, str + 1, flags, false);
    }
    case OP_MATCH_STAR:
    {
        // Dot check at start
        if (!(flags & RBCGLOB_FNM_DOTMATCH) && start_of_component && *str == '.')
            return false;

        // * matches 0 or more chars
        // 1. Try match 0 chars
        if (nfa_match_recursive(node->next, str, flags, false)) // false because if we consume 0, next node continues?
            // Wait, if we consume 0, next node sees the SAME string.
            // So start_of_component should be preserved?
            // Actually, if we consume 0, we are still at start_of_component? Yes.
            // But if we backtrack, we might loop.
            // Let's rely on iteration below.
            return true;

        // 0-match checked above (implied by loop start? No, loop consumes 1..N)
        // Wait, loop below starts consuming 0? No, pointer p=str. Loop checks *p.

        // Standard backtracking implementation for *
        // Try consuming 0, 1, 2...

        const char *p = str;
        // Try 0 match first:
        // Pass 'false' for start_of_component?
        // If * consumes nothing, the next node sees 'str' at 'start_of_component'.
        // So actually we should call:
        if (nfa_match_recursive(node->next, str, flags, start_of_component))
            return true;

        while (*p)
        {
            // If PATHNAME, stop at /
            if ((flags & RBCGLOB_FNM_PATHNAME) && *p == '/')
            {
                break;
            }

            p++;
            // Consumed one or more chars. Not start of component anymore.
            if (nfa_match_recursive(node->next, p, flags, false))
                return true;
        }
        return false;
    }
    case OP_MATCH_STAR2:
    {
        if (nfa_match_recursive(node->next, str, flags, start_of_component))
            return true;

        if (!(flags & RBCGLOB_FNM_DOTMATCH) && start_of_component && *str == '.')
        {
            return false;
        }

        const char *p = str;
        while (*p)
        {
            p++;
            bool is_start = false;
            // If we just consumed '/', next is start
            if ((flags & RBCGLOB_FNM_PATHNAME) && is_separator(*(p - 1)))
            {
                is_start = true;
            }

            if (nfa_match_recursive(node->next, p, flags, is_start))
                return true;
        }
        return false;
    }
    case OP_MATCH_CLASS:
    {
        if (*str == '\0')
            return false;

        if (!(flags & RBCGLOB_FNM_DOTMATCH) && start_of_component && *str == '.')
            return false;

        unsigned char uc = (unsigned char)*str;
        bool match = (node->data.char_class.map[uc / 8] & (1 << (uc % 8))) != 0;

        if (flags & RBCGLOB_FNM_CASEFOLD)
        {
            unsigned char lower = tolower(uc);
            unsigned char upper = toupper(uc);
            if ((node->data.char_class.map[lower / 8] & (1 << (lower % 8))))
                match = true;
            if ((node->data.char_class.map[upper / 8] & (1 << (upper % 8))))
                match = true;
        }

        if ((flags & RBCGLOB_FNM_PATHNAME) && uc == '/')
            return false;

        if (node->data.char_class.is_negated)
            match = !match;

        if (!match)
            return false;

        return nfa_match_recursive(node->next, str + 1, flags, false);
    }
    case OP_FORK:
    {
        if (nfa_match_recursive(node->data.branch.next, str, flags, start_of_component))
            return true;
        return nfa_match_recursive(node->data.branch.alt, str, flags, start_of_component);
    }
    case OP_JUMP:
    {
        return nfa_match_recursive(node->next, str, flags, start_of_component);
    }
    default:
        return false;
    }
}

bool rbcglob_fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
        return false;

    rbcglob_arena_t arena;
    rbcglob_arena_init(&arena, 4096);

    rbcglob_node_t *graph = rbcglob_compile_nfa_fragment(&arena, pattern);
    if (!graph)
    {
        rbcglob_arena_destroy(&arena);
        return false;
    }

    bool match = nfa_match_recursive(graph, string, flags, true);

    rbcglob_arena_destroy(&arena);
    return match;
}
