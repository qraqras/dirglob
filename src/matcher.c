#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <rbc/rbc.h>
#include "internal.h"
#include "utils.h"

// Branch prediction hints
#ifdef __GNUC__
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

bool rbc_matcher_build(rbc_arena_t *arena, rbc_matcher_t *m, const char *pattern, unsigned int flags)
{
    m->flags = flags;

    // Initialize prefilter as disabled by default
    m->prefilter.enabled = false;
    m->prefilter.min_length = 0;
    m->prefilter.prefix = NULL;
    m->prefilter.prefix_len = 0;
    m->prefilter.suffix = NULL;
    m->prefilter.suffix_len = 0;

    bool noescape = (flags & RBC_FNM_NOESCAPE);

    // Check if pattern has braces - if so, expand first
    bool has_brace = (strchr(pattern, '{') != NULL);

    if (has_brace)
    {
        // Expand braces and collect all alternatives
        rbc_str_list_t expanded = rbc_brace_collect(pattern, arena);

        if (expanded.count == 0)
        {
            // Failed to expand
            return false;
        }
        else if (expanded.count == 1)
        {
            // Single expansion - just use that pattern
            pattern = expanded.items[0];
            has_brace = false; // No longer has braces after expansion
        }
        else
        {
            // Multiple expansions - create ALTERNATIVES strategy
            m->strategy = RBC_STRATEGY_ALTERNATIVES;
            m->pk.alternatives.count = expanded.count;
            m->pk.alternatives.matchers = (rbc_matcher_t *)rbc_arena_alloc(
                arena, sizeof(rbc_matcher_t) * expanded.count);

            if (!m->pk.alternatives.matchers)
            {
                rbc_str_list_free(&expanded);
                return false;
            }

            // Build a matcher for each expanded pattern
            for (size_t i = 0; i < expanded.count; i++)
            {
                if (!rbc_matcher_build(arena, &m->pk.alternatives.matchers[i],
                                       expanded.items[i], flags))
                {
                    rbc_str_list_free(&expanded);
                    return false;
                }
            }

            rbc_str_list_free(&expanded);
            return true;
        }
    }

    // === Extract prefilter information from pattern (strategy-independent) ===
    size_t pattern_len = strlen(pattern);
    const char *prefix_start = NULL;
    size_t prefix_len = 0;
    const char *suffix_start = NULL;
    size_t suffix_len = 0;
    size_t qmark_count = 0;

    // Extract prefix (literal characters before first wildcard)
    const char *p = pattern;
    while (*p && *p != '*' && *p != '?' && *p != '[')
    {
        if (!noescape && *p == '\\')
        {
            p++;
            if (*p)
                p++;
        }
        else
        {
            p++;
        }
    }
    if (p > pattern)
    {
        prefix_start = pattern;
        prefix_len = p - pattern;
    }

    // Extract suffix (literal characters after last wildcard)
    p = pattern + pattern_len - 1;
    const char *suffix_end = p + 1;
    while (p >= pattern && *p != '*' && *p != '?')
    {
        // Check for closing bracket ]
        if (*p == ']')
        {
            // Scan backward to find matching [
            const char *scan = p - 1;
            while (scan >= pattern && *scan != '[')
            {
                if (!noescape && scan > pattern && scan[-1] == '\\')
                    scan -= 2;
                else
                    scan--;
            }
            if (scan >= pattern && *scan == '[')
            {
                // Found matching [, this is a character class
                p = scan - 1;
                break; // Character class is a wildcard, stop here
            }
            else
            {
                // No matching [, treat ] as literal
                p--;
            }
        }
        else if (!noescape && p > pattern && p[-1] == '\\')
        {
            p -= 2;
        }
        else
        {
            p--;
        }
    }
    if (p + 1 < pattern + pattern_len)
    {
        suffix_start = p + 1;
        suffix_len = suffix_end - suffix_start;
    }

    // Count question marks for minimum length calculation
    p = pattern;
    while (*p)
    {
        if (!noescape && *p == '\\')
        {
            p++;
            if (*p)
                p++;
            continue;
        }
        if (*p == '?')
            qmark_count++;
        p++;
    }

    // Calculate minimum length: fixed prefix + qmarks + fixed suffix
    size_t min_len = prefix_len + qmark_count + suffix_len;

    // Setup prefilter if we have useful constraints
    if (min_len > 0 || prefix_len > 0 || suffix_len > 0)
    {
        m->prefilter.enabled = true;
        m->prefilter.min_length = min_len;

        if (prefix_len > 0)
        {
            m->prefilter.prefix = rbc_arena_alloc(arena, prefix_len + 1);
            if (m->prefilter.prefix)
            {
                memcpy((char *)m->prefilter.prefix, prefix_start, prefix_len);
                ((char *)m->prefilter.prefix)[prefix_len] = '\0';
                m->prefilter.prefix_len = prefix_len;
            }
        }

        if (suffix_len > 0)
        {
            m->prefilter.suffix = rbc_arena_alloc(arena, suffix_len + 1);
            if (m->prefilter.suffix)
            {
                memcpy((char *)m->prefilter.suffix, suffix_start, suffix_len);
                ((char *)m->prefilter.suffix)[suffix_len] = '\0';
                m->prefilter.suffix_len = suffix_len;
            }
        }
    }

    // === Now select strategy based on pattern complexity ===

    // Fast path for ultra-common patterns (before any complex analysis)
    size_t len = strlen(pattern);
    
    // Single character patterns
    if (unlikely(len == 1))
    {
        if (pattern[0] == '*')
        {
            m->strategy = RBC_STRATEGY_PREFIX;
            m->pk.str.ptr = "";
            m->pk.str.len = 0;
            m->prefilter.enabled = false;
            return true;
        }
        if (pattern[0] == '?')
        {
            m->strategy = RBC_STRATEGY_RECURSIVE;
            m->pk.recursive.pattern = rbc_arena_strdup(arena, pattern);
            return m->pk.recursive.pattern != NULL;
        }
        // Single literal character
        m->strategy = RBC_STRATEGY_EXACT;
        m->pk.str.ptr = rbc_arena_strdup(arena, pattern);
        if (!m->pk.str.ptr)
            return false;
        m->pk.str.len = 1;
        m->prefilter.prefix_len = 1;
        return true;
    }
    
    // "*.ext" pattern (extremely common)
    if (likely(pattern[0] == '*' && pattern[1] == '.'))
    {
        bool is_simple_ext = true;
        for (size_t i = 2; i < len; i++)
        {
            char c = pattern[i];
            if (c == '*' || c == '?' || c == '[' || c == '{')
            {
                is_simple_ext = false;
                break;
            }
            if (!noescape && c == '\\')
            {
                is_simple_ext = false;
                break;
            }
        }
        
        if (is_simple_ext)
        {
            // Use SUFFIX strategy for *.ext
            m->strategy = RBC_STRATEGY_SUFFIX;
            m->pk.str.ptr = rbc_arena_strdup(arena, pattern + 1);
            if (!m->pk.str.ptr)
                return false;
            m->pk.str.len = len - 1;
            return true;
        }
    }

    // Check for complexity features
    bool has_qmark = (strchr(pattern, '?') != NULL);
    bool has_bracket = (strchr(pattern, '[') != NULL);
    bool has_paren = (strchr(pattern, '(') != NULL);
    bool has_pipe = (strchr(pattern, '|') != NULL);

    // Count stars
    int star_count = 0;
    p = pattern;
    while (*p)
    {
        if (!noescape && *p == '\\')
        {
            p++;
            if (*p)
                p++;
            continue;
        }
        if (*p == '*')
            star_count++;
        p++;
    }

    // Force VM if complex characters exist (but not braces - already handled)
    if (has_qmark || has_bracket || has_paren || has_pipe)
    {
        m->strategy = RBC_STRATEGY_RECURSIVE;
        // Disable prefilter only if pattern contains character classes
        // (? is fine for prefilter, but [...] extraction is complex)
        if (has_bracket)
        {
            m->prefilter.enabled = false;
        }
    }
    else if (star_count == 0)
    {
        fprintf(stderr, "[MATCHER_BUILD] Pattern '%s' has no stars, setting EXACT\n", pattern);
        m->strategy = RBC_STRATEGY_EXACT;
        m->pk.str.ptr = rbc_arena_strdup(arena, pattern);
        if (!m->pk.str.ptr)
            return false;
        m->pk.str.len = strlen(pattern);
        m->prefilter.prefix_len = m->pk.str.len;
    }
    else if (star_count == 1)
    {
        size_t len = strlen(pattern);
        if (has_qmark)
        {
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;
            goto build_chain;
        }

        if (pattern[0] == '*')
        {
            if (len == 1)
            {
                m->strategy = RBC_STRATEGY_PREFIX;
                m->pk.str.ptr = "";
                m->pk.str.len = 0;
                // No prefilter for "*" (matches everything)
            }
            else
            {
                m->strategy = RBC_STRATEGY_SUFFIX;
                m->pk.str.ptr = rbc_arena_strdup(arena, pattern + 1);
                if (!m->pk.str.ptr)
                    return false;
                m->pk.str.len = len - 1;
            }
        }
        else if (pattern[len - 1] == '*')
        {
            m->strategy = RBC_STRATEGY_PREFIX;
            m->pk.str.ptr = rbc_arena_strdup(arena, pattern);
            if (!m->pk.str.ptr)
                return false;
            m->pk.str.ptr[len - 1] = '\0';
            m->pk.str.len = len - 1;
        }
        else
        {
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;
            m->pk.chain.count = 2;
            m->pk.chain.parts = (char **)rbc_arena_alloc(arena, sizeof(char *) * 2);
            if (!m->pk.chain.parts)
                return false;
            m->pk.chain.lengths = (size_t *)rbc_arena_alloc(arena, sizeof(size_t) * 2);
            if (!m->pk.chain.lengths)
                return false;

            char *star_pos = strchr(pattern, '*');
            size_t pre_len = (size_t)(star_pos - pattern);
            char *pre = (char *)rbc_arena_alloc(arena, pre_len + 1);
            if (!pre)
                return false;
            memcpy(pre, pattern, pre_len);
            pre[pre_len] = '\0';
            m->pk.chain.parts[0] = pre;
            m->pk.chain.lengths[0] = pre_len;

            m->pk.chain.parts[1] = rbc_arena_strdup(arena, star_pos + 1);
            if (!m->pk.chain.parts[1])
                return false;
            m->pk.chain.lengths[1] = strlen(m->pk.chain.parts[1]);

            m->pk.chain.match_start = true;
            m->pk.chain.match_end = true;
        }
    }
    else
    {
        size_t len = strlen(pattern);
        if (!has_qmark && pattern[0] == '*' && pattern[len - 1] == '*' && star_count == 2)
        {
            m->strategy = RBC_STRATEGY_INFIX;
            m->pk.str.ptr = rbc_arena_strdup(arena, pattern + 1);
            if (!m->pk.str.ptr)
                return false;
            m->pk.str.ptr[len - 2] = '\0';
            m->pk.str.len = len - 2;
        }
        else
        {
            m->strategy = RBC_STRATEGY_PATTERN_CHAIN;

        build_chain:
        {
            const char *p_scan = pattern;
            if (!noescape && *p_scan == '\\')
            {
                m->pk.chain.match_start = true;
            }
            else if (*p_scan == '*')
            {
                m->pk.chain.match_start = false;
            }
            else
            {
                m->pk.chain.match_start = true;
            }

            bool last_was_star = false;
            p_scan = pattern;
            while (*p_scan)
            {
                if (!noescape && *p_scan == '\\')
                {
                    p_scan++;
                    if (*p_scan)
                        p_scan++;
                    last_was_star = false;
                    continue;
                }
                if (*p_scan == '*')
                {
                    last_was_star = true;
                    p_scan++;
                    continue;
                }
                last_was_star = false;
                p_scan++;
            }
            m->pk.chain.match_end = !last_was_star;
        }

            size_t max_parts = (size_t)star_count + 1;
            char **parts = (char **)rbc_arena_alloc(arena, sizeof(char *) * max_parts);
            if (!parts)
                return false;
            size_t *lengths = (size_t *)rbc_arena_alloc(arena, sizeof(size_t) * max_parts);
            if (!lengths)
                return false;
            size_t count = 0;

            const char *curr = pattern;
            const char *scan = pattern;

            while (*scan)
            {
                if (!noescape && *scan == '\\')
                {
                    scan++;
                    if (*scan)
                        scan++;
                    continue;
                }
                if (*scan == '*')
                {
                    if (scan > curr)
                    {
                        size_t plen = (size_t)(scan - curr);
                        char *part = (char *)rbc_arena_alloc(arena, plen + 1);
                        if (!part)
                            return false;
                        memcpy(part, curr, plen);
                        part[plen] = '\0';
                        parts[count] = part;
                        lengths[count] = plen;
                        count++;
                    }
                    curr = scan + 1;
                    scan = curr;
                    continue;
                }
                scan++;
            }
            if (*scan == '\0' && scan > curr)
            {
                size_t plen = (size_t)(scan - curr);
                char *part = (char *)rbc_arena_alloc(arena, plen + 1);
                if (!part)
                    return false;
                memcpy(part, curr, plen);
                part[plen] = '\0';
                parts[count] = part;
                lengths[count] = plen;
                count++;
            }

            m->pk.chain.parts = parts;
            m->pk.chain.lengths = lengths;
            m->pk.chain.count = count;
        }
    }

    if (m->strategy == RBC_STRATEGY_RECURSIVE)
    {
        m->pk.recursive.pattern = rbc_arena_strdup(arena, pattern);
        if (!m->pk.recursive.pattern)
            return false;
    }
    return true;
}

