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
#if HAVE_LIBPMIX
#include <pmix_server.h>
#include <pthread.h>
#endif

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/optparse.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/nodeset.h"
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
    struct x_server *xs;
};

struct client {
    int rank;
    struct subprocess *p;
    struct context *ctx;
    struct simple_client *sc;
    struct x_client *xc;
};

enum {
    X_FLAG_VERBOSE      = 0x0001,
    X_FLAG_SOCKETPAIR   = 0x0004,   /* PMIx uses socketpair */
};

struct simple_client *simple_client_create (struct simple_server *ss,
                                            struct subprocess *p, int rank);
void simple_client_destroy (struct simple_client *sc);
struct simple_server *simple_server_create (flux_reactor_t *r,
                                            int appnum, int size);
void simple_server_destroy (struct simple_server *ss);

#if HAVE_LIBPMIX
struct x_client *x_client_create (struct x_server *xs,
                                  struct subprocess *p, int rank);
void x_client_destroy (struct x_client *sc);

struct x_server *x_server_create (flux_reactor_t *r, const char *scratchdir,
                                  int appnum, int size,
                                  int flags);
void x_server_destroy (struct x_server *x);
#endif

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
#if HAVE_LIBPMIX
    { .name = "pmix",       .key = 'x', .has_arg = 0,
      .usage = "use OpenMPI-style bootstrap instead of MPICH-style", },
    { .name = "pmix-socketpair", .key = 'P', .has_arg = 0,
      .usage = "Use experimental PMIx socketpair mode", },
#endif
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
#if HAVE_LIBPMIX
    if (!optparse_hasopt (ctx->opts, "pmix")
                && optparse_hasopt (ctx->opts, "pmix-socketpair"))
        msg_exit ("Use --pmix with --pmix-socketpair");
#endif

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
#if HAVE_LIBPMIX
        x_client_destroy (cli->xc);
#endif
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

#if HAVE_LIBPMIX
    if (optparse_hasopt (ctx->opts, "pmix")) {
        int flags = 0;
        if (optparse_hasopt (ctx->opts, "verbose"))
            flags |= X_FLAG_VERBOSE;
        if (optparse_hasopt (ctx->opts, "pmix-socketpair"))
            flags |= X_FLAG_SOCKETPAIR;

        ctx->xs = x_server_create (ctx->reactor, ctx->scratch_dir,
                                   appnum, ctx->size, flags);
    } else
#endif
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
#if HAVE_LIBPMIX
        if (optparse_hasopt (ctx->opts, "pmix")) {
            cli->xc = x_client_create (ctx->xs, cli->p, rank);
        } else
#endif
            cli->sc = simple_client_create (ctx->ss, cli->p, rank);
        if (subprocess_run (cli->p) < 0)
            err_exit ("error starting subprocess");
        ctx->count++;
    }

    if (flux_reactor_run (ctx->reactor, 0) < 0)
        err_exit ("flux_reactor_run");

    simple_server_destroy (ctx->ss);
#if HAVE_LIBPMIX
    x_server_destroy (ctx->xs);
#endif

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
    bool use_pmi = false;

    if (ctx->size > 1)
        use_pmi = true;
#if HAVE_LIBPMIX
    if (optparse_hasopt (ctx->opts, "pmix"))
        use_pmi = true;
#endif

    if (use_pmi)
        rc = start_session_pmi (ctx, cmd);
    else
        rc = start_session_exec (ctx, cmd);
    return rc;
}

/**
 ** Simple PMI server implementation using libpmi-server,
 ** which implements the MPICH wire protocol for PMI 1.1.
        rc = start_session_exec (ctx, cmd);
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

void simple_server_unref (struct simple_server *ss)
{
    if (ss && --ss->usecount == 0) {
        zhash_destroy (&ss->kvs);
        pmi_simple_server_destroy (ss->ctx);
        free (ss);
    }
}

struct simple_server *simple_server_ref (struct simple_server *ss)
{
    ss->usecount++;
    return ss;
}

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

    return simple_server_ref (ss);
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


#if HAVE_LIBPMIX
/**
 ** PMIx server implementation
 **/

struct synchro {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    volatile pmix_status_t status;
    volatile int valid;
};

struct nspace {
    char name[PMIX_MAX_NSLEN];
    int len;
    pmix_info_t *info;
};

