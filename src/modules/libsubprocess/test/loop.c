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
#include <json.h>
#include <czmq.h>

#include <src/modules/libzio/zio.h>

#include "tap.h"
#include "subprocess.h"

extern char **environ;

static int exit_handler (struct subprocess *p, void *arg)
{
    ok (p != NULL, "exit_handler: valid subprocess");
    ok (arg != NULL, "exit_handler: arg is expected");
    ok (subprocess_exited (p), "exit_handler: subprocess exited");
    ok (subprocess_exit_code (p) == 0, "exit_handler: subprocess exited normally");
    subprocess_destroy (p);
    raise (SIGTERM);
    return (0);
}

static int io_cb (struct subprocess *p, const char *json_str)
{
    ok (p != NULL, "io_cb: valid subprocess");
    ok (json_str != NULL, "io_cb: valid output");
    note ("%s", json_str);
    return (0);
}

static int init_signalfd ()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigaddset(&mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        perror("sigprocmask");
        return 1;
    }

    return signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
}

static int signal_cb (zloop_t *zl, zmq_pollitem_t *item, void *arg)
{
    struct signalfd_siginfo fdsi;
    struct subprocess_manager *sm = arg;

    if (read (item->fd, &fdsi, sizeof (fdsi)) < 0)
        return (-1);

    note ("signal_cb signo = %d", fdsi.ssi_signo);
    if (fdsi.ssi_signo == SIGTERM)
        return (-1);

    ok (fdsi.ssi_signo == SIGCHLD, "got sigchld in signal_cb");
    ok (subprocess_manager_reap_all (sm) >= 0, "reap all children");

    //zloop_poller_end (zl, item);
    return (0);
}

int main (int ac, char **av)
{
    int rc;
    struct subprocess_manager *sm;
    struct subprocess *p;
    zloop_t *zloop;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .socket = NULL };

    zsys_handler_set (NULL);

    plan (NO_PLAN);

    if (!(sm = subprocess_manager_create ()))
        BAIL_OUT ("Failed to create subprocess manager");
    ok (sm != NULL, "create subprocess manager");

    if (!(zloop = zloop_new ()))
        BAIL_OUT ("Failed to create a zloop");

    zp.fd = init_signalfd ();
    ok (zp.fd >= 0, "signalfd created");

    ok (zloop_poller (zloop, &zp, (zloop_fn *) signal_cb, sm) >= 0,
        "Created zloop poller for signalfd");

    rc = subprocess_manager_set (sm, SM_ZLOOP, zloop);
    ok (rc == 0, "set subprocess manager zloop (rc=%d, %s)", rc, strerror (errno));

    if (!(p = subprocess_create (sm)))
        BAIL_OUT ("Failed to create a subprocess object");
    ok (subprocess_set_callback (p, exit_handler, zloop) >= 0,
        "set subprocess exit handler");
    ok (subprocess_set_io_callback (p, io_cb) >= 0,
        "set subprocess io callback");

    ok (subprocess_set_command (p, "sleep 0.5 && /bin/echo -n 'hello\nworld\n'") >= 0,
        "set subprocess command");
    ok (subprocess_set_environ (p, environ) >= 0,
        "set subprocess environ");

    ok (subprocess_fork (p) >= 0, "subprocess_fork");
    ok (subprocess_exec (p) >= 0, "subprocess_exec");

    rc = zloop_start (zloop);

    subprocess_manager_destroy (sm);
    zloop_destroy (&zloop);

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
