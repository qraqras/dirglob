#include "platform.h"
#include "rbc/rbc.h"
#include "../utils/utils.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define RBC_GLOB_MAX_PATH RBC_MAX_PATH
#define RBC_RESULTS_INIT_DATA (64 * 1024) // 64KB initial data buffer
#define RBC_RESULTS_INIT_COUNT 256        // Initial path count

/// @defgroup Segment Character Flags
/// @{
#define RBC_SEG_CONTAINS_STAR 0x01     // Contains '*'
#define RBC_SEG_CONTAINS_QUESTION 0x02 // Contains '?'
#define RBC_SEG_CONTAINS_BRACKET 0x04  // Contains '[...]'
#define RBC_SEG_CONTAINS_ESCAPE 0x08   // Contains escape sequences
#define RBC_SEG_CONTAINS_REGULAR 0x10  // Contains regular characters
/// @}

/// @defgroup Internal Glob Flags (high bits, not part of public FNM_* flags)
/// @{
#define RBC_GLOB_HAS_WILDCARD_ANCESTOR 0x10000000 // Internal: traversed through wildcard
/// @}

/// @defgroup Internal Error Codes
/// @{
typedef enum rbc_glob_err_e
{
    RBC_GLOB_OK = 0,           // No error
    RBC_GLOB_ERR_MEMORY,       // Memory allocation failure (fatal)
    RBC_GLOB_ERR_PATH_TOO_LONG // Path exceeded MAX_PATH (skip)
} rbc_glob_err_t;
/// @}

// Forward declaration
typedef struct rbc_results_s rbc_results_t;
static bool rbc_glob_error(rbc_results_t *r, rbc_glob_err_t err);

/// @defgroup Results
/// @{

/// @brief Results Buffer Structure
struct rbc_results_s
{
    char *arena;             // Arena buffer storing all paths contiguously
    size_t *offsets;         // Offset of each path in arena (in bytes)
    size_t count;            // Number of stored paths
    size_t arena_used;       // Bytes used in arena
    size_t arena_capacity;   // Total capacity of arena (in bytes)
    size_t offsets_capacity; // Capacity of offsets array (in elements)
    rbc_glob_err_t error;    // Error code (RBC_GLOB_OK if no fatal error)
};

/// @brief Initialize results buffer
/// @param[out] r Results buffer
/// @return true on success, false on failure
static bool rbc_results_init(rbc_results_t *r)
{
    r->arena = malloc(RBC_RESULTS_INIT_DATA);
    r->offsets = malloc(RBC_RESULTS_INIT_COUNT * sizeof(size_t));
    if (!r->arena || !r->offsets)
    {
        free(r->arena);
        free(r->offsets);
        return false;
    }
    r->count = 0;
    r->arena_used = 0;
    r->arena_capacity = RBC_RESULTS_INIT_DATA;
    r->offsets_capacity = RBC_RESULTS_INIT_COUNT;
    r->error = RBC_GLOB_OK;
    return true;
}

/// @brief Add a path to results buffer
/// @param[in, out] r Results buffer
/// @param[in] path Path string
/// @param[in] len Length of path
/// @return true on success, false on failure
static bool rbc_results_add(rbc_results_t *r, const char *path, size_t len)
{
    // Grow offsets array if needed
    if (r->count >= r->offsets_capacity)
    {
        size_t new_cap = r->offsets_capacity * 2;
        size_t *new_off = realloc(r->offsets, new_cap * sizeof(size_t));
        if (!new_off)
        {
            rbc_glob_error(r, RBC_GLOB_ERR_MEMORY);
            return false;
        }
        r->offsets = new_off;
        r->offsets_capacity = new_cap;
    }

    // Grow arena buffer if needed (include null terminator)
    size_t needed = r->arena_used + len + 1;
    if (needed > r->arena_capacity)
    {
        size_t new_cap = r->arena_capacity;
        while (new_cap < needed)
            new_cap *= 2;
        char *new_arena = realloc(r->arena, new_cap);
        if (!new_arena)
        {
            rbc_glob_error(r, RBC_GLOB_ERR_MEMORY);
            return false;
        }
        r->arena = new_arena;
        r->arena_capacity = new_cap;
    }

    // Store offset and copy path
    r->offsets[r->count++] = r->arena_used;
    memcpy(r->arena + r->arena_used, path, len);
    r->arena[r->arena_used + len] = '\0';
    r->arena_used += len + 1;

    return true;
}