struct x_server {
    int listen_fd;
    pmix_connection_cbfunc_t connect_cb;
    flux_watcher_t *listen_w;
    int num_clients;
    zhash_t *kvs;
    int barrier;
    struct pmi_simple_server *ctx;
    int size;
    flux_reactor_t *r;
    int usecount;
    uid_t uid;
    gid_t gid;
    struct nspace nspace;
    struct synchro sync;
    int flags;
};

struct x_client {
    pmix_proc_t proc;
    struct x_server *xs;
    struct synchro sync;
};

struct x_server *x_global_state = NULL;


/* Some PMIx server function calls are asynchronous, with completion
 * status returned to a pmix_op_cbfunc_t callback made in PMIx server
 * thread context.  The 'synchro' mini-class provides a way for the main
 * thread to synchronously obtain this status.
 */

void synchro_init (struct synchro *x)
{
    pthread_mutex_init (&x->lock, NULL);
    pthread_cond_init (&x->cond, NULL);
    x->valid = 0;
}

void synchro_clear (struct synchro *x)
{
    pthread_mutex_lock (&x->lock);
    x->valid = 0;
    pthread_mutex_unlock (&x->lock);
}

/* pmix_op_cbfunc_t compatible */
void synchro_signal (pmix_status_t status, void *cbdata)
{
    struct synchro *x = cbdata;

    pthread_mutex_lock (&x->lock);
    x->status = status;
    x->valid = 1;
    pthread_cond_signal (&x->cond);
    pthread_mutex_unlock (&x->lock);
}

pmix_status_t synchro_wait (struct synchro *x)
{
    pmix_status_t rc = PMIX_ERROR;

    pthread_mutex_lock (&x->lock);
    while (!x->valid)
        pthread_cond_wait (&x->cond, &x->lock);
    rc = x->status;
    pthread_mutex_unlock (&x->lock);

    return rc;
}

pmix_status_t x_client_connected (const pmix_proc_t *proc, void *server_object)
{
    struct x_server *xs = x_global_state;

    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s: rank=%d nspace=%s", __FUNCTION__, proc->rank, proc->nspace);
    return PMIX_SUCCESS;
}

pmix_status_t x_client_finalized (const pmix_proc_t *proc, void *server_object,
                                  pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s: rank=%d nspace=%s", __FUNCTION__, proc->rank, proc->nspace);
    if (cbfunc)
        cbfunc (PMIX_SUCCESS, cbdata);
    return PMIX_SUCCESS;
}

pmix_status_t x_abort (const pmix_proc_t *proc, void *server_object,
                       int status, const char xmsg[],
                       pmix_proc_t procs[], size_t nprocs,
                       pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    msg ("%s: rank=%d nspace=%s", __FUNCTION__, proc->rank, proc->nspace);
    if (cbfunc)
        cbfunc (PMIX_SUCCESS, cbdata);
    return PMIX_SUCCESS;
}

/* PMIx will gather data from clients and aggregate into 'data'.
 * Since all procs are local in this instance, we simply make the 'cbfunc'
 * call with what was passed to us and we're done.
 */
pmix_status_t x_fence_nb (const pmix_proc_t procs[], size_t nprocs,
                          const pmix_info_t info[], size_t ninfo,
                          char *data, size_t ndata,
                          pmix_modex_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    assert (nprocs == 1);
    assert (procs[0].rank == PMIX_RANK_WILDCARD); /* all ranks participating */
    if (cbfunc)
        cbfunc (PMIX_SUCCESS, data, ndata, cbdata, NULL, NULL);
    return PMIX_SUCCESS;
}

pmix_status_t x_direct_modex (const pmix_proc_t *proc,
                              const pmix_info_t info[], size_t ninfo,
                              pmix_modex_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    return PMIX_SUCCESS;
}

pmix_status_t x_publish (const pmix_proc_t *proc,
                         const pmix_info_t info[], size_t ninfo,
                         pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    return PMIX_SUCCESS;
}

pmix_status_t x_lookup (const pmix_proc_t *proc, char **keys,
                        const pmix_info_t info[], size_t ninfo,
                        pmix_lookup_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    return PMIX_SUCCESS;
}

pmix_status_t x_unpublish (const pmix_proc_t *proc, char **keys,
                           const pmix_info_t info[], size_t ninfo,
                           pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    return PMIX_SUCCESS;
}