/* --- Matcher Execution Implementation --- */

static bool rbc_match_fixed(const char *s, const char *pat, size_t len, bool casefold)
{
    if (casefold)
    {
        for (size_t i = 0; i < len; i++)
        {
            if (tolower((unsigned char)s[i]) != tolower((unsigned char)pat[i]))
                return false;
        }
        return true;
    }
    return strncmp(s, pat, len) == 0;
}

static const char *rbc_search_fixed(const char *s, const char *pat, const char *end, bool casefold)
{
    size_t pat_len = strlen(pat);
    if (pat_len == 0)
        return s;

    while (s + pat_len <= end)
    {
        if (rbc_match_fixed(s, pat, pat_len, casefold))
            return s;
        s++;
    }
    return NULL;
}

#define IS_PATHNAME (flags & RBC_FNM_PATHNAME)
#define IS_CASEFOLD (flags & RBC_FNM_CASEFOLD)
#define IS_DOTMATCH (flags & RBC_FNM_DOTMATCH)
#define IS_NOESCAPE (flags & RBC_FNM_NOESCAPE)

static const char *bracket_match(const char *p, unsigned char c, unsigned int flags)
{
    bool neg = false;
    if (*p == '!' || *p == '^')
    {
        neg = true;
        p++;
    }

    bool matched = false;

    while (*p && *p != ']')
    {
        unsigned char lower = (unsigned char)*p;
        unsigned char upper = lower;

        if (p[1] == '-' && p[2] && p[2] != ']')
        {
            upper = (unsigned char)p[2];
            p += 2;
        }

        if (IS_CASEFOLD)
        {
            unsigned char lc = (unsigned char)tolower(c);
            unsigned char ll = (unsigned char)tolower(lower);
            unsigned char lu = (unsigned char)tolower(upper);
            if (lc >= ll && lc <= lu)
                matched = true;
        }
        else
        {
            if (c >= lower && c <= upper)
                matched = true;
        }
        p++;
    }

    if (*p != ']')
        return NULL; // Invalid bracket

    return (matched ^ neg) ? p + 1 : NULL;
}

