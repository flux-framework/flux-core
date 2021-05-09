/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <sys/param.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "read_all.h"
#include "digest.h"

/* Test for bad args.
 */
void test_badargs (void)
{
    errno = 0;
    ok (digest_file (NULL, NULL) == NULL && errno == EINVAL,
        "digest_file path=NULL fails with EINVAL");
}

/* Write out 'sz' bytes to tmpfile, digest file */
void test_filesize (size_t sz)
{
    void *buf = NULL;
    char *t = getenv ("TMPDIR");
    char tmpfile[PATH_MAX + 1];
    int fd;
    size_t sz2;
    char *digest;

    /* Create tmpfile
     */
    if (snprintf (tmpfile, sizeof (tmpfile), "%s/read_all.XXXXXX",
                  t ? t : "/tmp") >= sizeof (tmpfile))
        BAIL_OUT ("tmpfile overflow");
    if ((fd = mkstemp (tmpfile)) < 0)
        BAIL_OUT ("mkstemp: %s", strerror (errno));

    if (sz) {
        /* Create buffer to write of specified size */
        if (!(buf = malloc (sz)))
            BAIL_OUT ("malloc failed");
        memset (buf, 'a', sz);

        /* Write data */
        ok (write_all (fd, buf, sz) == sz,
            "write_all wrote %lu bytes", sz);
    }

    digest = digest_file (tmpfile, &sz2);
    ok (digest != NULL,
        "digest_file digested %lu bytes: %s", sz, digest);
    ok (sz == sz2,
        "digest_file digested correct number of bytes");

    if (close (fd) < 0)
        BAIL_OUT ("close: %s", strerror (errno));
    free (buf);
    (void)unlink (tmpfile);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    test_filesize (0);
    test_filesize (33);
    test_filesize (8192); // more than internal chunk size

    test_badargs ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
