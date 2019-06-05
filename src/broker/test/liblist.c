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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <stdlib.h>
#include <czmq.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/iterators.h"

#include "liblist.h"

void diag_dump (zlist_t *libs)
{
    char *name;
    int i = 0;
    FOREACH_ZLIST (libs, name) {
        diag ("%d: %s", i, name);
        i++;
    }
}

int main (int argc, char **argv)
{
    zlist_t *libs;
    char *tmpdir = getenv ("TMPDIR");
    char testdir[PATH_MAX + 1];
    char path[PATH_MAX + 1];
    int fd;
    int n;

    if (!tmpdir)
        tmpdir = "/tmp";

    plan (NO_PLAN);

    /* First mode: library path contains slashes.
     * List will just contain that path without checking if it exists..
     */
    libs = liblist_create ("/my/libfoo.so");
    ok (libs != NULL,
        "liblist_create libname=/my/libfoo.so works");
    ok (zlist_size (libs) == 1,
        "liblist contains one entry");
    ok (!strcmp ("/my/libfoo.so", zlist_head (libs)),
        "liblist contains /my/libfoo.so");
    diag_dump (libs);
    liblist_destroy (libs);

    /* Second mode: library path contains no slashes.
     * List will contain first any occurrences in LD_LIBRARY_PATH dirs,
     * then any in ld.so.cache.
     * Focus on LD_LIBRARY_PATH since we can control but try a common name
     * to maybe pick up something from ld.so.cache.
     */
    n = snprintf (testdir, sizeof (testdir), "%s/test.XXXXXX", tmpdir);
    if (n >= sizeof (testdir))
        BAIL_OUT ("buffer overflow");
    if (mkdtemp (testdir) == NULL)
        BAIL_OUT ("mkdtemp failed");
    n = snprintf (path, sizeof (path), "%s/libSegFault.so", testdir);
    if (n >= sizeof (path))
        BAIL_OUT ("buffer overflow");
    if ((fd = open (path, O_CREAT | O_RDWR, 0666)) < 0)
        BAIL_OUT ("could not create %s", path);
    close (fd);

    if (setenv ("LD_LIBRARY_PATH", testdir, 1) < 0)
        BAIL_OUT ("setenv failed");
    libs = liblist_create ("libSegFault.so");
    ok (libs != NULL,
        "liblist_create libname=libSegFault.so works");
    ok (zlist_size (libs) >= 1,
        "liblist contains at least one entry");
    ok (!strcmp (path, zlist_head (libs)),
        "liblist contains %s", path);
    diag_dump (libs);
    liblist_destroy (libs);

    (void)rmdir (testdir);
    (void)unlink (path);
    done_testing ();
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
