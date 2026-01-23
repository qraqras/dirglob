#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <stdlib.h>
#include "internal.h"

// Wildmatch compatibility definitions
typedef unsigned char uchar;

#define RBC_NOMATCH 1
#define RBC_MATCH 0
#define RBC_ABORT_ALL -1
#define RBC_ABORT_TO_STARSTAR -2

// Character class macros
#define ISUPPER(c) isupper((unsigned char)(c))
#define ISLOWER(c) islower((unsigned char)(c))

#define IS_SEGMENT_START(text, text_start, flags) ((text) == (text_start) || ((flags) & RBC_FNM_PATHNAME && (text) > (text_start) && (text)[-1] == '/'))
#define IS_HIDDEN_TEXT(text, text_start, flags) (IS_SEGMENT_START(text, text_start, flags) && *(text) == '.')
#define IS_SLASH_DOT_PATTERN(p) ((p)[0] == '/' && (p)[1] == '.')

static inline int is_glob_special(unsigned char c)
{
    return c == '*' || c == '?' || c == '[' || c == '\\';
}

// ============================================================================
// Forward Declarations
// ============================================================================

static int rbc_match_core(const uchar *p, const uchar *text, unsigned int flags, const uchar *text_start);

/// @brief Core matching function (wildmatch-style)
/// @param p Pattern
/// @param t Text
/// @param flags Matching flags
/// @param t_start Start of the text (for dotfile checks)
/// @return RBC_MATCH, RBC_NOMATCH, RBC_ABORT_ALL, or RBC_ABORT_TO_STARSTAR
static int rbc_match_core(const uchar *p, const uchar *t, unsigned int flags, const uchar *t_start)
{
    uchar p_ch;

    for (; (p_ch = *p) != '\0'; t++, p++)
    {
        int matched, match_slash, negated;
        uchar t_ch, prev_ch;
        if ((t_ch = *t) == '\0' && p_ch != '*')
            return RBC_ABORT_ALL;
        if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(t_ch))
            t_ch = tolower(t_ch);
        if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(p_ch))
            p_ch = tolower(p_ch);
        switch (p_ch)
        {
        case '\\':
            // <Ruby>: NOESCAPE
            if (!(flags & RBC_FNM_NOESCAPE))
            {
                p_ch = *++p;
            }
            // **** fallthrough ****
        default:
            if (t_ch != p_ch)
                return RBC_NOMATCH;
            continue;
        case '?':
            if ((flags & RBC_FNM_PATHNAME) && t_ch == '/')
                return RBC_NOMATCH;
            // <Ruby>: DOTMATCH
            if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags))
                return RBC_NOMATCH;
            continue;
        case '*':
            // <Ruby>: DOTMATCH
            if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags))
                return RBC_NOMATCH;

            if (*++p == '*')
            {
                const uchar *prev_p = p;
                while (*++p == '*')
                {
                }
                if (!(flags & RBC_FNM_PATHNAME))
                    match_slash = 1;
                // <Ruby>: NOESCAPE
                else if ((prev_p - p + (*p ? 1 : 0) < 2 || (p > prev_p && *(p - (prev_p - p) - 2) == '/')) && (*p == '\0' || *p == '/' || (!(flags & RBC_FNM_NOESCAPE) && (p[0] == '\\' && p[1] == '/'))))
                {
                    if (p[0] == '/' &&
                        rbc_match_core(p + 1, t, flags, t_start) == RBC_MATCH)
                        return RBC_MATCH;
                    match_slash = 1;
                }
                else
                    match_slash = 0;
            }
            else
                match_slash = flags & RBC_FNM_PATHNAME ? 0 : 1;

            if (*p == '\0')
            {
                if (!match_slash)
                {
                    if (strchr((char *)t, '/'))
                        return RBC_ABORT_TO_STARSTAR;
                    // <Ruby>: DOTMATCH - single * should not match leading dot
                    if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags))
                        return RBC_NOMATCH;
                }
                return RBC_MATCH;
            }
            else if (!match_slash && *p == '/')
            {
                const char *slash = strchr((char *)t, '/');
                if (!slash)
                    return RBC_ABORT_ALL;
                t = (const uchar *)slash;
                break;
            }
            while (1)
            {
                if (t_ch == '\0')
                    break;

                // <Ruby>: DOTMATCH
                // next_slash check to avoid false positive on segments
                if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags) && !IS_SLASH_DOT_PATTERN(p))
                {
                    const uchar *next_slash = (const uchar *)strchr((char *)t, '/');
                    if (next_slash && next_slash[1] == '.')
                        return RBC_ABORT_ALL;
                }

                if (!is_glob_special(*p))
                {
                    p_ch = *p;
                    if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(p_ch))
                        p_ch = tolower(p_ch);
                    while ((t_ch = *t) != '\0' &&
                           (match_slash || t_ch != '/'))
                    {
                        if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(t_ch))
                            t_ch = tolower(t_ch);
                        if (t_ch == p_ch)
                            break;
                        t++;
                    }
                    if (t_ch != p_ch)
                    {
                        if (match_slash)
                            return RBC_ABORT_ALL;
                        else
                            return RBC_ABORT_TO_STARSTAR;
                    }
                }

                // <Ruby>: DOTMATCH - skip recursion if dotfile but pattern doesn't match dots
                if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags) && !IS_SLASH_DOT_PATTERN(p))
                {
                    matched = RBC_NOMATCH;
                }
                else if ((matched = rbc_match_core(p, t, flags, t_start)) != RBC_NOMATCH)
                {
                    if (!match_slash || matched != RBC_ABORT_TO_STARSTAR)
                    {
                        return matched;
                    }
                }
                else if (!match_slash && t_ch == '/')
                {
                    return RBC_ABORT_TO_STARSTAR;
                }

                t_ch = *++t;
            }
            return RBC_ABORT_ALL;
        case '[':
            // <Ruby>: DOTMATCH - bracket expressions don't match leading dot unless DOTMATCH
            if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(t, t_start, flags))
                return RBC_NOMATCH;
            p_ch = *++p;
            negated = p_ch == '!' || p_ch == '^' ? 1 : 0;
            if (negated)
            {
                p_ch = *++p;
            }
            prev_ch = 0;
            matched = 0;
            do
            {
                if (!p_ch)
                    return RBC_ABORT_ALL;
                // <Ruby>: NOESCAPE
                if (!(flags & RBC_FNM_NOESCAPE) && p_ch == '\\')
                {
                    p_ch = *++p;
                    if (!p_ch)
                        return RBC_ABORT_ALL;
                    if (t_ch == p_ch)
                        matched = 1;
                }
                else if (p_ch == '-' && prev_ch && p[1] && p[1] != ']')
                {
                    p_ch = *++p;
                    // <Ruby>: NOESCAPE
                    if (!(flags & RBC_FNM_NOESCAPE) && p_ch == '\\')
                    {
                        p_ch = *++p;
                        if (!p_ch)
                            return RBC_ABORT_ALL;
                    }
                    if (t_ch <= p_ch && t_ch >= prev_ch)
                        matched = 1;
                    else if ((flags & RBC_FNM_CASEFOLD) && ISLOWER(t_ch))
                    {
                        uchar t_ch_upper = toupper(t_ch);
                        if (t_ch_upper <= p_ch && t_ch_upper >= prev_ch)
                            matched = 1;
                    }
                    p_ch = 0;
                }
                else if (t_ch == p_ch)
                    matched = 1;
                prev_ch = p_ch;
                p_ch = *++p;
            } while (p_ch != ']');
            if (matched == negated ||
                ((flags & RBC_FNM_PATHNAME) && t_ch == '/'))
                return RBC_NOMATCH;
            continue;
        }
    }

    return *t ? RBC_NOMATCH : RBC_MATCH;
}

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Optimized suffix matching helper
/// @param text Text to match
/// @param suffix Suffix pattern
/// @param suffix_len Length of suffix
/// @param casefold Whether to perform case-insensitive matching
/// @return RBC_MATCH or RBC_NOMATCH
static inline int rbc_match_suffix(const char *text, const char *suffix, size_t suffix_len, bool casefold)
{
    const char *last = strrchr(text, suffix[suffix_len - 1]);
    if (!last || last[1] != '\0')
    {
        // If casefold, try case-insensitive search
        if (casefold)
        {
            char ch_lower = tolower((unsigned char)suffix[suffix_len - 1]);
            char ch_upper = toupper((unsigned char)suffix[suffix_len - 1]);
            const char *pos = text + strlen(text) - 1;
            while (pos >= text)
            {
                if (*pos == ch_lower || *pos == ch_upper)
                {
                    last = pos;
                    break;
                }
                pos--;
            }
            if (!last || last[1] != '\0')
                return RBC_NOMATCH;
        }
        else
        {
            return RBC_NOMATCH;
        }
    }
    if (last - text < (ptrdiff_t)(suffix_len - 1))
    {
        return RBC_NOMATCH;
    }

    const char *start = last - (suffix_len - 1);
    if (casefold)
    {
        return strncasecmp(start, suffix, suffix_len) == 0 ? RBC_MATCH : RBC_NOMATCH;
    }
    else
    {
        return memcmp(start, suffix, suffix_len) == 0 ? RBC_MATCH : RBC_NOMATCH;
    }
}

