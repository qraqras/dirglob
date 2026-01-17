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

static int dowild_internal(const uchar *p, const uchar *text, unsigned int flags, const rbc_match_hints_t *hints, const uchar *text_start);
static int dowild(const uchar *p, const uchar *text, unsigned int flags, const rbc_match_hints_t *hints);
static bool rbc_match_hints_generate(const char *pattern, unsigned flags, rbc_match_hints_t *hints);

// ============================================================================
// Wildmatch Core Implementation (same file for optimization)
// ============================================================================

/// @brief Check if text ends with suffix using SIMD-optimized strrchr
/// @param text Text to check
/// @param suffix Suffix pattern to match
/// @param suffix_len Length of suffix
/// @return RBC_MATCH if suffix matches, RBC_NOMATCH otherwise
static inline int match_suffix(const char *text, const char *suffix, size_t suffix_len)
{
    // SIMD-optimized: use strrchr to find last occurrence of suffix's final character
    const char *last = strrchr(text, suffix[suffix_len - 1]);

    // Check if found at end of string
    if (!last || last[1] != '\0')
        return RBC_NOMATCH;

    // Check if text is long enough for the suffix
    if (last - text < (ptrdiff_t)(suffix_len - 1))
        return RBC_NOMATCH;

    // Check if full suffix matches (using memcmp for performance)
    if (memcmp(last - (suffix_len - 1), suffix, suffix_len) == 0)
        return RBC_MATCH;

    return RBC_NOMATCH;
}

/// @brief Core matching function - RECURSIVE IMPLEMENTATION (internal)
/// @param p Pattern to match (uchar pointer)
/// @param text Text to match (uchar pointer)
/// @param flags Matching flags
/// @param hints Optimization hints (NULL for slow path)
/// @param text_start Original text start pointer (for boundary checking)
/// @return RBC_MATCH, RBC_NOMATCH, RBC_ABORT_ALL, or RBC_ABORT_TO_STARSTAR
static int dowild_internal(const uchar *p, const uchar *text, unsigned int flags, const rbc_match_hints_t *hints, const uchar *text_start)
{
    // **** FAST PATH MATCHING ****
    if (hints)
    {
        switch (hints->strategy)
        {
        case RBC_MATCH_STRATEGY_LITERAL:
            return strcmp((const char *)p, (const char *)text) == 0 ? RBC_MATCH : RBC_NOMATCH;
        case RBC_MATCH_STRATEGY_STAR:
            // <Ruby>: DOTMATCH
            if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text_start, flags))
                return RBC_NOMATCH;
            return RBC_MATCH;
        case RBC_MATCH_STRATEGY_QUESTION:
            // <Ruby>: DOTMATCH
            if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text_start, flags))
                return RBC_NOMATCH;
            if (strlen((const char *)text) != hints->pattern_len)
                return RBC_NOMATCH;
            return RBC_MATCH;
        case RBC_MATCH_STRATEGY_PREFIX:
            if (strncmp((const char *)p, (const char *)text, hints->prefix_len) != 0)
                return RBC_NOMATCH;
            return RBC_MATCH;
        case RBC_MATCH_STRATEGY_SUFFIX:
        {
            // <Ruby>: DOTMATCH
            if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text_start, flags))
                return RBC_NOMATCH;
            const char *ptr = (const char *)p;
            while (*ptr == '*')
                ptr++;
            return match_suffix((const char *)text, ptr, hints->suffix_len);
        }
        case RBC_MATCH_STRATEGY_PREFIX_SUFFIX:
        {
            if (strncmp((const char *)p, (const char *)text, hints->prefix_len) != 0)
                return RBC_NOMATCH;
            const char *t = (const char *)text + hints->prefix_len;
            const char *suffix = (const char *)p + hints->pattern_len - hints->suffix_len;
            return match_suffix(t, suffix, hints->suffix_len);
        }
        default:
            break;
        }
    }

    // **** RECURSIVE MATCHING (wildmatch-style) ****
    uchar p_ch;

    for (; (p_ch = *p) != '\0'; text++, p++)
    {
        int matched, match_slash, negated;
        uchar t_ch, prev_ch;
        if ((t_ch = *text) == '\0' && p_ch != '*')
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
            /* FALLTHROUGH */
        default:
            if (t_ch != p_ch)
                return RBC_NOMATCH;
            continue;
        case '?':
            if ((flags & RBC_FNM_PATHNAME) && t_ch == '/')
                return RBC_NOMATCH;
            // <Ruby>: DOTMATCH
            if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text_start, flags))
                return RBC_NOMATCH;
            continue;
        case '*':
            // <Ruby>: DOTMATCH
            if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text_start, flags))
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
                        dowild_internal(p + 1, text, flags, NULL, text_start) == RBC_MATCH)
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
                    if (strchr((char *)text, '/'))
                        return RBC_ABORT_TO_STARSTAR;
                    // <Ruby>: DOTMATCH - single * should not match leading dot
                    if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text_start, flags))
                        return RBC_NOMATCH;
                }
                return RBC_MATCH;
            }
            else if (!match_slash && *p == '/')
            {
                const char *slash = strchr((char *)text, '/');
                if (!slash)
                    return RBC_ABORT_ALL;
                text = (const uchar *)slash;
                break;
            }
            while (1)
            {
                if (t_ch == '\0')
                    break;

                // <Ruby>: DOTMATCH
                // next_slash check to avoid false positive on segments
                if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text_start, flags) && !IS_SLASH_DOT_PATTERN(p))
                {
                    const uchar *next_slash = (const uchar *)strchr((char *)text, '/');
                    if (next_slash && next_slash[1] == '.')
                        return RBC_ABORT_ALL;
                }

                if (!is_glob_special(*p))
                {
                    p_ch = *p;
                    if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(p_ch))
                        p_ch = tolower(p_ch);
                    while ((t_ch = *text) != '\0' &&
                           (match_slash || t_ch != '/'))
                    {
                        if ((flags & RBC_FNM_CASEFOLD) && ISUPPER(t_ch))
                            t_ch = tolower(t_ch);
                        if (t_ch == p_ch)
                            break;
                        text++;
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
                if (!(flags & RBC_FNM_DOTMATCH) && IS_HIDDEN_TEXT(text, text_start, flags) && !IS_SLASH_DOT_PATTERN(p))
                {
                    matched = RBC_NOMATCH;
                }
                else if ((matched = dowild_internal(p, text, flags, hints, text_start)) != RBC_NOMATCH)
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

                t_ch = *++text;
            }
            return RBC_ABORT_ALL;
        case '[':
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

    return *text ? RBC_NOMATCH : RBC_MATCH;
}

