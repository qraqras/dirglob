#ifndef DIRGLOB_DIRGLOB_DIRGLOB_H
#define DIRGLOB_DIRGLOB_DIRGLOB_H

#include <stdint.h>

/**
 * @brief Library version string
 */
#define DIRGLOB_VERSION "0.1.0"

/**
 * @defgroup fnm_flags Pattern matching flags
 * @brief Flags compatible with Ruby's File::FNM_* constants
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

const char *dirglob_version(void);

#endif /* DIRGLOB_DIRGLOB_DIRGLOB_H */
