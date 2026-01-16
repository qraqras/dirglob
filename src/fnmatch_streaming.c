#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <stdlib.h>
#include "internal.h"

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

/// @brief Precompiled match pattern structure
struct rbc_match_pattern_s
{
    const char *pattern;     // Original pattern string
    unsigned flags;          // FNM_* flags
    rbc_match_hints_t hints; // Optimization hints
};

/// @brief Streaming match state structure
typedef struct rbc_match_state_s
{
    const char *p;                  // Current pattern position
    const char *t;                  // Current text position
    const char *star_p;             // Backtrack pattern position
    const char *star_t;             // Backtrack text position
    const char *text_start;         // Original text start (for context checks)
    const char *text_end;           // Text end position (cached)
    unsigned int flags;             // FNM_* flags
    const rbc_match_hints_t *hints; // NULL for rbc_fnmatch
} rbc_match_state_t;

// Helper macros for text_end checks (supports both modes)
// When text_end is NULL (non-compiled), use '\0' check
// When text_end is set (compiled), use pointer comparison
#define TEXT_AT_END(state) \
    ((state)->text_end ? ((state)->t >= (state)->text_end) : (*(state)->t == '\0'))

#define TEXT_NOT_AT_END(state) \
    ((state)->text_end ? ((state)->t < (state)->text_end) : (*(state)->t != '\0'))

#define STAR_AT_END(state) \
    ((state)->text_end ? ((state)->star_t >= (state)->text_end) : (*(state)->star_t == '\0'))

#define STAR_NOT_AT_END(state) \
    ((state)->text_end ? ((state)->star_t < (state)->text_end) : (*(state)->star_t != '\0'))

// ============================================================================
// Helper Functions
// ============================================================================

/// @brief Compare two characters with optional case folding (optimized)
static inline bool rbc_char_equal_casefold(unsigned char a, unsigned char b)
{
    if (a == b)
        return true;
    // ASCII letters differ by 0x20 between upper/lower case
    // Convert both to lowercase using bitwise OR and compare
    unsigned char a_lower = a | 0x20;
    unsigned char b_lower = b | 0x20;
    return (a_lower == b_lower && a_lower >= 'a' && a_lower <= 'z');
}

/// @brief Compare strings with optional case folding
static inline bool rbc_match_strcmp(const char *s1, const char *s2, unsigned flags)
{
    return (flags & RBC_FNM_CASEFOLD) ? strcasecmp(s1, s2) == 0 : strcmp(s1, s2) == 0;
}

/// @brief Compare strings with length limit and optional case folding
static inline bool rbc_match_strncmp(const char *s1, const char *s2, size_t n, unsigned flags)
{
    return (flags & RBC_FNM_CASEFOLD) ? strncasecmp(s1, s2, n) == 0 : strncmp(s1, s2, n) == 0;
}

/// @brief Check if current position has a leading dot
/// @note `.hidden` or `**/.hidden` in PATHNAME mode
static inline bool rbc_match_has_leading_dot(const rbc_match_state_t *state)
{
    if (*state->t != '.')
    {
        return false;
    }
    // Leading dot at text start or after '/' in PATHNAME mode
    return (state->t == state->text_start || (state->flags & RBC_FNM_PATHNAME && state->t > state->text_start && *(state->t - 1) == '/'));
}

/// @brief Check if range contains '/' (PATHNAME violation)
/// @param start Start of range
/// @param end End of range (exclusive)
/// @return true if '/' found in range
static inline bool rbc_match_has_pathname_violation(const char *start, const char *end)
{
    return memchr(start, '/', end - start) != NULL;
}

// ============================================================================
// Forward Declarations
// ============================================================================

static int rbc_match_core_wildmatch(const unsigned char *p, const unsigned char *text);
static bool rbc_match_hints_generate(const char *pattern, unsigned flags, rbc_match_hints_t *hints);
static bool rbc_match_bracket(const char **pattern_ptr, char c, unsigned flags);

// ============================================================================
// Public API: Single-shot matching (no precompilation)
// ============================================================================

/**
 * @brief Match text against pattern without precompilation
 *
 * This is the main entry point for one-time pattern matching.
 * No heap allocation is performed.
 *
 * @param pattern Glob pattern
 * @param text Text to match
 * @param flags FNM_* flags
 * @return true if match, false otherwise
 */
