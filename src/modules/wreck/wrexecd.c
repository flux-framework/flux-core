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
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <czmq.h>
#include <sys/syslog.h>
#include <envz.h>
#include <sys/ptrace.h>

#include <lua.h>
#include <lauxlib.h>

#include <flux/core.h>

#include "src/common/libutil/optparse.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/zconnect.h"
#include "src/modules/kvs/kvs.h"
#include "src/modules/api/api.h"

#include "luastack.h"
#include "src/bindings/lua/lutil.h"
#include "src/bindings/lua/kvs-lua.h"
#include "src/bindings/lua/flux-lua.h"
#include "src/common/libzio/zio.h"
#include "src/common/libzio/kz.h"

enum { IN=0, OUT, ERR, NR_IO };
const char *ionames [] = { "stdin", "stdout", "stderr" };

struct task_info {
    struct prog_ctx *ctx;

    int      id;              /* local id for this task */
    int      globalid;
    pid_t    pid;

    flux_t   f;               /* local flux handle for task */
    kvsdir_t kvs;             /* kvs handle to this task's dir in kvs */
    int      status;
    int      exited;          /* non-zero if this task exited */

    /*  IO */
    zio_t zio[3];
    kz_t  kz[3];
};

struct prog_ctx {
    flux_t   flux;
    kvsdir_t kvs;           /* Handle to this job's dir in kvs */
    kvsdir_t resources;     /* Handle to this node's resource dir in kvs */

    int noderank;

    int64_t id;             /* id of this execution */
    int nnodes;
    int nodeid;
    int nprocs;             /* number of copies of command to execute */
    int globalbasis;        /* Global rank of first task on this node */
    int exited;

    /*
     *  Flags and options
     */
    zhash_t *options;

    int argc;
    char **argv;
    char *envz;
    size_t envz_len;

    char exedir[MAXPATHLEN]; /* Directory from which this executable is run */

    zctx_t *zctx;
    void *zs_req;
    void *zs_rep;
    int signalfd;

    /*  Per-task data. These members are only valid between fork and
     *   exec within each task and are created on-demand as needed by
     *   Lua scripts.
     */
    struct task_info **task;
    int in_task;            /* Non-zero if currently in task ctx  */
    int taskid;             /* Current taskid executing lua_stack */

    char *lua_pattern;      /* Glob for lua plugins */
    lua_stack_t lua_stack;
    int envref;             /* Global reference to Lua env obj    */
};

void *lsd_nomem_error (const char *file, int line, char *msg)
{
    return (NULL);
}

struct task_info *prog_ctx_current_task (struct prog_ctx *ctx)
{
    if (ctx->taskid >= 0)
        return (ctx->task[ctx->taskid]);
    return (NULL);
}

static flux_t prog_ctx_flux_handle (struct prog_ctx *ctx)
{
    struct task_info *t;

    if (!ctx->in_task)
        return (ctx->flux);

    t = prog_ctx_current_task (ctx);
    if (!t->f) {
        char name [128];
        t->f = flux_api_open ();
        snprintf (name, sizeof (name) - 1, "lwj.%ld.%d", ctx->id, t->globalid);
        flux_log_set_facility (t->f, name);
    }
    return (t->f);
}

static void log_fatal (struct prog_ctx *ctx, int code, char *format, ...)
{
    flux_t c = prog_ctx_flux_handle (ctx);
    va_list ap;
    va_start (ap, format);
    if ((ctx != NULL) && ((c = ctx->flux) != NULL))
        flux_vlog (c, LOG_EMERG, format, ap);
    else
        vfprintf (stderr, format, ap);
    va_end (ap);
    exit (code);
}

static int log_err (struct prog_ctx *ctx, const char *fmt, ...)
{
    flux_t c = prog_ctx_flux_handle (ctx);
    va_list ap;
    va_start (ap, fmt);
    flux_vlog (c, LOG_ERR, fmt, ap);
    va_end (ap);
    return (-1);
}

static void log_msg (struct prog_ctx *ctx, const char *fmt, ...)
{
    flux_t c = prog_ctx_flux_handle (ctx);
    va_list ap;
    va_start (ap, fmt);
    flux_vlog (c, LOG_INFO, fmt, ap);
    va_end (ap);
}

const char * prog_ctx_getopt (struct prog_ctx *ctx, const char *opt)
{
    if (ctx->options)
        return zhash_lookup (ctx->options, opt);
    return (NULL);
}

int prog_ctx_setopt (struct prog_ctx *ctx, const char *opt, const char *val)
{
    log_msg (ctx, "Setting option %s = %s\n", opt, val);
    zhash_insert (ctx->options, opt, strdup (val));
    zhash_freefn (ctx->options, opt, (zhash_free_fn *) free);
    return (0);
}

int globalid (struct prog_ctx *ctx, int localid)
{
    return (ctx->globalbasis + localid);
}

const char * ioname (int s)
{
    if (s == IN)
        return "stdin";
    if (s == OUT)
        return "stdout";
    if (s == ERR)
        return "stderr";
    return "";
}

void prog_ctx_signal_eof (struct prog_ctx *ctx)
{
    /*
     *  Signal that a stdio reader has been closed as this event may
     *   indicate completion if all tasks have exited and other IO
     *   streams are closed.
     *  XXX: For now we wake up the signal callback and force the
     *   check there.
     */
    kill (getpid(), SIGCHLD);
}
int stdout_cb (zio_t z, json_object *o, struct task_info *t)
{
    if (kz_put_json (t->kz[OUT], o) < 0)
        return log_err (t->ctx, "stdout: kz_put_json: %s", strerror (errno));
    if (zio_json_eof (o))
        prog_ctx_signal_eof (t->ctx);
    return (0);
}

int stderr_cb (zio_t z, json_object *o, struct task_info *t)
{
    if (kz_put_json (t->kz[ERR], o) < 0)
        return log_err (t->ctx, "stderr: kz_put_json: %s", strerror (errno));
    if (zio_json_eof (o))
        prog_ctx_signal_eof (t->ctx);
    return (0);
}

void kz_stdin (kz_t kz, struct task_info *t)
{
    json_object *o;
    while ((o = kz_get_json (kz))) {
        zio_write_json (t->zio [IN], o);
        json_object_put (o);
    }
    return;
}

