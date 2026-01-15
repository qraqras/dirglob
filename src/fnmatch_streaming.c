/**
 * @file fnmatch_streaming.c
 * @brief Zero-Allocation Streaming fnmatch implementation
 *
 * Architecture:
 * - Unified matching engine for both rbc_fnmatch() and rbc_xfnmatch()
 * - Zero heap allocation (stack-only)
 * - Precompiled hints for optimization (optional)
 * - Wildmatch-style backtracking
 */

#include "internal.h"
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <stdlib.h>

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Precompiled optimization hints
 *
 * These hints are generated once during pattern compilation and used to
 * optimize pattern matching without changing the core matching logic.
 */
/**
 * @brief Fast path type enumeration
 */
typedef enum fast_path_type_e
{
    FAST_PATH_LITERAL = 0,   // `literal`
    FAST_PATH_STAR,          // `*`
    FAST_PATH_QUESTION,      // `??`
    FAST_PATH_PREFIX,        // `prefix*`
    FAST_PATH_SUFFIX,        // `*suffix`
    FAST_PATH_PREFIX_SUFFIX, // `prefix*suffix`
} fast_path_type_t;

/**
 * @brief Minimal precompiled optimization hints
 *
 * Only contains data actually used by fast paths to minimize overhead.
 * Total size: 7 bytes (8 bytes with padding)
 */
typedef struct precompiled_hints_s
{
    fast_path_type_t fast_path_type; // Fast path type (1 byte)
    uint16_t pattern_len;            // Pattern length (avoids strlen)
    uint16_t literal_prefix_len;     // Literal prefix length
    uint16_t literal_suffix_len;     // Literal suffix length
} precompiled_hints_t;

/**
 * @brief Precompiled fnmatch pattern structure for streaming API
 *
 * This is a lightweight structure used only by the streaming API.
 * It conflicts with the arena-based structure in fnmatch.c, so we
 * define it here for the streaming implementation.
 */
struct rbc_fnmatch_pattern_streaming_s
{
    const char *pattern;
    unsigned flags;
    precompiled_hints_t hints;
};

/**
 * @brief Runtime matching state (fully stack-based)
 *
 * This structure holds the current matching state for the streaming
 * match engine. All fields are stored on the stack.
 */
typedef struct
{
    const char *p;                    // Current pattern position
    const char *t;                    // Current text position
    const char *star_p;               // Backtrack pattern position
    const char *star_t;               // Backtrack text position
    const char *text_start;           // Original text start (for context checks)
    const char *text_end;             // Text end position (cached)
    unsigned int flags;               // FNM_* flags
    const precompiled_hints_t *hints; // NULL for rbc_fnmatch
} stream_state_t;

// ============================================================================
// Forward Declarations
// ============================================================================

static bool match_engine(const char *pattern, const char *text,
                         unsigned flags, const precompiled_hints_t *hints);
static bool generate_hints(const char *pattern, unsigned flags,
                           precompiled_hints_t *hints);