/// @brief Free results buffer
/// @param[in] r Results buffer
static void rbc_results_free(rbc_results_t *r)
{
    free(r->arena);
    free(r->offsets);
}

/// @brief Report an error during glob operation
/// @param[in,out] r Results buffer
/// @param[in] err Error code
/// @return true if operation should continue (non-fatal), false if should abort (fatal)
static bool rbc_glob_error(rbc_results_t *r, rbc_glob_err_t err)
{
    switch (err)
    {
    case RBC_GLOB_ERR_MEMORY:
        r->error = err;
        return false; // Abort
    case RBC_GLOB_ERR_PATH_TOO_LONG:
    case RBC_GLOB_OK:
    default:
        return true; // Continue
    }
}

/// @}

/// @defgroup Path
/// @{

/// @brief Build path: base + '/' + component
/// @pre base must be normalized (no backslashes, no consecutive or trailing slashes)
/// @pre component should be a single path component (filename/dirname)
/// @param[out] buf Output buffer
/// @param[in] buf_size Size of output buffer
/// @param[in] base Base path (must be normalized: forward slashes only, no trailing slash)
/// @param[in] base_len Length of base
/// @param[in] component Path component to append (single component, not a path)
/// @param[in] component_len Length of component
/// @return Length of resulting path on success, 0 on error (buffer overflow)
static size_t rbc_path_join(
    char *buf,
    size_t buf_size,
    const char *base,
    size_t base_len,
    const char *component,
    size_t component_len)
{
    // Worst case: `base + '/' + component + '\0'`
    if ((base_len + component_len + 2) > buf_size)
        return 0;
    memcpy(buf, base, base_len);
    size_t pos = base_len;
    if (pos > 0 && base[pos - 1] != '/')
        buf[pos++] = '/';
    memcpy(buf + pos, component, component_len);
    pos += component_len;
    buf[pos] = '\0';
    return pos;
}

/// @brief Append trailing slash to path buffer if not present
/// @param[in, out] buf Buffer containing the path
/// @param[in] buf_size Size of the buffer
/// @param[in] path_len Current length of the path in the buffer
/// @return New length of the path after appending slash, or 0 on error
/// @note Empty path is treated as error because appending "/" would change semantics ("" → "/" converts relative to absolute root)
/// @note Returns path_len unchanged if path already has trailing slash
static size_t rbc_path_append_slash(char *buf, size_t buf_size, size_t path_len)
{
    if (path_len == 0)
        return 0;
    if (buf[path_len - 1] == '/')
        return path_len;
    if ((path_len + 2) > buf_size)
        return 0;
    buf[path_len++] = '/';
    buf[path_len] = '\0';
    return path_len;
}

/// @}

/// ============================================================================
/// Fnmatch Internal Implementation
/// ============================================================================
bool rbc_fnmatch(const char *pattern, const char *string, unsigned flags);
bool rbc_fnmatch_len(const char *pattern, size_t pattern_len, const char *path, unsigned flags);

/// @brief Check if wildcard pattern can match a dotfile entry
/// @param[in] name Entry name
/// @param[in] flags Matching flags (DOTMATCH, HAS_WILDCARD_ANCESTOR)
/// @param[in] explicit_dot Whether the pattern segment starts with '.'
/// @return true if pattern can match this entry, false if should be SKIPPED (hidden)
/// @note Used for wildcard matching decision (SEG_WILDCARD)
/// @note Considers both DOTMATCH flag and explicit pattern notation
static bool rbc_wildcard_can_match_dotfile(const char *name, unsigned flags, bool explicit_dot)
{
    if (name[0] != '.')
        return true;
    // `.`
    if (name[1] == '\0')
        return ((explicit_dot || (flags & RBC_FNM_DOTMATCH)) && !(flags & RBC_GLOB_HAS_WILDCARD_ANCESTOR));
    // `..`
    if (name[1] == '.' && name[2] == '\0')
        return false;
    // `.hidden`
    return (explicit_dot || (flags & RBC_FNM_DOTMATCH));
}

