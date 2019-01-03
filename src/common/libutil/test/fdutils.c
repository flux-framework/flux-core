/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "src/common/libtap/tap.h"
#include "src/common/libutil/fdutils.h"

int myfree_count;
void myfree (void *arg)
{
    myfree_count++;
}

int main (int argc, char *argv[])
{
    int pfds[2];
    int fd, fd2;
    int flags, flags2;
    int rc;
    plan (NO_PLAN);

    if (pipe (pfds) < 0)
        BAIL_OUT ("pipe");

    fd = pfds[0];
    fd2 = pfds[1];

    ok (fd_get_flags (-1) < 0 && errno == EBADF,
            "fd_get_flags fails on invalid fd");
    ok (fd_set_flags (-1, 0) < 0 && errno == EBADF,
            "fd_set_flags fails on invalid fd");
    ok (fd_set_blocking (-1) < 0 && errno == EBADF,
            "fd_set_blocking fails on invalid fd");
    ok (fd_set_nonblocking (-1) < 0 && errno == EBADF,
            "fd_set_nonblocking fails on invalid fd");
    ok (fd_set_cloexec (-1) < 0 && errno == EBADF,
            "fd_set_cloexec fails on invalid fd");
    ok (fd_unset_cloexec (-1) < 0 && errno == EBADF,
            "fd_unset_cloexec fails on invalid fd");

    flags = fd_get_flags (fd);
    cmp_ok (flags, ">=", 0,
            "fd_get_flags() works");

    rc = fd_set_nonblocking (fd);
    cmp_ok (rc, ">=", 0,
            "fd_set_nonblocking() returns Success");
    cmp_ok (rc, "==", flags,
            "fd_set_nonblocking returned original flags");

    flags2 = fd_get_flags (fd);
    cmp_ok (flags2, ">=", 0,
            "fd_get_flags() works");
    cmp_ok (flags2, "==", flags|O_NONBLOCK,
            "fd_set_nonblocking added O_NONBLOCK to flags");

    rc = fd_set_blocking (fd);
    cmp_ok (rc, ">=", 0,
            "fd_set_blocking() returns Success");
    cmp_ok (rc, "==", flags2,
            "fd_set_blocking() returned previous flags");

    flags2 = fd_get_flags (fd);
    cmp_ok (flags2, ">=", 0,
            "fd_get_flags() works");
    cmp_ok (flags2, "==", flags,
            "fd_set_blocking removed O_NONBLOCK flag");

    flags = fd_get_flags (fd2);
    cmp_ok (flags, ">=", 0,
            "fd_get_flags() works");
    cmp_ok (fd_set_nonblocking (fd2), ">=", 0,
            "fd_set_nonblocking() rc=0");
    flags2 = fd_get_flags (fd2);
    cmp_ok (flags2, ">=", 0,
            "fd_get_flags() works");
    cmp_ok (flags2, "==", flags|O_NONBLOCK,
            "fd_set_nonblocking added O_NONBLOCK to flags");
    cmp_ok (fd_set_flags (fd2, flags), "==", 0,
            "fd_set_flags() rc=0");
    flags2 = fd_get_flags (fd2);
    cmp_ok (flags2, "==", flags,
            "fd_set_flags restored flags");

    rc = fd_set_cloexec (fd);
    cmp_ok (rc, ">=", 0,
        "fd_set_cloexec() works rc=%d", rc);
    cmp_ok (rc&FD_CLOEXEC, "==", 0,
        "fd_set_cloexec() returns old flags");
    rc = fd_unset_cloexec (fd);
    cmp_ok (rc, ">=", 0,
        "fd_unset_cloexec() works rc=%d", rc);
    cmp_ok (rc&FD_CLOEXEC, "==", 1,
        "fd_unset_cloexec() returns old flags");

    done_testing ();
    close (fd);
    close (fd2);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
