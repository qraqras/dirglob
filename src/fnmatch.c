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
#define RBC_NEGATE_CLASS '!'
#define RBC_NEGATE_CLASS2 '^'

// Character class macros
#define ISUPPER(c) isupper((unsigned char)(c))
#define ISLOWER(c) islower((unsigned char)(c))
#define ISALNUM(c) isalnum((unsigned char)(c))
#define ISALPHA(c) isalpha((unsigned char)(c))
#define ISDIGIT(c) isdigit((unsigned char)(c))
#define ISXDIGIT(c) isxdigit((unsigned char)(c))
#define ISSPACE(c) isspace((unsigned char)(c))
#define ISBLANK(c) isblank((unsigned char)(c))
#define ISCNTRL(c) iscntrl((unsigned char)(c))
#define ISGRAPH(c) isgraph((unsigned char)(c))
#define ISPRINT(c) isprint((unsigned char)(c))
#define ISPUNCT(c) ispunct((unsigned char)(c))

#define CC_EQ(s, len, lit) ((len) == sizeof(lit) - 1 && memcmp(s, lit, sizeof(lit) - 1) == 0)

static inline int is_glob_special(unsigned char c)
{
    return c == '*' || c == '?' || c == '[' || c == '\\';
}

static int dowild(const uchar *p, const uchar *text, unsigned int flags);

// ============================================================================
// Type Definitions
// ============================================================================

/// @brief Match strategy enumeration
typedef enum rbc_match_strategy_e
{
    RBC_MATCH_STRATEGY_LITERAL,       // `literal`
    RBC_MATCH_STRATEGY_STAR,          // `*`
    RBC_MATCH_STRATEGY_QUESTION,      // `??`
    RBC_MATCH_STRATEGY_PREFIX,        // `prefix*`
    RBC_MATCH_STRATEGY_SUFFIX,        // `*suffix`
    RBC_MATCH_STRATEGY_PREFIX_SUFFIX, // `prefix*suffix`
} rbc_match_strategy_t;

/// @brief Match hints structure
typedef struct rbc_match_hints_s
{
    rbc_match_strategy_t strategy; // Fast path type (1 byte)
    uint16_t pattern_len;          // Pattern length (avoids strlen)
    uint16_t prefix_len;           // Literal prefix length
    uint16_t suffix_len;           // Literal suffix length
} rbc_match_hints_t;

/// @brief Precompiled fnmatch pattern structure
struct rbc_fnmatch_pattern_s
{
    const char *pattern;     // Original pattern string
    unsigned flags;          // FNM_* flags
    rbc_match_hints_t hints; // Optimization hints
};

// Internal return values (wildmatch-style)
#define RBC_WM_MATCH 1
#define RBC_WM_NOMATCH 0
#define RBC_WM_ABORT_ALL -1

// ============================================================================
// Forward Declarations
// ============================================================================

static int dowild(const uchar *p, const uchar *text, unsigned int flags);
static bool rbc_match_hints_generate(const char *pattern, unsigned flags, rbc_match_hints_t *hints);

// ============================================================================
// Wildmatch Core Implementation (same file for optimization)
// ============================================================================

// Stack entry for iterative matching
typedef struct
{
    const uchar *pattern;
    const uchar *text;
} rbc_match_frame_t;

#define RBC_STACK_SIZE 256

