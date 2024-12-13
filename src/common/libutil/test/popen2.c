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
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/wait.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/popen2.h"
#include "src/common/libutil/read_all.h"

static void test_popen2_stderr ()
{
    struct popen2_child *p;
    char *av[] = { "cat",  "/nosuchfile", NULL };
    char *buf;
    int efd;

    ok (popen2 ("cat", av, 42) == NULL && errno == EINVAL,
        "popen2() with invalid flags returns EINVAL");

    ok ((p = popen2 ("cat", av, POPEN2_CAPTURE_STDERR)) != NULL,
        "popen2() with POPEN2_CAPTURE_STDERR works");
    ok (pclose2 (p) == 0x100,
        "immediate pclose2 returns failed exit status of command");

    ok ((p = popen2 ("cat", av, POPEN2_CAPTURE_STDERR)) != NULL,
        "popen2() with POPEN2_CAPTURE_STDERR works");
    ok ((efd = popen2_get_stderr_fd (p)) >= 0,
        "popen2_get_stderr_fd() works");
    ok (read_all (efd, (void **) &buf) > 0,
        "read from stderr fd worked");
    ok (pclose2 (p) == 0x100,
        "pclose2 returns failed exit status of command");

    like (buf, ".*: No such file or directory",
        "stderr contained expected error");
    free (buf);
}

int main(int argc, char** argv)
{
    struct popen2_child *p;
    char *av[] = { "cat", NULL };
    uint8_t outbuf[] = "hello\n";
    uint8_t inbuf[sizeof (outbuf)];
    int fd;

    plan (NO_PLAN);

    /* open/close */
    ok ((p = popen2 ("cat", av, 0)) != NULL,
        "popen2 cat worked");
    ok (pclose2 (p) == 0,
        "immediate pclose2 OK");

    /* open/write/close */
    ok ((p = popen2 ("cat", av, 0)) != NULL,
        "popen2 cat worked");
    fd = popen2_get_fd (p);
    ok (fd >= 0,
        "popen2_get_fd returned %d", fd);
    ok (write_all (fd, outbuf, sizeof (outbuf)) == sizeof (outbuf),
        "write to fd worked");
    ok (pclose2 (p) == 0,
        "pclose2 with read data pending OK");

    /* open/write/read/close */
    ok ((p = popen2 ("cat", av, 0)) != NULL,
        "popen2 cat worked");
    fd = popen2_get_fd (p);
    ok (fd >= 0,
        "popen2_get_fd returned %d", fd);
    ok (write_all (fd, outbuf, sizeof (outbuf)) == sizeof (outbuf),
        "write to fd worked");
    ok (read (fd, inbuf, sizeof (inbuf)) == sizeof (outbuf)
        && !memcmp (inbuf, outbuf, sizeof (outbuf)),
        "read back what we wrote");
    ok (pclose2 (p) == 0,
        "pclose2 OK");

    /* open failure */
    errno = 0;
    p = popen2 ("/noexist", av, 0);
    ok (p == NULL && errno == ENOENT,
        "popen2 /noexist failed with ENOENT");

    /* open/close (child exit error) */
    ok ((p = popen2 ("false", (char*[]){ "false", NULL }, 0)) != NULL,
        "popen2 false OK");
    ok (pclose2 (p) == 0x100,
        "pclose2 returns child exit code 1");

    test_popen2_stderr ();
    done_testing();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
