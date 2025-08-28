/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/un.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/intree.h"
#include "ccan/str/str.h"
#ifndef HAVE_STRLCPY
#include "src/common/libmissing/strlcpy.h"
#endif
#ifndef HAVE_STRLCAT
#include "src/common/libmissing/strlcat.h"
#endif

#include "attr.h"
#include "rundir.h"

int rundir_checkdir (const char *path, flux_error_t *error)
{
    struct stat sb;

    if (stat (path, &sb) < 0) {
        errprintf (error, "cannot stat %s: %s", path, strerror (errno));
        return -1;
    }
    if (sb.st_uid != getuid ()) {
        errno = EPERM;
        errprintf (error,
                   "%s is not owned by instance owner: %s",
                   path,
                   strerror (errno));
        return -1;
    }
    if (!S_ISDIR (sb.st_mode)) {
        errno = ENOTDIR;
        errprintf (error, "%s: %s", path, strerror (errno));
        return -1;
    }
    if ((sb.st_mode & S_IRWXU) != S_IRWXU) {
        errprintf (error, "%s does not have owner=rwx permissions", path);
        errno = EPERM;
        return -1;
    }
    return 0;
}

/* Validate statedir, if set.
 * Ensure that the attribute cannot change from this point forward.
 */
static int statedir_check (attr_t *attrs, flux_error_t *error)
{
    const char *statedir;

    if (attr_get (attrs, "statedir", &statedir, NULL) < 0) {
        if (attr_add (attrs, "statedir", NULL, ATTR_IMMUTABLE) < 0)
            goto error_attr;
    }
    else {
        if (rundir_checkdir (statedir, error) < 0)
            return -1;
        if (attr_set_flags (attrs, "statedir", ATTR_IMMUTABLE) < 0)
            goto error_attr;
    }
    return 0;
error_attr:
    errprintf (error, "error setting broker attribute");
    return -1;
}

static int create_rundir_symlinks (const char *run_dir, flux_error_t *error)
{
    char path[1024];
    size_t size = sizeof (path);
    const char *target;

    if (strlcpy (path, run_dir, size) >= size
        || strlcat (path, "/bin", size) >= size)
        goto overflow;
    if (mkdir (path, 0755) < 0) {
        errprintf (error, "mkdir %s: %s", path, strerror (errno));
        return -1;
    }
    cleanup_push_string (cleanup_directory_recursive, path);
    if (strlcat (path, "/flux", size) >= size)
        goto overflow;
    if (executable_is_intree () == 1)
        target = ABS_TOP_BUILDDIR "/src/cmd/flux";
    else
        target = X_BINDIR "/flux";
    if (symlink (target, path) < 0) {
        errprintf (error, "symlink %s: %s", path, strerror (errno));
        return -1;
    }
    return 0;
overflow:
    errprintf (error, "buffer overflow");
    errno = EOVERFLOW;
    return -1;
}

/*  Handle global rundir attribute.
 */
int rundir_create (attr_t *attrs, const char *attr_name, flux_error_t *error)
{
    const char *tmpdir;
    const char *run_dir = NULL;
    char path[1024];
    int len;
    bool do_cleanup = true;
    int rc = -1;

    if (streq (attr_name, "statedir"))
        return statedir_check (attrs, error);

    /*  If rundir attribute isn't set, then create a temp directory
     *   and use that as rundir. If directory was set, try to create it if
     *   it doesn't exist. If directory was pre-existing, do not schedule
     *   the dir for auto-cleanup at broker exit.
     */
    if (attr_get (attrs, attr_name, &run_dir, NULL) < 0) {
        if (!(tmpdir = getenv ("TMPDIR")))
            tmpdir = "/tmp";
        len = snprintf (path, sizeof (path), "%s/flux-XXXXXX", tmpdir);
        if (len >= sizeof (path)) {
            errprintf (error, "buffer overflow");
            goto done;
        }
        if (!(run_dir = mkdtemp (path))) {
            errprintf (error,
                       "cannot create directory in %s: %s",
                       tmpdir,
                       strerror (errno));
            goto done;
        }
        if (attr_add (attrs, attr_name, run_dir, 0) < 0) {
            errprintf (error,
                       "error setting broker attribute: %s",
                       strerror (errno));
            goto done;
        }
    }
    else if (mkdir (run_dir, 0700) < 0) {
        if (errno != EEXIST) {
            errprintf (error,
                       "error creating %s: %s",
                       run_dir,
                       strerror (errno));
            goto done;
        }
        /* Do not cleanup directory if we did not create it here
         */
        do_cleanup = false;
    }

    /*  Ensure created or existing directory is writeable:
     */
    if (rundir_checkdir (run_dir, error) < 0)
        goto done;

    /*  Ensure that AF_UNIX sockets can be created in rundir - see #3925.
     */
    struct sockaddr_un sa;
    size_t path_limit = sizeof (sa.sun_path) - sizeof ("/local-9999");
    size_t path_length = strlen (run_dir);
    if (path_length > path_limit) {
        errprintf (error,
                   "length of %zu bytes exceeds max %zu"
                   " to allow for AF_UNIX socket creation.",
                   path_length,
                   path_limit);
        goto done;
    }

    /*  rundir is now fixed, so make the attribute immutable, and
     *   schedule the dir for cleanup at exit if we created it here.
     */
    if (attr_set_flags (attrs, attr_name, ATTR_IMMUTABLE) < 0) {
        errprintf (error,
                   "error setting broker attribute: %s",
                   strerror (errno));
        goto done;
    }

    /*  Create $rundir/bin/flux so flux-relay can be found - see #5583.
     */
    if (create_rundir_symlinks (run_dir, error) < 0) {
        if (errno != EEXIST)
            goto done;
    }
    rc = 0;
done:
    if (do_cleanup && run_dir != NULL)
        cleanup_push_string (cleanup_directory_recursive, run_dir);
    return rc;
}

// vi:ts=4 sw=4 expandtab
