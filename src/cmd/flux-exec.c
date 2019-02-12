/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <signal.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libidset/idset.h"
#include "src/common/libutil/log.h"
#include "src/common/libsubprocess/subprocess.h"

static struct optparse_option cmdopts[] = {
    { .name = "rank", .key = 'r', .has_arg = 1, .arginfo = "NODESET",
      .usage = "Specify specific target ranks.  Default is \"all\"" },
    { .name = "dir", .key = 'd', .has_arg = 1, .arginfo = "PATH",
      .usage = "Set the working directory to PATH" },
    { .name = "labelio", .key = 'l', .has_arg = 0,
      .usage = "Label lines of output with the source RANK" },
    { .name = "noinput", .key = 'n', .has_arg = 0,
      .usage = "Redirect stdin from /dev/null" },
    { .name = "verbose", .key = 'v', .has_arg = 0,
      .usage = "Run with more verbosity." },
    OPTPARSE_TABLE_END
};

extern char **environ;

uint32_t rank_count;
uint32_t started = 0;
uint32_t exited = 0;
int exit_code = 0;

zlist_t *subprocesses;

optparse_t *opts = NULL;

int stdin_flags;
flux_watcher_t *stdin_w;

/* time to wait in between SIGINTs */
#define INTERRUPT_MILLISECS 1000.0

struct timespec last;
int sigint_count = 0;

void completion_cb (flux_subprocess_t *p)
{
    int ec;

    if ((ec = flux_subprocess_exit_code (p)) < 0) {
        /* bash standard, signals + 128 */
        if ((ec = flux_subprocess_signaled (p)) > 0)
            ec += 128;
    }
    if (ec > exit_code)
        exit_code = ec;
}

void state_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    if (state == FLUX_SUBPROCESS_RUNNING) {
        started++;
        /* see FLUX_SUBPROCESS_FAILED case below */
        (void)flux_subprocess_aux_set (p, "started", p, NULL);
    }
    else if (state == FLUX_SUBPROCESS_EXITED)
        exited++;
    else if (state == FLUX_SUBPROCESS_EXEC_FAILED) {
        /* EXEC_FAILED means RUNNING never reached, so must increment started */
        started++;
        exited++;
    }
    else if (state == FLUX_SUBPROCESS_FAILED) {
        /* FLUX_SUBPROCESS_FAILED is a catch all error case, no way to
         * know if process started or not.  So we cheat with a
         * subprocess context setting.
         */
        if (flux_subprocess_aux_get (p, "started") == NULL)
            started++;
        exited++;
    }

    if (stdin_w) {
        if (started == rank_count)
            flux_watcher_start (stdin_w);
        if (exited == rank_count)
            flux_watcher_stop (stdin_w);
    }

    if (state == FLUX_SUBPROCESS_EXEC_FAILED
        || state == FLUX_SUBPROCESS_FAILED) {
        int errnum = flux_subprocess_fail_errno (p);
        int ec = 1;

        log_err ("Error: rank %d: %s", flux_subprocess_rank (p), strerror (errnum));

        /* bash standard, 126 for permission/access denied, 127 for
         * command not found.  68 (EX_NOHOST) for No route to host.
         */
        if (errnum == EPERM || errnum == EACCES)
            ec = 126;
        else if (errnum == ENOENT)
            ec = 127;
        else if (errnum == EHOSTUNREACH)
            ec = 68;

        if (ec > exit_code)
            exit_code = ec;
    }
}

void output_cb (flux_subprocess_t *p, const char *stream)
{
    FILE *fstream = !strcasecmp (stream, "STDERR") ? stderr : stdout;
    const char *ptr;
    int lenp;

    if (!(ptr = flux_subprocess_read_line (p, stream, &lenp)))
        log_err_exit ("flux_subprocess_read_line");

    /* if process exited, read remaining stuff or EOF, otherwise
     * wait for future newline */
    if (!lenp
        && flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED) {

        if (!(ptr = flux_subprocess_read (p, stream, -1, &lenp)))
            log_err_exit ("flux_subprocess_read");
    }

    if (lenp) {
        if (optparse_getopt (opts, "labelio", NULL) > 0)
            fprintf (fstream, "%d: ", flux_subprocess_rank (p));
        fwrite (ptr, lenp, 1, fstream);
    }
}

