#include "platform.h"
#include "rbc/rbc.h"
#include "../utils/utils.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define RBC_GLOB_MAX_PATH 4096     // Max path length for glob operations
#define RBC_NAME_BUF_SIZE 256      // Buffer for entry names (should be >= NAME_MAX)
#define RBC_RESULTS_INIT_COUNT 256 // Initial path count

/// @defgroup Internal Glob Flags (high bits, not part of public FNM_* flags)
/// @{
#define RBC_GLOB_HAS_WILDCARD_ANCESTOR 0x10000000 // Internal: traversed through wildcard
/// @}

/// @brief Error callback function type
typedef bool (*rbc_glob_emitfunc_t)(const char *path, size_t path_len, void *user_data);

/// @defgroup Glob Context
/// @{

/// @brief Glob Context Structure
typedef struct rbc_glob_ctx_s
{
    rbc_glob_status_t status;
    rbc_dirent_t shared_dirent;
    rbc_glob_errfunc_t errfunc;
    void *errfunc_data;
    // streaming mode
    rbc_glob_emitfunc_t emitfunc;
    void *emitfunc_data;
    // buffering mode
    rbc_glob_result_t *result;
    size_t result_capacity;
    // shared readdir buffer (avoids ~4KB per recursive frame)
} rbc_glob_ctx_t;

/// @brief Initialize emit context for streaming mode
/// @param[out] ctx
/// @param[in] emitfunc
/// @param[in] emitfunc_data
/// @param[in] errfunc
/// @param[in] errfunc_data
static bool rbc_glob_ctx_init_streaming(rbc_glob_ctx_t *ctx, rbc_glob_emitfunc_t emitfunc, void *emitfunc_data, rbc_glob_errfunc_t errfunc, void *errfunc_data)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->status = RBC_GLOB_SUCCESS;
    ctx->errfunc = errfunc;
    ctx->errfunc_data = errfunc_data;
    ctx->emitfunc = emitfunc;
    ctx->emitfunc_data = emitfunc_data;
    return true;
}

/// @brief Initialize emit context for buffering mode
/// @param[out] ctx
/// @param[in] result
/// @param[in] errfunc
/// @param[in] errfunc_data
/// @return
static bool rbc_glob_ctx_init_buffering(rbc_glob_ctx_t *ctx, rbc_glob_result_t *result, rbc_glob_errfunc_t errfunc, void *errfunc_data)
{
    // result setup
    result->paths = malloc(RBC_RESULTS_INIT_COUNT * sizeof(char *));
    if (!result->paths)
        return false;
    result->count = 0;
    // ctx setup
    memset(ctx, 0, sizeof(*ctx));
    ctx->status = RBC_GLOB_SUCCESS;
    ctx->errfunc = errfunc;
    ctx->errfunc_data = errfunc_data;
    ctx->result = result;
    ctx->result_capacity = RBC_RESULTS_INIT_COUNT;
    return true;
}

/// @}

/// @brief Check if emit context should exit
/// @param[in] ctx
/// @return
static inline bool rbc_glob_should_exit(const rbc_glob_ctx_t *ctx)
{
    return ctx->status != RBC_GLOB_SUCCESS;
}

/// @brief Report an error and determine whether to continue
/// @param[in,out] ctx Emit context
/// @param[in] errnum System errno value
/// @param[in] path Path where error occurred (may be NULL)
/// @return true if should continue, false if should abort
static bool rbc_glob_report_error(rbc_glob_ctx_t *ctx, int errnum, const char *path)
{
    // Non-fatal: call errfunc if available
    if (ctx->errfunc)
    {
        if (!ctx->errfunc(path, errnum, ctx->errfunc_data))
        {
            ctx->status = RBC_GLOB_ABORTED;
            return false;
        }
    }
    return true; // Continue
}

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
    if (base_len + component_len + 2 > buf_size)
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
    if (path_len + 2 > buf_size)
        return 0;
    buf[path_len++] = '/';
    buf[path_len] = '\0';
    return path_len;
}