bool rbc_fnmatch_streaming(const char *pattern, const char *text, unsigned flags)
{
    // Inline fast paths executed BEFORE rbc_match_core() to avoid strlen() overhead
    // This matches wildmatch's strategy: check simple cases without any setup cost

    // Fast path 1: Single '*' matches everything (except dotmatch/pathname cases)
    if (pattern[0] == '*' && pattern[1] == '\0')
    {
        if (!(flags & RBC_FNM_DOTMATCH) && text[0] == '.')
            return false;
        if ((flags & RBC_FNM_PATHNAME) && strchr(text, '/') != NULL)
            return false;
        return true;
    }

    // Fast path 2: No wildcards at all - use strcmp (wildmatch does the same)
    const char *p = pattern;
    bool has_meta = false;
    while (*p)
    {
        if (*p == '*' || *p == '?' || *p == '[' ||
            (*p == '\\' && !(flags & RBC_FNM_NOESCAPE)))
        {
            has_meta = true;
            break;
        }
        p++;
    }

    if (!has_meta)
    {
        // Pure literal match - no strlen() needed
        if (flags & RBC_FNM_CASEFOLD)
        {
            return strcasecmp(pattern, text) == 0;
        }
        else
        {
            return strcmp(pattern, text) == 0;
        }
    }

    // Complex pattern: need full matching logic
    int res = rbc_match_core_wildmatch((const unsigned char *)pattern, (const unsigned char *)text);
    return res == RBC_WM_MATCH;
}

// ============================================================================
// Public API: Precompiled pattern matching
// ============================================================================

/**
 * @brief Compile a pattern for repeated matching
 *
 * Analyzes the pattern and returns a precompiled structure ONLY if a fast path
 * is detected. If no fast path exists, returns NULL to indicate that the caller
 * should use rbc_fnmatch() instead.
 *
 * @param pattern Glob pattern
 * @param flags FNM_* flags
 * @return Compiled pattern structure if fast path exists, NULL otherwise
 */
