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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <flux/core.h>
#include <signal.h>

#include "src/common/libutil/log.h"
#include "src/common/subprocess/subprocess.h"

extern char **environ;

int exit_code = 0;

void completion_cb (flux_subprocess_t *p)
{
    int ec = flux_subprocess_exit_code (p);
    int sig;

    if (ec > exit_code)
        exit_code = ec;

    if ((sig = flux_subprocess_signaled (p)) < 0)
        log_err_exit ("flux_subprocess_signaled");
    printf ("subprocess terminated by signal %d\n", sig);
}

void signal_result (flux_future_t *f, void *arg)
{
    if (flux_future_get (f, NULL) < 0)
        log_err_exit ("flux_subprocess_kill error");
    flux_future_destroy (f);
}

void state_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    if (state == FLUX_SUBPROCESS_EXEC_FAILED
        || state == FLUX_SUBPROCESS_FAILED) {
        fprintf (stderr, "rank %d: %s: %s\n",
                 flux_subprocess_rank (p),
                 flux_subprocess_state_string (state),
                 strerror (flux_subprocess_fail_errno (p)));

        /* just so we fail non-zero */
        if (!exit_code)
            exit_code++;
    }

    if (state == FLUX_SUBPROCESS_RUNNING) {
        flux_future_t *f;
        if (!(f = flux_subprocess_kill (p, SIGTERM)))
            log_err_exit ("flux_subprocess_kill");
        if (flux_future_then (f, -1., signal_result, p) < 0)
            log_err_exit ("flux_future_then");
    }
}

void io_cb (flux_subprocess_t *p, const char *stream)
{
    const char *ptr;
    int lenp;

    if (strcasecmp (stream, "STDOUT")
        && strcasecmp (stream, "STDERR")) {
        fprintf (stderr, "unexpected stream %s\n", stream);
        exit (1);
    }

    if (!(ptr = flux_subprocess_read (p, stream, -1, &lenp))) {
        perror ("flux_subprocess_read");
        exit (1);
    }
    if (ptr && lenp == 0)
        fprintf (stderr, "stream %s got EOF\n", stream);
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_reactor_t *reactor;
    flux_cmd_t *cmd;
    char *cwd;
    flux_subprocess_t *p = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_cb,
        .on_channel_out = NULL,
        .on_stdout = io_cb,
        .on_stderr = io_cb,
    };

    log_init ("rexec_signal");

    if (!(cmd = flux_cmd_create (argc - 1, &argv[1], environ)))
        log_err_exit ("flux_cmd_create");

    if (!(cwd = get_current_dir_name ()))
        log_err_exit ("get_current_dir_name");

    if (flux_cmd_setcwd (cmd, cwd) < 0)
        log_err_exit ("flux_cmd_setcwd");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(reactor = flux_get_reactor (h)))
        log_err_exit ("flux_get_reactor");

    /* always to rank 1 */
    if (!(p = flux_rexec (h, 1, 0, cmd, &ops)))
        log_err_exit ("flux_rexec");

    if (flux_reactor_run (reactor, 0) < 0)
        log_err_exit ("flux_reactor_run");

    /* Clean up.
     */
    flux_subprocess_destroy (p);
    flux_close (h);
    log_fini ();

    return exit_code;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