// ============================================================================
// Pattern Analysis (Fast Path Detection)
// ============================================================================

/// @brief Generate match hints from pattern
/// @param p Pattern
/// @param hints Output hints structure
/// @return true if a fast path is detected, false otherwise
static bool rbc_match_hints_generate(const char *p, rbc_match_hints_t *hints)
{
    memset(hints, 0, sizeof(*hints));

    const char *p_ch;
    size_t leading_stars = 0;

    // State machine for pattern analysis
    enum
    {
        INIT,         // Initial state
        LITERAL,      // Scanning literal characters
        STAR,         // Found *, scanning for STAR or SUFFIX
        QUESTION,     // Found ?, scanning for QUESTION
        PREFIX,       // Found literal*, scanning for PREFIX or PREFIX_SUFFIX
        SUFFIX,       // Scanning suffix after *
        PREFIX_SUFFIX // Scanning suffix after literal*
    } state = INIT;

    for (p_ch = p; *p_ch != '\0'; p_ch++)
    {
        if (*p_ch == '[' || *p_ch == '\\')
        {
            return false;
        }

        switch (state)
        {
        case INIT:
            if (*p_ch == '*')
            {
                leading_stars++;
                state = STAR;
            }
            else if (*p_ch == '?')
            {
                state = QUESTION;
            }
            else
            {
                state = LITERAL;
                hints->prefix_len++;
            }
            break;

        case LITERAL:
            if (*p_ch == '*')
            {
                state = PREFIX;
            }
            else if (*p_ch == '?')
            {
                return false;
            }
            else
            {
                hints->prefix_len++;
            }
            break;

        case STAR:
            if (*p_ch == '*')
            {
                leading_stars++;
            }
            else if (*p_ch == '?')
            {
                return false;
            }
            else
            {
                state = SUFFIX;
                hints->suffix_len++;
            }
            break;

        case QUESTION:
            if (*p_ch == '?')
            {
                // Continue counting ?
            }
            else
            {
                return false;
            }
            break;

        case PREFIX:
            if (*p_ch == '*')
            {
                // Skip extra *
            }
            else if (*p_ch == '?')
            {
                return false;
            }
            else
            {
                state = PREFIX_SUFFIX;
                hints->suffix_len++;
            }
            break;

        case SUFFIX:
        case PREFIX_SUFFIX:
            if (*p_ch == '*' || *p_ch == '?')
            {
                return false;
            }
            hints->suffix_len++;
            break;
        }
    }

    // Generate hints based on final state
    switch (state)
    {
    case LITERAL:
        hints->strategy = RBC_MATCH_STRATEGY_LITERAL;
        return true;

    case STAR:
        hints->strategy = RBC_MATCH_STRATEGY_STAR;
        return true;

    case QUESTION:
        hints->strategy = RBC_MATCH_STRATEGY_QUESTION;
        hints->pattern_len = p_ch - p;
        return true;

    case PREFIX:
        hints->strategy = RBC_MATCH_STRATEGY_PREFIX;
        return true;

    case SUFFIX:
        hints->strategy = RBC_MATCH_STRATEGY_SUFFIX;
        hints->pattern_len = leading_stars + hints->suffix_len;
        return true;

    case PREFIX_SUFFIX:
        hints->strategy = RBC_MATCH_STRATEGY_PREFIX_SUFFIX;
        hints->pattern_len = p_ch - p;
        return true;

    default:
        return false;
    }
}