int prog_ctx_io_flags (struct prog_ctx *ctx)
{
    int flags = KZ_FLAGS_RAW;
    if (!prog_ctx_getopt (ctx, "stdio-commit-on-open"))
        flags |= KZ_FLAGS_NOCOMMIT_OPEN;
    if (!prog_ctx_getopt (ctx, "stdio-commit-on-close"))
        flags |= KZ_FLAGS_NOCOMMIT_CLOSE;
    if (prog_ctx_getopt (ctx, "stdio-delay-commit"))
        flags |= KZ_FLAGS_NOCOMMIT_PUT;
    return (flags);
}

kz_t task_kz_open (struct task_info *t, int type)
{
    struct prog_ctx *ctx = t->ctx;
    kz_t kz;
    char *key;
    int flags = prog_ctx_io_flags (ctx);

    if (type == IN)
        flags |= KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK | KZ_FLAGS_NOEXIST;
    else
        flags |= KZ_FLAGS_WRITE;

    if (asprintf (&key, "lwj.%ld.%d.%s",
        ctx->id, t->globalid, ioname (type)) < 0)
        log_fatal (ctx, 1, "task_kz_open: asprintf: %s", strerror (errno));
    if ((kz = kz_open (ctx->flux, key, flags)) == NULL)
        log_fatal (ctx, 1, "kz_open (%s): %s", key, strerror (errno));
    free (key);
    return (kz);
}

struct task_info * task_info_create (struct prog_ctx *ctx, int id)
{
    int i;
    struct task_info *t = xzmalloc (sizeof (*t));

    t->ctx = ctx;
    t->id = id;
    t->globalid = globalid (ctx, id);
    t->pid = (pid_t) 0;

    /* Handles to CMB are created on-demand (for lua callbacks) */
    t->f = NULL;
    t->kvs = NULL;

    t->zio [OUT] = zio_pipe_reader_create ("stdout", NULL, (void *) t);
    zio_set_send_cb (t->zio [OUT], (zio_send_f) stdout_cb);

    t->zio [ERR] = zio_pipe_reader_create ("stderr", NULL, (void *) t);
    zio_set_send_cb (t->zio [ERR], (zio_send_f) stderr_cb);

    t->zio [IN] = zio_pipe_writer_create ("stdin", (void *) t);

    for (i = 0; i < NR_IO; i++)
        t->kz [i] = task_kz_open (t, i);
    kz_set_ready_cb (t->kz [IN], (kz_ready_f) kz_stdin, t);

    return (t);
}

int task_completed (struct task_info *t)
{
    return (t->exited &&
            zio_closed (t->zio [OUT]) && zio_closed (t->zio [ERR]));
}

int all_tasks_completed (struct prog_ctx *ctx)
{
    int i;
    for (i = 0; i < ctx->nprocs; i++)
        if (!task_completed (ctx->task [i]))
            return (0);
    return (1);
}

void task_io_flush (struct task_info *t)
{
    int i;
    for (i = 0; i < NR_IO; i++) {
        zio_flush (t->zio [i]);
        zio_destroy (t->zio [i]);
        if (t->kz [i]) {
            kz_close (t->kz [i]);
            t->kz [i] = NULL;
        }
    }
}

void task_info_destroy (struct task_info *t)
{
    if (t->kvs)
        kvsdir_destroy (t->kvs);
    if (t->f)
        flux_handle_destroy (&t->f);
    free (t);
}

static int sigmask_unblock_all (void)
{
    sigset_t mask;
    sigemptyset (&mask);
    return sigprocmask (SIG_SETMASK, &mask, NULL);
}