pmix_status_t x_spawn (const pmix_proc_t *proc,
                       const pmix_info_t job_info[], size_t ninfo,
                       const pmix_app_t apps[], size_t napps,
                       pmix_spawn_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    return PMIX_SUCCESS;
}

pmix_status_t x_connect (const pmix_proc_t procs[], size_t nprocs,
                         const pmix_info_t info[], size_t ninfo,
                         pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    return PMIX_SUCCESS;
}

pmix_status_t x_disconnect (const pmix_proc_t procs[], size_t nprocs,
                            const pmix_info_t info[], size_t ninfo,
                            pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    return PMIX_SUCCESS;
}

pmix_status_t x_register_events (const pmix_info_t info[], size_t ninfo,
                                 pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    return PMIX_SUCCESS;
}

pmix_status_t x_deregister_events (const pmix_info_t info[], size_t ninfo,
                                   pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    return PMIX_SUCCESS;
}

/* Called in flux-start thread context, via PMIx_server_init().
 */
pmix_status_t x_listener (int listening_sd, pmix_connection_cbfunc_t cbfunc)
{
    struct x_server *xs = x_global_state;
    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("%s", __FUNCTION__);
    xs->listen_fd = listening_sd;
    xs->connect_cb  = cbfunc;
    return PMIX_SUCCESS;
}

pmix_server_module_t x_ops = {
    .client_connected   = x_client_connected,
    .client_finalized   = x_client_finalized,
    .abort              = x_abort,
    .fence_nb           = x_fence_nb,
    .direct_modex       = x_direct_modex,
    .publish            = x_publish,
    .lookup             = x_lookup,
    .unpublish          = x_unpublish,
    .spawn              = x_spawn,
    .connect            = x_connect,
    .disconnect         = x_disconnect,
    .register_events    = x_register_events,
    .deregister_events  = x_deregister_events,
    .listener           = x_listener,
};

void nspace_init (struct nspace *nspace, const char *name, int size, int appnum)
{
    nodeset_t *ns;
    pmix_status_t rc;

    if (!(ns = nodeset_create_range (0, size - 1)))
        msg_exit ("nodeset_create_range failed");
    nodeset_config_brackets (ns, false);
    nodeset_config_ranges (ns, false);

    assert (strlen (name) < sizeof (nspace->name) - 1);
    strncpy (nspace->name, name, sizeof (nspace->name));
    nspace->len = 8;
    nspace->info = xzmalloc (sizeof (nspace->info[0]) * nspace->len);

    strncpy (nspace->info[0].key, PMIX_UNIV_SIZE, PMIX_MAX_KEYLEN);
    nspace->info[0].value.type = PMIX_UINT32;
    nspace->info[0].value.data.uint32 = size;

    strncpy (nspace->info[1].key, PMIX_SPAWNED, PMIX_MAX_KEYLEN);
    nspace->info[1].value.type = PMIX_UINT32;
    nspace->info[1].value.data.uint32 = 0;

    strncpy (nspace->info[2].key, PMIX_LOCAL_SIZE, PMIX_MAX_KEYLEN);
    nspace->info[2].value.type = PMIX_UINT32;
    nspace->info[2].value.data.uint32 = size;

    strncpy (nspace->info[3].key, PMIX_LOCAL_PEERS, PMIX_MAX_KEYLEN);
    nspace->info[3].value.type = PMIX_STRING;
    nspace->info[3].value.data.string = xstrdup (nodeset_string (ns));

    strncpy (nspace->info[4].key, PMIX_NODE_MAP, PMIX_MAX_KEYLEN);
    nspace->info[4].value.type = PMIX_STRING;
    if ((rc = PMIx_generate_regex ("localhost",
                        &nspace->info[4].value.data.string)) != PMIX_SUCCESS)
        msg_exit ("PMIx_generate_regex failed rc=%d", rc);

    strncpy (nspace->info[5].key, PMIX_PROC_MAP, PMIX_MAX_KEYLEN);
    nspace->info[5].value.type = PMIX_STRING;
    if ((rc = PMIx_generate_ppn (nodeset_string (ns),
                        &nspace->info[5].value.data.string)) != PMIX_SUCCESS)
        msg_exit ("PMIx_generate_ppn failed rc=%d", rc);

    strncpy (nspace->info[6].key, PMIX_APPNUM, PMIX_MAX_KEYLEN);
    nspace->info[6].value.type = PMIX_UINT32;
    nspace->info[6].value.data.uint32 = appnum;

    strncpy (nspace->info[7].key, PMIX_JOB_SIZE, PMIX_MAX_KEYLEN);
    nspace->info[7].value.type = PMIX_UINT32;
    nspace->info[7].value.data.uint32 = size;

    nodeset_destroy (ns);
}