/// @brief Check if ** pattern can descend into or emit a dotfile entry
/// @param[in] name Entry name
/// @param[in] flags Matching flags (DOTMATCH only)
/// @return true if entry should be processed (emit/descend), false if should be SKIPPED
/// @note Used for ** (SEG_RECURSIVE) descending/emitting decision (Decision 3)
/// @note Considers only DOTMATCH flag, NOT pattern notation (unlike wildcard matching)
/// @note For example: `**/*` never reaches `.hidden/*` unless DOTMATCH is set
///                     because Decision 3 prevents descent into `.hidden`
static bool rbc_recursive_can_descend_into_dotfile(const char *name, unsigned flags)
{
    if (name[0] != '.')
        return true;
    // `.`
    if (name[1] == '\0')
        return false;
    // `..`
    if (name[1] == '.' && name[2] == '\0')
        return false;
    // `.hidden`
    return (flags & RBC_FNM_DOTMATCH);
}

/// @defgroup Path Segment
/// @{

/// @brief Segment Types
typedef enum rbc_segment_type_e
{
    SEG_LITERAL,   // `abc`
    SEG_DOT,       // `.`
    SEG_DOTDOT,    // `..`
    SEG_WILDCARD,  // `?`, `[abc]`, `*?`, etc. (needs fnmatch)
    SEG_RECURSIVE, // `**/`
} rbc_segment_type_t;

/// @brief Segment Structure
typedef struct rbc_segment_s
{
    const char *start;       // Segment start in pattern
    size_t len;              // Segment length
    rbc_segment_type_t type; // Segment classification
    bool starts_with_dot;    // Segment starts with '.'
    bool has_trailing_slash; // Pattern has '/' after this segment
    bool is_last;            // This is the last segment
    const char *next;        // Remaining pattern after this segment
} rbc_segment_t;

/// @brief Parse next segment from pattern
/// @param[in, out] pattern Pointer to pattern pointer (updated to after parsed segment)
/// @param[in] flags Matching flags
/// @param[out] seg Parsed segment
/// @return true if a segment was parsed, false if end of pattern
static bool rbc_segment_next(const char **pattern, unsigned flags, rbc_segment_t *seg)
{
    const char *p = *pattern;

    while (*p == '/')
        p++;

    if (*p == '\0')
        return false;

    seg->start = p;
    seg->starts_with_dot = (*p == '.');
    seg->has_trailing_slash = false;

    // Scan the segment
    unsigned char_flags = 0;
    bool in_bracket = false; // Bracket can not be nested, so a simple flag is sufficient

    while (*p && !(*p == '/' && !in_bracket))
    {
        // Handle escape sequences
        if (!(flags & RBC_FNM_NOESCAPE) && *p == '\\' && *(p + 1))
        {
            char_flags |= RBC_SEG_CONTAINS_ESCAPE;
            p += 2;
            continue;
        }

        // Collect character flags
        switch (*p)
        {
        case '*':
            char_flags |= RBC_SEG_CONTAINS_STAR;
            break;
        case '?':
            char_flags |= RBC_SEG_CONTAINS_QUESTION;
            break;
        case '[':
            char_flags |= RBC_SEG_CONTAINS_BRACKET;
            in_bracket = true;
            break;
        case ']':
            if (!in_bracket)
                char_flags |= RBC_SEG_CONTAINS_REGULAR;
            in_bracket = false;
            break;
        default:
            char_flags |= RBC_SEG_CONTAINS_REGULAR;
            break;
        }
        p++;
    }

    // If still in bracket at segment end, segment is invalid
    if (in_bracket)
        return false;

    seg->len = p - seg->start;
    if (*p == '/')
    {
        seg->has_trailing_slash = true;
        while (*p == '/')
            p++;
    }
    seg->is_last = (*p == '\0');

    if ((seg->len == 1) && (seg->start[0] == '.'))
        seg->type = SEG_DOT;
    else if ((seg->len == 2) && (seg->start[0] == '.') && (seg->start[1] == '.'))
        seg->type = SEG_DOTDOT;
    else if ((seg->len == 2) && (char_flags == RBC_SEG_CONTAINS_STAR) && seg->has_trailing_slash)
        seg->type = SEG_RECURSIVE;
    else if (char_flags == RBC_SEG_CONTAINS_REGULAR)
        seg->type = SEG_LITERAL;
    else
        seg->type = SEG_WILDCARD;

    seg->next = p;
    *pattern = p;

    return true;
}

