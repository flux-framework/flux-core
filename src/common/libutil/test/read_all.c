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
#include <sys/param.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "read_all.h"

/* Test for bad args.
 */
void test_badargs (void)
{
    char *buf;

    errno = 0;
    ok (write_all (-1, "", 1) < 0 && errno == EINVAL,
        "write_all fd=-1 fails with EINVAL");
    errno = 0;
    ok (write_all (STDOUT_FILENO, NULL, 1) < 0 && errno == EINVAL,
        "write_all buf=NULL fails with EINVAL");

    errno = 0;
    ok (read_all (-1, (void **)&buf) < 0 && errno == EINVAL,
        "read_all fd=-1 fails with EINVAL");
    errno = 0;
    ok (read_all (STDIN_FILENO, NULL) < 0 && errno == EINVAL,
        "read_all buf=NULL fails with EINVAL");
}

/* Write out 'n' bytes to tmpfile.
 * Read back 'n' bytes from tmpfile.
 * Verify bytes.
 */
void test_readback (size_t sz)
{
    void *buf;
    void *buf2;
    char *t = getenv ("TMPDIR");
    char tmpfile[PATH_MAX + 1];
    int fd;
    ssize_t sz2;

    /* Create tmpfile
     */
    if (snprintf (tmpfile, sizeof (tmpfile), "%s/read_all.XXXXXX",
                  t ? t : "/tmp") >= sizeof (tmpfile))
        BAIL_OUT ("tmpfile overflow");
    if ((fd = mkstemp (tmpfile)) < 0)
        BAIL_OUT ("mkstemp: %s", strerror (errno));

    /* Create buffer to write of specified size
     */
    if (!(buf = malloc (sz)))
        BAIL_OUT ("malloc failed");
    memset (buf, 'a', sz);

    /* Write, read, verify by fd
     */
    ok (write_all (fd, buf, sz) == sz,
        "write_all wrote %lu bytes", sz);
    if (lseek (fd, 0, SEEK_SET) < 0)
        BAIL_OUT ("lseek: %s", strerror (errno));
    ok ((sz2 = read_all (fd, &buf2)) == sz,
        "read_all read %lu bytes", sz);
    ok (memcmp (buf2, buf, sz2) == 0,
        "and data matches what was written");

    if (close (fd) < 0)
        BAIL_OUT ("close: %s", strerror (errno));
    free (buf);
    free (buf2);
    (void)unlink (tmpfile);
}

int main (int argc, char *argv[])
{

    plan (NO_PLAN);

    test_readback (33);
    test_readback (8192); // more than internal chunk size

    test_badargs ();

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