/// @brief Append a path component in-place (no base copy, avoids memcpy overlap)
/// @param[in,out] buf Buffer containing existing path (modified in place)
/// @param[in] buf_size Size of buffer
/// @param[in] path_len Current path length in buffer
/// @param[in] component Component to append
/// @param[in] component_len Length of component
/// @return New path length, or 0 on overflow
static size_t rbc_path_append_component(
    char *buf,
    size_t buf_size,
    size_t path_len,
    const char *component,
    size_t component_len)
{
    size_t need_sep = path_len > 0 && buf[path_len - 1] != '/' ? 1 : 0;
    if (path_len + need_sep + component_len + 1 > buf_size)
        return 0;
    if (need_sep)
        buf[path_len++] = '/';
    memcpy((buf + path_len), component, component_len);
    path_len += component_len;
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
        return (explicit_dot || (flags & RBC_FNM_DOTMATCH)) && !(flags & RBC_GLOB_HAS_WILDCARD_ANCESTOR);
    // `..`
    if (name[1] == '.' && name[2] == '\0')
        return false;
    // `.hidden`
    return explicit_dot || (flags & RBC_FNM_DOTMATCH);
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

/// @defgroup Segment Character Flags
/// @{
#define RBC_SEG_CONTAINS_STAR 0x01     // Contains '*'
#define RBC_SEG_CONTAINS_QUESTION 0x02 // Contains '?'
#define RBC_SEG_CONTAINS_BRACKET 0x04  // Contains '[...]'
#define RBC_SEG_CONTAINS_ESCAPE 0x08   // Contains escape sequences
#define RBC_SEG_CONTAINS_REGULAR 0x10  // Contains regular characters
/// @}

/// @brief Segment Types
typedef enum rbc_segment_type_e
{
    SEG_LITERAL,   // `abc`, `.`, `..`
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
    seg->starts_with_dot = *p == '.';
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

    if (seg->len == 2 && char_flags == RBC_SEG_CONTAINS_STAR && seg->has_trailing_slash)
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
    case SEG_LITERAL:
        if (flags & RBC_FNM_CASEFOLD)
            return rbc_fnmatch_len(seg->start, seg->len, string, flags);
        return strlen(string) == seg->len && strncmp(seg->start, string, seg->len) == 0;
    case SEG_WILDCARD:
        return rbc_fnmatch_len(seg->start, seg->len, string, flags);
    case SEG_RECURSIVE:
        return false; // RECURSIVE should not be matched directly
    }
    return false;
}

/// @}

/// @brief Add a path to the result buffer
/// @param[in,out] ctx Emit context (must be in accumulate mode)
/// @param[in] path Path string
/// @param[in] path_len Length of path
/// @return true on success, false on failure (sets nomem error)
static bool rbc_glob_emit_buffering(rbc_glob_ctx_t *ctx, const char *path, size_t path_len)
{
    rbc_glob_result_t *r = ctx->result;

    if (!r)
        return false;

    // Grow paths array if needed
    if (ctx->result_capacity <= r->count)
    {
        size_t new_cap = ctx->result_capacity * 2;
        char **new_paths = realloc(r->paths, new_cap * sizeof(char *));
        if (!new_paths)
        {
            ctx->status = RBC_GLOB_NOMEM;
            return false;
        }
        r->paths = new_paths;
        ctx->result_capacity = new_cap;
    }

    // Allocate and copy path
    char *copy = malloc(path_len + 1);
    if (!copy)
    {
        ctx->status = RBC_GLOB_NOMEM;
        return false;
    }
    memcpy(copy, path, path_len);
    copy[path_len] = '\0';
    r->paths[r->count++] = copy;

    return true;
}

