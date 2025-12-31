#ifndef DIRGLOB_INTERNAL_FLAGS_H
#define DIRGLOB_INTERNAL_FLAGS_H

/**
 * @defgroup fnm_flags fnmatch-like flags
 * @{
 */
#ifndef FNM_NOESCAPE
#define FNM_NOESCAPE (1U << 0)
#endif
#ifndef FNM_PATHNAME
#define FNM_PATHNAME (1U << 1)
#endif
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD (1U << 2)
#endif
#ifndef FNM_DOTMATCH
#define FNM_DOTMATCH (1U << 3)
#endif
#ifndef FNM_EXTGLOB
#define FNM_EXTGLOB (1U << 4)
#endif
/**
 * @}
 */

#endif /* DIRGLOB_INTERNAL_FLAGS_H */