int signalfd_setup (struct prog_ctx *ctx)
{
    sigset_t mask;

    sigemptyset (&mask);
    sigaddset (&mask, SIGCHLD);
    sigaddset (&mask, SIGTERM);
    sigaddset (&mask, SIGINT);

    if (sigprocmask (SIG_BLOCK, &mask, NULL) < 0)
        log_err (ctx, "Failed to block signals in parent");

    ctx->signalfd = signalfd (-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (ctx->signalfd < 0)
        log_fatal (ctx, 1, "signalfd");
    return (0);
}

static char * ctime_iso8601_now (char *buf, size_t sz)
{
    struct tm tm;
    time_t now = time (NULL);

    memset (buf, 0, sz);

    if (!localtime_r (&now, &tm))
        return (NULL);
    strftime (buf, sz, "%FT%T", &tm);

    return (buf);
}

/*
 *  Send a message to rexec plugin
 */
int rexec_send_msg (struct prog_ctx *ctx, char *tag, json_object *o)
{
    zmsg_t *zmsg = flux_msg_encode (tag, o);
    if (!zmsg)
        return (-1);
    zmsg_dump (zmsg);
    return zmsg_send (&zmsg, ctx->zs_req);
}

static int get_executable_path (char *buf, size_t len)
{
    char *p;
    if (readlink ("/proc/self/exe", buf, len) < 0)
        return (-1);

    p = buf + strlen (buf) - 1;
    while (*p == '/')
        p--;
    while (*p != '/')
        p--;
    if (p && *p == '/')
        *p = '\0';

    return (0);
}

void prog_ctx_destroy (struct prog_ctx *ctx)
{
    int i;

    for (i = 0; i < ctx->nprocs; i++) {
        task_io_flush (ctx->task [i]);
        task_info_destroy (ctx->task [i]);
    }

    if (ctx->kvs)
        kvsdir_destroy (ctx->kvs);
    if (ctx->resources)
        kvsdir_destroy (ctx->resources);

    free (ctx->envz);
    close (ctx->signalfd);


    zmq_close (ctx->zs_req);
    zmq_close (ctx->zs_rep);

    zmq_term (ctx->zctx);
    if (ctx->flux)
        flux_handle_destroy (&ctx->flux);

    free (ctx);
}

struct prog_ctx * prog_ctx_create (void)
{
    struct prog_ctx *ctx = malloc (sizeof (*ctx));
    memset (ctx, 0, sizeof (*ctx));
    zsys_handler_set (NULL); /* Disable czmq SIGINT/SIGTERM handlers */
    if (!ctx)
        log_fatal (ctx, 1, "malloc");

    ctx->options = zhash_new ();

    ctx->envz = NULL;
    ctx->envz_len = 0;

    ctx->id = -1;
    ctx->nodeid = -1;
    ctx->taskid = -1;

    ctx->envref = -1;

    if (get_executable_path (ctx->exedir, sizeof (ctx->exedir)) < 0)
        log_fatal (ctx, 1, "get_executable_path");

    ctx->lua_stack = lua_stack_create ();
    if (strcmp (ctx->exedir, WREXECD_BINDIR) != 0) {
        /* We are presumed to be executing out of builddir */
        if (asprintf (&ctx->lua_pattern, "%s/lua.d/*.lua", ctx->exedir) < 0)
            log_fatal (ctx, 1, "asprintf");
    }
    else {
        ctx->lua_pattern = xstrdup (WRECK_LUA_PATTERN);
    }

    return (ctx);
}

static int prog_ctx_zmq_socket_setup (struct prog_ctx *ctx)
{
    char uri [1024];
    unsigned long uid = geteuid();

    if ((ctx->zctx = zctx_new ()) == NULL)
        log_fatal (ctx, 1, "zctx_new: %s", strerror (errno));

    snprintf (uri, sizeof (uri), "ipc:///tmp/cmb-%d-%lu-rexec-req-%lu",
                ctx->nodeid, uid, ctx->id);
    zbind (ctx->zctx, &ctx->zs_rep, ZMQ_ROUTER, uri, -1);

    snprintf (uri, sizeof (uri), "ipc:///tmp/cmb-%d-%lu-rexec-rep-%lu",
                ctx->nodeid, uid, ctx->id);
    zconnect (ctx->zctx, &ctx->zs_req, ZMQ_DEALER, uri, -1, NULL);

    return (0);
}

int json_array_to_argv (struct prog_ctx *ctx,
    json_object *o, char ***argvp, int *argcp)
{
    int i;
    if (json_object_get_type (o) != json_type_array) {
        log_err (ctx, "json_array_to_argv: not an array");
        errno = EINVAL;
        return (-1);
    }

    *argcp = json_object_array_length (o);
    if (*argcp <= 0) {
        log_err (ctx, "json_array_to_argv: array length = %d", *argcp);
        return (-1);
    }

    *argvp = xzmalloc ((*argcp + 1) * sizeof (char **));

    for (i = 0; i < *argcp; i++) {
        json_object *ox = json_object_array_get_idx (o, i);
        if (json_object_get_type (ox) != json_type_string) {
            log_err (ctx, "malformed cmdline");
            free (*argvp);
            return (-1);
        }
        (*argvp) [i] = strdup (json_object_get_string (ox));
    }
    return (0);
}

int cmp_int (const void *x, const void *y)
{
    int a = *(int *)x;
    int b = *(int *)y;

    if (a < b)
        return (-1);
    else if (a == b)
        return (0);
    return (1);
}

int cores_on_node (struct prog_ctx *ctx, int nodeid)
{
    int rc;
    int ncores;
    char *key;

    if (asprintf (&key, "lwj.%ld.rank.%d.cores", ctx->id, nodeid) < 0)
        log_fatal (ctx, 1, "cores_on_node: out of memory");
    rc = kvs_get_int (ctx->flux, key, &ncores);
    free (key);
    return (rc < 0 ? -1 : ncores);
}

/*
 *  Get total number of nodes in this job from lwj.%d.rank dir
 */
int prog_ctx_get_nodeinfo (struct prog_ctx *ctx)
{
    int n = 0;
    int j;
    kvsdir_t rank = NULL;
    kvsitr_t i;
    const char *key;
    int *nodeids;

    nodeids = malloc (flux_size (ctx->flux) * sizeof (int));

    if (kvsdir_get_dir (ctx->kvs, &rank, "rank") < 0) {
        log_msg (ctx, "get_dir (%s.rank): %s",
                 kvsdir_key (ctx->kvs),
                 strerror (errno));
        ctx->nnodes = flux_size (ctx->flux);
        ctx->nodeid = ctx->noderank;
    }

    i = kvsitr_create (rank);
    while ((key = kvsitr_next (i))) {
        nodeids[n] = atoi (key);
        n++;
    }
    kvsitr_destroy (i);
    kvsdir_destroy (rank);
    ctx->nnodes = n;

    qsort (nodeids, n, sizeof (int), &cmp_int);
    for (j = 0; j < n; j++) {
        int ncores;
        if (nodeids[j] == ctx->noderank) {
            ctx->nodeid = j;
            break;
        }
        if ((ncores = cores_on_node (ctx, nodeids[j])) < 0)
            log_fatal (ctx, 1, "Failed to get ncores for node%d\n", nodeids[j]);
        ctx->globalbasis += ncores;
    }
    free (nodeids);
    log_msg (ctx, "lwj.%ld: node%d: basis=%d\n",
        ctx->id, ctx->nodeid, ctx->globalbasis);
    return (0);
}

int prog_ctx_options_init (struct prog_ctx *ctx)
{
    kvsdir_t opts;
    kvsitr_t i;
    const char *opt;

    if (kvsdir_get_dir (ctx->kvs, &opts, "options") < 0)
        return (0); /* Assume ENOENT */
    i = kvsitr_create (opts);
    while ((opt = kvsitr_next (i))) {
        json_object *v;
        char s [64];

        if (kvsdir_get (opts, opt, &v) < 0) {
            log_err (ctx, "skipping option '%s': %s", opt, strerror (errno));
            continue;
        }

        switch (json_object_get_type (v)) {
            case json_type_null:
                prog_ctx_setopt (ctx, opt, "");
                break;
            case json_type_string:
                prog_ctx_setopt (ctx, opt, json_object_get_string (v));
                break;
            case json_type_int:
                snprintf (s, sizeof (s) -1, "%ld", json_object_get_int64 (v));
                prog_ctx_setopt (ctx, opt, s);
                break;
            case json_type_boolean:
                if (json_object_get_boolean (v))
                    prog_ctx_setopt (ctx, opt, "");
                break;
            default:
                log_err (ctx, "skipping option '%s': invalid type", opt);
                break;
        }
    }
    kvsitr_destroy (i);
    kvsdir_destroy (opts);
    return (0);
}

int prog_ctx_load_lwj_info (struct prog_ctx *ctx, int64_t id)
{
    int i;
    json_object *v;

    if (prog_ctx_options_init (ctx) < 0)
        log_fatal (ctx, 1, "failed to read %s.options", kvsdir_key (ctx->kvs));

    if (kvsdir_get (ctx->kvs, "cmdline", &v) < 0)
        log_fatal (ctx, 1, "kvs_get: cmdline");

    if (json_array_to_argv (ctx, v, &ctx->argv, &ctx->argc) < 0)
        log_fatal (ctx, 1, "Failed to get cmdline from kvs");

    prog_ctx_get_nodeinfo (ctx);

    /*
     *  See if we've got 'cores' assigned for this host
     */
    if (ctx->resources) {
        if (kvsdir_get_int (ctx->resources, "cores", &ctx->nprocs) < 0)
            log_fatal (ctx, 1, "Failed to get resources for this node\n");
    }
    else if (kvsdir_get_int (ctx->kvs, "tasks-per-node", &ctx->nprocs) < 0)
            ctx->nprocs = 1;

    ctx->task = xzmalloc (ctx->nprocs * sizeof (struct task_info *));
    for (i = 0; i < ctx->nprocs; i++)
        ctx->task[i] = task_info_create (ctx, i);

    log_msg (ctx, "lwj %ld: node%d: nprocs=%d, nnodes=%d, cmdline=%s",
                   ctx->id, ctx->nodeid, ctx->nprocs, ctx->nnodes,
                   json_object_to_json_string (v));
    json_object_put (v);

    return (0);
}

int prog_ctx_signal_parent (int fd)
{
    int rc;
    char c = '\0';
    /*
     * Signal parent we are ready
     */
    rc = write (fd, &c, 1);
    close (fd);
    return (rc);
}

int prog_ctx_init_from_cmb (struct prog_ctx *ctx)
{
    char name [128];
    /*
     * Connect to CMB over api socket
     */
    if (!(ctx->flux = flux_api_open ()))
        log_fatal (ctx, 1, "cmb_init");

    snprintf (name, sizeof (name) - 1, "lwj.%ld", ctx->id);
    flux_log_set_facility (ctx->flux, name);

    if (kvs_get_dir (ctx->flux, &ctx->kvs,
                     "lwj.%lu", ctx->id) < 0) {
        log_fatal (ctx, 1, "kvs_get_dir (lwj.%lu): %s\n",
                   ctx->id, strerror (errno));
    }

    ctx->noderank = flux_rank (ctx->flux);
    /*
     *  If the "rank" dir exists in kvs, then this LWJ has been
     *   assigned specific resources by a scheduler.
     *
     *  First check to see if resources directory exists, if not
     *   then we'll fall back to tasks-per-node. O/w, if 'rank'
     *   exists and our rank isn't present, then there is nothing
     *   to do on this node and we'll just exit.
     *
     */
    if (kvsdir_isdir (ctx->kvs, "rank")) {
        log_msg (ctx, "Found kvs 'rank' dir");
        int rc = kvsdir_get_dir (ctx->kvs,
                                 &ctx->resources,
                                 "rank.%d", ctx->noderank);
        if (rc < 0) {
            if (errno == ENOENT)
                return (-1);
            log_fatal (ctx, 1, "kvs_get_dir (lwj.%lu.rank.%d): %s\n",
                        ctx->id, ctx->noderank, strerror (errno));
        }
    }

    log_msg (ctx, "initializing from CMB: rank=%d", ctx->noderank);
    if (prog_ctx_load_lwj_info (ctx, ctx->id) < 0)
        log_fatal (ctx, 1, "Failed to load lwj info");

    return (0);
}

void closeall (int fd)
{
    int fdlimit = sysconf (_SC_OPEN_MAX);

    while (fd < fdlimit)
        close (fd++);
    return;
}

void child_io_setup (struct task_info *t)
{
    /*
     *  Close parent end of stdio fds in child
     */
    close (zio_dst_fd (t->zio [IN]));
    close (zio_src_fd (t->zio [OUT]));
    close (zio_src_fd (t->zio [ERR]));

    /*
     *  Dup appropriate fds onto child STDIN/STDOUT/STDERR
     */
    if (  (dup2 (zio_src_fd (t->zio [IN]), STDIN_FILENO) < 0)
       || (dup2 (zio_dst_fd (t->zio [OUT]), STDOUT_FILENO) < 0)
       || (dup2 (zio_dst_fd (t->zio [ERR]), STDERR_FILENO) < 0))
        log_fatal (t->ctx, 1, "dup2: %s", strerror (errno));

    closeall (3);
}

void close_child_fds (struct task_info *t)
{
    close (zio_src_fd (t->zio [IN]));
    close (zio_dst_fd (t->zio [OUT]));
    close (zio_dst_fd (t->zio [ERR]));
}

int update_job_state (struct prog_ctx *ctx, const char *state)
{
    char buf [64];
    char *key;
    json_object *to =
        json_object_new_string (ctime_iso8601_now (buf, sizeof (buf)));

    assert (ctx->nodeid == 0);

    log_msg (ctx, "updating job state to %s", state);

    if (kvsdir_put_string (ctx->kvs, "state", state) < 0)
        return (-1);

    if (asprintf (&key, "%s-time", state) < 0)
        return (-1);
    if (kvsdir_put (ctx->kvs, key, to) < 0)
        return (-1);
    free (key);
    json_object_put (to);

    if (kvs_commit (ctx->flux) < 0)
        return (-1);

    return (0);
}

int rexec_state_change (struct prog_ctx *ctx, const char *state)
{
    int rc;
    char *name;

    if (strcmp (state, "running") == 0)
        rc = asprintf (&name, "lwj.%lu.startup", ctx->id);
    else
        rc = asprintf (&name, "lwj.%lu.shutdown", ctx->id);
    if (rc < 0)
        log_fatal (ctx, 1, "rexec_state_change: asprintf: %s", strerror (errno));

    /* Wait for all wrexecds to finish and commit */
    if (kvs_fence (ctx->flux, name, ctx->nnodes) < 0)
        log_fatal (ctx, 1, "kvs_fence");

    /* Rank 0 updates job state */
    if ((ctx->nodeid == 0) && update_job_state (ctx, state) < 0)
        log_fatal (ctx, 1, "update_job_state");

    return (0);
}


json_object * json_task_info_object_create (struct prog_ctx *ctx,
    const char *cmd, pid_t pid)
{
    json_object *o = json_object_new_object ();
    json_object *ocmd = json_object_new_string (cmd);
    json_object *opid = json_object_new_int (pid);
    json_object *onodeid = json_object_new_int (ctx->noderank);
    json_object_object_add (o, "command", ocmd);
    json_object_object_add (o, "pid", opid);
    json_object_object_add (o, "nodeid", onodeid);
    return (o);
}

int rexec_taskinfo_put (struct prog_ctx *ctx, int localid)
{
    json_object *o;
    char *key;
    int rc;
    struct task_info *t = ctx->task [localid];

    o = json_task_info_object_create (ctx, ctx->argv [0], t->pid);

    if (asprintf (&key, "%d.procdesc", t->globalid) < 0)
        log_fatal (ctx, 1, "rexec_taskinfo_put: asprintf: %s",
                    strerror (errno));

    rc = kvsdir_put (ctx->kvs, key, o);
    free (key);
    json_object_put (o);
    //kvs_commit (ctx->flux);

    if (rc < 0)
        return log_err (ctx, "kvs_put failure");
    return (0);
}

int send_startup_message (struct prog_ctx *ctx)
{
    int i;
    const char * state = "running";

    for (i = 0; i < ctx->nprocs; i++) {
        if (rexec_taskinfo_put (ctx, i) < 0)
            return (-1);
    }

    if (prog_ctx_getopt (ctx, "stop-children-in-exec"))
        state = "sync";

    if (rexec_state_change (ctx, state) < 0) {
        log_err (ctx, "rexec_state_change");
        return (-1);
    }

    return (0);
}

int send_exit_message (struct task_info *t)
{
    char *key;
    struct prog_ctx *ctx = t->ctx;
    json_object *o = json_object_new_int (t->status);

    if (asprintf (&key, "lwj.%lu.%d.exit_status", ctx->id, t->globalid) < 0)
        return (-1);
    if (kvs_put (ctx->flux, key, o) < 0)
        return (-1);
    free (key);
    json_object_put (o);

    if (WIFSIGNALED (t->status)) {
        o = json_object_new_int (WTERMSIG (t->status));
        if (asprintf (&key, "lwj.%lu.%d.exit_sig", ctx->id, t->globalid) < 0)
            return (-1);
        if (kvs_put (ctx->flux, key, o) < 0)
            return (-1);
        free (key);
        json_object_put (o);
    }
    else {
        o = json_object_new_int (WEXITSTATUS (t->status));
        if (asprintf (&key, "lwj.%lu.%d.exit_code", ctx->id, t->globalid) < 0)
            return (-1);
        if (kvs_put (ctx->flux, key, o) < 0)
            return (-1);
        free (key);
        json_object_put (o);
    }

    if (prog_ctx_getopt (ctx, "commit-on-task-exit")) {
        log_msg (ctx, "commit on task exit\n");
        if (kvs_commit (ctx->flux) < 0)
            return (-1);
    }

    return (0);
}

void prog_ctx_unsetenv (struct prog_ctx *ctx, const char *name)
{
    envz_remove (&ctx->envz, &ctx->envz_len, name);
}

int prog_ctx_setenv (struct prog_ctx *ctx, const char *name, const char *value)
{
    return ((int) envz_add (&ctx->envz, &ctx->envz_len, name, value));
}

int prog_ctx_setenvf (struct prog_ctx *ctx, const char *name, int overwrite,
        const char *fmt, ...)
{
    va_list ap;
    char *val;
    int rc;

    va_start (ap, fmt);
    rc = vasprintf (&val, fmt, ap);
    va_end (ap);
    if (rc < 0)
        return (rc);
    if (overwrite)
        prog_ctx_unsetenv (ctx, name);
    rc = prog_ctx_setenv (ctx, name, val);
    free (val);
    return (rc);

}

char * prog_ctx_getenv (struct prog_ctx *ctx, const char *name)
{
    return envz_get (ctx->envz, ctx->envz_len, name);
}

char ** prog_ctx_env_create (struct prog_ctx *ctx)
{
    char **env;
    size_t count;
    envz_strip (&ctx->envz, &ctx->envz_len);
    count = argz_count (ctx->envz, ctx->envz_len);
    env = xzmalloc ((count + 1) * sizeof (char *));
    argz_extract (ctx->envz, ctx->envz_len, env);
    return (env);
}

int exec_command (struct prog_ctx *ctx, int i)
{
    struct task_info *t = ctx->task [i];
    pid_t cpid = fork ();

    if (cpid < 0)
        log_fatal (ctx, 1, "fork: %s", strerror (errno));
    if (cpid == 0) {
        child_io_setup (t);
        //log_msg (ctx, "in child going to exec %s", ctx->argv [0]);

        if (sigmask_unblock_all () < 0)
            fprintf (stderr, "sigprocmask: %s\n", strerror (errno));

        /*
         *  Set current taskid and invoke rexecd_task_init
         */
        ctx->taskid = i;
        ctx->in_task = 1;
        lua_stack_call (ctx->lua_stack, "rexecd_task_init");

        prog_ctx_setenv  (ctx, "FLUX_TMPDIR", getenv ("FLUX_TMPDIR"));
        prog_ctx_setenvf (ctx, "MPIRUN_RANK",     1, "%d", t->globalid);
        prog_ctx_setenvf (ctx, "PMI_RANK", 1, "%d", t->globalid);
        prog_ctx_setenvf (ctx, "FLUX_LWJ_TASK_ID", 1, "%d", t->globalid);
        prog_ctx_setenvf (ctx, "FLUX_LWJ_LOCAL_TASK_ID", 1, "%d", i);

        if (prog_ctx_getopt (ctx, "stop-children-in-exec")) {
            /* Stop process on exec with parent attached */
            ptrace (PTRACE_TRACEME, 0, NULL, 0);
        }

        /* give each task its own process group so we can use killpg(2) */
        setpgrp();
        /*
         *  Reassign environment:
         */
        environ = prog_ctx_env_create (ctx);
        if (execvp (ctx->argv [0], ctx->argv) < 0) {
            fprintf (stderr, "execvp: %s\n", strerror (errno));
            exit (255);
        }
        exit (255);
    }

    /*
     *  Parent: Close child fds
     */
    close_child_fds (t);
    log_msg (ctx, "in parent: child pid[%d] = %d", i, cpid);
    t->pid = cpid;


    return (0);
}

char *gtid_list_create (struct prog_ctx *ctx, char *buf, size_t len)
{
    char *str = NULL;
    int i, n = 0;
    int truncated = 0;

    memset (buf, 0, len);

    for (i = 0; i < ctx->nprocs; i++) {
        int count;

        if (!truncated)  {
            struct task_info *t = ctx->task [i];
            count = snprintf (buf + n, len - n, "%u,", t->globalid);

            if ((count >= (len - n)) || (count < 0))
                truncated = 1;
            else
                n += count;
        }
        else
            n += strlen (str) + 1;
    }

    if (truncated)
        buf [len - 1] = '\0';
    else {
        /*
         * Delete final separator
         */
        buf[strlen(buf) - 1] = '\0';
    }

    return (buf);
}

static struct prog_ctx *l_get_prog_ctx (lua_State *L, int index)
{
    struct prog_ctx **ctxp = luaL_checkudata (L, index, "WRECK.ctx");
    return (*ctxp);
}

static int l_environ_destroy (lua_State *L)
{
    int *refp = luaL_checkudata (L, 1, "WRECK.environ");
    luaL_unref (L, LUA_REGISTRYINDEX, *refp);
    return (0);
}

static struct prog_ctx *l_get_prog_ctx_from_environ (lua_State *L, int index)
{
    struct prog_ctx *ctx;
    int *refp = luaL_checkudata (L, index, "WRECK.environ");
    lua_rawgeti (L, LUA_REGISTRYINDEX, *refp);
    ctx = l_get_prog_ctx (L, -1);
    lua_pop (L, 1);
    return ctx;
}

static int l_environ_index (lua_State *L)
{
    struct prog_ctx *ctx = l_get_prog_ctx_from_environ (L, 1);
    const char *key = lua_tostring (L, 2);
    const char *val = prog_ctx_getenv (ctx, key);

    if (val)
        lua_pushstring (L, val);
    else
        lua_pushnil (L);
    return (1);
}

static int l_environ_newindex (lua_State *L)
{
    struct prog_ctx *ctx = l_get_prog_ctx_from_environ (L, 1);
    const char *key = lua_tostring (L, 2);

    if (lua_isnil (L, 3))
        prog_ctx_unsetenv (ctx, key);
    else
        prog_ctx_setenv (ctx, key, lua_tostring (L, 3));
    return (0);
}

static int l_push_environ (lua_State *L, int index)
{
    int ref;
    int *ctxref;

    /*
     *  Store the "environ" object as a reference to the existing
     *   prog_ctx object, which already stores our real environment.
     */
    if (!lua_isuserdata (L, index))
        return lua_pusherror (L, "Invalid index when pushing environ");

    /*  Push userdata at stack position [index] to top of stack and then
     *   take a reference to it in the registry:
     */
    lua_pushvalue (L, index);
    ref = luaL_ref (L, LUA_REGISTRYINDEX);

    ctxref = lua_newuserdata (L, sizeof (int *));
    *ctxref = ref;
    luaL_getmetatable (L, "WRECK.environ");
    lua_setmetatable (L, -2);

    return (1);
}

static kvsdir_t prog_ctx_kvsdir (struct prog_ctx *ctx)
{
    struct task_info *t;

    if (!ctx->in_task)
        return (ctx->kvs);

    t = prog_ctx_current_task (ctx);
    if (!t->kvs) {
        if (kvs_get_dir (prog_ctx_flux_handle (ctx),
            &t->kvs, "lwj.%ld.%d", ctx->id, t->id) < 0)
            log_err (ctx, "kvs_get_dir: %s", strerror (errno));
    }
    return (t->kvs);
}

static int l_wreck_index (lua_State *L)
{
    struct task_info *t;
    struct prog_ctx *ctx = l_get_prog_ctx (L, 1);
    const char *key = lua_tostring (L, 2);

    t = prog_ctx_current_task (ctx);

    if (key == NULL)
        return luaL_error (L, "wreck: invalid key");

    if (strcmp (key, "id") == 0) {
        lua_pushnumber (L, ctx->id);
        return (1);
    }
    if (strcmp (key, "globalid") == 0) {
        lua_pushnumber (L, t->globalid);
        return (1);
    }
    if (strcmp (key, "taskid") == 0) {
        lua_pushnumber (L, t->id);
        return (1);
    }
    if (strcmp (key, "kvsdir") == 0) {
        kvsdir_t d = prog_ctx_kvsdir (ctx);
        if (d == NULL)
            return lua_pusherror (L, "No such file or directory");
        l_push_kvsdir (L, prog_ctx_kvsdir (ctx));
        return (1);
    }
    if (strcmp (key, "flux") == 0) {
        lua_push_flux_handle (L, prog_ctx_flux_handle (ctx));
        return (1);
    }
    if (strcmp (key, "nodeid") == 0) {
        lua_pushnumber (L, ctx->nodeid);
        return (1);
    }
    if (strcmp (key, "environ") == 0) {
        if (ctx->envref < 0) {
            /* Push environment object, then take a reference
             *  in the registry so we don't have to create a new environ
             *  object each time wreck.environ is accessed
             */
            l_push_environ (L, 1);
            ctx->envref = luaL_ref (L, LUA_REGISTRYINDEX);
        }
        lua_rawgeti (L, LUA_REGISTRYINDEX, ctx->envref);
        return (1);
    }
    if (strcmp (key, "argv") == 0) {
        /*  Push copy of argv */
        int i;
        lua_newtable (L);
        for (i = 0; i < ctx->argc; i++) {
            lua_pushstring (L, ctx->argv[i]);
            lua_rawseti (L, -2, i);
        }
        return (1);
    }
    if (strcmp (key, "exit_status") == 0) {
        if (ctx->in_task || ctx->taskid < 0)
            return lua_pusherror (L, "Not valid in this context");
        lua_pushnumber (L, t->status);
        return (1);
    }
    if (strcmp (key, "exitcode") == 0) {
        int status;
        if (ctx->in_task || ctx->taskid < 0)
            return lua_pusherror (L, "Not valid in this context");
        status = t->status;
        if (WIFEXITED (status))
            lua_pushnumber (L, WEXITSTATUS(status));
        else
            lua_pushnil (L);
        return (1);
    }
    if (strcmp (key, "termsig") == 0) {
        int status;
        if (ctx->in_task || ctx->taskid < 0)
            return lua_pusherror (L, "Not valid in this context");
        status = t->status;
        if (WIFSIGNALED (status))
            lua_pushnumber (L, WTERMSIG (status));
        else
            lua_pushnil (L);
        return (1);
    }
    return (0);
}

static int l_push_prog_ctx (lua_State *L, struct prog_ctx *ctx)
{
    struct prog_ctx **ctxp = lua_newuserdata (L, sizeof (*ctxp));
    *ctxp = ctx;
    luaL_getmetatable (L, "WRECK.ctx");
    lua_setmetatable (L, -2);
    return (1);
}

static const struct luaL_Reg wreck_methods [] = {
    { "__index",    l_wreck_index },
    { NULL,         NULL          },
};

static const struct luaL_Reg environ_methods [] = {
    { "__gc",       l_environ_destroy  },
    { "__index",    l_environ_index    },
    { "__newindex", l_environ_newindex },
    { NULL,         NULL           },
};

static int wreck_lua_init (struct prog_ctx *ctx)
{
    lua_State *L = lua_stack_state (ctx->lua_stack);

    luaopen_flux (L); /* Also loads kvs metatable */

    luaL_newmetatable (L, "WRECK.ctx");
    luaL_register (L, NULL, wreck_methods);
    luaL_newmetatable (L, "WRECK.environ");
    luaL_register (L, NULL, environ_methods);
    l_push_prog_ctx (L, ctx);
    lua_setglobal (L, "wreck");
    log_msg (ctx, "reading lua files from %s\n", ctx->lua_pattern);
    lua_stack_append_file (ctx->lua_stack, ctx->lua_pattern);
    return (0);
}

int task_exit (struct task_info *t, int status)
{
    struct prog_ctx *ctx = t->ctx;

    log_msg (ctx, "task%d: pid %d (%s) exited with status 0x%04x",
            t->id, t->pid, ctx->argv [0], status);
    t->status = status;
    t->exited = 1;

    ctx->taskid = t->id;
    lua_stack_call (ctx->lua_stack, "rexecd_task_exit");

    if (send_exit_message (t) < 0)
        log_msg (ctx, "Sending exit message failed!");
    return (0);
}

int start_trace_task (struct task_info *t)
{
    int status;
    pid_t pid = t->pid;
    struct prog_ctx *ctx = t->ctx;

    int rc = waitpid (pid, &status, WUNTRACED);
    if (rc < 0) {
        log_err (ctx, "start_trace: waitpid: %s\n", strerror (errno));
        return (-1);
    }
    if (WIFSTOPPED (status)) {
        /*
         *  Send SIGSTOP and detach from process.
         */
        if (kill (pid, SIGSTOP) < 0) {
            log_err (ctx, "start_trace: kill: %s\n", strerror (errno));
            return (-1);
        }
        if (ptrace (PTRACE_DETACH, pid, NULL, 0) < 0) {
            log_err (ctx, "start_trace: ptrace: %s\n", strerror (errno));
            return (-1);
        }
        return (0);
    }

    /*
     *  Otherwise, did task exit?
     */
    if (WIFEXITED (status)) {
        log_err (ctx, "start_trace: task unexpectedly exited\n");
        task_exit (t, status);
    }
    else
        log_err (ctx, "start_trace: Unexpected status 0x%04x\n", status);

    return (-1);
}

int exec_commands (struct prog_ctx *ctx)
{
    char buf [4096];
    int i;
    int stop_children = 0;

    wreck_lua_init (ctx);

    lua_stack_call (ctx->lua_stack, "rexecd_init");

    prog_ctx_setenvf (ctx, "FLUX_LWJ_ID",    1, "%d", ctx->id);
    prog_ctx_setenvf (ctx, "FLUX_LWJ_NNODES",1, "%d", ctx->nnodes);
    prog_ctx_setenvf (ctx, "FLUX_NODE_ID",   1, "%d", ctx->nodeid);
    prog_ctx_setenvf (ctx, "FLUX_LWJ_NTASKS",1, "%d", ctx->nprocs * ctx->nnodes);
    prog_ctx_setenvf (ctx, "MPIRUN_NPROCS", 1, "%d", ctx->nprocs * ctx->nnodes);
    prog_ctx_setenvf (ctx, "PMI_SIZE", 1, "%d", ctx->nprocs * ctx->nnodes);
    gtid_list_create (ctx, buf, sizeof (buf));
    prog_ctx_setenvf (ctx, "FLUX_LWJ_GTIDS",  1, "%s", buf);

    for (i = 0; i < ctx->nprocs; i++)
        exec_command (ctx, i);

    if (prog_ctx_getopt (ctx, "stop-children-in-exec"))
        stop_children = 1;
    for (i = 0; i < ctx->nprocs; i++) {
        if (stop_children)
            start_trace_task (ctx->task [i]);
    }

    return send_startup_message (ctx);
}

struct task_info *pid_to_task (struct prog_ctx *ctx, pid_t pid)
{
    int i;
    struct task_info *t = NULL;

    for (i = 0; i < ctx->nprocs; i++) {
        t = ctx->task[i];
        if (t->pid == pid)
            break;
    }
    return (t);
}

int reap_child (struct prog_ctx *ctx)
{
    struct task_info *t;
    int status;
    pid_t wpid;

    wpid = waitpid ((pid_t) -1, &status, WNOHANG);
    if (wpid == (pid_t) 0)
        return (0);

    if (wpid < (pid_t) 0) {
        log_err (ctx, "waitpid ()");
        return (0);
    }

    if ((t = pid_to_task (ctx, wpid)) == NULL)
        return log_err (ctx, "Failed to find task for pid %d\n", wpid);

    task_exit (t, status);

    return (1);
}

int prog_ctx_signal (struct prog_ctx *ctx, int sig)
{
    int i;
    for (i = 0; i < ctx->nprocs; i++)
        killpg (ctx->task[i]->pid, sig);
    return (0);
}

int cleanup (struct prog_ctx *ctx)
{
    return prog_ctx_signal (ctx, SIGKILL);
}

int signal_cb (flux_t f, int fd, short revents, struct prog_ctx *ctx)
{
    int n;
    struct signalfd_siginfo si;

    n = read (fd, &si, sizeof (si));
    if (n < 0) {
        log_err (ctx, "read");
        return (0);
    }
    else if (n != sizeof (si)) {
        log_err (ctx, "partial read?");
        return (0);
    }

    if (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT) {
        cleanup (ctx);
        return (0); /* Continue, so we reap children */
    }

    /* SIGCHLD assumed */
    while (reap_child (ctx))
        ++ctx->exited;
    if (all_tasks_completed (ctx))
        flux_reactor_stop (f);
    return (0);
}

int cmb_cb (flux_t f, void *zs, short revents, struct prog_ctx *ctx)
{
    char *tag;
    json_object *o;

    zmsg_t *zmsg = zmsg_recv (zs);
    if (!zmsg) {
        log_msg (ctx, "rexec_cb: no msg to recv!");
        return (0);
    }
    free (zmsg_popstr (zmsg)); /* Destroy dealer id */

    if (flux_msg_decode (zmsg, &tag, &o) < 0) {
        log_err (ctx, "cmb_msg_decode");
        return (0);
    }

    /* Got an incoming message from cmbd */
    if (strcmp (tag, "rexec.kill") == 0) {
        int sig = json_object_get_int (o);
        if (sig == 0)
            sig = 9;
        log_msg (ctx, "Killing jobid %lu with signal %d", ctx->id, sig);
        prog_ctx_signal (ctx, sig);
    }
    zmsg_destroy (&zmsg);
    json_object_put (o);
    return (0);
}

int task_info_io_setup (struct task_info *t)
{
    flux_t f = t->ctx->flux;
    int i;

    for (i = 0; i < NR_IO; i++) {
        zio_flux_attach (t->zio [i], f);
        //zio_set_debug (t->zio [i], NULL, NULL);
    }
    return (0);
}

int prog_ctx_reactor_init (struct prog_ctx *ctx)
{
    int i;
    for (i = 0; i < ctx->nprocs; i++)
        task_info_io_setup (ctx->task [i]);

    /*
     *  Listen for "events" coming from the signalfd
     */
    flux_fdhandler_add (ctx->flux,
        ctx->signalfd,
        ZMQ_POLLIN | ZMQ_POLLERR,
        (FluxFdHandler) signal_cb,
        (void *) ctx);

    /*
     *  Add a handler for events coming from CMB
     */
    flux_zshandler_add (ctx->flux,
        ctx->zs_rep,
        ZMQ_POLLIN | ZMQ_POLLERR,
        (FluxZsHandler) cmb_cb,
        (void *) ctx);

    return (0);
}

static void daemonize ()
{
    switch (fork ()) {
        case  0 : break;        /* child */
        case -1 : exit (2);
        default : _exit(0);     /* exit parent */
    }

    if (setsid () < 0)
        exit (3);

    switch (fork ()) {
        case  0 : break;        /* child */
        case -1 : exit (4);
        default : _exit(0);     /* exit parent */
    }
}

int optparse_get_int (optparse_t p, char *name)
{
    long l;
    char *end;
    const char *s;

    if (!optparse_getopt (p, name, &s))
        return (-1);

    l = strtol (s, &end, 10);
    if ((end == s) || (*end != '\0') || (l < 0) || (l > INT_MAX))
        log_fatal (NULL, 1, "--%s=%s invalid", name, s);
    return ((int) l);
}

int prog_ctx_get_id (struct prog_ctx *ctx, optparse_t p)
{
    const char *id;
    char *end;

    if (!optparse_getopt (p, "lwj-id", &id))
        log_fatal (ctx, 1, "Required argument --lwj-id missing");

    errno = 0;
    ctx->id = strtol (id, &end, 10);
    if (  (*end != '\0')
       || (ctx->id == 0 && errno == EINVAL)
       || (ctx->id == ULONG_MAX && errno == ERANGE))
           log_fatal (ctx, 1, "--lwj-id=%s invalid", id);

    return (0);
}

int main (int ac, char **av)
{
    int parent_fd = -1;
    struct prog_ctx *ctx = NULL;
    optparse_t p;
    struct optparse_option opts [] = {
        { .name =    "lwj-id",
          .key =     1000,
          .has_arg = 1,
          .arginfo = "ID",
          .usage =   "Operate on LWJ id [ID]",
        },
        { .name =    "parent-fd",
          .key =     1001,
          .has_arg = 1,
          .arginfo = "FD",
          .usage =   "Signal parent on file descriptor [FD]",
        },
        OPTPARSE_TABLE_END,
    };

    p = optparse_create (av[0]);
    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        log_fatal (ctx, 1, "optparse_add_option_table");
    if (optparse_parse_args (p, ac, av) < 0)
        log_fatal (ctx, 1, "parse args");

    daemonize ();

    ctx = prog_ctx_create ();
    signalfd_setup (ctx);

    if (prog_ctx_get_id (ctx, p) < 0)
        log_fatal (ctx, 1, "Failed to get lwj id from cmdline");

    if (prog_ctx_init_from_cmb (ctx) < 0) /* Nothing to do here */
        exit (0);

    prog_ctx_zmq_socket_setup (ctx);

    if ((ctx->nodeid == 0) && update_job_state (ctx, "starting") < 0)
        log_fatal (ctx, 1, "update_job_state");

    if ((parent_fd = optparse_get_int (p, "parent-fd")) >= 0)
        prog_ctx_signal_parent (parent_fd);
    prog_ctx_reactor_init (ctx);
    exec_commands (ctx);

    flux_reactor_start (ctx->flux);

    rexec_state_change (ctx, "complete");
    log_msg (ctx, "exiting...");

    prog_ctx_destroy (ctx);

    return (0);
}

/*
 *  vi: ts=4 sw=4 expandtab
 */