static void stdin_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    flux_buffer_t *fb = flux_buffer_read_watcher_get_buffer (w);
    flux_subprocess_t *p;
    const char *ptr;
    int lenp;

    if (!(ptr = flux_buffer_read (fb, -1, &lenp)))
        log_err_exit ("flux_buffer_read");

    if (lenp) {
        p = zlist_first (subprocesses);
        while (p) {
            if (flux_subprocess_state (p) == FLUX_SUBPROCESS_INIT
                || flux_subprocess_state (p) == FLUX_SUBPROCESS_STARTED
                || flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING) {
                if (flux_subprocess_write (p, "STDIN", ptr, lenp) < 0)
                    log_err_exit ("flux_subprocess_write");
            }
            p = zlist_next (subprocesses);
        }
    }
    else {
        p = zlist_first (subprocesses);
        while (p) {
            if (flux_subprocess_close (p, "STDIN") < 0)
                log_err_exit ("flux_subprocess_close");
            p = zlist_next (subprocesses);
        }
        flux_watcher_stop (stdin_w);
    }
}

static void signal_cb (int signum)
{
    flux_subprocess_t *p = zlist_first (subprocesses);

    if (signum == SIGINT) {
        if (sigint_count >= 2) {
            double since_last = monotime_since (last);
            if (since_last < INTERRUPT_MILLISECS)
                exit (1);
        }
    }

    if (optparse_getopt (opts, "verbose", NULL) > 0)
        fprintf (stderr, "sending signal %d to %d running processes\n",
                 signum, started - exited);

    while (p) {
        if (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING) {
            flux_future_t *f = flux_subprocess_kill (p, signum);
            if (!f) {
                if (optparse_getopt (opts, "verbose", NULL) > 0)
                    fprintf (stderr, "failed to signal rank %d: %s\n",
                             flux_subprocess_rank (p), strerror (errno));
            }
            /* don't care about response */
            flux_future_destroy (f);
        }
        p = zlist_next (subprocesses);
    }

    if (signum == SIGINT) {
        if (sigint_count)
            fprintf (stderr,
                     "interrupt (Ctrl+C) one more time "
                     "within %.2f sec to exit\n",
                     (INTERRUPT_MILLISECS / 1000.0));

        monotime (&last);
        sigint_count++;
    }
}

void subprocess_destroy (void *arg)
{
    flux_subprocess_t *p = arg;
    flux_subprocess_destroy (p);
}

/* atexit handler
 * This is a good faith attempt to restore stdin flags to what they were
 * before we set O_NONBLOCK per bug #1803.
 */
void restore_stdin_flags (void)
{
    (void)fcntl (STDIN_FILENO, F_SETFL, stdin_flags);
}

