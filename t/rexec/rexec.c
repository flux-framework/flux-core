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
#include <flux/optparse.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "src/common/subprocess/subprocess.h"

extern char **environ;

static struct optparse_option cmdopts[] = {
    { .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "rank",
      .usage = "Specify rank for test" },
    { .name = "outputstates", .key = 's', .has_arg = 0, .arginfo = "NONE",
      .usage = "Output state changes as they occur" },
    { .name = "stdin2stream", .key = 'i', .has_arg = 1, .arginfo = "CHANNEL",
      .usage = "Read in stdin and forward to subprocess channel" },
    OPTPARSE_TABLE_END
};

optparse_t *opts;

int exit_code = 0;

void completion_cb (flux_subprocess_t *p)
{
    int ec = flux_subprocess_exit_code (p);

    if (ec > exit_code)
        exit_code = ec;
}

void state_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    if (optparse_getopt (opts, "outputstates", NULL) > 0)
        printf ("%s\n", flux_subprocess_state_string (state));

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
}

void stdin2stream (flux_subprocess_t *p, const char *stream)
{
    char *buf = NULL;
    int tmp, len;

    if ((len = read_all (STDIN_FILENO, (void **)&buf)) < 0)
        log_err_exit ("read_all");

    if (len) {
        if ((tmp = flux_subprocess_write (p, stream, buf, len)) < 0)
            log_err_exit ("flux_subprocess_write");

        if (tmp != len)
            log_err_exit ("overflow in write");
    }

    /* do not close for channel, b/c can race w/ data coming back */
    if (!strcmp (stream, "STDIN")) {
        if (flux_subprocess_close (p, stream) < 0)
            log_err_exit ("flux_subprocess_close");
    }

    free (buf);
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
        .on_stdout = flux_subprocess_output,
        .on_stderr = flux_subprocess_output,
    };
    const char *optargp;
    int optindex;
    int rank = 0;

    log_init ("rexec");

    opts = optparse_create ("rexec");
    if (optparse_add_option_table (opts, cmdopts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if ((optindex = optparse_parse_args (opts, argc, argv)) < 0)
        exit (1);

    if (optparse_getopt (opts, "rank", &optargp) > 0)
        rank = atoi (optargp);

    if (optindex == argc) {
        optparse_print_usage (opts);
        exit (1);
    }

    /* all args to cmd */
    if (!(cmd = flux_cmd_create (argc - optindex, &argv[optindex], environ)))
        log_err_exit ("flux_cmd_create");

    if (!(cwd = get_current_dir_name ()))
        log_err_exit ("get_current_dir_name");

    if (flux_cmd_setcwd (cmd, cwd) < 0)
        log_err_exit ("flux_cmd_setcwd");

    if (optparse_getopt (opts, "stdin2stream", &optargp) > 0) {
        if (strcmp (optargp, "STDIN")
            && strcmp (optargp, "STDOUT")
            && strcmp (optargp, "STDERR")) {
            if (flux_cmd_add_channel (cmd, optargp) < 0)
                log_err_exit ("flux_cmd_add_channel");
            ops.on_channel_out = flux_subprocess_output;
        }
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(reactor = flux_get_reactor (h)))
        log_err_exit ("flux_get_reactor");

    if (!(p = flux_rexec (h, rank, 0, cmd, &ops)))
        log_err_exit ("flux_rexec");

    if (optparse_getopt (opts, "stdin2stream", &optargp) > 0)
        stdin2stream (p, optargp);

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
