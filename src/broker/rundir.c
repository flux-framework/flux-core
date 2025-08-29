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

/* Make path fit in half a flux_error_t text buffer,
 * replacing the last character with '+'.
 */
const char *xpath (const char *path)
{
    static __thread flux_error_t error;
    size_t size = sizeof (error.text) / 2;

    if (strlcpy (error.text, path, size) > size) {
        error.text[size - 2] = '+';
        error.text[size - 1] = '\0';
    }
    return error.text;
}

int rundir_checkdir (const char *path, flux_error_t *error)
{
    struct stat sb;

    if (stat (path, &sb) < 0) {
        errprintf (error, "cannot stat %s: %s", xpath (path), strerror (errno));
        return -1;
    }
    if (sb.st_uid != getuid ()) {
        errno = EPERM;
        errprintf (error,
                   "%s is not owned by instance owner: %s",
                   xpath (path),
                   strerror (errno));
        return -1;
    }
    if (!S_ISDIR (sb.st_mode)) {
        errno = ENOTDIR;
        errprintf (error, "%s: %s", xpath (path), strerror (errno));
        return -1;
    }
    if ((sb.st_mode & S_IRWXU) != S_IRWXU) {
        errprintf (error,
                   "%s does not have owner=rwx permissions",
                   xpath (path));
        errno = EPERM;
        return -1;
    }
    return 0;
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
        errprintf (error, "mkdir %s: %s", xpath (path), strerror (errno));
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
        errprintf (error, "symlink %s: %s", xpath (path), strerror (errno));
        return -1;
    }
    return 0;
overflow:
    errprintf (error, "buffer overflow");
    errno = EOVERFLOW;
    return -1;
}

static int rundir_special (const char *dirpath, flux_error_t *error)
{
    /*  Ensure that AF_UNIX sockets can be created in rundir - see #3925.
     */
    struct sockaddr_un sa;
    size_t path_limit = sizeof (sa.sun_path) - sizeof ("/local-9999");
    size_t path_length = strlen (dirpath);
    if (path_length > path_limit) {
        errprintf (error,
                   "length of %zu bytes exceeds max %zu"
                   " to allow for AF_UNIX socket creation.",
                   path_length,
                   path_limit);
        return -1;
    }
    /*  Create $rundir/bin/flux so flux-relay can be found - see #5583.
     */
    if (create_rundir_symlinks (dirpath, error) < 0) {
        if (errno != EEXIST)
            return -1;
    }
    return 0;
}

static bool path_exists (const char *path)
{
    struct stat sb;
    if (stat (path, &sb) < 0)
        return false;
    return true;
}

int rundir_create (attr_t *attrs,
                   const char *attr_name,
                   const char *tmpdir,
                   flux_error_t *error)
{
    const char *dirpath = NULL;
    char path[1024];
    int len;
    bool do_cleanup = true;
    int rc = -1;

    /*  If attribute isn't set, then create a temp directory and use that.
     */
    if (attr_get (attrs, attr_name, &dirpath, NULL) < 0) {
        len = snprintf (path, sizeof (path), "%s/flux-XXXXXX", tmpdir);
        if (len >= sizeof (path)) {
            errprintf (error, "buffer overflow");
            goto done;
        }
        if (!(dirpath = mkdtemp (path))) {
            errprintf (error,
                       "cannot create directory in %s: %s",
                       tmpdir,
                       strerror (errno));
            goto done;
        }
        if (attr_add (attrs, attr_name, dirpath, 0) < 0)
            goto error_setattr;
    }
    /*  If attribute is set, but the directory doesn't exist,
     * try to create the named directory.
     */
    else if (!path_exists (dirpath)) {
        if (mkdir (dirpath, 0700) < 0) {
            errprintf (error,
                       "error creating %s: %s",
                       dirpath,
                       strerror (errno));
                goto done;
        }
    }
    /*  If directory was pre-existing, do not schedule it for
     * auto-cleanup at broker exit.
     */
    else {
        do_cleanup = false;
    }

    /*  Ensure created or existing directory is writeable:
     */
    if (rundir_checkdir (dirpath, error) < 0)
        goto done;

    /*  Perform rundir specific actions
     */
    if (streq (attr_name, "rundir")) {
        if (rundir_special (dirpath, error) < 0)
            goto done;
    }

    /*  dirpath is now fixed, so make the attribute immutable, and
     * schedule the dir for cleanup at exit if we created it here.
     */
    if (attr_set_flags (attrs, attr_name, ATTR_IMMUTABLE) < 0)
        goto error_setattr;

    /*  If attr-cleanup is set on the command line, it overrides default
     * do_cleanup behavior set above.
     */
    char key[64];
    const char *val = NULL;

    (void)snprintf (key, sizeof (key), "%s-cleanup", attr_name);
    if (attr_get (attrs, key, &val, NULL) == 0 && val)
        do_cleanup = streq (val, "0") ? false : true;
    (void)attr_delete (attrs, key, true);
    if (attr_add_int (attrs, key, do_cleanup ? 1 : 0, ATTR_IMMUTABLE) < 0)
        goto error_setattr;

    rc = 0;
done:
    if (do_cleanup && dirpath)
        cleanup_push_string (cleanup_directory_recursive, dirpath);
    return rc;

error_setattr:
    errprintf (error, "error setting broker attribute: %s", strerror (errno));
    return -1;
}

// vi:ts=4 sw=4 expandtab