void nspace_finalize (struct nspace *nspace)
{
    int i;
    for (i = 0; i < nspace->len; i++) {
        if (nspace->info[i].value.type == PMIX_STRING)
            free (nspace->info[i].value.data.string);
    }
    free (nspace->info);
    nspace->info = NULL;
    nspace->len = 0;
    memset (nspace->name, 0, sizeof (nspace->name));
}


/* pmix errhandler skel
 */

void op_callbk(pmix_status_t status, void *cbdata)
{
}

void errhandler(pmix_status_t status,
                pmix_proc_t procs[], size_t nprocs,
                pmix_info_t info[], size_t ninfo)
{
    msg ("PMIx server error status = %d", status);
    //PMIx_Notify_error(status, NULL, 0, NULL, 0, NULL, 0, op_callbk, NULL);
}

void errhandler_reg_callbk (pmix_status_t status, int errhandler_ref,
                            void *cbdata)
{
    if (status != PMIX_SUCCESS)
        msg_exit ("errhandler registration status=%d", status);
}

void x_acceptor (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct x_server *xs = arg;
    int fd;

    if (revents & FLUX_POLLERR) {
        msg ("%s: poll error", __FUNCTION__);
        return;
    }
    if (revents & FLUX_POLLIN) {
        if ((fd = accept4 (xs->listen_fd, NULL, NULL, SOCK_CLOEXEC)) < 0) {
            err ("%s: accept error", __FUNCTION__);
            return;
        }
        if ((xs->flags & X_FLAG_VERBOSE))
            msg ("%s: accepting fd=%d", __FUNCTION__, fd);
        xs->connect_cb (fd);
    }
}

struct x_server *x_server_create (flux_reactor_t *r, const char *scratchdir,
                                  int appnum, int size, int flags)
{
    pmix_status_t rc;
    struct x_server *xs = xzmalloc (sizeof (*xs));

    xs->r = r;
    xs->size = size;
    xs->uid = geteuid ();
    xs->gid = getegid ();
    xs->flags = flags;
    xs->listen_fd = -1;
    assert (x_global_state == NULL);
    x_global_state = xs;
    int ninfo = 0;
    pmix_info_t info[1];

    if ((xs->flags & X_FLAG_VERBOSE))
        msg ("PMIx version %s", PMIx_Get_version());
    if ((xs->flags & X_FLAG_SOCKETPAIR)) {
#ifdef PMIX_SOCKET_SUPPRESS
        (void)strncpy(info[0].key, PMIX_SOCKET_SUPPRESS, PMIX_MAX_KEYLEN);
        info[0].value.type = PMIX_BOOL;
        info[0].value.data.flag = true;
        ninfo = 1;
#endif
    } else {
#ifdef PMIX_SOCKET_FILENAME
        char sockpath[PATH_MAX + 1];
        int n;
        n = snprintf (sockpath, sizeof (sockpath), "%s/pmix-start", scratchdir);
        assert (n < sizeof (sockpath));
        (void)strncpy(info[0].key, PMIX_SOCKET_FILENAME, PMIX_MAX_KEYLEN);
        info[0].value.type = PMIX_STRING;
        info[0].value.data.string = sockpath;
        ninfo = 1;
#endif
    }
    if ((rc = PMIx_server_init (&x_ops, info, ninfo)) != PMIX_SUCCESS)
        msg_exit ("PMIx_server_init failed rc=%d", rc);
    PMIx_Register_errhandler(NULL, 0, errhandler, errhandler_reg_callbk, NULL);

    nspace_init (&xs->nspace, "FLUX-START", size, appnum);
    synchro_init (&xs->sync);

    if (xs->listen_fd != -1) {
        if (!(xs->listen_w = flux_fd_watcher_create (r, xs->listen_fd,
                                                     FLUX_POLLIN | FLUX_POLLERR,
                                                     x_acceptor, xs)))
            err_exit ("flux_fd_watcher_create");
    }

    if ((rc = PMIx_server_register_nspace (xs->nspace.name, size,
                                           xs->nspace.info, xs->nspace.len,
                                           synchro_signal,
                                           &xs->sync)) != PMIX_SUCCESS
                    || (rc = synchro_wait (&xs->sync)) != PMIX_SUCCESS)
        msg_exit ("PMIx_server_register_nspace failed rc=%d", rc);

    return xs;
}