int main (int argc, char *argv[])
{
    const char *optargp;
    int optindex;
    flux_t *h;
    flux_reactor_t *r;
    struct idset *ns;
    uint32_t rank;
    flux_cmd_t *cmd;
    char *cwd = NULL;
    flux_subprocess_ops_t ops = {
        .on_completion = completion_cb,
        .on_state_change = state_cb,
        .on_channel_out = NULL,
        .on_stdout = output_cb,
        .on_stderr = output_cb,
    };
    struct timespec t0;

    log_init ("flux-exec");

    opts = optparse_create ("flux-exec");
    if (optparse_add_option_table (opts, cmdopts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if ((optindex = optparse_parse_args (opts, argc, argv)) < 0)
        exit (1);

    if (optindex == argc) {
        optparse_print_usage (opts);
        exit (1);
    }

    if (!(cmd = flux_cmd_create (argc - optindex, &argv[optindex], environ)))
        log_err_exit ("flux_cmd_create");

    if (optparse_getopt (opts, "dir", &optargp) > 0) {
        if (!(cwd = strdup (optargp)))
            log_err_exit ("strdup");
    }
    else {
        if (!(cwd = get_current_dir_name ()))
            log_err_exit ("get_current_dir_name");
    }

    if (flux_cmd_setcwd (cmd, cwd) < 0)
        log_err_exit ("flux_cmd_setcwd");

    if (!(h = flux_open (NULL, 0)))
        log_err_exit ("flux_open");

    if (!(r = flux_get_reactor (h)))
        log_err_exit ("flux_get_reactor");

    if (optparse_getopt (opts, "rank", &optargp) > 0
        && strcmp (optargp, "all")) {
        if (!(ns = idset_decode (optargp)))
            log_err_exit ("idset_decode");
        if (flux_get_size (h, &rank_count) < 0)
            log_err_exit ("flux_get_size");
    }
    else {
        if (flux_get_size (h, &rank_count) < 0)
            log_err_exit ("flux_get_size");
        if (!(ns = idset_create (0, IDSET_FLAG_AUTOGROW)))
            log_err_exit ("idset_create");
        if (idset_range_set (ns, 0, rank_count - 1) < 0)
            log_err_exit ("idset_range_set");
    }

    monotime (&t0);
    if (optparse_getopt (opts, "verbose", NULL) > 0) {
        const char *argv0 = flux_cmd_arg (cmd, 0);
        char *nodeset = idset_encode (ns, IDSET_FLAG_RANGE
                                        | IDSET_FLAG_BRACKETS);
        if (!nodeset)
            log_err_exit ("idset_encode");
        fprintf (stderr, "%03fms: Starting %s on %s\n",
                 monotime_since (t0), argv0, nodeset);
        free (nodeset);
    }

    if (!(subprocesses = zlist_new ()))
        log_err_exit ("zlist_new");

    rank = idset_first (ns);
    while (rank != IDSET_INVALID_ID) {
        flux_subprocess_t *p;
        if (!(p = flux_rexec (h, rank, 0, cmd, &ops)))
            log_err_exit ("flux_rexec");
        if (zlist_append (subprocesses, p) < 0)
            log_err_exit ("zlist_append");
        if (!zlist_freefn (subprocesses, p, subprocess_destroy, true))
            log_err_exit ("zlist_freefn");
        rank = idset_next (ns, rank);
    }

    if (optparse_getopt (opts, "verbose", NULL) > 0)
        fprintf (stderr, "%03fms: Sent all requests\n", monotime_since (t0));

    /* -n,--noinput: close subprocess STDIN
     */
    if (optparse_getopt (opts, "noinput", NULL) > 0) {
        flux_subprocess_t *p;
        p = zlist_first (subprocesses);
        while (p) {
            if (flux_subprocess_close (p, "STDIN") < 0)
                log_err_exit ("flux_subprocess_close");
            p = zlist_next (subprocesses);
        }
    }
    /* configure stdin watcher
     */
    else {
        if ((stdin_flags = fcntl (STDIN_FILENO, F_GETFL)) < 0)
            log_err_exit ("fcntl F_GETFL stdin");
        if (atexit (restore_stdin_flags) != 0)
            log_err_exit ("atexit");
        if (fcntl (STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK) < 0)
            log_err_exit ("fcntl F_SETFL stdin");
        if (!(stdin_w = flux_buffer_read_watcher_create (r, STDIN_FILENO,
                                                         1 << 20, stdin_cb,
                                                         0, NULL)))
            log_err_exit ("flux_buffer_read_watcher_create");
    }
    if (signal (SIGINT, signal_cb) == SIG_ERR)
        log_err_exit ("signal");

    if (signal (SIGTERM, signal_cb) == SIG_ERR)
        log_err_exit ("signal");

    if (flux_reactor_run (r, 0) < 0)
        log_err_exit ("flux_reactor_run");

    if (optparse_getopt (opts, "verbose", NULL) > 0)
        fprintf (stderr, "%03fms: %d tasks complete with code %d\n",
                 monotime_since (t0), exited, exit_code);

    /* Clean up.
     */
    idset_destroy (ns);
    free (cwd);
    flux_close (h);
    optparse_destroy (opts);
    log_fini ();
    zlist_destroy (&subprocesses);

    return exit_code;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
