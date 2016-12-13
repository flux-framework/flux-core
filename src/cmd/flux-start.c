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
#include <flux/optparse.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/setenvf.h"
#include "src/common/libpmi/simple_server.h"
#include "src/common/libpmi/dgetline.h"
#include "src/common/libsubprocess/subprocess.h"

struct pmi_server {
    zhash_t *kvs;
    struct pmi_simple_server *srv;
};

struct context {
    double killer_timeout;
    flux_reactor_t *reactor;
    flux_watcher_t *timer;
    struct subprocess_manager *sm;
    optparse_t *opts;
    char *session_id;
    char *scratch_dir;
    char *broker_path;
    int size;
    int count;
    int exit_rc;
    struct pmi_server pmi;
};

struct client {
    int rank;
    int fd;
    struct subprocess *p;
    flux_watcher_t *w;
    struct context *ctx;
    char buf[SIMPLE_MAX_PROTO_LINE];
};

void killer (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg);
int start_session (struct context *ctx, const char *cmd);
int exec_broker (struct context *ctx, const char *cmd);
char *create_scratch_dir (struct context *ctx);
struct client *client_create (struct context *ctx, int rank, const char *cmd);
void client_destroy (struct client *cli);
char *find_broker (const char *searchpath);
static void setup_profiling_env (struct context *ctx);

double default_killer_timeout = 1.0;

const int default_size = 1;

#ifndef HAVE_CALIPER
static int no_caliper_fatal_err (optparse_t *p, struct optparse_option *o,
                                 const char *optarg)
{
    log_msg_exit ("Error: --caliper-profile used but no Caliper support found");
}
#endif /* !HAVE_CALIPER */

const char *usage_msg = "[OPTIONS] command ...";
static struct optparse_option opts[] = {
    { .name = "verbose",    .key = 'v', .has_arg = 0,
      .usage = "Be annoyingly informative", },
    { .name = "noexec",     .key = 'X', .has_arg = 0,
      .usage = "Don't execute (useful with -v, --verbose)", },
    { .name = "size",       .key = 's', .has_arg = 1, .arginfo = "N",
      .usage = "Set number of ranks in new instance", },
    { .name = "broker-opts",.key = 'o', .has_arg = 1, .arginfo = "OPTS",
      .flags = OPTPARSE_OPT_AUTOSPLIT,
      .usage = "Add comma-separated broker options, e.g. \"-o,-q\"", },
    { .name = "killer-timeout",.key = 'k', .has_arg = 1, .arginfo = "SECONDS",
      .usage = "After a broker exits, kill other brokers after SECONDS", },
    { .name = "trace-pmi-server", .has_arg = 0, .arginfo = NULL,
      .usage = "Trace pmi simple server protocol exchange", },

/* Option group 1, these options will be listed after those above */
    { .group = 1,
      .name = "caliper-profile", .has_arg = 1,
      .arginfo = "PROFILE",
      .usage = "Enable profiling in brokers using Caliper configuration "
               "profile named `PROFILE'",
#ifndef HAVE_CALIPER
      .cb = no_caliper_fatal_err, /* Emit fatal err if not built w/ Caliper */
#endif /* !HAVE_CALIPER */
    },
    OPTPARSE_TABLE_END,
};