/// @brief Match a single segment against a string
/// @param[in] seg Segment to match
/// @param[in] string String to match against
/// @param[in] flags Matching flags
/// @return true if matched, false if no match
static bool rbc_segment_match(const rbc_segment_t *seg, const char *string, unsigned flags)
{
    switch (seg->type)
    {
    case SEG_DOT:
        return string[0] == '.' && string[1] == '\0';
    case SEG_DOTDOT:
        return string[0] == '.' && string[1] == '.' && string[2] == '\0';
    case SEG_LITERAL:
        if (flags & RBC_FNM_CASEFOLD)
        {
            const char *p_start = seg->start;
            const char *p_end = seg->start + seg->len;
            const char *s = string;
            while (p_start < p_end && *s)
                if (!rbc_char_match(*p_start++, *s++, flags))
                    return false;
            return p_start == p_end && *s == '\0';
        }
        return strlen(string) == seg->len && strncmp(seg->start, string, seg->len) == 0;
    case SEG_WILDCARD:
        return rbc_fnmatch_len(seg->start, seg->len, string, flags);
    case SEG_RECURSIVE:
        return false; // RECURSIVE should not be matched directly
    }
    return false;
}

/// @}

/// ============================================================================
/// Glob Internal Implementation
/// ============================================================================

/// @brief Emit a path to results buffer
/// @param[in] path Base path
/// @param[in] path_len Length of base path
/// @param[in] name Entry name to append (NULL to emit path only)
/// @param[in] trailing_slash Whether to append trailing slash
/// @param[in] baselen Base length to strip from output path
/// @param[in,out] results Results buffer
/// @note On memory error, sets results->error. Path too long is silently skipped.
static void rbc_glob_emit(
    const char *path,
    size_t path_len,
    const char *name,
    bool trailing_slash,
    size_t baselen,
    rbc_results_t *results)
{
    char pathbuf[RBC_GLOB_MAX_PATH];
    size_t len;

    if (name)
    {
        // Join path + name
        size_t name_len = strlen(name);
        len = rbc_path_join(pathbuf, sizeof(pathbuf), path, path_len, name, name_len);
        if (len == 0)
        {
            rbc_glob_error(results, RBC_GLOB_ERR_PATH_TOO_LONG);
            return; // Skip silently (non-fatal)
        }
    }
    else
    {
        // Use path only
        if (path_len >= sizeof(pathbuf))
        {
            rbc_glob_error(results, RBC_GLOB_ERR_PATH_TOO_LONG);
            return; // Skip silently (non-fatal)
        }
        memcpy(pathbuf, path, path_len);
        pathbuf[path_len] = '\0';
        len = path_len;
    }

    if (trailing_slash)
    {
        len = rbc_path_append_slash(pathbuf, sizeof(pathbuf), len);
        if (len == 0)
        {
            rbc_glob_error(results, RBC_GLOB_ERR_PATH_TOO_LONG);
            return; // Skip silently (non-fatal)
        }
    }

    // Strip baselen and skip leading slashes
    const char *result = pathbuf;
    if (baselen > 0)
    {
        result += baselen;
        while (*result == '/')
            result++;
    }

    if (*result == '\0')
        return; // Skip empty results

    rbc_results_add(results, result, strlen(result));
}

// Forward declaration
static void rbc_glob_dispatch(const char *path, size_t path_len, size_t baselen, const char *pattern, unsigned flags, rbc_results_t *results);

/// @brief Emit or descend based on segment state
/// @param[in] path Current directory path
/// @param[in] path_len Path length
/// @param[in] name Entry name
/// @param[in] is_dir Whether entry is a directory
/// @param[in] baselen Length to strip from output
/// @param[in] seg Segment to check (is_last, has_trailing_slash, next)
/// @param[in] flags Match flags
/// @param[in,out] results Results buffer
/// @note Errors are recorded in results->error
static void rbc_glob_emit_or_descend(
    const char *path,
    size_t path_len,
    const char *name,
    bool is_dir,
    size_t baselen,
    const rbc_segment_t *seg,
    unsigned flags,
    rbc_results_t *results)
{
    char pathbuf[RBC_GLOB_MAX_PATH];

    if (seg->is_last)
    {
        if (seg->has_trailing_slash && !is_dir)
            return; // Skip non-directories when trailing slash is required
        rbc_glob_emit(path, path_len, name, seg->has_trailing_slash, baselen, results);
    }
    else
    {
        if (!is_dir)
            return; // Can't descend into non-directories
        size_t new_len = rbc_path_join(pathbuf, sizeof(pathbuf), path, path_len, name, strlen(name));
        if (new_len > 0)
            rbc_glob_dispatch(pathbuf, new_len, baselen, seg->next, flags, results);
    }
}