void x_server_destroy (struct x_server *xs)
{
    pmix_status_t rc;

    if (xs) {
        PMIx_server_deregister_nspace (xs->nspace.name);
        nspace_finalize (&xs->nspace);
        //PMIx_Deregister_errhandler(0, op_callback, NULL);
        if ((rc = PMIx_server_finalize ()) != PMIX_SUCCESS)
            msg_exit ("PMIx_server_finalize failed rc=%d", rc);
        flux_watcher_destroy (xs->listen_w);
        free (xs);
    }
}

void env_setup (struct x_client *xc, struct subprocess *p)
{
    struct x_server *xs = xc->xs;
    pmix_status_t rc;
    char **env = NULL;
    int i;

    if ((rc = PMIx_server_setup_fork (&xc->proc, &env)) != PMIX_SUCCESS)
        msg_exit ("%d: PMIx_server_setup_fork failed rc=%d", xc->proc.rank, rc);
    if (env) {
        for (i = 0; env[i] != NULL; i++) {
            char *name = env[i];
            char *value = strchr (name, '=');
            if (value)
                *value++ = '\0';
            if ((xs->flags & X_FLAG_VERBOSE))
                msg ("%d: setenv %s=%s", xc->proc.rank, name, value);
            subprocess_setenv (p, name, value, 1);
            free (env[i]);
        }
        free (env);
    }
}

struct x_client *x_client_create (struct x_server *xs,
                                  struct subprocess *p, int rank)
{
    struct x_client *xc = xzmalloc (sizeof (*xc));
    pmix_status_t rc;

    xc->xs = xs;
    xc->proc.rank = rank;
    (void)strncpy (xc->proc.nspace, xs->nspace.name, PMIX_MAX_NSLEN);

    synchro_init (&xc->sync);
    env_setup (xc, p);

    if ((rc = PMIx_server_register_client (&xc->proc, xs->uid, xs->gid, xc,
                                           synchro_signal,
                                           &xc->sync)) != PMIX_SUCCESS
                    || (rc = synchro_wait (&xc->sync)) != PMIX_SUCCESS)
        msg_exit ("%d: PMIx_server_register_client failed rc=%d", rank, rc);
    if ((xs->flags & X_FLAG_SOCKETPAIR)) {
        int client_fd, server_fd;

        if ((server_fd = subprocess_socketpair (p, &client_fd)) < 0)
            err_exit ("subprocess_socketpair");
        if ((xs->flags & X_FLAG_VERBOSE))
            msg ("%d: setenv PMIX_SERVER_FD=%d", rank, client_fd);
        subprocess_setenvf (p, "PMIX_SERVER_FD", 1, "%d", client_fd);
        if ((xs->flags & X_FLAG_VERBOSE))
            msg ("%d: telling PMIx to handle fd=%d", rank, server_fd);
        if ((rc = PMIx_server_register_client_fd (&xc->proc, server_fd))
                                                            != PMIX_SUCCESS)
            msg_exit ("%d: PMIx_server_register_client_fd failed rc=%d",
                      rank, rc);
    }

    if (xs->num_clients++ == 0) {
        if (xs->listen_w) {
            if ((xs->flags & X_FLAG_VERBOSE))
                msg ("Starting pmix listener");
            flux_watcher_start (xs->listen_w);
        }
    }
    return xc;
}

void x_client_destroy (struct x_client *xc)
{
    if (xc) {
        struct x_server *xs = xc->xs;
        PMIx_server_deregister_client (&xc->proc);
        free (xc);
        if (--xs->num_clients == 0) {
            if (xs->listen_w) {
                if ((xs->flags & X_FLAG_VERBOSE))
                    msg ("Stopping pmix listener");
                flux_watcher_stop (xs->listen_w);
            }
        }
    }
}
#endif /* HAVE_LIBPMIX */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
