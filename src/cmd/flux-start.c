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
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/optparse.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libpmi-server/simple.h"
#include "src/common/libsubprocess/subprocess.h"

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
    struct simple_server *ss;
};

struct client {
    int rank;
    struct subprocess *p;
    struct context *ctx;
    struct simple_client *sc;
};

struct simple_client *simple_client_create (struct simple_server *ss,
                                            struct subprocess *p, int rank);
void simple_client_destroy (struct simple_client *sc);
struct simple_server *simple_server_create (flux_reactor_t *r,
                                            int appnum, int size);
void simple_server_destroy (struct simple_server *ss);

void killer (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg);
int start_session (struct context *ctx, const char *cmd);
char *create_scratch_dir (struct context *ctx);
struct client *client_create (struct context *ctx, int rank, const char *cmd);
void client_destroy (struct client *cli);
char *find_broker (const char *searchpath);

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
    const char *searchpath;

    log_init ("flux-start");

    ctx->opts = optparse_create ("flux-start");
    if (optparse_add_option_table (ctx->opts, opts) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_add_option_table");
    if (optparse_set (ctx->opts, OPTPARSE_USAGE, usage_msg) != OPTPARSE_SUCCESS)
        msg_exit ("optparse_set usage");
    if ((optind = optparse_parse_args (ctx->opts, argc, argv)) < 0)
        exit (1);
    ctx->killer_timeout = strtod (optparse_get_str (ctx->opts, "killer-timeout",
                                                    default_killer_timeout),
                                  NULL);
    if (ctx->killer_timeout < 0.)
        msg_exit ("--killer-timeout argument must be >= 0");
    if (optind < argc) {
        if ((e = argz_create (argv + optind, &command, &len)) != 0)
            errn_exit (e, "argz_create");
        argz_stringify (command, len, ' ');
    }

    if (!(searchpath = getenv ("FLUX_EXEC_PATH")))
        msg_exit ("FLUX_EXEC_PATH is not set");
    if (!(ctx->broker_path = find_broker (searchpath)))
        msg_exit ("Could not locate broker in %s", searchpath);

    ctx->size = optparse_get_int (ctx->opts, "size", default_size);

    status = start_session (ctx, command);

    optparse_destroy (ctx->opts);
    free (ctx->broker_path);
    free (ctx);

    if (command)
        free (command);

    log_fini ();

    return status;
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

int child_report (struct subprocess *p)
{
    struct client *cli = subprocess_get_context (p, "cli");
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

int child_exit (struct subprocess *p)
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

int start_session_exec (struct context *ctx, const char *cmd)
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
        msg ("%s", cpy);
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

    cli->rank = rank;
    cli->ctx = ctx;
    if (!(cli->p = subprocess_create (ctx->sm)))
        goto fail;
    subprocess_set_context (cli->p, "cli", cli);
    subprocess_add_hook (cli->p, SUBPROCESS_COMPLETE, child_exit);
    subprocess_add_hook (cli->p, SUBPROCESS_STATUS, child_report);
    add_arg (cli->p, "%s", ctx->broker_path);
    add_arg (cli->p, "--boot-method=PMI");
    add_arg (cli->p, "--shared-ipc-namespace");
    add_arg (cli->p, "--setattr=scratch-directory=%s", ctx->scratch_dir);
    add_args_list (cli->p, ctx->opts, "broker-opts");
    if (rank == 0 && cmd)
        add_arg (cli->p, "%s", cmd); /* must be last arg */

    subprocess_set_environ (cli->p, environ);
    subprocess_set_context (cli->p, "client", cli);

    return cli;
fail:
    client_destroy (cli);
    return NULL;
}

void client_destroy (struct client *cli)
{
    if (cli) {
        simple_client_destroy (cli->sc);
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
            errn_exit (e, "argz_add");
    argz_stringify (az, az_len, ' ');
    msg ("%d: %s", cli->rank, az);
    free (az);
}

int start_session_pmi (struct context *ctx, const char *cmd)
{
    struct client *cli;
    int appnum = getpid ();
    int rank;

    if (!(ctx->reactor = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
        err_exit ("flux_reactor_create");
    if (!(ctx->timer = flux_timer_watcher_create (ctx->reactor,
                                                  ctx->killer_timeout, 0.,
                                                  killer, ctx)))
        err_exit ("flux_timer_watcher_create");
    if (!(ctx->sm = subprocess_manager_create ()))
        err_exit ("subprocess_manager_create");
    if (subprocess_manager_set (ctx->sm, SM_REACTOR, ctx->reactor) < 0)
        err_exit ("subprocess_manager_set reactor");
    ctx->session_id = xasprintf ("%d", appnum);
    ctx->scratch_dir = create_scratch_dir (ctx);

    ctx->ss = simple_server_create (ctx->reactor, appnum, ctx->size);

    for (rank = 0; rank < ctx->size; rank++) {
        if (!(cli = client_create (ctx, rank, cmd)))
            err_exit ("client_create");
        if (optparse_hasopt (ctx->opts, "verbose"))
            client_dumpargs (cli);
        if (optparse_hasopt (ctx->opts, "noexec")) {
            client_destroy (cli);
            continue;
        }
        cli->sc = simple_client_create (ctx->ss, cli->p, rank);
        if (subprocess_run (cli->p) < 0)
            err_exit ("subprocess_run");
        ctx->count++;
    }
    if (flux_reactor_run (ctx->reactor, 0) < 0)
        err_exit ("flux_reactor_run");

    simple_server_destroy (ctx->ss);

    free (ctx->session_id);
    free (ctx->scratch_dir);

    subprocess_manager_destroy (ctx->sm);
    flux_watcher_destroy (ctx->timer);
    flux_reactor_destroy (ctx->reactor);

    return (ctx->exit_rc);
}

int start_session (struct context *ctx, const char *cmd)
{
    int rc;
    if (ctx->size == 1)
        rc = start_session_exec (ctx, cmd);
    else
        rc = start_session_pmi (ctx, cmd);
    return rc;
}

/**
 ** Simple PMI server implementation using libpmi-server,
 ** which implements the MPICH wire protocol for PMI 1.1.
 **/

struct simple_server {
    zhash_t *kvs;
    int barrier;
    struct pmi_simple_server *ctx;
    int size;
    flux_reactor_t *r;
    int usecount;
};

struct simple_client {
    int fd;
    flux_watcher_t *w;
    char *buf;
    int buflen;
    struct simple_server *ss;
};

int simple_dgetline (int fd, char *buf, int len)
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

int simple_dputline (int fd, const char *buf)
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

int simple_kvs_put_cb (void *arg, const char *kvsname,
                       const char *key, const char *val)
{
    struct simple_server *ss = arg;
    zhash_update (ss->kvs, key, xstrdup (val));
    zhash_freefn (ss->kvs, key, (zhash_free_fn *)free);
    return 0;
}

int simple_kvs_get_cb (void *arg, const char *kvsname,
                       const char *key, char *val, int len)
{
    struct simple_server *ss = arg;
    char *v = zhash_lookup (ss->kvs, key);
    if (!v || strlen (v) >= len)
        return -1;
    strcpy (val, v);
    return 0;
}

int simple_barrier_cb (void *arg)
{
    struct simple_server *ss = arg;
    if (++ss->barrier == ss->size) {
        ss->barrier = 0;
        return 1;
    }
    return 0;
}

struct pmi_simple_ops simple_ops = {
    .kvs_put = simple_kvs_put_cb,
    .kvs_get = simple_kvs_get_cb,
    .barrier = simple_barrier_cb,
};

struct simple_server *simple_server_create (flux_reactor_t *r,
                                            int appnum, int size)
{
    struct simple_server *ss = xzmalloc (sizeof (*ss));

    if (!(ss->kvs = zhash_new()))
        oom ();
    if (!(ss->ctx = pmi_simple_server_create (&simple_ops,
                                              appnum, size, "-", ss)))
        err_exit ("pmi_simple_server_create");
    ss->r = r;
    ss->size = size;
    ss->usecount = 1;
    return ss;
}

struct simple_server *simple_server_ref (struct simple_server *ss)
{
    ss->usecount++;
    return ss;
}

void simple_server_unref (struct simple_server *ss)
{
    if (ss && --ss->usecount == 0) {
        zhash_destroy (&ss->kvs);
        pmi_simple_server_destroy (ss->ctx);
        free (ss);
    }
}

void simple_server_destroy (struct simple_server *ss)
{
    simple_server_unref (ss);
}

/* per-client context */

void simple_request_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    struct simple_client *rsc, *sc = arg;
    struct simple_server *ss = sc->ss;
    int rc;
    char *resp;

    if (simple_dgetline (sc->fd, sc->buf, sc->buflen) < 0)
        err_exit ("%s", __FUNCTION__);
    rc = pmi_simple_server_request (ss->ctx, sc->buf, sc);
    if (rc < 0)
        err_exit ("%s", __FUNCTION__);
    while (pmi_simple_server_response (ss->ctx, &resp, &rsc) == 0) {
        if (simple_dputline (rsc->fd, resp) < 0)
            err_exit ("%s", __FUNCTION__);
        free (resp);
    }
    if (rc == 1) {
        close (sc->fd);
        sc->fd = -1;
        flux_watcher_stop (sc->w);
    }
}


struct simple_client *simple_client_create (struct simple_server *ss,
                                            struct subprocess *p, int rank)
{
    struct simple_client *sc = xzmalloc (sizeof (*sc));
    int client_fd;

    sc->buflen = pmi_simple_server_get_maxrequest (ss->ctx);
    sc->buf = xzmalloc (sc->buflen);

    if ((sc->fd = subprocess_socketpair (p, &client_fd)) < 0)
        err_exit ("subprocess_socketpair");
    if (!(sc->w = flux_fd_watcher_create (ss->r, sc->fd, FLUX_POLLIN,
                                          simple_request_cb, sc)))
        err_exit ("flux_fd_watcher_create");
    flux_watcher_start (sc->w);
    sc->ss = simple_server_ref (ss);

    subprocess_setenvf (p, "PMI_FD", 1, "%d", client_fd);
    subprocess_setenvf (p, "PMI_RANK", 1, "%d", rank);
    subprocess_setenvf (p, "PMI_SIZE", 1, "%d", ss->size);

    return sc;
}

void simple_client_destroy (struct simple_client *sc)
{
    if (sc) {
        flux_watcher_destroy (sc->w);
        if (sc->fd != -1)
            close (sc->fd);
        if (sc->buf)
            free (sc->buf);
        simple_server_unref (sc->ss);
        free (sc);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