// Wildmatch-style implementation with backtrack optimization (no recursion)
// Based on wildmatch algorithm used in Git/rsync

static bool match_wildmatch(const char *p, const char *s, const char *initial_str, unsigned int flags)
{
    const char *star_p = NULL;
    const char *star_s = NULL;

    while (true)
    {
        // Match current character
        if (*p == '*')
        {
            // Collapse consecutive stars
            while (p[1] == '*')
                p++;
            p++;

            // Optimization: trailing star matches everything (unless pathname constraint)
            if (unlikely(*p == '\0'))
            {
                if (IS_PATHNAME)
                    return strchr(s, '/') == NULL;
                return true;
            }

            // Save backtrack point
            star_p = p;
            star_s = s;
            continue;
        }

        // Current positions don't match
        bool char_matched = false;

        if (*s == '\0')
        {
            // End of string - only matches if pattern is also done (or only stars remain)
            while (*p == '*')
                p++;
            char_matched = (*p == '\0');
        }
        else if (*p == '?')
        {
            // Question mark matches any single character (with constraints)
            if (unlikely(IS_PATHNAME && *s == '/'))
                char_matched = false;
            else if (unlikely(!IS_DOTMATCH && *s == '.' &&
                             (s == initial_str || (IS_PATHNAME && s[-1] == '/'))))
                char_matched = false;
            else
            {
                p++;
                s++;
                char_matched = true;
            }
        }
        else if (*p == '[')
        {
            // Character class
            if (unlikely(IS_PATHNAME && *s == '/'))
                char_matched = false;
            else if (unlikely(!IS_DOTMATCH && *s == '.' &&
                             (s == initial_str || (IS_PATHNAME && s[-1] == '/'))))
                char_matched = false;
            else
            {
                const char *nxt = bracket_match(p + 1, (unsigned char)*s, flags);
                if (nxt)
                {
                    p = nxt;
                    s++;
                    char_matched = true;
                }
            }
        }
        else
        {
            // Literal character match (handle escape)
            char c1 = *p;
            if (unlikely(c1 == '\\' && !IS_NOESCAPE && p[1]))
            {
                p++;
                c1 = *p;
            }

            char c2 = *s;
            if (IS_CASEFOLD)
            {
                c1 = (char)tolower((unsigned char)c1);
                c2 = (char)tolower((unsigned char)c2);
            }

            if (c1 == c2)
            {
                p++;
                s++;
                char_matched = true;
            }
        }

        // If current match succeeded, continue
        if (likely(char_matched))
            continue;

        // Match failed - try backtracking
        if (star_p)
        {
            // Backtrack: advance string position and retry from saved pattern
            s = ++star_s;

            // Check constraints before backtracking
            if (unlikely(*s == '\0'))
            {
                return false;  // Can't backtrack past end
            }
            if (unlikely(IS_PATHNAME && *s == '/'))
            {
                return false;  // Star can't cross '/' boundary
            }
            if (unlikely(!IS_DOTMATCH && *s == '.' &&
                        (s == initial_str || (IS_PATHNAME && s[-1] == '/'))))
            {
                return false;  // Star can't match leading dot
            }

            p = star_p;
            continue;
        }

        // No backtrack point available - match failed
        return false;
    }
}