// ============================================================================
// Public API: Single-shot matching (no precompilation)
// ============================================================================

/**
 * @brief File::fnmatch implementation
 *
 * This is the main entry point for one-time pattern matching.
 * No heap allocation is performed.
 *
 * @param pattern Pattern string to match
 * @param string String to match against
 * @param flags Matching flags
 * @return true if the string matches the pattern, false otherwise
 */
bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags)
{
    if (!pattern || !string)
    {
        return false;
    }

    // Call core matching (no fast path for single-shot)
    int res = rbc_match_core((const uchar *)pattern, (const uchar *)string, flags, (const uchar *)string);
    return res == RBC_MATCH;
}

/**
 * @brief Precompile fnmatch pattern
 *
 * Analyzes the pattern and returns a precompiled structure ONLY if a fast path
 * is detected. If no fast path exists, returns NULL to indicate that the caller
 * should use rbc_fnmatch() instead.
 *
 * @param pattern Pattern string to compile
 * @param flags Compilation flags
 * @return Pointer to precompiled pattern, or NULL on failure
 */
rbc_fnmatch_pattern_t *rbc_fnmatch_compile(const char *pattern, unsigned int flags)
{
    if (!pattern)
    {
        return NULL;
    }

    // First, analyze the pattern to detect fast paths
    rbc_match_hints_t hints;
    if (!rbc_match_hints_generate(pattern, &hints))
    {
        return NULL; // No optimization available, caller should use rbc_fnmatch()
    }

    // Fast path detected - proceed with compilation
    rbc_fnmatch_pattern_t *p = malloc(sizeof(*p));
    if (!p)
        return NULL;

    p->pattern = strdup(pattern);
    if (!p->pattern)
    {
        free(p);
        return NULL;
    }
    p->hints = hints;

    return p;
}