static bool match_bracket(const char **pattern_ptr, char c, unsigned flags);

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
    return match_engine(pattern, text, flags, NULL);
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
    precompiled_hints_t hints;
    if (!generate_hints(pattern, flags, &hints))
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
    return match_engine(p->pattern, text, p->flags, &p->hints);
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
static bool generate_hints(const char *pattern, unsigned flags,
                           precompiled_hints_t *hints)
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
        hints->literal_prefix_len++;
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
            hints->literal_suffix_len++;
            p--;
        }
    }

    // Detect fast path type (priority order matters!)
    if (!has_star && !has_question && !has_bracket && !has_escape)
    {
        hints->fast_path_type = FAST_PATH_LITERAL;
        return true;
    }
    else if (hints->pattern_len == 1 && pattern[0] == '*')
    {
        hints->fast_path_type = FAST_PATH_STAR;
        return true;
    }
    else if (has_question && !has_star && !has_bracket && !has_escape)
    {
        // All characters are '?' - fixed length match
        hints->fast_path_type = FAST_PATH_QUESTION;
        return true;
    }
    else if (pattern[0] == '*' && hints->literal_suffix_len > 0 &&
             hints->literal_suffix_len == hints->pattern_len - 1 &&
             hints->literal_suffix_len > 1 && // Exclude ".*" (suffix must be at least 2 chars for real benefit)
             !has_question && !has_bracket && !has_escape)
    {
        hints->fast_path_type = FAST_PATH_SUFFIX;
        return true;
    }
    else if (pattern[hints->pattern_len - 1] == '*' &&
             hints->literal_prefix_len > 0 &&
             hints->literal_prefix_len == hints->pattern_len - 1 &&
             hints->literal_prefix_len > 1 && // Exclude ".*" (prefix must be at least 2 chars for real benefit)
             !has_question && !has_bracket)
    {
        hints->fast_path_type = FAST_PATH_PREFIX;
        return true;
    }
    else if (hints->literal_prefix_len > 0 &&
             hints->literal_suffix_len > 0 &&
             hints->literal_prefix_len + hints->literal_suffix_len + 1 == hints->pattern_len &&
             !has_question && !has_bracket)
    {
        hints->fast_path_type = FAST_PATH_PREFIX_SUFFIX;
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
static bool match_bracket(const char **pattern_ptr, char c, unsigned flags)
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

/**
 * @brief Unified pattern matching engine
 *
 * This is the core matching function used by both rbc_fnmatch and rbc_xfnmatch.
 * It uses wildmatch-style backtracking for '*' handling.
 *
 * @param pattern Pattern to match
 * @param text Text to match
 * @param flags FNM_* flags
 * @param hints Optimization hints (NULL for rbc_fnmatch)
 * @return true if match, false otherwise
 */
static bool match_engine(const char *pattern, const char *text,
                         unsigned flags, const precompiled_hints_t *hints)
{
    // Calculate text_len once (used for fast paths and text_end)
    size_t text_len = strlen(text);
    stream_state_t state = {
        .p = pattern,
        .t = text,
        .star_p = NULL,
        .star_t = NULL,
        .text_start = text,
        .text_end = text + text_len,
        .flags = flags,
        .hints = hints};

    // === Fast Path: Single switch for hint-based optimizations ===
    if (hints)
    {
        switch (hints->fast_path_type)
        {
        case FAST_PATH_LITERAL:
            return (flags & RBC_FNM_CASEFOLD)
                       ? strcasecmp(pattern, text) == 0
                       : strcmp(pattern, text) == 0;

        case FAST_PATH_STAR:
            return true;

        case FAST_PATH_QUESTION:
            return text_len == hints->pattern_len;

        case FAST_PATH_SUFFIX:
            if (text_len < hints->literal_suffix_len)
                return false;
            if (!(flags & RBC_FNM_DOTMATCH) && text[0] == '.')
                return false;
            return (flags & RBC_FNM_CASEFOLD)
                       ? strcasecmp(text + text_len - hints->literal_suffix_len, pattern + 1) == 0
                       : strcmp(text + text_len - hints->literal_suffix_len, pattern + 1) == 0;

        case FAST_PATH_PREFIX:
            if (text_len < hints->literal_prefix_len)
                return false;
            if (!(flags & RBC_FNM_DOTMATCH) && text[0] == '.' && pattern[0] != '.')
                return false;
            return (flags & RBC_FNM_CASEFOLD)
                       ? strncasecmp(text, pattern, hints->literal_prefix_len) == 0
                       : strncmp(text, pattern, hints->literal_prefix_len) == 0;

        case FAST_PATH_PREFIX_SUFFIX:
        {
            if (text_len < hints->literal_prefix_len + hints->literal_suffix_len)
                return false;
            if (!(flags & RBC_FNM_DOTMATCH) && text[0] == '.' && pattern[0] != '.')
                return false;

            // Check prefix
            bool prefix_match = (flags & RBC_FNM_CASEFOLD)
                                    ? strncasecmp(text, pattern, hints->literal_prefix_len) == 0
                                    : strncmp(text, pattern, hints->literal_prefix_len) == 0;
            if (!prefix_match)
                return false;

            // Check suffix
            const char *pattern_suffix = pattern + hints->literal_prefix_len + 1;
            const char *text_suffix = text + text_len - hints->literal_suffix_len;
            return (flags & RBC_FNM_CASEFOLD)
                       ? strcasecmp(text_suffix, pattern_suffix) == 0
                       : strcmp(text_suffix, pattern_suffix) == 0;
        }

        default:
            break;
        }
    }

    // No early rejection checks for non-fast-path patterns
    // Let the main loop handle everything to avoid overhead

    // === Main Loop: Wildmatch-style backtracking ===
    while (1)
    {
        // Check pattern end
        if (*state.p == '\0')
        {
            return (*state.t == '\0');
        }

        switch (*state.p)
        {
        case '*':
            // Skip consecutive '*'
            while (*state.p == '*')
                state.p++;

            if (*state.p == '\0')
            {
                // Trailing '*' - need to check remaining text for special characters
                const char *check = state.t;
                while (*check)
                {
                    // Cannot match '/' with FNM_PATHNAME
                    if (*check == '/' && (flags & RBC_FNM_PATHNAME))
                    {
                        return false;
                    }
                    // Cannot match leading '.' without DOTMATCH
                    if (*check == '.' && !(flags & RBC_FNM_DOTMATCH) &&
                        (check == state.text_start || (flags & RBC_FNM_PATHNAME && *(check - 1) == '/')))
                    {
                        return false;
                    }
                    check++;
                }
                return true;
            }

            // Record backtrack point
            state.star_p = state.p;
            state.star_t = state.t;

            // Optimization: If next pattern char is a literal, fast-forward to it
            // This avoids trying every position one-by-one
            if (*state.p != '?' && *state.p != '[' && *state.p != '*' && *state.p != '\\')
            {
                // Next char is a literal - fast forward in text to find it
                char literal = *state.p;

                if (flags & RBC_FNM_CASEFOLD)
                {
                    // Case-insensitive: need to scan manually
                    literal = tolower((unsigned char)literal);
                    while (*state.t != '\0')
                    {
                        char t_ch = tolower((unsigned char)*state.t);
                        if (t_ch == literal)
                        {
                            break; // Found
                        }
                        if (t_ch == '/' && (flags & RBC_FNM_PATHNAME))
                        {
                            goto backtrack; // Cannot cross directory boundary
                        }
                        state.t++;
                    }
                    if (*state.t == '\0')
                    {
                        goto backtrack;
                    }
                }
                else if (flags & RBC_FNM_PATHNAME)
                {
                    // Case-sensitive with PATHNAME: need to check for '/'
                    while (*state.t != '\0')
                    {
                        if (*state.t == literal)
                        {
                            break; // Found
                        }
                        if (*state.t == '/')
                        {
                            goto backtrack; // Cannot cross directory boundary
                        }
                        state.t++;
                    }
                    if (*state.t == '\0')
                    {
                        goto backtrack;
                    }
                }
                else
                {
                    // Case-sensitive without PATHNAME: use memchr() for maximum speed
                    size_t remaining = state.text_end - state.t;
                    const char *found = memchr(state.t, literal, remaining);
                    if (!found)
                    {
                        goto backtrack; // Literal not found
                    }
                    state.t = found;
                }
            }
            break;

        case '?':
            if (*state.t == '\0')
                goto backtrack;
            if (*state.t == '/' && (flags & RBC_FNM_PATHNAME))
                goto backtrack;
            if (*state.t == '.' && !(flags & RBC_FNM_DOTMATCH) &&
                (state.t == state.text_start || (flags & RBC_FNM_PATHNAME && *(state.t - 1) == '/')))
            {
                goto backtrack;
            }
            state.p++;
            state.t++;
            break;

        case '[':
            if (*state.t == '\0')
                goto backtrack;
            if (*state.t == '/' && (flags & RBC_FNM_PATHNAME))
                goto backtrack;
            if (*state.t == '.' && !(flags & RBC_FNM_DOTMATCH) &&
                (state.t == state.text_start || (flags & RBC_FNM_PATHNAME && *(state.t - 1) == '/')))
            {
                goto backtrack;
            }

            const char *bracket_start = state.p;
            if (!match_bracket(&state.p, *state.t, flags))
            {
                state.p = bracket_start;
                goto backtrack;
            }
            state.t++;
            break;

        case '\\':
            if (!(flags & RBC_FNM_NOESCAPE))
            {
                state.p++;
                if (*state.p == '\0')
                    goto backtrack;
            }
            // fall through

        default:
            // Literal character matching
            if (*state.p != *state.t)
            {
                if ((flags & RBC_FNM_CASEFOLD) &&
                    tolower((unsigned char)*state.p) == tolower((unsigned char)*state.t))
                {
                    // OK
                }
                else
                {
                    goto backtrack;
                }
            }
            state.p++;
            state.t++;
            break;
        }
        continue;

    backtrack:
        if (!state.star_p)
        {
            return false; // No backtrack point available
        }

        // Check if we've exhausted the text
        if (*state.star_t == '\0')
        {
            return false; // Nothing left to backtrack through
        }

        // Check the character we're about to consume with '*' - is it allowed?
        // Cannot match '/' with FNM_PATHNAME
        if (*state.star_t == '/' && (flags & RBC_FNM_PATHNAME))
        {
            return false;
        }
        // Cannot match leading '.' without DOTMATCH
        if (*state.star_t == '.' && !(flags & RBC_FNM_DOTMATCH) &&
            (state.star_t == state.text_start || (flags & RBC_FNM_PATHNAME && state.star_t > state.text_start && *(state.star_t - 1) == '/')))
        {
            return false;
        }

        // Advance text position for backtracking
        state.star_t++;

        // Optimization: If next pattern char is a literal, fast-forward to next occurrence
        if (*state.star_p != '?' && *state.star_p != '[' && *state.star_p != '*' && *state.star_p != '\\')
        {
            char literal = *state.star_p;

            if (flags & RBC_FNM_CASEFOLD)
            {
                // Case-insensitive: scan manually
                literal = tolower((unsigned char)literal);
                while (*state.star_t != '\0')
                {
                    char t_ch = tolower((unsigned char)*state.star_t);
                    if (t_ch == literal)
                    {
                        break;
                    }
                    if (t_ch == '/' && (flags & RBC_FNM_PATHNAME))
                    {
                        return false;
                    }
                    state.star_t++;
                }
                if (*state.star_t == '\0')
                {
                    return false;
                }
            }
            else if (flags & RBC_FNM_PATHNAME)
            {
                // Case-sensitive with PATHNAME: check for '/'
                while (*state.star_t != '\0')
                {
                    if (*state.star_t == literal)
                    {
                        break;
                    }
                    if (*state.star_t == '/')
                    {
                        return false;
                    }
                    state.star_t++;
                }
                if (*state.star_t == '\0')
                {
                    return false;
                }
            }
            else
            {
                // Case-sensitive without PATHNAME: use memchr() for maximum speed
                size_t remaining = state.text_end - state.star_t;
                const char *found = memchr(state.star_t, literal, remaining);
                if (!found)
                {
                    return false;
                }
                state.star_t = found;
            }
        }

        state.p = state.star_p;
        state.t = state.star_t;
    }
}
