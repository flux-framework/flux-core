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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/resource.h>

#include "src/common/libtap/tap.h"

#define FDWALK_INTERFACE_TEST
#include "src/common/libutil/fdwalk.h"


static int get_high_fd_number (void)
{
    struct rlimit rl = { 100, 100 };
    ok (getrlimit (RLIMIT_NOFILE, &rl) == 0,
        "getrlimit (RLIMIT_NOFILE)");
    diag ("rlmit.nofile = %d", rl.rlim_cur);
    // Let's be reasonable here
    if (rl.rlim_cur > 10000) {
        rl.rlim_cur = 10000;
        ok (setrlimit (RLIMIT_NOFILE, &rl) == 0,
            "setrlimit nofile=%d", rl.rlim_cur);
    }
    return rl.rlim_cur - 1;
}

static void set_fd (void *data, int fd)
{
    int *fds = data;
    fds[fd]++;
}

static void set_fd_if_open (void *data, int fd)
{
    if (fcntl (fd, F_GETFL) < 0 && errno == EBADF)
        return;
    int *fds = data;
    fds[fd]++;
}

static int * get_open_fds (int maxfd)
{
    /* Valgrind may show open fds > maxfd, so double the space
     *  allocated so we don't overflow when run under valgrind.
     */
    int * fds = calloc (maxfd * 2, sizeof (int));
    if (fds)
        ok (fdwalk (set_fd_if_open, fds) == 0,
            "fdwalk () worked");
    return fds;
}

static void test_fdwalk_fallback (int maxfd)
{
    int *fds = calloc (maxfd * 2, sizeof (int));
    if (!fds)
        BAIL_OUT ("test_fdwalk_fallback: out of memory");
    ok (_fdwalk_portable (set_fd, fds) == 0,
        "_fdwalk_portable() worked");
    int count = 0;
    for (int i = 0; i <= maxfd; i++)
        if (fds[i] == 1) count++;

    ok (count == maxfd + 1,
        "_fdwalk_portable() visited all %d fds (expected %d)",
        count, maxfd + 1);

    free (fds);
}

int main (int argc, char *argv[])
{
    int *openfds = NULL;
    int *fds = NULL;
    int i, maxfd;

    plan (NO_PLAN);

    maxfd = get_high_fd_number ();
    ok (maxfd > 0,
       "got maxfd = %d", maxfd);

    if (!(openfds = get_open_fds (maxfd)))
        BAIL_OUT ("Failed to create open fds");

    for (i = 0; i < maxfd; i++) {
        if (openfds[i])
            ok (openfds[i] == 1, "fd=%d visited once", i);
    }

    /*  Open some more fds */
    int pfds[2];
    ok (pipe (pfds) == 0,
       "Using pipe(2) to open arbitrary fds");

    errno = 0;
    ok (dup2 (pfds[0], maxfd) == maxfd,
       "Using dup2(2) to open fd %d", maxfd);

    if (!(fds = get_open_fds (maxfd)))
        BAIL_OUT ("failed to get open fds");

    ok (fds [pfds[0]] == 1,
       "newly opened fd=%d found on second fdwalk()", pfds[0]);
    ok (fds [pfds[1]] == 1,
       "newly opened fd=%d found on second fdwalk()", pfds[1]);
    ok (fds [maxfd] == 1,
       "newly opened fd=%d found on second fdwalk()", maxfd);

    close (pfds[0]);
    close (pfds[1]);
    close (maxfd);
    free (fds);

    if (!(fds = get_open_fds (maxfd)))
        BAIL_OUT ("failed to get open fds");

    ok (fds [pfds[0]] == 0,
       "closed fd=%d not found on final fdwalk()", pfds[0]);
    ok (fds [pfds[1]] == 0,
       "closed fd=%d not found on final fdwalk()", pfds[1]);
    ok (fds [maxfd] == 0,
       "closed fd=%d not found on final fdwalk()", maxfd);

    free (fds);
    free (openfds);

    test_fdwalk_fallback (maxfd);

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
