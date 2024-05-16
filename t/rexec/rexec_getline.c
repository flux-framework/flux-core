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
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <flux/core.h>
#include <flux/optparse.h>

#include "src/common/libsubprocess/subprocess_private.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/read_all.h"
#include "ccan/str/str.h"

extern char **environ;

static struct optparse_option cmdopts[] = {
    { .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "rank",
      .usage = "Specify rank for test" },
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

void stdin2stream (flux_subprocess_t *p, const char *stream)
{
    char *buf = NULL;
    int len;

    if ((len = read_all (STDIN_FILENO, (void **)&buf)) < 0)
        log_err_exit ("read_all");

    if (len) {
        if (flux_subprocess_write (p, stream, buf, len) < 0)
            log_err_exit ("flux_subprocess_write");
    }

    /* do not close for channel, b/c can race w/ data coming back */
    if (streq (stream, "stdin")) {
        if (flux_subprocess_close (p, stream) < 0)
            log_err_exit ("flux_subprocess_close");
    }

    free (buf);
}

void output_cb (flux_subprocess_t *p, const char *stream)
{
    FILE *fstream = streq (stream, "stderr") ? stderr : stdout;
    const char *buf;
    int len;

    if ((len = flux_subprocess_getline (p, stream, &buf)) < 0)
        log_err_exit ("flux_subprocess_getline");
    if (len)
        fwrite (buf, len, 1, fstream);
    else
        fprintf (fstream, "EOF\n");
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
        .on_state_change = NULL,
        .on_channel_out = NULL,
        .on_stdout = output_cb,
        .on_stderr = NULL,
    };
    const char *optargp;
    int optindex;
    int rank = 0;

    log_init ("rexec-until-eof");

    opts = optparse_create ("rexec-until-eof");
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
        if (!streq (optargp, "stdin")
            && !streq (optargp, "stdout")
            && !streq (optargp, "stderr")) {
            if (flux_cmd_add_channel (cmd, optargp) < 0)
                log_err_exit ("flux_cmd_add_channel");
            ops.on_channel_out = subprocess_standard_output;
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
