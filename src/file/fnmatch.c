#include <rbcglob/rbcglob.h>
#include <rbcglob/internal/file.h>
#include <rbcglob/internal/graph.h>
#include <rbcglob/internal/traverse.h>
#include <rbcglob/internal/utils.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static bool nfa_match_recursive(const rbcglob_node_t *node, const char *str, unsigned flags, bool start_of_component);

/* Helper to determine if we update start_of_component logic */
static bool is_separator(char c)
{
    return c == '/';
}

static bool nfa_match_recursive(const rbcglob_node_t *node, const char *str, unsigned flags, bool start_of_component)
{
    if (!node)
        return false;

    // End of graph?
    if (node->type == OP_ACCEPT)
        return *str == '\0';

    switch (node->type)
    {
    case OP_MATCH_LITERAL:
    {
        size_t len = strlen(node->data.literal);
        if (flags & RBCGLOB_FNM_CASEFOLD)
        {
            if (strncasecmp(str, node->data.literal, len) != 0)
                return false;
        }
        else
        {
            if (strncmp(str, node->data.literal, len) != 0)
                return false;
        }

        // Update start_of_component for next node
        // Only if FNM_PATHNAME is set do we care about components resetting?
        // Actually FNM_DOTMATCH check usually applies at path parts.
        // If we matched a literal '/', next is start of component.
        bool next_start = false;
        if (flags & RBCGLOB_FNM_PATHNAME)
        {
            if (len > 0 && is_separator(node->data.literal[len - 1]))
            {
                next_start = true;
            }
        }
        // If literal did not end in /, next_start is false (unless we are still at start?? no)
        // Actually, if literal consumes "foo/", next is start.
        // If literal is ".", next is NOT start.

        return nfa_match_recursive(node->next, str + len, flags, next_start);
    }
    case OP_MATCH_QMARK:
    {
        if (*str == '\0')
            return false;
        if (flags & RBCGLOB_FNM_PATHNAME)
        {
            if (*str == '/')
                return false;
        }

        // Dot check
        if (!(flags & RBCGLOB_FNM_DOTMATCH) && start_of_component && *str == '.')
        {
            return false;
        }

        return nfa_match_recursive(node->next, str + 1, flags, false);
    }
    case OP_MATCH_STAR:
    {
        // Check leading dot constraint for the star itself
        // Star can match empty string always.
        // Star can match more characters ONLY IF valid.

        // 1. Try match 0 chars (always allowed, does not consume dot)
        if (nfa_match_recursive(node->next, str, flags, start_of_component))
            return true;

        // 2. Prepare to consume chars
        if (*str == '\0')
            return false;

        // If leading dot constraint applies, star cannot consume the first char if it is '.'
        if (!(flags & RBCGLOB_FNM_DOTMATCH) && start_of_component && *str == '.')
        {
            return false;
        }

        const char *p = str;
        while (*p)
        {
            // If PATHNAME, stop at /
            if ((flags & RBCGLOB_FNM_PATHNAME) && *p == '/')
            {
                break;
            }

            p++; // Consume one char

            // After consuming at least one char, we are definitely NOT at start of component relative to the STAR's context.
            // But we pass 'false' to next node?
            // If STAR consumes "foo", next node sees what follows.
            // However, if we just consumed, the next state is checked against p.
            // Does STAR consumption change start_of_component for the *next* node?
            // No, start_of_component indicates if *str* (current pos) is at start.
            // If we advance p, recursing with node->next calls with new string position.
            // If we consumed chars, the next node is NOT at start of component (unless we consumed '/')
            // But STAR (OP_MATCH_STAR) does not match '/' if PATHNAME is set.
            // If PATHNAME is NOT set, STAR can consume '/'.

            bool next_start = false;
            if (!(flags & RBCGLOB_FNM_PATHNAME))
            {
                // If not pathname, star can consume anything.
                // Does it reset component start?
                // If we invoke next node, we are inside a string.
                // Usually FNM_DOTMATCH check is only strictly at the beginning of the filename or after /.
            }
            // Logic: if we advanced p, we are not at start of component for the remainder?
            // Unless we crossed a separator.

            if (nfa_match_recursive(node->next, p, flags, next_start))
                return true;
        }
        return false;
    }
    case OP_MATCH_STAR2: // **
    {
        // ** matches everything
        // 1. Try 0
        if (nfa_match_recursive(node->next, str, flags, start_of_component))
            return true;

        if (*str == '\0')
            return false;

        // Leading dot check for **?
        // Ruby: File.fnmatch('**', '.a', 0) => true (Wait, ** matches dotfiles?)
        // Docs: "** matches recursively".
        // Ruby check: `File.fnmatch('**', '.a')` -> true in generic sense?
        // Actually `**` usually implies `*` logic but flexible.
        // If `**` starts with `.`, does it match?
        // `Dir.glob` with `**` does NOT match dotfiles unless DOTMATCH.
        // So I should enforce dot check here too.

        if (!(flags & RBCGLOB_FNM_DOTMATCH) && start_of_component && *str == '.')
        {
            // Cannot consume leading dot?
            // Actually `**` includes `*`.
            return false;
        }

        const char *p = str;
        while (*p)
        {
            p++;
            // Determine if p is now start of component?
            // If we crossed '/', yes.
            bool is_start = false;
            if ((flags & RBCGLOB_FNM_PATHNAME) && p > str && is_separator(*(p - 1)))
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

        // Dot check
        if (!(flags & RBCGLOB_FNM_DOTMATCH) && start_of_component && *str == '.')
            return false;

        char c = *str; // Check logic from prev attempt
        if (flags & RBCGLOB_FNM_CASEFOLD)
            c = tolower((unsigned char)c);

        if ((flags & RBCGLOB_FNM_PATHNAME) && c == '/')
            return false;

        bool match = true; // Placeholder for class logic
        if (node->data.char_class.negated)
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

    rbcglob_ctx_t ctx;
    rbcglob_ctx_init(&ctx);

    // Compile pattern
    rbcglob_node_t *graph = rbcglob_nfa_compile(&ctx.arena, pattern);
    if (!graph)
    {
        rbcglob_ctx_free(&ctx);
        return false;
    }

    // Initial start_of_component is true
    bool match = nfa_match_recursive(graph, string, flags, true);

    rbcglob_ctx_free(&ctx);
    return match;
}
