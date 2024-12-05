/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h> /* MAXPATHLEN */
#include <libgen.h>    /* dirname(3) */
#include <pthread.h>

#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"


/*  Strip trailing ".libs", otherwise do nothing
 */
static char *strip_trailing_dot_libs (char *dir)
{
    char *p = dir + strlen (dir) - 1;
    if (   (*(p--) == 's')
        && (*(p--) == 'b')
        && (*(p--) == 'i')
        && (*(p--) == 'l')
        && (*(p--) == '.')
        && (*p == '/') )
        *p = '\0';
    return (dir);
}

#if __APPLE__
#include <mach-o/dyld.h>
static char *nsget_wrap (void)
{
    char *path = NULL;
    uint32_t size = 0;

    (void)_NSGetExecutablePath (path, &size); // get the required buffer size
    if (!(path = calloc (1, size))
        || _NSGetExecutablePath (path, &size) < 0) {
        ERRNO_SAFE_WRAP (free, path);
        return NULL;
    }
    return path;
}
static int executable_self (char *buf, size_t bufsize)
{
    char *path;
    char *newpath = NULL;
    int rc = -1;

    if (!(path = nsget_wrap ())
        || !(newpath = realpath (path, NULL))) {
        goto done;
    }
    if (strlcpy (buf, newpath, bufsize) >= bufsize) {
        errno = EOVERFLOW;
        goto done;
    }
    rc = 0;
done:
    ERRNO_SAFE_WRAP (free, newpath);
    ERRNO_SAFE_WRAP (free, path);
    return rc;
}
#else
static int executable_self (char *buf, size_t bufsize)
{
    ssize_t len = readlink ("/proc/self/exe", buf, bufsize - 1);
    if (len < 0)
        return -1;
    buf[len] = '\0';
    return 0;
}
#endif

/*  Return directory containing this executable.
 */
const char *executable_selfdir (void)
{
    static pthread_mutex_t selfdir_lock = PTHREAD_MUTEX_INITIALIZER;
    static char current_exe_path [MAXPATHLEN];
    static char *current_exe_dir = NULL;
    pthread_mutex_lock (&selfdir_lock);
    if (!current_exe_dir) {
        if (executable_self (current_exe_path, sizeof (current_exe_path)) < 0)
            goto out;
        current_exe_dir = strip_trailing_dot_libs (dirname (current_exe_path));
    }
out:
    pthread_mutex_unlock (&selfdir_lock);
    return current_exe_dir;
}

/*   Check if the path to the current executable is in a subdirectory
 *   of the top build directory of flux-core. This should work to detect
 *   if an executable is running in-tree no matter where in the build
 *   tree that executable was built.
 */
static int is_intree (void)
{
    const char *selfdir = NULL;
    char *builddir = NULL;
    int ret = 0;

    if (!(selfdir = executable_selfdir ()))
        return -1;
    /*
     *  Calling realpath(3) with NULL second arg is safe since POSIX.1-2008.
     *   (Equivalent to glibc's canonicalize_path_name(3))
     *
     *  If realpath(3) returns ENOENT, then BINDIR doesn't exist and flux
     *   clearly can't be from the installed path:
     */
    if (!(builddir = realpath (ABS_TOP_BUILDDIR, NULL))) {
        if ((errno == ENOENT) || (errno == EACCES))
            return 0;
        return -1;
    }
    if (strstarts (selfdir, builddir))
        ret = 1;
    free (builddir);
    return ret;
}

int executable_is_intree (void)
{
    static pthread_mutex_t intree_lock = PTHREAD_MUTEX_INITIALIZER;
    static int current_exe_intree = -1;
    /* If uninitialized, initialize current_exe_intree now */
    pthread_mutex_lock (&intree_lock);
    if (current_exe_intree < 0)
        current_exe_intree = is_intree ();
    pthread_mutex_unlock (&intree_lock);
    return current_exe_intree;
}

/* vi: ts=4 sw=4 expandtab
 */