/// @brief Collapse consecutive **/ segments
/// @param[in,out] seg First segment (updated to collapsed version)
/// @param[out] next_seg Segment after collapsed **/ chain (if any)
/// @param[in] flags Matching flags
/// @note Only call when seg->type == SEG_RECURSIVE
static void rbc_collapse_recursive(rbc_segment_t *seg, rbc_segment_t *next_seg, unsigned flags)
{
    rbc_segment_t collapsed = *seg;
    rbc_segment_t after = {0};

    while (!collapsed.is_last)
    {
        const char *next_pat = collapsed.next;
        if (!rbc_segment_next(&next_pat, flags, &after))
            break;
        if (after.type != SEG_RECURSIVE)
            break;
        collapsed = after;
    }

    *seg = collapsed;
    if (!collapsed.is_last)
        *next_seg = after;
}

/// @brief Scan directory for ** pattern (handles 0-directory and N-directory match)
/// @param[in] path Directory path
/// @param[in] path_len Path length
/// @param[in] baselen Length to strip from output
/// @param[in] seg Current ** segment (collapsed)
/// @param[in] next_seg Next segment after ** (valid when !seg->is_last)
/// @param[in] flags Match flags
/// @param[in] is_first_call True if this is the initial call (not self-recurse)
/// @param[in,out] results Results buffer
static void rbc_glob_scan_recursive(
    const char *path,
    size_t path_len,
    size_t baselen,
    const rbc_segment_t *seg,
    const rbc_segment_t *next_seg,
    unsigned flags,
    bool is_first_call,
    rbc_results_t *results)
{
    // Zero-directory match: **/ at end matches current directory itself
    // Only emit on first call to avoid duplicates from self_recurse
    if (is_first_call && seg->is_last && path_len > 0)
    {
        rbc_glob_emit(path, path_len, NULL, true, baselen, results);
    }

    rbc_dir_t *dirp = rbc_opendir(path_len > 0 ? path : ".");
    if (!dirp)
        return;

    rbc_dirent_t entry;
    char pathbuf[RBC_GLOB_MAX_PATH];

    while (rbc_readdir(dirp, &entry))
    {
        if (results->error)
            break;

        if (is_first_call && (entry.name[0] == '.' && entry.name[1] == '\0') && !(flags & RBC_FNM_DOTMATCH))
            continue; // Skip "." on first call to avoid duplicate of current directory

        const char *name = entry.name;
        bool is_dir = entry.is_dir;
        bool can_descend = rbc_recursive_can_descend_into_dotfile(name, flags);

        // Decision 1: **/ at end - emit directory entries
        if (seg->is_last && is_dir && can_descend)
            rbc_glob_emit(path, path_len, name, true, baselen, results);

        // Decision 2: Zero-directory match with next segment
        if (!seg->is_last && !results->error)
        {
            bool can_match = rbc_wildcard_can_match_dotfile(name, flags, next_seg->starts_with_dot);
            if (can_match && rbc_segment_match(next_seg, name, flags))
            {
                unsigned descend_flags = flags | RBC_GLOB_HAS_WILDCARD_ANCESTOR;
                rbc_glob_emit_or_descend(path, path_len, name, is_dir, baselen, next_seg, descend_flags, results);
            }
        }

        // Decision 3: Self-recurse into subdirectories for ** continuation
        if (is_dir && can_descend)
        {
            size_t new_len = rbc_path_join(pathbuf, sizeof(pathbuf), path, path_len, name, strlen(name));
            if (new_len > 0)
            {
                unsigned recurse_flags = flags | RBC_GLOB_HAS_WILDCARD_ANCESTOR;
                rbc_glob_scan_recursive(pathbuf, new_len, baselen, seg, next_seg, recurse_flags, false, results);
            }
        }
    }

    rbc_closedir(dirp);
}