/// @brief Emit a path via callback or to results buffer
/// @param[in] path Base path
/// @param[in] path_len Length of base path
/// @param[in] name Entry name to append (NULL to emit path only)
/// @param[in] trailing_slash Whether to append trailing slash
/// @param[in] baselen Base length to strip from output path
/// @param[in,out] ctx Emit context
/// @note On memory error, sets ctx->status. Path too long is silently skipped.
static void rbc_glob_emit(const char *path, size_t path_len, const char *name, bool trailing_slash, size_t baselen, rbc_glob_ctx_t *ctx)
{
    if (rbc_glob_should_exit(ctx))
        return;

    char pathbuf[RBC_GLOB_MAX_PATH];
    size_t len;

    if (name)
    {
        // Join path + name
        size_t name_len = strlen(name);
        len = rbc_path_join(pathbuf, sizeof(pathbuf), path, path_len, name, name_len);
        if (len == 0)
            return; // Skip silently (path too long, non-fatal)
    }
    else
    {
        // Use path only
        if (path_len >= sizeof(pathbuf))
            return; // Skip silently (path too long, non-fatal)
        memcpy(pathbuf, path, path_len);
        pathbuf[path_len] = '\0';
        len = path_len;
    }

    if (trailing_slash)
    {
        len = rbc_path_append_slash(pathbuf, sizeof(pathbuf), len);
        if (len == 0)
            return; // Skip silently (path too long, non-fatal)
    }

    // Strip baselen and skip leading slashes
    const char *result = pathbuf;
    size_t result_len = len;
    if (baselen > 0)
    {
        result += baselen;
        result_len -= baselen;
        while (*result == '/' && result_len > 0)
        {
            result++;
            result_len--;
        }
    }

    if (result_len == 0)
        return; // Skip empty results

    // Emit via callback or accumulate
    if (ctx->emitfunc)
    {
        if (!ctx->emitfunc(result, result_len, ctx->emitfunc_data))
        {
            ctx->status = RBC_GLOB_STOPPED;
        }
    }
    else if (ctx->result)
    {
        rbc_glob_emit_buffering(ctx, result, result_len);
    }
}

// Forward declarations
static void rbc_glob_dispatch(const char *path, size_t path_len, size_t baselen, const char *pattern, unsigned flags, rbc_glob_ctx_t *ctx);