int main (int argc, char *argv[])
{
    int e, status = 0;
    char *command = NULL;
    size_t len = 0;
    struct context *ctx = xzmalloc (sizeof (*ctx));
    const char *searchpath;
    int optindex;

    log_init ("flux-start");

    ctx->opts = optparse_create ("flux-start");
    if (optparse_add_option_table (ctx->opts, opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_add_option_table");
    if (optparse_set (ctx->opts, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        log_msg_exit ("optparse_set usage");
    if ((optindex = optparse_parse_args (ctx->opts, argc, argv)) < 0)
        exit (1);
    ctx->killer_timeout = optparse_get_double (ctx->opts, "killer-timeout",
                                               default_killer_timeout);
    if (ctx->killer_timeout < 0.)
        log_msg_exit ("--killer-timeout argument must be >= 0");
    if (optindex < argc) {
        if ((e = argz_create (argv + optindex, &command, &len)) != 0)
            log_errn_exit (e, "argz_creawte");
        argz_stringify (command, len, ' ');
    }

    if (!(searchpath = getenv ("FLUX_EXEC_PATH")))
        log_msg_exit ("FLUX_EXEC_PATH is not set");
    if (!(ctx->broker_path = find_broker (searchpath)))
        log_msg_exit ("Could not locate broker in %s", searchpath);

    setup_profiling_env (ctx);

    ctx->size = optparse_get_int (ctx->opts, "size", default_size);
    if (ctx->size <= 0)
        log_msg_exit ("--size argument must be >= 0");

    if (ctx->size == 1) {
        status = exec_broker (ctx, command);
    } else {
        status = start_session (ctx, command);
    }

    optparse_destroy (ctx->opts);
    free (ctx->broker_path);
    free (ctx);

    if (command)
        free (command);

    log_fini ();

    return status;
}

static void setup_profiling_env (struct context *ctx)
{
#if HAVE_CALIPER
    const char *profile;
    /*
     *  If --profile was used, set or append libcaliper.so in LD_PRELOAD
     *   to subprocess environment, swapping stub symbols for the actual
     *   libcaliper symbols.
     */
    if (optparse_getopt (ctx->opts, "caliper-profile", &profile) == 1) {
        const char *pl = getenv ("LD_PRELOAD");
        int rc = setenvf ("LD_PRELOAD", 1, "%s%s%s",
                          pl ? pl : "",
                          pl ? " ": "",
                          "libcaliper.so");
        if (rc < 0)
            log_err_exit ("Unable to set LD_PRELOAD in environment");

        if ((profile != NULL) &&
            (setenv ("CALI_CONFIG_PROFILE", profile, 1) < 0))
                log_err_exit ("setenv (CALI_CONFIG_PROFILE)");
        setenv ("CALI_LOG_VERBOSITY", "0", 0);
    }
#endif
}



char *find_broker (const char *searchpath)
{
    char *cpy = xstrdup (searchpath);
    char *dir, *saveptr = NULL, *a1 = cpy;
    char path[PATH_MAX];

    while ((dir = strtok_r (a1, ":", &saveptr))) {
        snprintf (path, sizeof (path), "%s/flux-broker", dir);
        if (access (path, X_OK) == 0)
            break;
        a1 = NULL;
    }
    free (cpy);
    return dir ? xstrdup (path) : NULL;
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

static int child_report (struct subprocess *p)
{
    struct client *cli = subprocess_get_context (p, "cli");
    pid_t pid = subprocess_pid (p);
    int sig;

    if ((sig = subprocess_stopped (p)))
        log_msg ("%d (pid %d) %s", cli->rank, pid, strsignal (sig));
    else if ((subprocess_continued (p)))
        log_msg ("%d (pid %d) %s", cli->rank, pid, strsignal (SIGCONT));
    else if ((sig = subprocess_signaled (p)))
        log_msg ("%d (pid %d) %s", cli->rank, pid, strsignal (sig));
    else if (subprocess_exited (p)) {
        int rc = subprocess_exit_code (p);
        if (rc >= 128)
            log_msg ("%d (pid %d) exited with rc=%d (%s)", cli->rank, pid, rc,
                                                       strsignal (rc - 128));
        else if (rc > 0)
            log_msg ("%d (pid %d) exited with rc=%d", cli->rank, pid, rc);
    } else
        log_msg ("%d (pid %d) status=%d", cli->rank, pid,
                                      subprocess_exit_status (p));
    return 0;
}

static int child_exit (struct subprocess *p)
{
    struct client *cli = subprocess_get_context (p, "cli");
    struct context *ctx = cli->ctx;
    int rc = subprocess_exit_code (p);

    if (ctx->exit_rc < rc)
        ctx->exit_rc = rc;
    if (--ctx->count > 0)
        flux_watcher_start (ctx->timer);
    else
        flux_watcher_stop (ctx->timer);
    client_destroy (cli);
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
        log_err_exit ("subprocess_argv_append");
    free (arg);
}

void add_args_list (struct subprocess *p, optparse_t *opt, const char *name)
{
    const char *arg;
    optparse_getopt_iterator_reset (opt, name);
    while ((arg = optparse_getopt_next (opt, name)))
        if (subprocess_argv_append  (p, arg) < 0)
            log_err_exit ("subprocess_argv_append");
}

char *create_scratch_dir (struct context *ctx)
{
    char *tmpdir = getenv ("TMPDIR");
    char *scratchdir = xasprintf ("%s/flux-%s-XXXXXX",
                                  tmpdir ? tmpdir : "/tmp", ctx->session_id);

    if (!mkdtemp (scratchdir))
        log_err_exit ("mkdtemp %s", scratchdir);
    cleanup_push_string (cleanup_directory, scratchdir);
    return scratchdir;
}

static int pmi_response_send (void *client, const char *buf)
{
    struct client *cli = client;
    return dputline (cli->fd, buf);
}

static void pmi_debug_trace (void *client, const char *buf)
{
    struct client *cli = client;
    fprintf (stderr, "%d: %s", cli->rank, buf);
}

void pmi_simple_cb (flux_reactor_t *r, flux_watcher_t *w,
                    int revents, void *arg)
{
    struct client *cli = arg;
    struct context *ctx = cli->ctx;
    int rc;
    if (dgetline (cli->fd, cli->buf, sizeof (cli->buf)) < 0)
        log_err_exit ("%s", __FUNCTION__);
    rc = pmi_simple_server_request (ctx->pmi.srv, cli->buf, cli);
    if (rc < 0)
        log_err_exit ("%s", __FUNCTION__);
    if (rc == 1) {
        close (cli->fd);
        cli->fd = -1;
        flux_watcher_stop (w);
    }
}

int pmi_kvs_put (void *arg, const char *kvsname,
                 const char *key, const char *val)
{
    struct context *ctx = arg;
    zhash_update (ctx->pmi.kvs, key, xstrdup (val));
    zhash_freefn (ctx->pmi.kvs, key, (zhash_free_fn *)free);
    return 0;
}

int pmi_kvs_get (void *arg, const char *kvsname,
                 const char *key, char *val, int len)
{
    struct context *ctx = arg;
    char *v = zhash_lookup (ctx->pmi.kvs, key);
    if (!v || strlen (v) >= len)
        return -1;
    strcpy (val, v);
    return 0;
}

int execvp_argz (const char *cmd, char *argz, size_t argz_len)
{
    char **av = malloc (sizeof (char *) * (argz_count (argz, argz_len) + 1));
    if (!av) {
        errno = ENOMEM;
        return -1;
    }
    argz_extract (argz, argz_len, av);
    execvp (cmd, av);
    free (av);
    return -1;
}

int exec_broker (struct context *ctx, const char *cmd)
{
    char *argz = NULL;
    size_t argz_len = 0;
    const char *arg;

    if (argz_add (&argz, &argz_len, ctx->broker_path) != 0)
        goto nomem;

    optparse_getopt_iterator_reset (ctx->opts, "broker-opts");
    while ((arg = optparse_getopt_next (ctx->opts, "broker-opts"))) {
        if (argz_add (&argz, &argz_len, arg) != 0)
            goto nomem;
    }
    if (cmd) {
        if (argz_add (&argz, &argz_len, cmd) != 0)
            goto nomem;
    }
    if (optparse_hasopt (ctx->opts, "verbose")) {
        char *cpy = malloc (argz_len);
        if (!cpy)
            goto nomem;
        memcpy (cpy, argz, argz_len);
        argz_stringify (cpy, argz_len, ' ');
        log_msg ("%s", cpy);
        free (cpy);
    }
    if (!optparse_hasopt (ctx->opts, "noexec")) {
        if (execvp_argz (ctx->broker_path, argz, argz_len) < 0)
            goto error;
    }
    free (argz);
    return 0;
nomem:
    errno = ENOMEM;
error:
    free (argz);
    return -1;
}

struct client *client_create (struct context *ctx, int rank, const char *cmd)
{
    struct client *cli = xzmalloc (sizeof (*cli));
    int client_fd;

    cli->rank = rank;
    cli->fd = -1;
    cli->ctx = ctx;
    if (!(cli->p = subprocess_create (ctx->sm)))
        goto fail;
    subprocess_set_context (cli->p, "cli", cli);
    subprocess_add_hook (cli->p, SUBPROCESS_COMPLETE, child_exit);
    subprocess_add_hook (cli->p, SUBPROCESS_STATUS, child_report);
    add_arg (cli->p, "%s", ctx->broker_path);
    add_arg (cli->p, "--shared-ipc-namespace");
    add_arg (cli->p, "--setattr=scratch-directory=%s", ctx->scratch_dir);
    add_args_list (cli->p, ctx->opts, "broker-opts");
    if (rank == 0 && cmd)
        add_arg (cli->p, "%s", cmd); /* must be last arg */

    subprocess_set_environ (cli->p, environ);

    if ((cli->fd = subprocess_socketpair (cli->p, &client_fd)) < 0)
        goto fail;
    subprocess_set_context (cli->p, "client", cli);
    cli->w = flux_fd_watcher_create (ctx->reactor, cli->fd, FLUX_POLLIN,
                                     pmi_simple_cb, cli);
    if (!cli->w)
        goto fail;
    flux_watcher_start (cli->w);
    subprocess_setenvf (cli->p, "PMI_FD", 1, "%d", client_fd);
    subprocess_setenvf (cli->p, "PMI_RANK", 1, "%d", rank);
    subprocess_setenvf (cli->p, "PMI_SIZE", 1, "%d", ctx->size);
    return cli;
fail:
    client_destroy (cli);
    return NULL;
}

void client_destroy (struct client *cli)
{
    if (cli) {
        flux_watcher_destroy (cli->w);
        if (cli->fd != -1)
            close (cli->fd);
        if (cli->p)
            subprocess_destroy (cli->p);
        free (cli);
    }
}

void client_dumpargs (struct client *cli)
{
    int i, argc = subprocess_get_argc (cli->p);
    char *az = NULL;
    size_t az_len = 0;
    int e;

    for (i = 0; i < argc; i++)
        if ((e = argz_add (&az, &az_len, subprocess_get_arg (cli->p, i))) != 0)
            log_errn_exit (e, "argz_add");
    argz_stringify (az, az_len, ' ');
    log_msg ("%d: %s", cli->rank, az);
    free (az);
}

void pmi_server_initialize (struct context *ctx, int flags)
{
    struct pmi_simple_ops ops = {
        .kvs_put = pmi_kvs_put,
        .kvs_get = pmi_kvs_get,
        .barrier_enter = NULL,
        .response_send = pmi_response_send,
        .debug_trace = pmi_debug_trace,
    };
    int appnum = strtol (ctx->session_id, NULL, 10);
    if (!(ctx->pmi.kvs = zhash_new()))
        oom ();
    ctx->pmi.srv = pmi_simple_server_create (&ops, appnum, ctx->size,
                                             ctx->size, "-", flags, ctx);
    if (!ctx->pmi.srv)
        log_err_exit ("pmi_simple_server_create");
}

void pmi_server_finalize (struct context *ctx)
{
    zhash_destroy (&ctx->pmi.kvs);
    pmi_simple_server_destroy (ctx->pmi.srv);
}

int client_run (struct client *cli)
{
    return subprocess_run (cli->p);
}

int start_session (struct context *ctx, const char *cmd)
{
    struct client *cli;
    int rank;
    int flags = 0;

    if (!(ctx->reactor = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        log_err_exit ("flux_reactor_create");
    if (!(ctx->timer = flux_timer_watcher_create (ctx->reactor,
                                                  ctx->killer_timeout, 0.,
                                                  killer, ctx)))
        log_err_exit ("flux_timer_watcher_create");
    if (!(ctx->sm = subprocess_manager_create ()))
        log_err_exit ("subprocess_manager_create");
    if (subprocess_manager_set (ctx->sm, SM_REACTOR, ctx->reactor) < 0)
        log_err_exit ("subprocess_manager_set reactor");
    ctx->session_id = xasprintf ("%d", getpid ());
    ctx->scratch_dir = create_scratch_dir (ctx);

    if (optparse_hasopt (ctx->opts, "trace-pmi-server"))
        flags |= PMI_SIMPLE_SERVER_TRACE;

    pmi_server_initialize (ctx, flags);

    for (rank = 0; rank < ctx->size; rank++) {
        if (!(cli = client_create (ctx, rank, cmd)))
            log_err_exit ("client_create");
        if (optparse_hasopt (ctx->opts, "verbose"))
            client_dumpargs (cli);
        if (optparse_hasopt (ctx->opts, "noexec")) {
            client_destroy (cli);
            continue;
        }
        if (client_run (cli) < 0)
            log_err_exit ("subprocess_run");
        ctx->count++;
    }
    if (flux_reactor_run (ctx->reactor, 0) < 0)
        log_err_exit ("flux_reactor_run");

    pmi_server_finalize (ctx);

    free (ctx->session_id);
    free (ctx->scratch_dir);

    subprocess_manager_destroy (ctx->sm);
    flux_watcher_destroy (ctx->timer);
    flux_reactor_destroy (ctx->reactor);

    return (ctx->exit_rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
