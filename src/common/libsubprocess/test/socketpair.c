/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/signalfd.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libjson-c/json.h"
#include "tap.h"
#include "subprocess.h"

extern char **environ;

static int exit_handler (struct subprocess *p)
{
    ok (subprocess_exited (p), "exit_handler: subprocess exited");
    ok (subprocess_exit_code (p) == 0, "exit_handler: subprocess exited normally");
    subprocess_destroy (p);
    return (0);
}

int fdcount (void)
{
    int fd, fdlimit = sysconf (_SC_OPEN_MAX);
    int count = 0;
    for (fd = 0; fd < fdlimit; fd++) {
        if (fcntl (fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

int main (int ac, char **av)
{
    int rc;
    struct subprocess_manager *sm;
    struct subprocess *p;
    flux_reactor_t *r;
    int parent_fd, child_fd;
    int start_fdcount, end_fdcount;

    plan (NO_PLAN);

    start_fdcount = fdcount ();
    diag ("initial fd count %d", start_fdcount);

    if (!(sm = subprocess_manager_create ()))
        BAIL_OUT ("Failed to create subprocess manager");
    ok (sm != NULL, "create subprocess manager");

    if (!(r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        BAIL_OUT ("Failed to create a reactor");

    errno = 0;
    rc = subprocess_manager_set (sm, SM_REACTOR, r);
    ok (rc == 0, "set subprocess manager reactor (rc=%d, %s)", rc, strerror (errno));

    if (!(p = subprocess_create (sm)))
        BAIL_OUT ("Failed to create a subprocess object");
    ok (subprocess_add_hook (p, SUBPROCESS_COMPLETE, exit_handler) >= 0,
        "set subprocess exit handler");

    child_fd = -1;
    ok ((parent_fd = subprocess_socketpair (p, &child_fd)) >= 0
        && child_fd >= 0,
        "subprocess_socketpair returned valid fd for parent + child");
    diag ("socketpair parent %d child %d", parent_fd, child_fd);
    ok (subprocess_set_environ (p, environ) >= 0,
        "set subprocess environ");
    ok (subprocess_setenvf (p, "FD", 1, "%d", child_fd) >= 0,
        "set FD in subprocess environ");
    // on some shells $FD must be a single digit
    ok (subprocess_set_command (p, "bash -c \"cat <&$FD\"") >= 0,
        "set subprocess command");

    ok (subprocess_fork (p) >= 0, "subprocess_fork");
    ok (subprocess_exec (p) >= 0, "subprocess_exec");

    ok (write (parent_fd, "# hello world\n", 14) == 14,
        "wrote to parent fd");
    close (parent_fd);

    ok (flux_reactor_run (r, 0) == 0,
        "reactor returned normally");

    subprocess_manager_destroy (sm);
    flux_reactor_destroy (r);

    end_fdcount = fdcount ();
    diag ("final fd count %d", end_fdcount);
    ok (start_fdcount == end_fdcount,
        "no file descriptors were leaked");

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
