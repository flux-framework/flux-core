/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rexec_count_stdout - predominant purpose is for line buffering
 * tests, will count how many times the stdout callback is called */

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

extern char **environ;

static struct optparse_option cmdopts[] = {
    { .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "rank",
      .usage = "Specify rank for test" },
    { .name = "linebuffer", .key = 'l', .has_arg = 1, .arginfo = "bool",
      .usage = "Specify true/false for line buffering" },
    OPTPARSE_TABLE_END
};

optparse_t *opts;

int stdout_count = 0;
int exit_code = 0;

void completion_cb (flux_subprocess_t *p)
{
    int ec = flux_subprocess_exit_code (p);

    if (ec > exit_code)
        exit_code = ec;
}

void output_cb (flux_subprocess_t *p, const char *stream)
{
    FILE *fstream = !strcasecmp (stream, "STDERR") ? stderr : stdout;
    const char *ptr;
    int lenp;

    /* Do not use flux_subprocess_getline(), testing is against
     * streams that are line buffered and not line buffered */

    if (!(ptr = flux_subprocess_read_line (p, stream, &lenp))) {
        log_err ("flux_subprocess_read_line");
        return;
    }

    /* we're at the end of the stream, read any lingering data */
    if (!lenp && flux_subprocess_read_stream_closed (p, stream) > 0) {
        if (!(ptr = flux_subprocess_read (p, stream, -1, &lenp))) {
            log_err ("flux_subprocess_read");
            return;
        }
    }

    if (lenp)
        fwrite (ptr, lenp, 1, fstream);

    if (!strcasecmp (stream, "STDOUT"))
        stdout_count++;
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
        .on_stderr = output_cb,
    };
    const char *optargp;
    int optindex;
    int rank = 0;

    log_init ("rexec-count-stdout");

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

    if (optparse_getopt (opts, "linebuffer", &optargp) > 0) {
        if (strcasecmp (optargp, "true")
            && strcasecmp (optargp, "false"))
            log_err_exit ("invalid linebuffer value");
        if (flux_cmd_setopt (cmd, "STDOUT_LINE_BUFFER", optargp) < 0)
            log_err_exit ("flux_cmd_setopt");
    }

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(reactor = flux_get_reactor (h)))
        log_err_exit ("flux_get_reactor");

    if (!(p = flux_rexec (h, rank, 0, cmd, &ops)))
        log_err_exit ("flux_rexec");

    if (flux_reactor_run (reactor, 0) < 0)
        log_err_exit ("flux_reactor_run");

    printf ("final stdout callback count: %d\n", stdout_count);
    fflush (stdout);

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