/// @brief Scan directory for non-RECURSIVE patterns
/// @param[in] path Directory path
/// @param[in] path_len Path length
/// @param[in] baselen Length to strip from output
/// @param[in] seg Current segment (must NOT be SEG_RECURSIVE)
/// @param[in] flags Match flags
/// @param[in,out] results Results buffer
static void rbc_glob_scan(const char *path, size_t path_len, size_t baselen, const rbc_segment_t *seg, unsigned flags, rbc_results_t *results)
{
    rbc_dir_t *dirp = rbc_opendir(path_len > 0 ? path : ".");
    if (!dirp)
        return;

    rbc_dirent_t entry;

    while (rbc_readdir(dirp, &entry))
    {
        if (results->error)
            break;

        const char *name = entry.name;
        bool is_dir = entry.is_dir;

        // Wildcard segments: check dotfile visibility
        if (seg->type == SEG_WILDCARD)
        {
            if (!rbc_wildcard_can_match_dotfile(name, flags, seg->starts_with_dot))
                continue;
        }

        // Match segment against entry name
        if (!rbc_segment_match(seg, name, flags))
            continue;

        // Emit or descend
        unsigned descend_flags = flags;
        if (seg->type == SEG_WILDCARD)
            descend_flags |= RBC_GLOB_HAS_WILDCARD_ANCESTOR;
        rbc_glob_emit_or_descend(path, path_len, name, is_dir, baselen, seg, descend_flags, results);
    }

    rbc_closedir(dirp);
}

/// @brief Parse pattern and dispatch to appropriate scan function
/// @param[in] path Current directory path
/// @param[in] path_len Path length
/// @param[in] baselen Length to strip from output path
/// @param[in] pattern Pattern string
/// @param[in] flags Match flags
/// @param[in,out] results Results buffer
static void rbc_glob_dispatch(const char *path, size_t path_len, size_t baselen, const char *pattern, unsigned flags, rbc_results_t *results)
{
    // Parse first segment
    rbc_segment_t seg;
    const char *pat_ptr = pattern;
    if (!rbc_segment_next(&pat_ptr, flags, &seg))
        return; // Empty pattern

    if (seg.type == SEG_RECURSIVE)
    {
        // Collapse consecutive **/ segments and parse next segment
        rbc_segment_t next_seg = {0};
        rbc_collapse_recursive(&seg, &next_seg, flags);

        // Dispatch to recursive scan
        rbc_glob_scan_recursive(path, path_len, baselen, &seg, &next_seg, flags, true, results);
    }
    else
    {
        // Dispatch to normal scan
        rbc_glob_scan(path, path_len, baselen, &seg, flags, results);
    }
}

/**
 * @brief Main glob walker entry point
 */
static void rbc_glob_walk(const char *base, size_t baselen, const char *pattern, unsigned flags, rbc_results_t *results)
{
    char root_buf[4];
    rbc_path_root_t root_info;

    if (rbc_parse_absolute_root(pattern, &root_info, root_buf))
    {
        // Absolute path
        if (*root_info.remainder == '\0')
        {
            // Pattern is just root ("/" or "C:/") - return root itself
            rbc_results_add(results, root_info.root, root_info.root_len);
            return;
        }
        rbc_glob_dispatch(root_info.root, root_info.root_len, 0, root_info.remainder, flags, results);
    }
    else
    {
        // Relative path
        rbc_glob_dispatch(base, baselen, baselen, pattern, flags, results);
    }
}

// ============================================================================
// Brace Expansion (Preprocessor)
// ============================================================================

#define BRACE_MAX_EXPANSIONS 256
#define BRACE_MAX_DEPTH 8

typedef struct
{
    char **patterns;
    size_t count;
    size_t capacity;
} rbc_brace_result_t;

static bool rbc_brace_add(rbc_brace_result_t *r, const char *pattern, size_t len)
{
    if (r->count >= r->capacity)
    {
        size_t new_cap = r->capacity * 2;
        if (new_cap > BRACE_MAX_EXPANSIONS)
            new_cap = BRACE_MAX_EXPANSIONS;
        if (r->count >= new_cap)
            return false; // Limit reached
        char **new_patterns = realloc(r->patterns, new_cap * sizeof(char *));
        if (!new_patterns)
            return false;
        r->patterns = new_patterns;
        r->capacity = new_cap;
    }

    char *copy = malloc(len + 1);
    if (!copy)
        return false;
    memcpy(copy, pattern, len);
    copy[len] = '\0';
    r->patterns[r->count++] = copy;
    return true;
}

