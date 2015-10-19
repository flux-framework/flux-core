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
#include <sys/time.h>
#include <sys/resource.h>
#include <libgen.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/optparse.h"
#include "src/common/libutil/cleanup.h"
#include "src/modules/libsubprocess/subprocess.h"

struct context {
    flux_reactor_t *reactor;
    flux_watcher_t *timer;
    struct subprocess_manager *sm;
    optparse_t *opts;
    char *session_id;
    char *socket_dir;
    const char *broker_path;
    int size;
    int count;
    int exit_rc;
};

void killer (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg);
int start_direct (struct context *ctx, const char *cmd);
void remove_corelimit (void);
char *create_socket_dir (struct context *ctx);

const char *default_killer_timeout = "1.0";

const int default_size = 1;

const char *usage_msg = "[OPTIONS] command ...";
static struct optparse_option opts[] = {
    { .name = "verbose",    .key = 'v', .has_arg = 0,
      .usage = "Be annoyingly informative", },
    { .name = "noexec",     .key = 'X', .has_arg = 0,
      .usage = "Don't execute (useful with -v, --verbose)", },
    { .name = "size",       .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Set number of ranks in new instance", },
    { .name = "broker-opts",.key = 'o', .has_arg = 3, .arginfo = "OPTS",
      .usage = "Add comma-separated broker options, e.g. \"-o,-q\"", },
    { .name = "killer-timeout",.key = 'k', .has_arg = 1, .arginfo = "SECONDS",
      .usage = "After a broker exits, kill other brokers after SECONDS", },
    OPTPARSE_TABLE_END,
};

int main (int argc, char *argv[])
{
    int e, status = 0;
    char *command = NULL;
    size_t len = 0;
    struct context *ctx = xzmalloc (sizeof (*ctx));
    double killer_timeout;

    log_init ("flux-start");

    ctx->opts = optparse_create ("flux-start");
    if (optparse_add_option_table (ctx->opts, opts) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option_table");
    if (optparse_set (ctx->opts, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_set usage");
    if ((optind = optparse_parse_args (ctx->opts, argc, argv)) < 0)
        exit (1);
    if (optind < argc) {
        if ((e = argz_create (argv + optind, &command, &len)) != 0)
            errn_exit (e, "argz_creawte");
        argz_stringify (command, len, ' ');
    }
    remove_corelimit ();

    if (!(ctx->broker_path = getenv ("FLUX_BROKER_PATH")))
        msg_exit ("FLUX_BROKER_PATH is not set");

    ctx->size = optparse_get_int (ctx->opts, "size", default_size);
    if (!(ctx->reactor = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        err_exit ("flux_reactor_create");
    killer_timeout = strtod (optparse_get_str (ctx->opts, "killer-timeout",
                                               default_killer_timeout), NULL);
    if (killer_timeout < 0.)
        msg_exit ("--killer-timeout argument must be >= 0");
    if (!(ctx->timer = flux_timer_watcher_create (ctx->reactor,
                                                  killer_timeout, 0.,
                                                  killer, ctx)))
        err_exit ("flux_timer_watcher_create");
    if (!(ctx->sm = subprocess_manager_create ()))
        err_exit ("subprocess_manager_create");
    if (subprocess_manager_set (ctx->sm, SM_REACTOR, ctx->reactor) < 0)
        err_exit ("subprocess_manager_set reactor");
    ctx->session_id = xasprintf ("%d", getpid ());
    ctx->socket_dir = create_socket_dir (ctx);

    status = start_direct (ctx, command);

    optparse_destroy (ctx->opts);
    flux_watcher_destroy (ctx->timer);
    flux_reactor_destroy (ctx->reactor);
    subprocess_manager_destroy (ctx->sm);
    free (ctx->session_id);
    free (ctx->socket_dir);
    if (command)
        free (command);

    log_fini ();

    return status;
}

void remove_corelimit (void)
{
    struct rlimit rl;
    rl.rlim_cur = RLIM_INFINITY;
    rl.rlim_max = RLIM_INFINITY;
    if (setrlimit (RLIMIT_CORE, &rl) < 0)
        err ("setrlimit: could not remove core file size limit");
}

void killer (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct context *ctx = arg;
    struct subprocess *p;

    p = subprocess_manager_first (ctx->sm);
    while (p) {
        if (subprocess_pid (p))
            (void)subprocess_kill (p, SIGKILL);
        p = subprocess_manager_next (ctx->sm);
    }
}

static int child_report (struct subprocess *p, void *arg)
{
    char *rankstr = subprocess_get_context (p, "rank");
    pid_t pid = subprocess_pid (p);
    int sig;

    if ((sig = subprocess_stopped (p)))
        msg ("%s (pid %d) %s", rankstr, pid, strsignal (sig));
    else if ((subprocess_continued (p)))
        msg ("%s (pid %d) %s", rankstr, pid, strsignal (SIGCONT));
    else if ((sig = subprocess_signaled (p)))
        msg ("%s (pid %d) %s", rankstr, pid, strsignal (sig));
    else if (subprocess_exited (p)) {
        int rc = subprocess_exit_code (p);
        if (rc != 0)
            msg ("%s (pid %d) exited with rc=%d", rankstr, pid, rc);
    } else
        msg ("%s (pid %d) status=%d", rankstr, pid, subprocess_exit_status (p));
    return 0;
}

static int child_exit (struct subprocess *p, void *arg)
{
    struct context *ctx = arg;
    char *rankstr = subprocess_get_context (p, "rank");
    int rc = subprocess_exit_code (p);

    if (ctx->exit_rc < rc)
        ctx->exit_rc = rc;
    if (--ctx->count > 0)
        flux_watcher_start (ctx->timer);
    else
        flux_watcher_stop (ctx->timer);
    subprocess_destroy (p);
    free (rankstr);
    return 0;
}

void add_arg (struct subprocess *p, const char *fmt, ...)
{
    va_list ap;
    char *arg;

    va_start (ap, fmt);
    arg = xvasprintf (fmt, ap);
    va_end (ap);
    if (subprocess_argv_append (p, arg) < 0)
        err_exit ("subprocess_argv_append");
    free (arg);
}

void add_args_list (struct subprocess *p, optparse_t *opt, const char *name)
{
    const char *arg;
    optparse_getopt_iterator_reset (opt, name);
    while ((arg = optparse_getopt_next (opt, name)))
        if (subprocess_argv_append  (p, arg) < 0)
            err_exit ("subprocess_argv_append");
}

char *args_str (struct subprocess *p)
{
    int i, argc = subprocess_get_argc (p);
    char *az = NULL;
    size_t az_len = 0;
    int e;

    for (i = 0; i < argc; i++)
        if ((e = argz_add (&az, &az_len, subprocess_get_arg (p, i))) != 0)
            errn_exit (e, "argz_add");
    argz_stringify (az, az_len, ' ');
    return az;
}

char *create_socket_dir (struct context *ctx)
{
    char *tmpdir = getenv ("TMPDIR");
    char *sockdir = xasprintf ("%s/flux-%s-XXXXXX",
                               tmpdir ? tmpdir : "/tmp", ctx->session_id);

    if (!mkdtemp (sockdir))
        err_exit ("mkdtemp %s", sockdir);
    cleanup_push_string (cleanup_directory, sockdir);
    return sockdir;
}

int start_direct (struct context *ctx, const char *cmd)
{
    struct subprocess *p;
    int rank;

    for (rank = 0; rank < ctx->size; rank++) {
        if (!(p = subprocess_create (ctx->sm)))
            err_exit ("subprocess_create");
        subprocess_set_context (p, "rank", xasprintf ("%d", rank));
        subprocess_set_callback (p, child_exit, ctx);
        subprocess_set_status_callback (p, child_report, ctx);

        add_arg (p, "%s", ctx->broker_path);
        add_arg (p, "--boot-method=LOCAL");
        add_arg (p, "--size=%d", ctx->size);
        add_arg (p, "--rank=%d", rank);
        add_arg (p, "--sid=%s", ctx->session_id);
        add_arg (p, "--socket-directory=%s", ctx->socket_dir);
        add_args_list (p, ctx->opts, "broker-opts");
        if (rank == 0 && cmd)
            add_arg (p, "%s", cmd); /* must be last */
        subprocess_set_environ (p, environ);
        if (optparse_hasopt (ctx->opts, "verbose")) {
            char *s = args_str (p);
            msg ("%d: %s", rank, s);
            free (s);
        }
        if (optparse_hasopt (ctx->opts, "noexec")) {
            char *rankstr = subprocess_get_context (p, "rank");
            free (rankstr);
            subprocess_destroy (p);
        } else if (subprocess_run (p) < 0)
            err_exit ("subprocess_run");
        ctx->count++;
    }
    if (flux_reactor_run (ctx->reactor, 0) < 0)
        err_exit ("flux_reactor_run");

    return (ctx->exit_rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