/// @brief Core matching function - STACK-BASED IMPLEMENTATION
/// @param pattern Pattern to match (uchar pointer)
/// @param text Text to match (uchar pointer)
/// @return WM_MATCH, WM_NOMATCH, or WM_ABORT_ALL
static int dowild(const uchar *pattern, const uchar *text, unsigned int flags)
{
    rbc_match_frame_t stack[RBC_STACK_SIZE];
    int stack_top = 0;
    const uchar *p = pattern;
    const uchar *original_pattern = pattern;
    int final_result = RBC_NOMATCH;

    // Push initial frame
    stack[stack_top].pattern = pattern;
    stack[stack_top].text = text;
    stack_top++;

    while (stack_top > 0)
    {
        // Pop frame
        stack_top--;
        p = stack[stack_top].pattern;
        text = stack[stack_top].text;
        original_pattern = p;

        uchar p_ch;
        for (; (p_ch = *p) != '\0'; text++, p++)
        {
            int matched, match_slash, negated;
            uchar t_ch, prev_ch;
            if ((t_ch = *text) == '\0' && p_ch != '*')
            {
                return RBC_ABORT_ALL;
            }
            if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(t_ch))
            {
                t_ch = tolower(t_ch);
            }
            if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(p_ch))
            {
                p_ch = tolower(p_ch);
            }
            switch (p_ch)
            {
            case '\\':
                if (!(flags & RBC_FNM_NOESCAPE))
                {
                    p_ch = *++p;
                }
                // **** FALLTHROUGH ****
            default:
                if (t_ch != p_ch)
                {
                    final_result = RBC_NOMATCH;
                    goto next_frame;
                }
                continue;
            case '?':
                if ((flags & RBC_FNM_PATHNAME) && t_ch == '/')
                {
                    final_result = RBC_NOMATCH;
                    goto next_frame;
                }
                continue;
            case '*':
                if (*++p == '*')
                {
                    const uchar *prev_p = p;
                    while (*++p == '*')
                    {
                    }
                    if (!(flags & RBC_FNM_PATHNAME))
                    {
                        // without RBC_FNM_PATHNAME, '*' == '**'
                        match_slash = 1;
                    }
                    else if ((prev_p - original_pattern < 2 || *(prev_p - 2) == '/') && (*p == '\0' || *p == '/' || (!(flags & RBC_FNM_NOESCAPE) && p[0] == '\\' && p[1] == '/')))
                    {
                        if (p[0] == '/')
                        {
                            // Push recursive call to stack
                            if (stack_top < RBC_STACK_SIZE)
                            {
                                stack[stack_top].pattern = p + 1;
                                stack[stack_top].text = text;
                                stack_top++;
                            }
                        }
                        match_slash = 1;
                    }
                    else
                    {
                        // RBC_FNM_PATHNAME is set
                        match_slash = 0;
                    }
                }
                else
                {
                    // without RBC_FNM_PATHNAME, '*' == '**'
                    match_slash = flags & RBC_FNM_PATHNAME ? 0 : 1;
                }
                if (*p == '\0')
                {
                    // trailing "**" matches everything.
                    // trailing "*" matches everything but '/'.
                    if (!match_slash)
                    {
                        if (strchr((char *)text, '/'))
                        {
                            final_result = RBC_ABORT_TO_STARSTAR;
                            goto next_frame;
                        }
                    }
                    return RBC_MATCH;
                }
                else if (!match_slash && *p == '/')
                {
                    // jump to next slash in text
                    const char *slash = strchr((char *)text, '/');
                    if (!slash)
                    {
                        return RBC_ABORT_ALL;
                    }
                    text = (const uchar *)slash;
                    break;
                }
                // Star matching loop - push frames for backtracking
                while (1)
                {
                    if (t_ch == '\0')
                    {
                        break;
                    }
                    /*
                     * Try to advance faster when an asterisk is
                     * followed by a literal. We know in this case
                     * that the string before the literal
                     * must belong to "*".
                     * If match_slash is false, do not look past
                     * the first slash as it cannot belong to '*'.
                     */
                    if (!is_glob_special(*p))
                    {
                        p_ch = *p;
                        if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(p_ch))
                        {
                            p_ch = tolower(p_ch);
                        }
                        while ((t_ch = *text) != '\0' && (match_slash || t_ch != '/'))
                        {
                            if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(t_ch))
                            {
                                t_ch = tolower(t_ch);
                            }
                            if (t_ch == p_ch)
                            {
                                break;
                            }
                            text++;
                        }
                        if (t_ch != p_ch)
                        {
                            if (match_slash)
                            {
                                return RBC_ABORT_ALL;
                            }
                            final_result = RBC_ABORT_TO_STARSTAR;
                            goto next_frame;
                        }
                    }
                    // Push frame for dowild(p, text, flags)
                    if (stack_top < RBC_STACK_SIZE)
                    {
                        stack[stack_top].pattern = p;
                        stack[stack_top].text = text;
                        stack_top++;
                    }
                    if (!match_slash && t_ch == '/')
                    {
                        final_result = RBC_ABORT_TO_STARSTAR;
                        goto next_frame;
                    }
                    t_ch = *++text;
                }
                return RBC_ABORT_ALL;
            case '[':
                p_ch = *++p;
                negated = p_ch == '!' || p_ch == '^' ? 1 : 0;
                if (negated)
                {
                    // Inverted character class.
                    p_ch = *++p;
                }
                prev_ch = 0;
                matched = 0;
                do
                {
                    if (!p_ch)
                    {
                        return RBC_ABORT_ALL;
                    }
                    if (!(flags & RBC_FNM_NOESCAPE) && p_ch == '\\')
                    {
                        p_ch = *++p;
                        if (!p_ch)
                        {
                            return RBC_ABORT_ALL;
                        }
                        if (t_ch == p_ch)
                        {
                            matched = 1;
                        }
                    }
                    else if (p_ch == '-' && prev_ch && p[1] && p[1] != ']')
                    {
                        p_ch = *++p;
                        if (!(flags & RBC_FNM_NOESCAPE) && p_ch == '\\')
                        {
                            p_ch = *++p;
                            if (!p_ch)
                            {
                                return RBC_ABORT_ALL;
                            }
                        }
                        if (t_ch <= p_ch && t_ch >= prev_ch)
                        {
                            matched = 1;
                        }
                        else if ((flags & RBC_FNM_CASEFOLD) && ISLOWER(t_ch))
                        {
                            uchar t_ch_upper = toupper(t_ch);
                            if (t_ch_upper <= p_ch && t_ch_upper >= prev_ch)
                            {
                                matched = 1;
                            }
                        }
                        p_ch = 0;
                    }
                    else if (t_ch == p_ch)
                    {
                        matched = 1;
                    }
                } while (p_ch != ']');
                if (matched == negated || ((flags & RBC_FNM_PATHNAME) && t_ch == '/'))
                {
                    final_result = RBC_NOMATCH;
                    goto next_frame;
                }
                continue;
            }
        }

        // Pattern consumed successfully
        if (*text == '\0')
        {
            return RBC_MATCH;
        }
        else
        {
            final_result = RBC_NOMATCH;
        }

    next_frame:
        continue;
    }

    return final_result;
}

