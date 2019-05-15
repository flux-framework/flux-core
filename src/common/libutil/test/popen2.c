/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/popen2.h"
#include "src/common/libutil/read_all.h"

int main (int argc, char **argv)
{
    struct popen2_child *p;
    char *av[] = {"cat", NULL};
    uint8_t outbuf[] = "hello\n";
    uint8_t inbuf[sizeof (outbuf)];
    int fd;

    plan (NO_PLAN);

    /* open/close */
    ok ((p = popen2 ("cat", av)) != NULL, "popen2 cat worked");
    ok (pclose2 (p) == 0, "immediate pclose2 OK");

    /* open/write/close */
    ok ((p = popen2 ("cat", av)) != NULL, "popen2 cat worked");
    fd = popen2_get_fd (p);
    ok (fd >= 0, "popen2_get_fd returned %d", fd);
    ok (write_all (fd, outbuf, sizeof (outbuf)) == sizeof (outbuf),
        "write to fd worked");
    ok (pclose2 (p) == 0, "pclose2 with read data pending OK");

    /* open/write/read/close */
    ok ((p = popen2 ("cat", av)) != NULL, "popen2 cat worked");
    fd = popen2_get_fd (p);
    ok (fd >= 0, "popen2_get_fd returned %d", fd);
    ok (write_all (fd, outbuf, sizeof (outbuf)) == sizeof (outbuf),
        "write to fd worked");
    ok (read (fd, inbuf, sizeof (inbuf)) == sizeof (outbuf)
            && !memcmp (inbuf, outbuf, sizeof (outbuf)),
        "read back what we wrote");
    ok (pclose2 (p) == 0, "pclose2 OK");

    /* open failure */
    errno = 0;
    p = popen2 ("/noexist", av);
    ok (p == NULL && errno == ENOENT, "popen2 /noexist failed with ENOENT");

    /* open/close (child exit error) */
    ok ((p = popen2 ("/bin/false", av)) != NULL, "popen2 /bin/false OK");
    errno = 0;
    ok (pclose2 (p) < 0 && errno == EIO, "pclose2 failed with EIO");

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
