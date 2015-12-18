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
#include "src/common/libpmi-server/simple.h"
#include "src/common/libsubprocess/subprocess.h"

struct pmi_server {
    zhash_t *kvs;
    int barrier;
    struct pmi_simple_server *srv;
};

struct context {
    flux_reactor_t *reactor;
    flux_watcher_t *timer;
    struct subprocess_manager *sm;
    optparse_t *opts;
    char *session_id;
    char *scratch_dir;
    const char *broker_path;
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
    char *buf;
    int buflen;
};

void killer (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg);
int start_pmi (struct context *ctx, const char *cmd);
void remove_corelimit (void);
char *create_scratch_dir (struct context *ctx);
struct client *client_create (struct context *ctx, int rank, const char *cmd);
void client_destroy (struct client *cli);

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
    ctx->scratch_dir = create_scratch_dir (ctx);

    status = start_pmi (ctx, command);

    optparse_destroy (ctx->opts);
    flux_watcher_destroy (ctx->timer);
    flux_reactor_destroy (ctx->reactor);
    subprocess_manager_destroy (ctx->sm);
    free (ctx->session_id);
    free (ctx->scratch_dir);
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
    struct client *cli = arg;
    pid_t pid = subprocess_pid (p);
    int sig;

    if ((sig = subprocess_stopped (p)))
        msg ("%d (pid %d) %s", cli->rank, pid, strsignal (sig));
    else if ((subprocess_continued (p)))
        msg ("%d (pid %d) %s", cli->rank, pid, strsignal (SIGCONT));
    else if ((sig = subprocess_signaled (p)))
        msg ("%d (pid %d) %s", cli->rank, pid, strsignal (sig));
    else if (subprocess_exited (p)) {
        int rc = subprocess_exit_code (p);
        if (rc >= 128)
            msg ("%d (pid %d) exited with rc=%d (%s)", cli->rank, pid, rc,
                                                       strsignal (rc - 128));
        else if (rc > 0)
            msg ("%d (pid %d) exited with rc=%d", cli->rank, pid, rc);
    } else
        msg ("%d (pid %d) status=%d", cli->rank, pid,
                                      subprocess_exit_status (p));
    return 0;
}

static int child_exit (struct subprocess *p, void *arg)
{
    struct client *cli = arg;
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

char *create_scratch_dir (struct context *ctx)
{
    char *tmpdir = getenv ("TMPDIR");
    char *scratchdir = xasprintf ("%s/flux-%s-XXXXXX",
                                  tmpdir ? tmpdir : "/tmp", ctx->session_id);

    if (!mkdtemp (scratchdir))
        err_exit ("mkdtemp %s", scratchdir);
    cleanup_push_string (cleanup_directory, scratchdir);
    return scratchdir;
}

static int dgetline (int fd, char *buf, int len)
{
    int i = 0;
    while (i < len - 1) {
        if (read (fd, &buf[i], 1) <= 0)
            return -1;
        if (buf[i] == '\n')
            break;
        i++;
    }
    if (buf[i] != '\n') {
        errno = EPROTO;
        return -1;
    }
    buf[i] = '\0';
    return 0;
}

static int dputline (int fd, const char *buf)
{
    int len = strlen (buf);
    int n, count = 0;
    while (count < len) {
        if ((n = write (fd, buf + count, len - count)) < 0)
            return n;
        count += n;
    }
    return count;
}

void pmi_simple_cb (flux_reactor_t *r, flux_watcher_t *w,
                    int revents, void *arg)
{
    struct client *rcli, *cli = arg;
    struct context *ctx = cli->ctx;
    int rc;
    char *resp;
    if (dgetline (cli->fd, cli->buf, cli->buflen) < 0)
        err_exit ("%s", __FUNCTION__);
    rc = pmi_simple_server_request (ctx->pmi.srv, cli->buf, cli);
    if (rc < 0)
        err_exit ("%s", __FUNCTION__);
    while (pmi_simple_server_response (ctx->pmi.srv, &resp, &rcli) == 0) {
        if (dputline (rcli->fd, resp) < 0)
            err_exit ("%s", __FUNCTION__);
        free (resp);
    }
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

int pmi_barrier (void *arg)
{
    struct context *ctx = arg;
    if (++ctx->pmi.barrier == ctx->size) {
        ctx->pmi.barrier = 0;
        return 1;
    }
    return 0;
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
    cli->buflen = pmi_simple_server_get_maxrequest (ctx->pmi.srv);
    cli->buf = xzmalloc (cli->buflen);
    subprocess_set_callback (cli->p, child_exit, cli);
    subprocess_set_status_callback (cli->p, child_report, cli);
    add_arg (cli->p, "%s", ctx->broker_path);
    add_arg (cli->p, "--boot-method=PMI");
    add_arg (cli->p, "--shared-ipc-namespace");
    add_arg (cli->p, "--scratch-directory=%s", ctx->scratch_dir);
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
        if (cli->buf)
            free (cli->buf);
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
            errn_exit (e, "argz_add");
    argz_stringify (az, az_len, ' ');
    msg ("%d: %s", cli->rank, az);
    free (az);
}

void pmi_server_initialize (struct context *ctx)
{
    struct pmi_simple_ops ops = {
        .kvs_put = pmi_kvs_put,
        .kvs_get = pmi_kvs_get,
        .barrier = pmi_barrier,
    };
    int appnum = strtol (ctx->session_id, NULL, 10);
    if (!(ctx->pmi.kvs = zhash_new()))
        oom ();
    ctx->pmi.srv = pmi_simple_server_create (&ops, appnum, ctx->size, "-", ctx);
    if (!ctx->pmi.srv)
        err_exit ("pmi_simple_server_create");
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

int start_pmi (struct context *ctx, const char *cmd)
{
    struct client *cli;
    int rank;

    pmi_server_initialize (ctx);

    for (rank = 0; rank < ctx->size; rank++) {
        if (!(cli = client_create (ctx, rank, cmd)))
            err_exit ("client_create");
        if (optparse_hasopt (ctx->opts, "verbose"))
            client_dumpargs (cli);
        if (optparse_hasopt (ctx->opts, "noexec")) {
            client_destroy (cli);
            continue;
        }
        if (client_run (cli) < 0)
            err_exit ("subprocess_run");
        ctx->count++;
    }
    if (flux_reactor_run (ctx->reactor, 0) < 0)
        err_exit ("flux_reactor_run");

    pmi_server_finalize (ctx);

    return (ctx->exit_rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
