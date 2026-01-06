#include <rbcglob/internal/fnmatch.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/* Forward declarations */
static int match_bracket(const char **pattern, char c, unsigned flags);
static int match_recursive(const char *pattern, const char *string, unsigned flags);

int rbcglob_fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
        return 1;
    return match_recursive(pattern, string, flags);
}

static int match_recursive(const char *pattern, const char *string, unsigned flags)
{
    const char *p = pattern;
    const char *s = string;

    while (*p)
    {
        switch (*p)
        {
        case '*':
        {
            /* FNM_DOTMATCH controls whether * matches leading dot */
            if (!(flags & FNM_DOTMATCH) && *s == '.' &&
                (s == string || ((flags & FNM_PATHNAME) && s > string && s[-1] == '/')))
            {
                return 1;
            }

            /* Skip consecutive asterisks */
            while (*p == '*')
                p++;

            /* Trailing * matches everything */
            if (*p == '\0')
            {
                /* But check FNM_PATHNAME: * doesn't match / */
                if (flags & FNM_PATHNAME)
                {
                    while (*s)
                    {
                        if (*s == '/')
                            return 1;
                        s++;
                    }
                }
                return 0;
            }

            /* Try matching at each position */
            while (*s)
            {
                if (match_recursive(p, s, flags) == 0)
                {
                    return 0;
                }
                /* FNM_PATHNAME: * doesn't match / */
                if ((flags & FNM_PATHNAME) && *s == '/')
                {
                    return 1;
                }
                s++;
            }
            return 1;
        }

        case '?':
            /* ? matches any single character except / (if FNM_PATHNAME) */
            if (*s == '\0')
                return 1;
            if ((flags & FNM_PATHNAME) && *s == '/')
                return 1;
            /* FNM_DOTMATCH controls whether ? matches leading dot */
            if (!(flags & FNM_DOTMATCH) && *s == '.' &&
                (s == string || ((flags & FNM_PATHNAME) && s[-1] == '/')))
            {
                return 1;
            }
            p++;
            s++;
            break;

        case '[':
        {
            /* Character class */
            if (*s == '\0')
                return 1;
            /* FNM_PATHNAME: [...] doesn't match / */
            if ((flags & FNM_PATHNAME) && *s == '/')
                return 1;
            /* FNM_DOTMATCH controls whether [...] matches leading dot */
            if (!(flags & FNM_DOTMATCH) && *s == '.' &&
                (s == string || ((flags & FNM_PATHNAME) && s[-1] == '/')))
            {
                return 1;
            }

            p++; /* Skip '[' */
            if (match_bracket(&p, *s, flags) != 0)
            {
                return 1;
            }
            s++;
            break;
        }

        case '\\':
            /* Backslash escaping */
            if (!(flags & FNM_NOESCAPE) && p[1] != '\0')
            {
                p++; /* Skip backslash, use next char literally */
            }
            /* Fall through to literal match */
            /* fallthrough */

        default:
        {
            /* Literal character match */
            char pc = *p;
            char sc = *s;

            /* FNM_CASEFOLD: case-insensitive comparison */
            if (flags & FNM_CASEFOLD)
            {
                pc = tolower((unsigned char)pc);
                sc = tolower((unsigned char)sc);
            }

            if (pc != sc)
                return 1;

            /* Check for leading dot */
            if (!(flags & FNM_DOTMATCH) && *s == '.' &&
                (s == string || ((flags & FNM_PATHNAME) && s > string && s[-1] == '/')))
            {
                /* Pattern must explicitly match leading dot */
                if (*p != '.')
                    return 1;
            }

            p++;
            s++;
            break;
        }
        }
    }

    /* Both must be at end */
    return (*s == '\0') ? 0 : 1;
}

static int match_bracket(const char **pattern, char c, unsigned flags)
{
    const char *p = *pattern;
    bool negate = false;
    bool matched = false;

    /* Check for negation */
    if (*p == '!' || *p == '^')
    {
        negate = true;
        p++;
    }

    /* Empty bracket is invalid, treat ] as literal */
    if (*p == ']')
    {
        p++;
        if (flags & FNM_CASEFOLD)
        {
            matched = (tolower((unsigned char)c) == tolower((unsigned char)']'));
        }
        else
        {
            matched = (c == ']');
        }
    }

    char prev = 0;
    while (*p && *p != ']')
    {
        if (*p == '-' && prev && p[1] != ']' && p[1] != '\0')
        {
            /* Range */
            p++;
            char start = prev;
            char end = *p;

            if (flags & FNM_CASEFOLD)
            {
                c = tolower((unsigned char)c);
                start = tolower((unsigned char)start);
                end = tolower((unsigned char)end);
            }

            if (c >= start && c <= end)
            {
                matched = true;
            }
            prev = 0;
        }
        else
        {
            /* Single character */
            char pc = *p;
            char cc = c;

            if (flags & FNM_CASEFOLD)
            {
                pc = tolower((unsigned char)pc);
                cc = tolower((unsigned char)cc);
            }

            if (pc == cc)
            {
                matched = true;
            }
            prev = *p;
        }
        p++;
    }

    /* Skip closing ] */
    if (*p == ']')
        p++;

    *pattern = p;
    return (matched != negate) ? 0 : 1;
}