// ============================================================================
// Hint Generation (Pattern Analysis)
// ============================================================================

/**
 * @brief Generate optimization hints by analyzing pattern
 *
 * This function scans the pattern once to extract optimization hints.
 * No allocation is performed.
 *
 * @param pattern Pattern to analyze
 * @param flags FNM_* flags
 * @param hints Output hints structure
 * @return true if fast path detected, false otherwise
 */
static bool rbc_match_hints_generate(const char *pattern, unsigned flags,
                                     rbc_match_hints_t *hints)
{
    memset(hints, 0, sizeof(*hints));
    hints->pattern_len = strlen(pattern);

    // Quick scan for meta-characters and question count
    const char *p = pattern;
    bool has_star = false;
    bool has_question = false;
    bool has_bracket = false;
    bool has_escape = false;

    while (*p)
    {
        switch (*p)
        {
        case '*':
            has_star = true;
            p++;
            break;
        case '?':
            has_question = true;
            p++;
            break;
        case '[':
            has_bracket = true;
            // Skip to matching ']'
            p++;
            if (*p == '!' || *p == '^')
                p++;
            if (*p == ']')
                p++;
            while (*p && *p != ']')
            {
                if (*p == '\\' && !(flags & RBC_FNM_NOESCAPE))
                    p++;
                if (*p)
                    p++;
            }
            if (*p == ']')
                p++;
            break;
        case '\\':
            if (!(flags & RBC_FNM_NOESCAPE))
            {
                has_escape = true;
                p++;
                if (*p)
                    p++;
            }
            else
            {
                p++;
            }
            break;
        default:
            p++;
            break;
        }
    }

    // Detect literal prefix (only if needed for fast paths)
    p = pattern;
    while (*p && *p != '*' && *p != '?' && *p != '[' &&
           !(*p == '\\' && !(flags & RBC_FNM_NOESCAPE)))
    {
        hints->prefix_len++;
        p++;
    }

    // Detect literal suffix (only if needed for fast paths)
    if (hints->pattern_len > 0)
    {
        p = pattern + hints->pattern_len - 1;
        while (p >= pattern)
        {
            if (*p == '*' || *p == '?' || *p == ']')
                break;
            if (*p == '\\' && p > pattern && !(flags & RBC_FNM_NOESCAPE))
            {
                p--;
                if (p < pattern)
                    break;
            }
            hints->suffix_len++;
            p--;
        }
    }

    // Detect fast path type (priority order matters!)
    if (!has_star && !has_question && !has_bracket && !has_escape)
    {
        hints->strategy = RBC_MATCH_STRATEGY_LITERAL;
        return true;
    }
    else if (hints->pattern_len == 1 && pattern[0] == '*')
    {
        hints->strategy = RBC_MATCH_STRATEGY_STAR;
        return true;
    }
    else if (has_question && !has_star && !has_bracket && !has_escape)
    {
        // All characters are '?' - fixed length match
        hints->strategy = RBC_MATCH_STRATEGY_QUESTION;
        return true;
    }
    else if (pattern[0] == '*' && hints->suffix_len > 0 &&
             hints->suffix_len == hints->pattern_len - 1 &&
             hints->suffix_len > 1 && // Exclude ".*" (suffix must be at least 2 chars for real benefit)
             !has_question && !has_bracket && !has_escape)
    {
        hints->strategy = RBC_MATCH_STRATEGY_SUFFIX;
        return true;
    }
    else if (pattern[hints->pattern_len - 1] == '*' &&
             hints->prefix_len > 0 &&
             hints->prefix_len == hints->pattern_len - 1 &&
             hints->prefix_len > 1 && // Exclude ".*" (prefix must be at least 2 chars for real benefit)
             !has_question && !has_bracket)
    {
        hints->strategy = RBC_MATCH_STRATEGY_PREFIX;
        return true;
    }
    else if (hints->prefix_len > 0 &&
             hints->suffix_len > 0 &&
             hints->prefix_len + hints->suffix_len + 1 == hints->pattern_len &&
             !has_question && !has_bracket)
    {
        hints->strategy = RBC_MATCH_STRATEGY_PREFIX_SUFFIX;
        return true;
    }

    return false; // No fast path detected
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

    // Complex pattern: call wildmatch core (same file = optimized)
    int res = dowild((const uchar *)pattern, (const uchar *)string, flags);
    return res == RBC_MATCH;
}

// ============================================================================
// Public API: Precompiled pattern matching
// ============================================================================

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
    if (!rbc_match_hints_generate(pattern, flags, &hints))
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
    p->flags = flags;
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
 * @return true if the string matches the pattern, false otherwise
 */
bool rbc_xfnmatch(const rbc_fnmatch_pattern_t *p, const char *string)
{
    if (!p || !string)
    {
        return false;
    }
    int res = dowild((const uchar *)p->pattern, (const uchar *)string, p->flags);
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