static bool rbc_match_recursive_entry(const char *text, const char *pattern, unsigned int flags)
{
    if (unlikely(!text || !pattern))
        return false;
    // Use wildmatch implementation for better performance
    return match_wildmatch(pattern, text, text, flags);
}

bool rbc_matcher_exec(const rbc_matcher_t *m, const char *name)
{
    size_t name_len = strlen(name);
    bool matched = false;
    unsigned int flags = m->flags;
    bool pathname = (flags & RBC_FNM_PATHNAME);
    bool casefold = (flags & RBC_FNM_CASEFOLD);
    
    // Ultra-fast path for common empty/trivial cases
    if (unlikely(name_len == 0))
    {
        // Only matches empty pattern or "*"
        return (m->strategy == RBC_STRATEGY_EXACT && m->pk.str.len == 0) ||
               (m->strategy == RBC_STRATEGY_PREFIX && m->pk.str.len == 0);
    }

    // === Phase 1: Pre-filter (fast early rejection) ===
    if (m->prefilter.enabled)
    {
        // Length check (fastest)
        if (unlikely(name_len < m->prefilter.min_length))
            return false;

        // Prefix check
        if (m->prefilter.prefix)
        {
            if (casefold)
            {
                if (unlikely(!rbc_match_fixed(name, m->prefilter.prefix, m->prefilter.prefix_len, true)))
                    return false;
            }
            else
            {
                if (unlikely(name_len < m->prefilter.prefix_len ||
                    strncmp(name, m->prefilter.prefix, m->prefilter.prefix_len) != 0))
                    return false;
            }
        }

        // Suffix check
        if (m->prefilter.suffix)
        {
            if (casefold)
            {
                if (unlikely(!rbc_match_fixed(name + name_len - m->prefilter.suffix_len,
                                     m->prefilter.suffix, m->prefilter.suffix_len, true)))
                    return false;
            }
            else
            {
                if (unlikely(name_len < m->prefilter.suffix_len ||
                    strcmp(name + name_len - m->prefilter.suffix_len, m->prefilter.suffix) != 0))
                    return false;
            }
        }
    }

    // === Phase 2: Strategy execution ===
    switch (m->strategy)
    {
    case RBC_STRATEGY_EXACT:
        if (casefold)
            matched = (rbc_match_fixed(name, m->pk.str.ptr, name_len, true) && m->pk.str.len == name_len);
        else
            matched = (m->pk.str.len == name_len && strcmp(name, m->pk.str.ptr) == 0);
        break;
    case RBC_STRATEGY_PREFIX:
        if (casefold)
            matched = rbc_match_fixed(name, m->pk.str.ptr, m->pk.str.len, true);
        else
            matched = (name_len >= m->pk.str.len && strncmp(name, m->pk.str.ptr, m->pk.str.len) == 0);

        if (matched && unlikely(pathname))
        {
            if (memchr(name + m->pk.str.len, '/', name_len - m->pk.str.len))
                matched = false;
        }
        break;
    case RBC_STRATEGY_SUFFIX:
        if (likely(name_len >= m->pk.str.len))
        {
            if (casefold)
                matched = rbc_match_fixed(name + name_len - m->pk.str.len, m->pk.str.ptr, m->pk.str.len, true);
            else
                matched = (strcmp(name + name_len - m->pk.str.len, m->pk.str.ptr) == 0);

            if (matched && unlikely(pathname))
            {
                if (memchr(name, '/', name_len - m->pk.str.len))
                    matched = false;
            }
        }
        break;
    case RBC_STRATEGY_INFIX:
        if (!pathname)
        {
            if (casefold)
                matched = (rbc_search_fixed(name, m->pk.str.ptr, name + name_len, true) != NULL);
            else
                matched = (strstr(name, m->pk.str.ptr) != NULL);
        }
        else
        {
            const char *p = name;
            size_t pat_len = m->pk.str.len;
            const char *end_ptr = name + name_len;

            while (true)
            {
                if (casefold)
                    p = rbc_search_fixed(p, m->pk.str.ptr, end_ptr, true);
                else
                    p = strstr(p, m->pk.str.ptr);

                if (!p)
                    break;

                // Check prefix and suffix gaps for '/'
                if (memchr(name, '/', p - name) == NULL &&
                    memchr(p + pat_len, '/', end_ptr - (p + pat_len)) == NULL)
                {
                    matched = true;
                    break;
                }
                p++;
                if (p >= end_ptr)
                    break;
            }
        }
        break;
    case RBC_STRATEGY_PATTERN_CHAIN:
    {
        const char *p = name;
        const char *end_limit = name + name_len;
        matched = true;
        size_t count = m->pk.chain.count;

        // Special fast path for prefix+suffix pattern (e.g., a*c, test_*.c)
        // Prefilter has already verified prefix/suffix, just check pathname
        if (count == 2 && m->pk.chain.match_start && m->pk.chain.match_end)
        {
            size_t prefix_len = m->pk.chain.lengths[0];
            size_t suffix_len = m->pk.chain.lengths[1];

            // Prefilter already verified length, prefix, and suffix
            // Only need to check pathname constraint
            if (pathname && memchr(name + prefix_len, '/', name_len - prefix_len - suffix_len))
            {
                matched = false;
            }
            else
            {
                matched = true;
            }
            break;
        }

        // Prefilter already checked minimum length and prefix/suffix
        // General PATTERN_CHAIN logic for multiple parts
        if (m->pk.chain.match_end && count > 0)
        {
            char *last = m->pk.chain.parts[count - 1];
            size_t last_len = m->pk.chain.lengths[count - 1];
            if (name_len < last_len)
                matched = false;
            else if (!rbc_match_fixed(name + name_len - last_len, last, last_len, casefold))
                matched = false;
            else
            {
                end_limit -= last_len;
                count--;
            }
        }
        if (matched)
        {
            for (size_t i = 0; i < count; i++)
            {
                char *part = m->pk.chain.parts[i];
                size_t part_len = m->pk.chain.lengths[i];
                if (i == 0 && m->pk.chain.match_start)
                {
                    if ((size_t)(end_limit - p) < part_len)
                    {
                        matched = false;
                        break;
                    }
                    if (!rbc_match_fixed(p, part, part_len, casefold))
                    {
                        matched = false;
                        break;
                    }
                    p += part_len;
                }
                else
                {
                    const char *search_start = p;
                    bool part_found = false;
                    while (search_start + part_len <= end_limit)
                    {
                        const char *found = rbc_search_fixed(search_start, part, end_limit, casefold);
                        if (!found)
                            break;

                        if (pathname && memchr(p, '/', found - p))
                        {
                            search_start = found + 1;
                            continue;
                        }

                        p = found + part_len;
                        part_found = true;
                        break;
                    }
                    if (!part_found)
                    {
                        matched = false;
                        break;
                    }
                }
            }
            if (matched && pathname && p < end_limit)
            {
                if (memchr(p, '/', end_limit - p))
                    matched = false;
            }
        }
    }
    break;
    case RBC_STRATEGY_ALTERNATIVES:
    {
        // Try each alternative matcher - if any matches, return true
        for (size_t i = 0; i < m->pk.alternatives.count; i++)
        {
            if (rbc_matcher_exec(&m->pk.alternatives.matchers[i], name))
            {
                matched = true;
                break;
            }
        }
    }
    break;
    case RBC_STRATEGY_RECURSIVE:
        matched = rbc_match_recursive_entry(name, m->pk.recursive.pattern, m->flags);
        break;
    }
    return matched;
}
