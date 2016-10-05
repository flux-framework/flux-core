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
    ok (p != NULL, "exit_handler: valid subprocess");
    ok (subprocess_get_context (p, "reactor") != NULL,
        "exit_handler: context is set for subprocess");
    ok (subprocess_exited (p), "exit_handler: subprocess exited");
    ok (subprocess_exit_code (p) == 0, "exit_handler: subprocess exited normally");
    diag ("code = %d\n", subprocess_exit_code (p));
    subprocess_destroy (p);
    return (0);
}

int io_cb (struct subprocess *p, const char *json_str)
{
    ok (p != NULL, "io_cb: valid subprocess");
    ok (json_str != NULL, "io_cb: valid output");
    diag ("%s", json_str);
    return (0);
}

int main (int ac, char **av)
{
    int rc;
    struct subprocess_manager *sm;
    struct subprocess *p;
    flux_reactor_t *r;

    zsys_handler_set (NULL);

    plan (NO_PLAN);

    if (!(sm = subprocess_manager_create ()))
        BAIL_OUT ("Failed to create subprocess manager");
    ok (sm != NULL, "create subprocess manager");

    if (!(r = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        BAIL_OUT ("Failed to create a reactor");

    rc = subprocess_manager_set (sm, SM_REACTOR, r);
    ok (rc == 0, "set subprocess manager reactor (rc=%d, %s)", rc, strerror (errno));

    if (!(p = subprocess_create (sm)))
        BAIL_OUT ("Failed to create a subprocess object");
    ok (subprocess_set_context (p, "reactor", r) >= 0, "set subprocess context");
    ok (subprocess_add_hook (p, SUBPROCESS_COMPLETE, exit_handler) >= 0,
        "set subprocess exit handler");
    ok (subprocess_set_io_callback (p, io_cb) >= 0,
        "set subprocess io callback");

    ok (subprocess_set_command (p, "sleep 0.5 && /bin/echo -n 'hello\nworld\n'") >= 0,
        "set subprocess command");
    ok (subprocess_set_environ (p, environ) >= 0,
        "set subprocess environ");

    ok (subprocess_fork (p) >= 0, "subprocess_fork");
    ok (subprocess_exec (p) >= 0, "subprocess_exec");

    ok (flux_reactor_run (r, 0) == 0,
        "reactor returned normally");

    subprocess_manager_destroy (sm);
    flux_reactor_destroy (r);

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */
