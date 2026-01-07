#include <rbcglob/internal/dir.h>
#include <rbcglob/internal/utils.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#endif

char *rbcglob_home_dir(const char *user)
{
    /* Get home directory for specified user, or current user if NULL */
    if (!user || !*user)
    {
        /* Current user */
#ifdef _WIN32
        /* Windows: Try USERPROFILE first, then HOMEPATH */
        char *home = getenv("USERPROFILE");
        if (!home)
        {
            home = getenv("HOMEPATH");
            if (home)
            {
                /* HOMEPATH may need HOMEDRIVE prepended */
                char *drive = getenv("HOMEDRIVE");
                if (drive)
                {
                    size_t drive_len = strlen(drive);
                    size_t home_len = strlen(home);
                    char *full_path = malloc(drive_len + home_len + 1);
                    if (full_path)
                    {
                        memcpy(full_path, drive, drive_len);
                        memcpy(full_path + drive_len, home, home_len + 1);
                        return full_path;
                    }
                }
            }
        }
        return home ? rbcglob_strdup(home) : NULL;
#else
        /* Unix: Try HOME environment variable first */
        char *home = getenv("HOME");
        if (home && *home)
        {
            return rbcglob_strdup(home);
        }

        /* Fallback to getpwuid() */
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_dir)
        {
            return rbcglob_strdup(pw->pw_dir);
        }

        return NULL;
#endif
    }
    else
    {
        /* Specified user */
#ifdef _WIN32
        /* Windows: Getting another user's home directory is not straightforward */
        /* Would require NetUserGetInfo or similar Win32 APIs */
        return NULL;
#else
        /* Unix: Use getpwnam() */
        struct passwd *pw = getpwnam(user);
        if (pw && pw->pw_dir)
        {
            return rbcglob_strdup(pw->pw_dir);
        }
        return NULL;
#endif
    }
}