rbc_fnmatch_pattern_streaming_t *rbc_fnmatch_compile_streaming(const char *pattern, unsigned flags)
{
    // First, analyze the pattern to detect fast paths
    rbc_match_hints_t hints;
    if (!rbc_match_hints_generate(pattern, flags, &hints))
    {
        return NULL; // No optimization available, caller should use rbc_fnmatch()
    }

    // Fast path detected - proceed with compilation
    rbc_fnmatch_pattern_streaming_t *p = malloc(sizeof(*p));
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
 * @brief Match text against precompiled pattern
 *
 * Uses optimization hints to accelerate matching.
 *
 * @param p Precompiled pattern
 * @param text Text to match
 * @return true if match, false otherwise
 */
bool rbc_xfnmatch_streaming(const rbc_fnmatch_pattern_streaming_t *p, const char *text)
{
    int res = rbc_match_core_wildmatch((const unsigned char *)p->pattern, (const unsigned char *)text);
    return res == RBC_WM_MATCH;
}

/**
 * @brief Free precompiled pattern
 *
 * @param p Pattern to free
 */
void rbc_fnmatch_pattern_free_streaming(rbc_fnmatch_pattern_streaming_t *p)
{
    if (p)
    {
        free((void *)p->pattern);
        free(p);
    }
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
// Character Class Matching
// ============================================================================

/**
 * @brief Match a character against a bracket expression
 *
 * Handles [...] patterns including negation and ranges.
 *
 * @param pattern_ptr Pointer to pattern position (updated)
 * @param c Character to match
 * @param flags FNM_* flags
 * @return true if character matches
 */
static bool rbc_match_bracket(const char **pattern_ptr, char c, unsigned flags)
{
    const char *p = *pattern_ptr;
    bool negate = false;
    bool matched = false;

    if (*p != '[')
        return false;
    p++;

    // Check for negation
    if (*p == '!' || *p == '^')
    {
        negate = true;
        p++;
    }

    // Handle ']' as first character
    if (*p == ']')
    {
        if (c == ']')
            matched = true;
        p++;
    }

    // Scan character class
    while (*p && *p != ']')
    {
        char start = *p;

        // Handle escape
        if (start == '\\' && !(flags & RBC_FNM_NOESCAPE))
        {
            p++;
            if (!*p)
                break;
            start = *p;
        }

        p++;

        // Check for range
        if (*p == '-' && *(p + 1) != ']' && *(p + 1) != '\0')
        {
            p++; // Skip '-'
            char end = *p;

            // Handle escape in range end
            if (end == '\\' && !(flags & RBC_FNM_NOESCAPE))
            {
                p++;
                if (!*p)
                    break;
                end = *p;
            }

            p++;

            // Check if c is in range
            if (flags & RBC_FNM_CASEFOLD)
            {
                char c_lower = tolower((unsigned char)c);
                char start_lower = tolower((unsigned char)start);
                char end_lower = tolower((unsigned char)end);
                if (c_lower >= start_lower && c_lower <= end_lower)
                {
                    matched = true;
                }
            }
            else
            {
                if (c >= start && c <= end)
                {
                    matched = true;
                }
            }
        }
        else
        {
            // Single character match
            if (flags & RBC_FNM_CASEFOLD)
            {
                if (tolower((unsigned char)c) == tolower((unsigned char)start))
                {
                    matched = true;
                }
            }
            else
            {
                if (c == start)
                {
                    matched = true;
                }
            }
        }
    }

    // Skip to closing ']'
    while (*p && *p != ']')
        p++;
    if (*p == ']')
        p++;

    *pattern_ptr = p;
    return negate ? !matched : matched;
}

// ============================================================================
// Unified Matching Engine
// ============================================================================

// Internal return values (wildmatch-style)
#define RBC_WM_MATCH 1
#define RBC_WM_NOMATCH 0
#define RBC_WM_ABORT_ALL -1

/// @brief Core matching function - EXACT WILDMATCH IMPLEMENTATION
/// @param pattern Pattern to match (uchar pointer)
/// @param text Text to match (uchar pointer)
/// @return RBC_WM_MATCH, RBC_WM_NOMATCH, or RBC_WM_ABORT_ALL
///
/// This is a direct port of git's wildmatch dowild() function to identify
/// the fundamental performance characteristics.
static inline int rbc_match_core_wildmatch(const unsigned char *p, const unsigned char *text)
{
    unsigned char p_ch;

    for (; (p_ch = *p) != '\0'; text++, p++)
    {
        unsigned char t_ch;
        if ((t_ch = *text) == '\0' && p_ch != '*')
            return RBC_WM_ABORT_ALL;

        switch (p_ch)
        {
        case '\\':
            // Literal match with following character
            p_ch = *++p;
            /* FALLTHROUGH */
        default:
            if (t_ch != p_ch)
                return RBC_WM_NOMATCH;
            continue;

        case '?':
            // Match anything
            continue;

        case '*':
            // Consume consecutive stars
            while (*++p == '*')
            {
            }

            // Trailing star matches everything
            if (*p == '\0')
                return RBC_WM_MATCH;

            // Literal lookahead optimization
            while (1)
            {
                if (t_ch == '\0')
                    break;

                // Try to advance faster when an asterisk is followed by a literal
                if (*p != '?' && *p != '[' && *p != '*' && *p != '\\')
                {
                    p_ch = *p;
                    // Fast-forward to next occurrence of literal
                    while ((t_ch = *text) != '\0')
                    {
                        if (t_ch == p_ch)
                            break;
                        text++;
                    }
                    if (t_ch != p_ch)
                        return RBC_WM_ABORT_ALL;
                }

                // Try recursive match from this position
                int matched = rbc_match_core_wildmatch(p, text);
                if (matched != RBC_WM_NOMATCH)
                    return matched;

                t_ch = *++text;
            }
            return RBC_WM_ABORT_ALL;

        case '[':
        {
            if (t_ch == '\0')
                return RBC_WM_NOMATCH;

            const char *bracket_p = (const char *)(p - 1);
            if (!rbc_match_bracket(&bracket_p, (char)t_ch, 0))
                return RBC_WM_NOMATCH;
            p = (const unsigned char *)bracket_p - 1; // -1 because loop will increment
            continue;
        }
        }
    }

    return *text ? RBC_WM_NOMATCH : RBC_WM_MATCH;
}