/// @brief Core matching function - wrapper for external calls
static int dowild(const uchar *p, const uchar *text, unsigned int flags, const rbc_match_hints_t *hints)
{
    return dowild_internal(p, text, flags, hints, text);
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
static bool rbc_match_hints_generate(const char *pattern, unsigned flags, rbc_match_hints_t *hints)
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

    // Count leading stars (for SUFFIX pattern detection)
    size_t leading_stars = 0;
    while (pattern[leading_stars] == '*')
        leading_stars++;

    // Detect fast path type (priority order matters!)
    if (!has_star && !has_question && !has_bracket && !has_escape)
    {
        hints->strategy = RBC_MATCH_STRATEGY_LITERAL;
        return true;
    }
    else if (leading_stars == hints->pattern_len)
    {
        // Pattern is all stars (e.g., "*", "**", "***")
        hints->strategy = RBC_MATCH_STRATEGY_STAR;
        return true;
    }
    else if (has_question && !has_star && !has_bracket && !has_escape)
    {
        // All characters are '?' - fixed length match
        hints->strategy = RBC_MATCH_STRATEGY_QUESTION;
        return true;
    }
    else if (leading_stars > 0 && hints->suffix_len > 0 &&
             leading_stars + hints->suffix_len == hints->pattern_len &&
             hints->suffix_len >= 1 && // Allow single-character suffixes
             !has_question && !has_bracket && !has_escape)
    {
        // Pattern is "*suffix", "**suffix", etc.
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
    int res = dowild((const uchar *)pattern, (const uchar *)string, flags, NULL);
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
    int res = dowild((const uchar *)p->pattern, (const uchar *)string, flags, &p->hints);
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