/// @brief Emit or descend based on segment state
/// @param[in] path Current directory path
/// @param[in] path_len Path length
/// @param[in] name Entry name
/// @param[in] is_dir Whether entry is a directory
/// @param[in] baselen Length to strip from output
/// @param[in] seg Segment to check (is_last, has_trailing_slash, next)
/// @param[in] flags Match flags
/// @param[in,out] ctx Emit context
static void rbc_glob_emit_or_descend(
    const char *path,
    size_t path_len,
    const char *name,
    bool is_dir,
    size_t baselen,
    const rbc_segment_t *seg,
    unsigned flags,
    rbc_glob_ctx_t *ctx)
{
    char pathbuf[RBC_GLOB_MAX_PATH];

    if (seg->is_last)
    {
        if (seg->has_trailing_slash && !is_dir)
            return; // Skip non-directories when trailing slash is required
        rbc_glob_emit(path, path_len, name, seg->has_trailing_slash, baselen, ctx);
    }
    else
    {
        if (!is_dir)
            return; // Can't descend into non-directories
        size_t new_len = rbc_path_join(pathbuf, sizeof(pathbuf), path, path_len, name, strlen(name));
        if (new_len > 0)
            rbc_glob_dispatch(pathbuf, new_len, baselen, seg->next, flags, ctx);
        else
            rbc_glob_report_error(ctx, ENAMETOOLONG, path);
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
/// @param[in,out] path Mutable shared path buffer (RBC_GLOB_MAX_PATH bytes, modified in-place during recursion)
/// @param[in] path_len Current path length in buffer
/// @param[in] baselen Length to strip from output
/// @param[in] seg Current ** segment (collapsed)
/// @param[in] next_seg Next segment after ** (valid when !seg->is_last)
/// @param[in] flags Match flags
/// @param[in] is_first_call True if this is the initial call (not self-recurse)
/// @param[in,out] ctx Emit context
static void rbc_glob_scan_recursive(
    char *path,
    size_t path_len,
    size_t baselen,
    const rbc_segment_t *seg,
    const rbc_segment_t *next_seg,
    unsigned flags,
    bool is_first_call,
    rbc_glob_ctx_t *ctx)
{
    // Zero-directory match: **/ at end matches current directory itself
    // Only emit on first call to avoid duplicates from self_recurse
    if (is_first_call && seg->is_last && path_len > 0)
        rbc_glob_emit(path, path_len, NULL, true, baselen, ctx);

    if (rbc_glob_should_exit(ctx))
        return;

    const char *dir_path = path_len > 0 ? path : ".";
    rbc_dir_t *dirp = rbc_opendir(dir_path);
    if (!dirp)
    {
        // ENOENT/ENOTDIR: path doesn't exist, silently skip (FreeBSD convention)
        if (errno != ENOENT && errno != ENOTDIR)
            rbc_glob_report_error(ctx, errno, dir_path);
        return;
    }

    while (rbc_readdir(dirp, &ctx->shared_dirent))
    {
        if (rbc_glob_should_exit(ctx))
            break;

        if (is_first_call && ctx->shared_dirent.name[0] == '.' && ctx->shared_dirent.name[1] == '\0' && !(flags & RBC_FNM_DOTMATCH))
            continue; // Skip "." on first call to avoid duplicate of current directory

        // Copy entry fields locally: shared_dirent is overwritten by descendant readdir calls
        char name_buf[RBC_NAME_BUF_SIZE];
        size_t name_len = strlen(ctx->shared_dirent.name);
        if (name_len >= sizeof(name_buf))
            continue; // Exceeds NAME_MAX, skip
        memcpy(name_buf, ctx->shared_dirent.name, name_len + 1);
        const char *name = name_buf;
        bool is_dir = ctx->shared_dirent.is_dir;
        bool is_link = ctx->shared_dirent.is_link;
        bool can_descend = rbc_recursive_can_descend_into_dotfile(name, flags);

        // Decision 1: **/ at end - emit directory entries
        // Skip symlinks (Ruby behavior: **/ does not emit symlinks as directories)
        if (seg->is_last && is_dir && !is_link && can_descend)
            rbc_glob_emit(path, path_len, name, true, baselen, ctx);

        // Decision 2: Zero-directory match with next segment
        if (!seg->is_last && !rbc_glob_should_exit(ctx))
        {
            bool can_match = rbc_wildcard_can_match_dotfile(name, flags, next_seg->starts_with_dot);
            if (can_match && rbc_segment_match(next_seg, name, flags))
            {
                unsigned descend_flags = flags | RBC_GLOB_HAS_WILDCARD_ANCESTOR;
                rbc_glob_emit_or_descend(path, path_len, name, is_dir, baselen, next_seg, descend_flags, ctx);
            }
        }

        // Decision 3: Self-recurse into subdirectories for ** continuation
        // Skip symlinks to avoid infinite loops (Ruby behavior: ** does not follow symlinks)
        if (is_dir && !is_link && can_descend && !rbc_glob_should_exit(ctx))
        {
            size_t new_len = rbc_path_append_component(path, RBC_GLOB_MAX_PATH, path_len, name, name_len);
            if (new_len > 0)
            {
                unsigned recurse_flags = flags | RBC_GLOB_HAS_WILDCARD_ANCESTOR;
                rbc_glob_scan_recursive(path, new_len, baselen, seg, next_seg, recurse_flags, false, ctx);
                path[path_len] = '\0'; // Restore shared path buffer
            }
            else
            {
                rbc_glob_report_error(ctx, ENAMETOOLONG, path);
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
/// @param[in,out] ctx Emit context
static void rbc_glob_scan(const char *path, size_t path_len, size_t baselen, const rbc_segment_t *seg, unsigned flags, rbc_glob_ctx_t *ctx)
{
    const char *dir_path = path_len > 0 ? path : ".";
    rbc_dir_t *dirp = rbc_opendir(dir_path);
    if (!dirp)
    {
        // ENOENT/ENOTDIR: path doesn't exist, silently skip (FreeBSD convention)
        if (errno != ENOENT && errno != ENOTDIR)
            rbc_glob_report_error(ctx, errno, dir_path);
        return;
    }

    while (rbc_readdir(dirp, &ctx->shared_dirent))
    {
        if (rbc_glob_should_exit(ctx))
            break;

        // Copy entry fields locally: shared_dirent may be overwritten by descendant calls
        char name_buf[RBC_NAME_BUF_SIZE];
        size_t name_len = strlen(ctx->shared_dirent.name);
        if (name_len >= sizeof(name_buf))
            continue; // Exceeds NAME_MAX, skip
        memcpy(name_buf, ctx->shared_dirent.name, name_len + 1);
        const char *name = name_buf;
        bool is_dir = ctx->shared_dirent.is_dir;

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
        rbc_glob_emit_or_descend(path, path_len, name, is_dir, baselen, seg, descend_flags, ctx);
    }

    rbc_closedir(dirp);
}

/// @brief Parse pattern and dispatch to appropriate scan function
/// @param[in] path Current directory path
/// @param[in] path_len Path length
/// @param[in] baselen Length to strip from output path
/// @param[in] pattern Pattern string
/// @param[in] flags Match flags
/// @param[in,out] ctx Emit context
static void rbc_glob_dispatch(const char *path, size_t path_len, size_t baselen, const char *pattern, unsigned flags, rbc_glob_ctx_t *ctx)
{
    if (rbc_glob_should_exit(ctx))
        return;

    // Parse first segment (single parse, reused by all code paths)
    const char *pat_ptr = pattern;
    rbc_segment_t seg;
    if (!rbc_segment_next(&pat_ptr, flags, &seg))
        return; // Empty pattern

    // Optimization: Skip consecutive LITERAL segments (including "." and "..")
    // using direct path construction + stat(), avoiding readdir entirely.
    //   - Intermediate chains: path join and recurse (no readdir for parent dirs)
    //   - Terminal chains: stat() O(1) instead of readdir O(n)
    // On POSIX with CASEFOLD, skip optimization (need readdir for case-insensitive match)
    if (seg.type == SEG_LITERAL
#ifndef _WIN32
        && !(flags & RBC_FNM_CASEFOLD)
#endif
    )
    {
        // Consume consecutive LITERAL segments
        const char *literal_end = seg.start + seg.len;
        bool last_trailing_slash = seg.has_trailing_slash;
        bool chain_is_last = seg.is_last;

        while (!seg.is_last)
        {
            const char *peek = pat_ptr;
            if (!rbc_segment_next(&peek, flags, &seg) || seg.type != SEG_LITERAL)
                break;
            pat_ptr = peek;
            literal_end = seg.start + seg.len;
            last_trailing_slash = seg.has_trailing_slash;
            chain_is_last = seg.is_last;
        }

        // Build full path: path + "/" + pattern[0:literal_end]
        char full_path[RBC_GLOB_MAX_PATH];
        size_t full_len;
        size_t literal_len = literal_end - pattern;
        bool path_ok;

        if (path_len > 0)
        {
            full_len = rbc_path_join(full_path, sizeof(full_path), path, path_len, pattern, literal_len);
            path_ok = full_len > 0;
        }
        else
        {
            path_ok = literal_len < sizeof(full_path);
            if (path_ok)
            {
                memcpy(full_path, pattern, literal_len);
                full_path[literal_len] = '\0';
                full_len = literal_len;
            }
        }

        if (path_ok)
        {
            if (chain_is_last)
            {
                // Terminal LITERAL chain: use stat() for O(1) existence check
                int errnum = 0;
                rbc_stat_result_t st = rbc_stat_type(full_path, &errnum);

                if (st == RBC_STAT_NOTFOUND)
                    return; // Does not exist
                if (st == RBC_STAT_ERROR)
                {
                    rbc_glob_report_error(ctx, errnum, full_path);
                    return;
                }

                // Trailing slash in pattern requires target to be a directory
                if (last_trailing_slash && st != RBC_STAT_DIR)
                    return;

                rbc_glob_emit(full_path, full_len, NULL, last_trailing_slash, baselen, ctx);
                return;
            }

            // Intermediate chain: continue with remaining pattern
            rbc_glob_dispatch(full_path, full_len, baselen, seg.start, flags, ctx);
            return;
        }

        // Path too long for joined chain - re-parse first segment and fall through
        // to single-segment processing (rare case)
        pat_ptr = pattern;
        rbc_segment_next(&pat_ptr, flags, &seg);
    }

    // Dispatch based on first (already parsed) segment type
    if (seg.type == SEG_RECURSIVE)
    {
        // Allocate mutable path buffer shared across recursive descent (one per ** encounter)
        char rec_pathbuf[RBC_GLOB_MAX_PATH];
        if (path_len >= sizeof(rec_pathbuf))
            return;
        memcpy(rec_pathbuf, path, path_len + 1); // Include '\0'
        rbc_segment_t rec_next_seg = {0};
        rbc_collapse_recursive(&seg, &rec_next_seg, flags);
        rbc_glob_scan_recursive(rec_pathbuf, path_len, baselen, &seg, &rec_next_seg, flags, true, ctx);
    }
    else
    {
        rbc_glob_scan(path, path_len, baselen, &seg, flags, ctx);
    }
}

/**
 * @brief Main glob walker entry point
 */
static void rbc_glob_walk(const char *base, size_t baselen, const char *pattern, unsigned flags, rbc_glob_ctx_t *ctx)
{
#ifdef _WIN32
    // Windows filesystem is case-insensitive, always use CASEFOLD
    flags |= RBC_FNM_CASEFOLD;
#endif

    char root_buf[4];
    rbc_path_root_t root_info;

    if (rbc_parse_absolute_root(pattern, &root_info, root_buf))
    {
        // Absolute path
        if (*root_info.remainder == '\0')
        {
            // Pattern is just root ("/" or "C:/") - emit root itself
            if (ctx->emitfunc)
            {
                if (!ctx->emitfunc(root_info.root, root_info.root_len, ctx->emitfunc_data))
                {
                    ctx->status = RBC_GLOB_STOPPED;
                }
            }
            else if (ctx->result)
            {
                rbc_glob_emit_buffering(ctx, root_info.root, root_info.root_len);
            }
            return;
        }
        rbc_glob_dispatch(root_info.root, root_info.root_len, 0, root_info.remainder, flags, ctx);
    }
    else
    {
        // Relative path
        rbc_glob_dispatch(base, baselen, baselen, pattern, flags, ctx);
    }
}

// ============================================================================
// Brace Expansion (Preprocessor)
// ============================================================================

#define BRACE_MAX_EXPANSIONS 256
#define BRACE_MAX_DEPTH 8
#define BRACE_MAX_OPTIONS 64

typedef struct
{
    char **patterns;
    size_t count;
    size_t capacity;
    bool error; // Memory allocation error
} rbc_brace_result_t;

static bool rbc_brace_add(rbc_brace_result_t *r, const char *pattern, size_t len)
{
    if (r->error)
        return false;

    if (r->capacity <= r->count)
    {
        size_t new_cap = r->capacity * 2;
        if (new_cap > BRACE_MAX_EXPANSIONS)
            new_cap = BRACE_MAX_EXPANSIONS;
        if (r->count >= new_cap)
            return true; // Limit reached, silently skip
        char **new_patterns = realloc(r->patterns, new_cap * sizeof(char *));
        if (!new_patterns)
        {
            r->error = true;
            return false;
        }
        r->patterns = new_patterns;
        r->capacity = new_cap;
    }

    char *copy = malloc(len + 1);
    if (!copy)
    {
        r->error = true;
        return false;
    }
    memcpy(copy, pattern, len);
    copy[len] = '\0';
    r->patterns[r->count++] = copy;
    return true;
}

static void rbc_brace_expand_impl(const char *pattern, size_t len, rbc_brace_result_t *result, int depth);

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
    if (result->error || depth > BRACE_MAX_DEPTH)
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
    if (result->error || result->count >= BRACE_MAX_EXPANSIONS)
        return;

    char buf[RBC_GLOB_MAX_PATH];
    size_t buf_pos = 0;
    const char *p = pattern;
    const char *end = pattern + len;

    while (p < end)
    {
        if (buf_pos >= sizeof(buf) - 1)
            return; // Buffer full

        if (*p == '\\' && p + 1 < end)
        {
            if (buf_pos >= sizeof(buf) - 2)
                return;
            buf[buf_pos++] = *p++;
            buf[buf_pos++] = *p++;
            continue;
        }

        if (*p == '{')
        {
            // Find matching close brace and collect options
            const char *opt_starts[BRACE_MAX_OPTIONS];
            size_t opt_count = 0;
            const char *scan = p + 1;
            const char *close = NULL;
            int brace_depth = 0;
            bool has_comma = false;

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
                else if (brace_depth == 0 && *scan == ',')
                {
                    has_comma = true;
                    if (opt_count < BRACE_MAX_OPTIONS)
                        opt_starts[opt_count++] = scan + 1;
                }
                scan++;
            }

            // No closing brace or no comma (single element like {a})
            if (!close || !has_comma)
            {
                buf[buf_pos++] = *p++;
                continue;
            }

            // Expand all options
            size_t suffix_len = end - close + 1;
            for (size_t i = 0; i < opt_count && !result->error; i++)
            {
                const char *opt_start = opt_starts[i];
                const char *opt_end = i + 1 < opt_count ? opt_starts[i + 1] - 1 : close;
                size_t opt_len = opt_end - opt_start;

                rbc_brace_expand_option(
                    buf, buf_pos,
                    opt_start, opt_len,
                    close + 1, suffix_len,
                    result, depth);
            }
            return;
        }

        buf[buf_pos++] = *p++;
    }

    // No brace found - add as final pattern
    buf[buf_pos] = '\0';
    rbc_brace_add(result, buf, buf_pos);
}

/// @brief Check if pattern contains expandable braces
/// @param[in] pattern Pattern string
/// @param[in] len Pattern length
/// @return true if pattern contains '{' followed by ',' and '}', false otherwise
static bool rbc_brace_has_expandable(const char *pattern, size_t len)
{
    const char *p = pattern;
    const char *end = pattern + len;
    int depth = 0;
    bool has_comma_at_depth0 = false;

    while (p < end)
    {
        if (*p == '\\' && p + 1 < end)
        {
            p += 2;
            continue;
        }
        if (*p == '{')
        {
            if (depth == 0)
                has_comma_at_depth0 = false;
            depth++;
        }
        else if (*p == '}')
        {
            if (depth == 1 && has_comma_at_depth0)
                return true;
            if (depth > 0)
                depth--;
        }
        else if (*p == ',' && depth == 1)
        {
            has_comma_at_depth0 = true;
        }
        p++;
    }
    return false;
}

static rbc_brace_result_t *rbc_brace_expand(const char *pattern)
{
    size_t len = strlen(pattern);

    rbc_brace_result_t *result = malloc(sizeof(rbc_brace_result_t));
    if (!result)
        return NULL;

    result->patterns = malloc(8 * sizeof(char *));
    result->count = 0;
    result->capacity = 8;
    result->error = false;

    if (!result->patterns)
    {
        free(result);
        return NULL;
    }

    // Fast path: no expandable braces
    if (!rbc_brace_has_expandable(pattern, len))
    {
        if (!rbc_brace_add(result, pattern, len))
        {
            free(result->patterns);
            free(result);
            return NULL;
        }
        return result;
    }

    rbc_brace_expand_impl(pattern, len, result, 0);

    // On error, clean up and return NULL
    if (result->error)
    {
        for (size_t i = 0; i < result->count; i++)
            free(result->patterns[i]);
        free(result->patterns);
        free(result);
        return NULL;
    }

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

static int rbc_glob_strcmp_wrapper(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/**
 * @brief Sort a range of results
 *
 * Ruby's Dir.glob with sort:true sorts each brace-expanded pattern's
 * results individually, then concatenates them in brace expansion order.
 * This function sorts results from index `start` to `end` (exclusive).
 *
 * @param r Results structure
 * @param start Start index (inclusive)
 * @param end End index (exclusive)
 */
static void rbc_glob_result_sort_range(rbc_glob_result_t *r, size_t start, size_t end)
{
    if (end <= start + 1)
        return; // 0 or 1 element, nothing to sort

    qsort(r->paths + start, end - start, sizeof(char *), rbc_glob_strcmp_wrapper);
}

/// @brief Free result paths on error
static void rbc_glob_result_cleanup(rbc_glob_result_t *r)
{
    if (!r->paths)
        return;
    for (size_t i = 0; i < r->count; i++)
        free(r->paths[i]);
    free(r->paths);
    r->paths = NULL;
    r->count = 0;
}

// ============================================================================
// Public API
// ============================================================================

/// @brief Internal: Process patterns with emit context
/// @param[in] patterns Array of pattern strings
/// @param[in] npatterns Number of patterns
/// @param[in] flags Matching flags
/// @param[in] base Base directory
/// @param[in] sort Whether to sort results
/// @param[in,out] ctx Emit context
/// @return true on success, false on error
static bool rbc_glob_internal(
    const char **patterns,
    size_t npatterns,
    unsigned flags,
    const char *base,
    bool sort,
    rbc_glob_ctx_t *ctx)
{
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

    // For sort:true with accumulate mode, we sort after each pattern
    // Callback mode never sorts (always uses filesystem order for efficiency)

    // Process each pattern
    for (size_t i = 0; i < npatterns; i++)
    {
        if (rbc_glob_should_exit(ctx))
            break;

        // Normalize path separators (Windows: \ -> /)
        char norm_buf[RBC_GLOB_MAX_PATH];
        const char *pattern = rbc_normalize_path(patterns[i], norm_buf, sizeof(norm_buf));

        // Expand braces
        rbc_brace_result_t *expanded = rbc_brace_expand(pattern);
        if (!expanded)
        {
            ctx->status = RBC_GLOB_NOMEM;
            break;
        }

        // Process each expanded pattern
        for (size_t j = 0; j < expanded->count; j++)
        {
            if (rbc_glob_should_exit(ctx))
                break;

            size_t count_before = ctx->result ? ctx->result->count : 0;

            rbc_glob_walk(actual_base, baselen, expanded->patterns[j], flags, ctx);

            // Sort results for this brace-expanded pattern (accumulate mode only)
            if (sort && ctx->result && ctx->result->count > count_before)
                rbc_glob_result_sort_range(ctx->result, count_before, ctx->result->count);
        }

        rbc_brace_free(expanded);
    }

    return ctx->status == RBC_GLOB_SUCCESS;
}

rbc_glob_status_t rbc_glob(
    const char **patterns,
    size_t npatterns,
    unsigned flags,
    const char *base,
    bool sort,
    rbc_glob_result_t *result,
    rbc_glob_errfunc_t errfunc,
    void *errfunc_data)
{
    if (!patterns || npatterns == 0 || !result)
        return RBC_GLOB_INVAL;
    rbc_glob_ctx_t ctx;
    if (!rbc_glob_ctx_init_buffering(&ctx, result, errfunc, errfunc_data))
        return RBC_GLOB_NOMEM;
    rbc_glob_internal(patterns, npatterns, flags, base, sort, &ctx);
    if (ctx.status != RBC_GLOB_SUCCESS)
        rbc_glob_result_cleanup(result);
    return ctx.status;
}

rbc_glob_status_t rbc_glob_each(
    const char **patterns,
    size_t npatterns,
    unsigned flags,
    const char *base,
    rbc_glob_callback_t callback,
    void *user_data,
    rbc_glob_errfunc_t errfunc,
    void *errfunc_data)
{
    if (!patterns || npatterns == 0 || !callback)
        return RBC_GLOB_INVAL;
    rbc_glob_ctx_t ctx;
    rbc_glob_ctx_init_streaming(&ctx, callback, user_data, errfunc, errfunc_data);
    rbc_glob_internal(patterns, npatterns, flags, base, false, &ctx);
    return ctx.status;
}

void rbc_globfree(rbc_glob_result_t *result)
{
    if (!result || !result->paths)
        return;
    for (size_t i = 0; i < result->count; i++)
        free(result->paths[i]);
    free(result->paths);
    result->paths = NULL;
    result->count = 0;
}