static void rbc_brace_expand_impl(const char *pattern, size_t len,
                                  rbc_brace_result_t *result, int depth);

static void rbc_brace_expand_option(
    const char *prefix,
    size_t prefix_len,
    const char *option,
    size_t option_len,
    const char *suffix,
    size_t suffix_len,
    rbc_brace_result_t *result,
    int depth)
{
    if (depth > BRACE_MAX_DEPTH)
        return;

    char buf[RBC_GLOB_MAX_PATH];
    size_t total = prefix_len + option_len + suffix_len;
    if (total >= sizeof(buf))
        return;

    memcpy(buf, prefix, prefix_len);
    memcpy(buf + prefix_len, option, option_len);
    memcpy(buf + prefix_len + option_len, suffix, suffix_len);

    rbc_brace_expand_impl(buf, total, result, depth + 1);
}

static void rbc_brace_expand_impl(
    const char *pattern,
    size_t len,
    rbc_brace_result_t *result,
    int depth)
{
    if (result->count >= BRACE_MAX_EXPANSIONS)
        return;

    char buf[RBC_GLOB_MAX_PATH];
    size_t buf_pos = 0;
    const char *p = pattern;
    const char *end = pattern + len;

    while (p < end)
    {
        if (*p == '\\' && p + 1 < end)
        {
            buf[buf_pos++] = *p++;
            buf[buf_pos++] = *p++;
            continue;
        }

        if (*p == '{')
        {
            // Find matching close brace
            const char *opt_starts[256];
            size_t opt_count = 0;
            const char *scan = p + 1;
            const char *close = NULL;
            int brace_depth = 0;

            opt_starts[opt_count++] = scan;

            while (scan < end)
            {
                if (*scan == '\\' && scan + 1 < end)
                {
                    scan += 2;
                    continue;
                }
                if (*scan == '{')
                {
                    brace_depth++;
                }
                else if (*scan == '}')
                {
                    if (brace_depth == 0)
                    {
                        close = scan;
                        break;
                    }
                    brace_depth--;
                }
                else if (brace_depth == 0 && *scan == ',' && opt_count < 256)
                {
                    opt_starts[opt_count++] = scan + 1;
                }
                scan++;
            }

            if (!close)
            {
                buf[buf_pos++] = *p++;
                continue;
            }

            // Expand all options
            size_t suffix_len = end - (close + 1);
            for (size_t i = 0; i < opt_count; i++)
            {
                const char *opt_start = opt_starts[i];
                const char *opt_end = (i + 1 < opt_count) ? opt_starts[i + 1] - 1 : close;
                size_t opt_len = opt_end - opt_start;

                rbc_brace_expand_option(
                    buf,
                    buf_pos,
                    opt_start,
                    opt_len,
                    close + 1,
                    suffix_len,
                    result,
                    depth);
            }
            return;
        }

        buf[buf_pos++] = *p++;
    }

    // No brace found - add as final pattern
    buf[buf_pos] = '\0';
    rbc_brace_add(result, buf, buf_pos);
}

static rbc_brace_result_t *rbc_brace_expand(const char *pattern)
{
    rbc_brace_result_t *result = malloc(sizeof(rbc_brace_result_t));
    if (!result)
        return NULL;

    result->patterns = malloc(8 * sizeof(char *));
    result->count = 0;
    result->capacity = 8;

    if (!result->patterns)
    {
        free(result);
        return NULL;
    }

    rbc_brace_expand_impl(pattern, strlen(pattern), result, 0);
    return result;
}

static void rbc_brace_free(rbc_brace_result_t *result)
{
    if (!result)
        return;
    for (size_t i = 0; i < result->count; i++)
    {
        free(result->patterns[i]);
    }
    free(result->patterns);
    free(result);
}

// ============================================================================
// Result Conversion and Sorting
// ============================================================================

