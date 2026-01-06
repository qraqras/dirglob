#include <rbcglob/internal/fnmatch.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>

/* Forward declarations */
static int rbcglob_fnmatch_match_single(const char *p, const char *s, const char *p_end, const char *s_end, unsigned flags, bool is_leading);
static int rbcglob_fnmatch_match_bracket(const char **p, char c, const char *p_end, unsigned flags);

static int rbcglob_fnmatch_match_single(const char *p, const char *s, const char *p_end, const char *s_end, unsigned flags, bool is_leading) {
    const char *pp = p;
    const char *ss = s;

    while (pp < p_end) {
        switch (*pp) {
        case '*': {
            while (pp < p_end && *pp == '*') pp++;
            if (pp == p_end) {
                if (is_leading && ss < s_end && *ss == '.' && !(flags & RBCGLOB_FNM_DOTMATCH)) {
                    return 1;
                }
                return 0;
            }
            for (; ss <= s_end; ss++) {
                if (is_leading && ss < s_end && *ss == '.' && !(flags & RBCGLOB_FNM_DOTMATCH)) {
                    if (ss == s) continue;
                }
                if (rbcglob_fnmatch_match_single(pp, ss, p_end, s_end, flags, false) == 0) return 0;
            }
            return 1;
        }
        case '?': {
            if (ss == s_end) return 1;
            if (is_leading && ss == s && *ss == '.' && !(flags & RBCGLOB_FNM_DOTMATCH)) return 1;
            pp++; ss++; is_leading = false;
            break;
        }
        case '[': {
            if (ss == s_end) return 1;
            if (is_leading && ss == s && *ss == '.' && !(flags & RBCGLOB_FNM_DOTMATCH)) return 1;
            pp++;
            if (rbcglob_fnmatch_match_bracket(&pp, *ss, p_end, flags) != 0) return 1;
            ss++; is_leading = false;
            break;
        }
        case '\\':
            if (!(flags & RBCGLOB_FNM_NOESCAPE) && (pp + 1 < p_end)) {
                pp++;
            }
            /* fallthrough */
        default: {
            if (ss == s_end) return 1;
            char pc = *pp, sc = *ss;
            if (flags & RBCGLOB_FNM_CASEFOLD) {
                pc = (char)tolower((unsigned char)pc);
                sc = (char)tolower((unsigned char)sc);
            }
            if (pc != sc) return 1;
            pp++; ss++; is_leading = false;
            break;
        }
        }
    }
    return (ss == s_end) ? 0 : 1;
}

static int rbcglob_fnmatch_match_bracket(const char **p, char c, const char *p_end, unsigned flags) {
    const char *pp = *p;
    bool negate = false, matched = false;
    if (pp < p_end && (*pp == '!' || *pp == '^')) { negate = true; pp++; }
    if (pp < p_end && *pp == ']') { if (c == ']') matched = true; pp++; }
    while (pp < p_end && *pp != ']') {
        if (pp + 2 < p_end && pp[1] == '-' && pp[2] != ']') {
            char start = *pp, end = pp[2], cc = c;
            if (flags & RBCGLOB_FNM_CASEFOLD) {
                start = (char)tolower((unsigned char)start);
                end = (char)tolower((unsigned char)end);
                cc = (char)tolower((unsigned char)cc);
            }
            if (cc >= start && cc <= end) matched = true;
            pp += 3;
        } else {
            char pc = *pp, cc = c;
            if (flags & RBCGLOB_FNM_CASEFOLD) {
                pc = (char)tolower((unsigned char)pc);
                cc = (char)tolower((unsigned char)cc);
            }
            if (pc == cc) matched = true;
            pp++;
        }
    }
    if (pp < p_end && *pp == ']') pp++;
    *p = pp;
    return (matched != negate) ? 0 : 1;
}

static int rbcglob_fnmatch_match_pathname(const char *p, const char *s, unsigned flags) {
    const char *p_end = p + strlen(p);
    const char *s_end = s + strlen(s);
    const char *pp = p, *sp = s;

    if (!*p && !*s) return 0;

    while (pp < p_end) {
        const char *next_p = memchr(pp, '/', p_end - pp);
        const char *next_s = memchr(sp, '/', s_end - sp);
        const char *cp_end = next_p ? next_p : p_end;
        const char *cs_end = next_s ? next_s : s_end;

        if ((cp_end - pp) == 2 && pp[0] == '*' && pp[1] == '*') {
            if (next_p) {
                if (rbcglob_fnmatch_match_pathname(next_p + 1, sp, flags) == 0) return 0;
                while (next_s) {
                    if (*sp == '.' && !(flags & RBCGLOB_FNM_DOTMATCH)) break;
                    sp = next_s + 1;
                    if (rbcglob_fnmatch_match_pathname(next_p + 1, sp, flags) == 0) return 0;
                    next_s = memchr(sp, '/', s_end - sp);
                }
                return 1;
            } else {
                if (*sp == '.' && !(flags & RBCGLOB_FNM_DOTMATCH)) return 1;
                return (next_s == NULL) ? 0 : 1;
            }
        }

        if (rbcglob_fnmatch_match_single(pp, sp, cp_end, cs_end, flags, true) != 0) return 1;

        if (!!next_p != !!next_s) return 1;
        if (!next_p) return 0;

        pp = next_p + 1;
        sp = next_s + 1;
    }
    return (*sp == '\0') ? 0 : 1;
}

int rbcglob_fnmatch(const char *pattern, const char *string, unsigned flags) {
    if (!pattern || !string) return 1;
    if (flags & RBCGLOB_FNM_PATHNAME) {
        return rbcglob_fnmatch_match_pathname(pattern, string, flags);
    } else {
        return rbcglob_fnmatch_match_single(pattern, string, pattern + strlen(pattern), string + strlen(string), flags, true);
    }
}