/**
 * @brief File::fnmatch implementation with precompiled pattern
 *
 * Uses optimization hints to accelerate matching.
 *
 * @param p Precompiled pattern
 * @param string String to match against
 * @param flags Matching flags
 * @return true if the string matches the pattern, false otherwise
 */
bool rbc_xfnmatch(const rbc_fnmatch_pattern_t *p, const char *string, unsigned flags)
{
    if (!p || !string)
    {
        return false;
    }

    const uchar *text = (const uchar *)string;
    const rbc_match_hints_t *hints = &p->hints;

    // **** FAST PATH OPTIMIZATION (hints-based) ****
    switch (hints->strategy)
    {
    case RBC_MATCH_STRATEGY_LITERAL:
        if (flags & RBC_FNM_CASEFOLD)
        {
            // Case-insensitive comparison
            return strcasecmp(p->pattern, string) == 0;
        }
        return strcmp(p->pattern, string) == 0;

    case RBC_MATCH_STRATEGY_STAR:
        // <Ruby>: PATHNAME
        if ((flags & RBC_FNM_PATHNAME) && strchr(string, '/'))
            return false;
        // <Ruby>: DOTMATCH
        if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text, flags))
            return false;
        return true;

    case RBC_MATCH_STRATEGY_QUESTION:
        // <Ruby>: PATHNAME
        if ((flags & RBC_FNM_PATHNAME) && strchr(string, '/'))
            return false;
        // <Ruby>: DOTMATCH
        if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text, flags))
            return false;
        return strlen(string) == hints->pattern_len;

    case RBC_MATCH_STRATEGY_PREFIX:
        if (flags & RBC_FNM_CASEFOLD)
        {
            if (strncasecmp(p->pattern, string, hints->prefix_len) != 0)
                return false;
        }
        else
        {
            if (strncmp(p->pattern, string, hints->prefix_len) != 0)
                return false;
        }
        // Check if remaining text contains '/' when PATHNAME is set
        if ((flags & RBC_FNM_PATHNAME) && strchr(string + hints->prefix_len, '/'))
            return false;
        return true;

    case RBC_MATCH_STRATEGY_SUFFIX:
    {
        // <Ruby>: DOTMATCH
        if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text, flags))
            return false;
        const char *ptr = p->pattern;
        while (*ptr == '*')
            ptr++;
        return rbc_match_suffix(string, ptr, hints->suffix_len, flags & RBC_FNM_CASEFOLD) == RBC_MATCH;
    }

    case RBC_MATCH_STRATEGY_PREFIX_SUFFIX:
    {
        if (flags & RBC_FNM_CASEFOLD)
        {
            if (strncasecmp(p->pattern, string, hints->prefix_len) != 0)
                return false;
        }
        else
        {
            if (strncmp(p->pattern, string, hints->prefix_len) != 0)
                return false;
        }
        const char *t = string + hints->prefix_len;
        const char *suffix = p->pattern + hints->pattern_len - hints->suffix_len;
        return rbc_match_suffix(t, suffix, hints->suffix_len, flags & RBC_FNM_CASEFOLD) == RBC_MATCH;
    }

    default:
        break;
    }

    // **** SLOW PATH (wildmatch core) ****
    int res = rbc_match_core((const uchar *)p->pattern, text, flags, text);
    return res == RBC_MATCH;
}

/**
 * @brief Free precompiled fnmatch pattern
 *
 * @param p Precompiled pattern to free
 */
void rbc_fnmatch_pattern_free(rbc_fnmatch_pattern_t *p)
{
    if (p)
    {
        free((void *)p->pattern);
        free(p);
    }
}