static int rbc_strcmp_wrapper(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/**
 * @brief Sort a range of results within the arena
 *
 * Ruby's Dir.glob with sort:true sorts each brace-expanded pattern's
 * results individually, then concatenates them in brace expansion order.
 * This function sorts results from index `start` to `end` (exclusive).
 *
 * @param r Results structure
 * @param start Start index (inclusive)
 * @param end End index (exclusive)
 */
static void rbc_results_sort_range(rbc_results_t *r, size_t start, size_t end)
{
    if (end <= start + 1)
        return; // 0 or 1 element, nothing to sort

    size_t n = end - start;

    // Create temporary array of string pointers for sorting
    const char **ptrs = malloc(n * sizeof(const char *));
    if (!ptrs)
        return;

    // Build pointer array from offsets
    for (size_t i = 0; i < n; i++)
    {
        ptrs[i] = r->arena + r->offsets[start + i];
    }

    // Sort the pointer array
    qsort(ptrs, n, sizeof(const char *), rbc_strcmp_wrapper);

    // Rebuild offsets array from sorted pointers
    for (size_t i = 0; i < n; i++)
    {
        r->offsets[start + i] = (size_t)(ptrs[i] - r->arena);
    }

    free(ptrs);
}

/**
 * @brief Convert arena results to output format
 */
static bool rbc_results_to_output(
    rbc_results_t *r,
    char ***out,
    size_t *count,
    size_t **lengths)
{
    *count = r->count;

    if (r->count == 0)
    {
        *out = NULL;
        if (lengths)
            *lengths = NULL;
        return true;
    }

    // Allocate output arrays
    char **items = malloc(r->count * sizeof(char *));
    size_t *lens = lengths ? malloc(r->count * sizeof(size_t)) : NULL;

    if (!items || (lengths && !lens))
    {
        free(items);
        free(lens);
        return false;
    }

    // Copy paths from arena to individual allocations
    for (size_t i = 0; i < r->count; i++)
    {
        const char *src = r->arena + r->offsets[i];
        size_t len = strlen(src);
        items[i] = malloc(len + 1);
        if (!items[i])
        {
            for (size_t j = 0; j < i; j++)
                free(items[j]);
            free(items);
            free(lens);
            return false;
        }
        memcpy(items[i], src, len + 1);
        if (lens)
            lens[i] = len;
    }

    *out = items;
    if (lengths)
        *lengths = lens;
    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool rbc_glob(
    const char **patterns,
    size_t npatterns,
    unsigned flags,
    const char *base,
    bool sort,
    char ***out,
    size_t *count,
    size_t **lengths)
{
    if (!patterns || npatterns == 0 || !out || !count)
        return false;

    rbc_results_t results;
    if (!rbc_results_init(&results))
        return false;

    // Calculate base length
    size_t baselen = 0;
    const char *actual_base = "";
    char base_norm_buf[RBC_GLOB_MAX_PATH];

    if (base && base[0] != '\0')
    {
        // Normalize path separators (Windows: \ -> /)
        actual_base = rbc_normalize_path(base, base_norm_buf, sizeof(base_norm_buf));
        baselen = strlen(actual_base);
        // Strip trailing slashes
        while (baselen > 0 && actual_base[baselen - 1] == '/')
            baselen--;
    }

    // Process each pattern
    for (size_t i = 0; i < npatterns; i++)
    {
        // Normalize path separators (Windows: \ -> /)
        char norm_buf[RBC_GLOB_MAX_PATH];
        const char *pattern = rbc_normalize_path(patterns[i], norm_buf, sizeof(norm_buf));

        // Expand braces
        rbc_brace_result_t *expanded = rbc_brace_expand(pattern);
        if (!expanded)
            continue;

        // Process each expanded pattern
        for (size_t j = 0; j < expanded->count; j++)
        {
            size_t count_before = results.count;

            rbc_glob_walk(
                actual_base,
                baselen,
                expanded->patterns[j],
                flags,
                &results);

            // Sort results for this brace-expanded pattern
            // Ruby sorts each pattern's results individually, then concatenates
            if (sort && results.count > count_before)
            {
                rbc_results_sort_range(&results, count_before, results.count);
            }

            // Stop processing if memory allocation failed
            if (results.error)
                break;
        }

        rbc_brace_free(expanded);

        // Stop processing patterns if memory allocation failed
        if (results.error)
            break;
    }

    // Convert to output format (already sorted per-pattern)
    bool ok = rbc_results_to_output(&results, out, count, lengths);

    // Check for errors during processing
    if (results.error)
        ok = false;

    rbc_results_free(&results);

    return ok;
}

void rbc_globfree(char **list, size_t count, size_t *lengths)
{
    if (!list)
        return;
    for (size_t i = 0; i < count; i++)
        free(list[i]);
    free(list);
    free(lengths);
}
