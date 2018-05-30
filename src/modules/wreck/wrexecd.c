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
#include <dirent.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <czmq.h>
#include <sys/syslog.h>
#include <envz.h>
#include <sys/ptrace.h>
#include <inttypes.h>
#include <sys/resource.h>
#include <hwloc.h>

#include <lua.h>
#include <lauxlib.h>

#include <jansson.h>

#include <flux/core.h>

#include "src/common/liboptparse/optparse.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/sds.h"
#include "src/common/libutil/fdwalk.h"
#include "src/common/libsubprocess/zio.h"
#include "src/common/libpmi/simple_server.h"
#include "src/common/libkz/kz.h"

#include "luastack.h"
#include "src/bindings/lua/lutil.h"
#include "src/bindings/lua/kvs-lua.h"
#include "src/bindings/lua/flux-lua.h"

#include "rcalc.h"
enum { IN=0, OUT, ERR, NR_IO };
const char *ionames [] = { "stdin", "stdout", "stderr" };

struct task_info {
    struct prog_ctx *ctx;

    int      id;              /* local id for this task */
    int      globalid;
    pid_t    pid;

    flux_t   *f;              /* local flux handle for task */
    flux_kvsdir_t *kvs;       /* kvs handle to this task's dir in kvs */
    int      status;
    int      exited;          /* non-zero if this task exited */

    /*  IO */
    zio_t *zio[3];
    kz_t  *kz[3];

    int pmi_fds[2];
    zio_t *pmi_zio;           /* zio reader for pmi-simple server */
    zio_t *pmi_client;        /* zio writer for pmi-simple client */
};

struct prog_ctx {
    flux_t   *flux;

    char *kvspath;          /* basedir path in kvs for this lwj.id */
    flux_kvsdir_t *kvs;     /* Handle to this job's dir in kvs */
    int *tasks_per_node;    /* Number of tasks per nodeid in this job */

    kz_t *kz_err;           /* kz stream for errors and debug */

    flux_watcher_t *fdw;
    flux_msg_handler_t *mw;

    struct pmi_simple_server *pmi;
    unsigned int barrier_sequence;
    char barrier_name[64];
    flux_kvs_txn_t *barrier_txn;

    uint32_t noderank;

    int epoch;              /* current heartbeat epoch */

    int64_t id;             /* id of this execution */
    int total_ntasks;       /* Total number of tasks in job */
    int nnodes;
    int exited;

    struct rcalc_rankinfo rankinfo;  /* Rank information from `R_lite` */

    int errnum;

    /*
     *  Flags and options
     */
    zhash_t *options;

    zhash_t *completion_refs;

    int argc;
    char **argv;
    char *envz;
    size_t envz_len;

    char exedir[MAXPATHLEN]; /* Directory from which this executable is run */

    int signalfd;

    char *topic;            /* Per program topic base string for events */

    /*  Per-task data. These members are only valid between fork and
     *   exec within each task and are created on-demand as needed by
     *   Lua scripts.
     */
    struct task_info **task;
    int in_task;            /* Non-zero if currently in task ctx  */
    int taskid;             /* Current taskid executing lua_stack */

    const char *lua_pattern;/* Glob for lua plugins */
    lua_stack_t lua_stack;
    int envref;             /* Global reference to Lua env obj    */
};

int prog_ctx_remove_completion_ref (struct prog_ctx *ctx, const char *fmt, ...);
int prog_ctx_add_completion_ref (struct prog_ctx *ctx, const char *fmt, ...);
static void wlog_msg (struct prog_ctx *ctx, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
static void wlog_error_kvs (struct prog_ctx *ctx, int fatal, const char *fmt, ...)
    __attribute__ ((format (printf, 3, 4)));
static void wlog_fatal (struct prog_ctx *ctx, int code, const char *format, ...)
    __attribute__ ((format (printf, 3, 4)));
static int wlog_err (struct prog_ctx *ctx, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
static void wlog_msg (struct prog_ctx *ctx, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
static void wlog_debug (struct prog_ctx *ctx, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));

void send_job_state_event (struct prog_ctx *ctx, const char *state);
int update_job_state (struct prog_ctx *ctx, const char *state);

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

static flux_t *prog_ctx_flux_handle (struct prog_ctx *ctx)
{
    struct task_info *t;

    if (!ctx->in_task)
        return (ctx->flux);

    t = prog_ctx_current_task (ctx);
    if (!t->f) {
        char name [128];
        t->f = flux_open (NULL, 0);
        snprintf (name, sizeof (name) - 1, "lwj.%"PRId64".%d",
		  ctx->id, t->globalid);
        flux_log_set_appname (t->f, name);
    }
    return (t->f);
}

static int archive_lwj (struct prog_ctx *ctx)
{
    char *to = ctx->kvspath;
    char *link = NULL;
    int rc = -1;

    wlog_msg (ctx, "archiving lwj %" PRIi64, ctx->id);

    if (asprintf (&link, "lwj-complete.%d.%"PRId64, ctx->epoch, ctx->id) < 0) {
        flux_log_error (ctx->flux, "archive_lwj: asprintf");
        goto out;
    }
    /*  Link lwj-complete.<hb>.id -> src
     */
    if (flux_kvs_symlink (ctx->flux, link, to) < 0)
        flux_log_error (ctx->flux, "flux_kvs_symlink (%s -> %s)", link, to);

    if ((rc = flux_kvs_commit_anon (ctx->flux, 0)) < 0)
        flux_log_error (ctx->flux, "flux_kvs_commit_anon");
out:
    free (link);
    return (rc);
}

static void vlog_error_kvs (struct prog_ctx *ctx, int fatal, const char *fmt, va_list ap)
{
    int n;
    int len = 2048;
    char msg [len];

    if ((n = vsnprintf (msg, len, fmt, ap) < 0))
        strcpy (msg, "Error formatting failure!");
    else if (n > len) {
        /* Indicate truncation */
        msg [len-2] = '+';
        msg [len-1] = '\0';
        n = len;
    }
    if (kz_put (ctx->kz_err, msg, strlen (msg)) < 0)
        flux_log (ctx->flux, LOG_EMERG,
            "Failed to write error to kvs error stream: %s",
            flux_strerror (errno));

    if (fatal) {
        // best effort
        if (flux_kvsdir_pack (ctx->kvs, "fatalerror", "i", fatal) == 0)
            (void)flux_kvs_commit_anon (ctx->flux, 0);
    }
}

static void wlog_error_kvs (struct prog_ctx *ctx, int fatal, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vlog_error_kvs (ctx, fatal, fmt, ap);
    va_end (ap);
}

static void wlog_fatal (struct prog_ctx *ctx, int code, const char *format, ...)
{
    flux_t *c = NULL;
    va_list ap;
    va_start (ap, format);
    if ((ctx != NULL) && ((c = prog_ctx_flux_handle (ctx)) != NULL))
        flux_vlog (c, LOG_EMERG, format, ap);
    else {
        vfprintf (stderr, format, ap);
        putc ('\n', stderr);
    }
    va_end (ap);

    /* Copy error to kvs if we're not in task context:
     *  (If in task context we'll be printing errors onto stderr)
     */
    if (c == ctx->flux && ctx->kz_err) {
        va_start (ap, format);
        vlog_error_kvs (ctx, 1, format, ap);
        va_end (ap);

        if (archive_lwj (ctx) < 0)
            flux_log_error (ctx->flux, "wlog_fatal: archive_lwj");
    }
    /*  Attempt to update job state to failed so commands do not hang */
    if (ctx->rankinfo.nodeid == 0 && ctx->flux) {
        update_job_state (ctx, "failed");
        send_job_state_event (ctx, "failed");
        flux_kvs_commit_anon (ctx->flux, 0);
    }

    if (code > 0)
        exit (code);
}

static int wlog_err (struct prog_ctx *ctx, const char *fmt, ...)
{
    flux_t *c = prog_ctx_flux_handle (ctx);
    va_list ap;
    va_start (ap, fmt);
    flux_vlog (c, LOG_ERR, fmt, ap);
    va_end (ap);
    return (-1);
}

static int fatalerr (struct prog_ctx *ctx, int code)
{
    if (code > 0)
        exit (code);
    return (0);
}

static void wlog_msg (struct prog_ctx *ctx, const char *fmt, ...)
{
    flux_t *c = prog_ctx_flux_handle (ctx);
    va_list ap;
    va_start (ap, fmt);
    flux_vlog (c, LOG_INFO, fmt, ap);
    va_end (ap);
}

static void wlog_debug (struct prog_ctx *ctx, const char *fmt, ...)
{
    flux_t *c = prog_ctx_flux_handle (ctx);
    va_list ap;
    va_start (ap, fmt);
    flux_vlog (c, LOG_DEBUG, fmt, ap);
    va_end (ap);
}


const char * prog_ctx_getopt (struct prog_ctx *ctx, const char *opt)
{
    if (ctx->options)
        return zhash_lookup (ctx->options, opt);
    return (NULL);
}

void prog_ctx_unsetopt (struct prog_ctx *ctx, const char *opt)
{
    if (ctx->options)
        zhash_delete (ctx->options, opt);
}

int prog_ctx_setopt (struct prog_ctx *ctx, const char *opt, const char *val)
{
    char *cpy = strdup (val);
    if (cpy == NULL)
        wlog_fatal (ctx, 1, "prog_ctx_setopt(%s=%s): Out of memory", opt, val);
    wlog_debug (ctx, "Setting option %s = %s", opt, val);
    zhash_insert (ctx->options, opt, cpy);
    zhash_freefn (ctx->options, opt, (zhash_free_fn *) free);
    return (0);
}

int globalid (struct prog_ctx *ctx, int localid)
{
    return (ctx->rankinfo.global_basis + localid);
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

/*
 *  Turn a possibly multi-line zio json_string into one kz_put() per
 *   line with a delayed commit until after the line(s) are complete.
 */
int task_kz_put_lines (struct task_info *t, kz_t *kz, const char *data, int len)
{
    int i, count;
    sds *line;

    assert (data != NULL && len > 0);

    line = sdssplitlen (data, strlen (data), "\n", 1, &count);
    if (!line)
        return (-1);

    /*  Reduce count by 1 if last "line" is empty since this represents
     *   a trailing newline and this empty string should not be included
     *   in output
     */
    if (line[count-1][0] == '\0')
        count--;

    for (i = 0; i < count; i++) {
        line[i] = sdscatlen (line[i], "\n", 1); /* replace newline */
        if (kz_put (kz, line[i], sdslen (line[i])) < 0)
            wlog_err (t->ctx, "kz_put (%s): %s", line[i], flux_strerror (errno));
    }

    sdsfreesplitres (line, count);

    if (!prog_ctx_getopt (t->ctx, "stdio-delay-commit"))
        kz_flush (kz);

    return (count);
}

static void wreck_pmi_close (struct task_info *t)
{
    if (t->pmi_zio)
        zio_destroy (t->pmi_zio);
    t->pmi_zio = NULL;
    if (t->pmi_client) {
        /* flush any lingering data before destroying */
        if (zio_flush (t->pmi_client) < 0)
            wlog_err (t->ctx, "zio_flush: %s", flux_strerror (errno));
        zio_destroy (t->pmi_client);
    }
    t->pmi_client = NULL;
}

static int wreck_pmi_send (void *cli, const char *s)
{
    struct task_info *t = cli;
    return zio_write (t->pmi_client, (void *) s, strlen (s));
}

static void wreck_pmi_line (struct task_info *t, const char *line)
{
    struct prog_ctx *ctx = t->ctx;
    int rc;
    if ((rc = pmi_simple_server_request (ctx->pmi, line, t)) < 0)
        wlog_fatal (ctx, 1, "pmi_simple_server_request: %s",
                    strerror (errno));
    if (rc == 1)
        wreck_pmi_close (t);
}

static int wreck_pmi_cb (zio_t *z, const char *s, int len, void *arg)
{
    struct task_info *t = arg;

    if (len > 0) /* !eof */
        wreck_pmi_line (t, s);
    else /* client closed fd */
        wreck_pmi_close (t);
    return (0);
}

int io_cb (zio_t *z, const char *s, int len, void *arg)
{
    struct task_info *t = arg;
    int type = z == t->zio [OUT] ? OUT : ERR;
    kz_t *kz = t->kz [type];

    if (len > 0)
        task_kz_put_lines (t, kz, s, len);
    else if (kz) {
        kz_close (kz);
        t->kz [type] = NULL;
        prog_ctx_remove_completion_ref (t->ctx, "task.%d.%s",
            t->id, ionames [type]);
    }
    return (0);
}

void kz_stdin (kz_t *kz, struct task_info *t)
{
    char *json_str;
    if ((json_str = kz_get_json (kz))) {
        zio_write_json (t->zio [IN], json_str);
        free (json_str);
    }
    else if (errno != 0 && errno != EAGAIN)
        wlog_err (t->ctx, "kz_get_json: %s", flux_strerror (errno));
    return;
}

int prog_ctx_io_flags (struct prog_ctx *ctx)
{
    int flags = KZ_FLAGS_NOCOMMIT_PUT;
    if (!prog_ctx_getopt (ctx, "stdio-commit-on-open"))
        flags |= KZ_FLAGS_NOCOMMIT_OPEN;
    if (!prog_ctx_getopt (ctx, "stdio-commit-on-close"))
        flags |= KZ_FLAGS_NOCOMMIT_CLOSE;
    return (flags);
}

kz_t *task_kz_open (struct task_info *t, int type)
{
    struct prog_ctx *ctx = t->ctx;
    kz_t *kz;
    char *key;
    int flags = prog_ctx_io_flags (ctx);

    if (type == IN)
        flags |= KZ_FLAGS_READ | KZ_FLAGS_NONBLOCK | KZ_FLAGS_RAW;
    else
        flags |= KZ_FLAGS_WRITE;

    if (asprintf (&key, "%s.%d.%s",
        ctx->kvspath, t->globalid, ioname (type)) < 0)
        wlog_fatal (ctx, 1, "task_kz_open: asprintf: %s", flux_strerror (errno));
    if ((kz = kz_open (ctx->flux, key, flags)) == NULL)
        wlog_fatal (ctx, 1, "kz_open (%s): %s", key, flux_strerror (errno));
    free (key);
    return (kz);
}

static void task_pmi_setup (struct task_info *t)
{
    /* Setup socketpair for pmi-simple server */
    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, t->pmi_fds) < 0)
        wlog_fatal (t->ctx, 1, "socketpair: %s", strerror (errno));

    /* ZIO object for reading pmi-simple server requests */
    t->pmi_zio = zio_reader_create ("pmi", t->pmi_fds[0], (void *) t);
    if (t->pmi_zio == NULL)
        wlog_fatal (t->ctx, 1, "zio_reader_create: %s", strerror (errno));

    zio_set_line_buffered (t->pmi_zio, 1);
    zio_set_send_cb (t->pmi_zio, wreck_pmi_cb);
    zio_set_raw_output (t->pmi_zio);

    t->pmi_client = zio_writer_create ("pmi", t->pmi_fds[0], t);
    if (t->pmi_client == NULL)
        wlog_fatal (t->ctx, 1, "zio_writer_create: %s", strerror (errno));
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

    if (!(t->zio [OUT] = zio_pipe_reader_create ("stdout", (void *) t)))
        wlog_fatal (ctx, 1, "task%d: zio_pipe_reader_create: %s",
                    id, strerror (errno));
    zio_set_send_cb (t->zio [OUT], io_cb);
    zio_set_raw_output (t->zio [OUT]);
    prog_ctx_add_completion_ref (ctx, "task.%d.stdout", id);

    if (!(t->zio [ERR] = zio_pipe_reader_create ("stderr", (void *) t)))
        wlog_fatal (ctx, 1, "task%d: zio_pipe_reader_create: %s",
                    id, strerror (errno));
    zio_set_send_cb (t->zio [ERR], io_cb);
    zio_set_raw_output (t->zio [ERR]);
    prog_ctx_add_completion_ref (ctx, "task.%d.stderr", id);

    t->zio [IN] = zio_pipe_writer_create ("stdin", (void *) t);

    for (i = 0; i < NR_IO; i++) {
        if (!(t->kz [i] = task_kz_open (t, i)))
            wlog_fatal (ctx, 1, "task%d: task_kz_open: %s",
                        id, strerror (errno));
    }
    kz_set_ready_cb (t->kz [IN], (kz_ready_f) kz_stdin, t);

    if (!prog_ctx_getopt (ctx, "no-pmi-server"))
        task_pmi_setup (t);

    prog_ctx_add_completion_ref (ctx, "task.%d.exit", id);

    return (t);
}

void task_io_flush (struct task_info *t)
{
    int i;
    for (i = 0; i < NR_IO; i++) {
        zio_flush (t->zio [i]);
        zio_destroy (t->zio [i]);
        /* Close all kz objects that haven't been closed already
         */
        if (t->kz [i]) {
            kz_close (t->kz [i]);
            t->kz [i] = NULL;
        }
    }
}

void task_info_destroy (struct task_info *t)
{
    if (t->kvs)
        flux_kvsdir_destroy (t->kvs);
    if (t->f)
        flux_close (t->f);
    wreck_pmi_close (t);
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
        wlog_err (ctx, "Failed to block signals in parent");

    ctx->signalfd = signalfd (-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (ctx->signalfd < 0)
        wlog_fatal (ctx, 1, "signalfd");
    return (0);
}

static char * realtime_string (char *buf, size_t sz)
{
    struct timespec tm;
    clock_gettime (CLOCK_REALTIME, &tm);
    memset (buf, 0, sz);
    snprintf (buf, sz, "%ju.%06ld", (uintmax_t) tm.tv_sec, tm.tv_nsec/1000);
    return (buf);
}

/*
 *  Send a message to rexec plugin
 */

static int get_executable_path (char *buf, size_t len)
{
    char *p;
    ssize_t n = readlink ("/proc/self/exe", buf, len);
    if (n < 0)
        return (-1);
    p = buf + n;
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

    for (i = 0; i < ctx->rankinfo.ntasks; i++) {
        task_io_flush (ctx->task [i]);
        task_info_destroy (ctx->task [i]);
    }

    free (ctx->kvspath);

    if (ctx->topic)
        free (ctx->topic);

    if (ctx->kvs)
        flux_kvsdir_destroy (ctx->kvs);

    if (ctx->fdw)
        flux_watcher_destroy (ctx->fdw);
    if (ctx->mw)
        flux_msg_handler_destroy (ctx->mw);

    free (ctx->envz);
    if (ctx->signalfd >= 0)
        close (ctx->signalfd);

    if (ctx->flux)
        flux_close (ctx->flux);

    if (ctx->pmi)
        pmi_simple_server_destroy (ctx->pmi);

    free (ctx->tasks_per_node);

    if (ctx->options)
        zhash_destroy (&ctx->options);
    if (ctx->completion_refs)
        zhash_destroy (&ctx->completion_refs);

    flux_kvs_txn_destroy (ctx->barrier_txn);

    free (ctx);
}

int prog_ctx_remove_completion_ref (struct prog_ctx *ctx, const char *fmt, ...)
{
    int rc = 0;
    int *intp;
    va_list ap;
    char *ref = NULL;

    va_start (ap, fmt);
    if ((rc = vasprintf (&ref, fmt, ap)) < 0) {
        flux_log_error (ctx->flux, "add_completion_ref: vasprintf");
        goto out;
    }
    if (!(intp = zhash_lookup (ctx->completion_refs, ref))) {
        errno = ENOENT;
        goto out;
    }
    if (--(*intp) == 0) {
        zhash_delete (ctx->completion_refs, ref);
        if (zhash_size (ctx->completion_refs) == 0)
            flux_reactor_stop (flux_get_reactor (ctx->flux));
    }
out:
    free (ref);
    va_end (ap);
    return (rc);
}

int prog_ctx_add_completion_ref (struct prog_ctx *ctx, const char *fmt, ...)
{
    int rc = 0;
    int *intp;
    va_list ap;
    char *ref = NULL;

    va_start (ap, fmt);
    if ((rc = vasprintf (&ref, fmt, ap)) < 0) {
        flux_log_error (ctx->flux, "add_completion_ref: vasprintf");
        goto out;
    }
    if (!(intp = zhash_lookup (ctx->completion_refs, ref))) {
        if (!(intp = calloc (1, sizeof (*intp)))) {
            flux_log_error (ctx->flux, "add_completion_ref: calloc");
            goto out;
        }
        if (zhash_insert (ctx->completion_refs, ref, intp) < 0) {
            flux_log_error (ctx->flux, "add_completion_ref: zhash_insert");
            free (intp);
            goto out;
        }
        zhash_freefn (ctx->completion_refs, ref, free);
    }
    rc = ++(*intp);
out:
    free (ref);
    va_end (ap);
    return (rc);
}

struct prog_ctx * prog_ctx_create (void)
{
    struct prog_ctx *ctx = malloc (sizeof (*ctx));
    if (!ctx)
        wlog_fatal (ctx, 1, "malloc");

    memset (ctx, 0, sizeof (*ctx));
    if (!(ctx->options = zhash_new ())
        || !(ctx->completion_refs = zhash_new ()))
        wlog_fatal (ctx, 1, "zhash_new");

    ctx->envz = NULL;
    ctx->envz_len = 0;

    ctx->id = -1;
    ctx->taskid = -1;

    ctx->envref = -1;

    if (get_executable_path (ctx->exedir, sizeof (ctx->exedir)) < 0)
        wlog_fatal (ctx, 1, "get_executable_path: %s", flux_strerror (errno));

    ctx->lua_stack = lua_stack_create ();
    ctx->lua_pattern = NULL;
    return (ctx);
}

int json_array_to_argv (struct prog_ctx *ctx,
        json_t *o, char ***argvp, int *argcp)
{
    int i;
    if (json_typeof (o) != JSON_ARRAY) {
        wlog_err (ctx, "json_array_to_argv: not an array");
        errno = EINVAL;
        return (-1);
    }

    *argcp = json_array_size (o);
    if (*argcp <= 0) {
        wlog_err (ctx, "json_array_to_argv: array length = %d", *argcp);
        return (-1);
    }

    *argvp = xzmalloc ((*argcp + 1) * sizeof (char **));

    for (i = 0; i < *argcp; i++) {
        json_t *ox = json_array_get (o, i);
        if (json_typeof (ox) != JSON_STRING) {
            wlog_err (ctx, "malformed cmdline");
            free (*argvp);
            return (-1);
        }
        if (!((*argvp) [i] = strdup (json_string_value (ox))))
            wlog_fatal (ctx, 1, "json_array_to_argv: strdup: Out of memory");
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

int prog_ctx_options_init (struct prog_ctx *ctx, const char *basedir)
{
    flux_future_t *f;
    char key [1024];
    char s [64];
    json_t *options;
    json_t *v;
    const char *opt;

    assert (strlen (basedir) < sizeof (key)+9);
    sprintf (key, "%s.options", basedir);

    if (!(f = flux_kvs_lookup (ctx->flux, 0, key))) {
        wlog_err (ctx, "flux_kvs_lookup (%s): %s\n",
                        key, flux_strerror (errno));
        return (-1);
    }
    if (flux_kvs_lookup_get_unpack (f, "o", &options) < 0) {
        flux_future_destroy (f);
        if (errno == ENOENT)
            return (0);
        wlog_err (ctx, "lookup_get_unpack (%s): %s\n",
                        key, flux_strerror (errno));
        return (-1);
    }

    json_object_foreach (options, opt, v) {
        switch (json_typeof (v)) {
            case JSON_NULL:
                prog_ctx_unsetopt (ctx, opt);
                break;
            case JSON_STRING:
                prog_ctx_setopt (ctx, opt, json_string_value (v));
                break;
            case JSON_INTEGER:
                if (json_integer_value (v) == 0) {
                    prog_ctx_unsetopt (ctx, opt);
                    break;
                }
                snprintf (s, sizeof (s) -1, "%ju",
                         (uintmax_t) json_integer_value (v));
                prog_ctx_setopt (ctx, opt, s);
                break;
            case JSON_TRUE:
                prog_ctx_setopt (ctx, opt, "");
                break;
            case JSON_FALSE:
                prog_ctx_unsetopt (ctx, opt);
                break;
            default:
                wlog_err (ctx, "skipping option '%s': invalid type", opt);
                break;
        }
    }
    flux_future_destroy (f);
    return (0);
}

static void prog_ctx_kz_err_open (struct prog_ctx *ctx)
{
    int n;
    char key [256];
    int kz_flags =
        KZ_FLAGS_NOCOMMIT_OPEN |
        KZ_FLAGS_NOCOMMIT_CLOSE |
        KZ_FLAGS_WRITE;

    n = snprintf (key, sizeof (key), "%s.log.%d", ctx->kvspath,
                  ctx->rankinfo.nodeid);
    if ((n < 0) || (n > sizeof (key)))
        wlog_fatal (ctx, 1, "snprintf: %s", flux_strerror (errno));
    if (!(ctx->kz_err = kz_open (ctx->flux, key, kz_flags)))
        wlog_fatal (ctx, 1, "kz_open (%s): %s", key, flux_strerror (errno));
}


static int * rcalc_tasks_per_node_create (rcalc_t *r)
{
    int i;
    int n = rcalc_total_nodes (r);
    int *tpn = calloc (n, sizeof (int));
    if (!tpn)
        return (NULL);
    for (i = 0; i < n; i++) {
        struct rcalc_rankinfo ri;
        if (rcalc_get_nth (r, i, &ri) < 0)
            goto fail;
        tpn[i] = ri.ntasks;
    }
    return tpn;
fail:
    free (tpn);
    return (NULL);

}

static int prog_ctx_process_rcalc (struct prog_ctx *ctx, rcalc_t *r)
{
    if (rcalc_distribute (r, ctx->total_ntasks) < 0)
        wlog_fatal (ctx, 1, "failed to distribute tasks over R_lite");

    ctx->nnodes = rcalc_total_nodes (r);

    if (rcalc_get_rankinfo (r, ctx->noderank, &ctx->rankinfo) < 0)
        wlog_fatal (ctx, 1, "no info about this rank in R_lite");

    if (!(ctx->tasks_per_node = rcalc_tasks_per_node_create (r)))
        wlog_fatal (ctx, 1, "Failed to create tasks-per-node array");

    return (0);
}

static int prog_ctx_read_R_lite (struct prog_ctx *ctx)
{
    rcalc_t *r = NULL;
    char *json_str;
    if (flux_kvsdir_get (ctx->kvs, "R_lite", &json_str) < 0)
        return (-1);
    if ((r = rcalc_create (json_str)) == NULL)
        wlog_fatal (ctx, 1, "failed to load R_lite");
    if (prog_ctx_process_rcalc (ctx, r) < 0)
        wlog_fatal (ctx, 1, "Failed to process resource information");
    rcalc_destroy (r);
    free (json_str);
    return (0);
}

int prog_ctx_load_lwj_info (struct prog_ctx *ctx)
{
    int i;
    char *json_str;
    json_t *v;
    flux_future_t *f = NULL;
    char *key;

    key = flux_kvsdir_key_at (ctx->kvs, "ntasks");
    if (!key || !(f = flux_kvs_lookup (ctx->flux, 0, key))
             || flux_kvs_lookup_get_unpack (f, "i", &ctx->total_ntasks) < 0)
        wlog_fatal (ctx, 1, "Failed to get ntasks from kvs");
    flux_future_destroy (f);
    free (key);

    if (prog_ctx_read_R_lite (ctx) < 0)
        wlog_fatal (ctx, 1, "Failed to read resource info from kvs");

    prog_ctx_kz_err_open (ctx);

    if (ctx->nnodes > ctx->total_ntasks) {
        wlog_fatal (ctx, 1,
            "nnodes assigned to job (%d) greater than ntasks (%d)!",
            ctx->nnodes, ctx->total_ntasks);
    }

    /*  Initialize global job options first from 'lwj.options', then
     *    override with options set locally for this job
     */
    if (prog_ctx_options_init (ctx, "lwj") < 0)
        wlog_fatal (ctx, 1, "failed to read lwj.options");
    if (prog_ctx_options_init (ctx, flux_kvsdir_key (ctx->kvs)) < 0)
        wlog_fatal (ctx, 1, "failed to read %s.options",
                    flux_kvsdir_key (ctx->kvs));

    if (flux_kvsdir_get (ctx->kvs, "cmdline", &json_str) < 0)
        wlog_fatal (ctx, 1, "kvs_get: cmdline");

    if (!(v = json_loads (json_str, 0, NULL)))
        wlog_fatal (ctx, 1, "kvs_get: cmdline: json parser failed");

    if (json_array_to_argv (ctx, v, &ctx->argv, &ctx->argc) < 0)
        wlog_fatal (ctx, 1, "Failed to get cmdline from kvs");

    ctx->task = xzmalloc (ctx->rankinfo.ntasks * sizeof (struct task_info *));
    for (i = 0; i < ctx->rankinfo.ntasks; i++)
        ctx->task[i] = task_info_create (ctx, i);

    wlog_msg (ctx, "lwj %" PRIi64 ": node%d: nprocs=%d, nnodes=%d, cmdline=%s",
                   ctx->id, ctx->rankinfo.nodeid, ctx->rankinfo.ntasks,
                   ctx->nnodes, json_str);
    free (json_str);
    json_decref (v);

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

static int flux_heartbeat_epoch (flux_t *h)
{
    long int epoch;
    char *p;
    const char *val = flux_attr_get (h, "heartbeat-epoch", NULL);
    if (!val)
        return 0;
    epoch = strtol (val, &p, 10);
    if (*p != '\0' || epoch == LONG_MIN || epoch == LONG_MAX)
        return 0;
    return epoch;
}

int prog_ctx_init_from_cmb (struct prog_ctx *ctx)
{
    const char *lua_pattern;
    char name [128];
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    /*
     * Connect to CMB over api socket
     */
    if (!(ctx->flux = flux_open (NULL, 0)))
        wlog_fatal (ctx, 1, "flux_open");

    snprintf (name, sizeof (name) - 1, "lwj.%"PRId64, ctx->id);
    flux_log_set_appname (ctx->flux, name);

    if (!(f = flux_kvs_lookup (ctx->flux, FLUX_KVS_READDIR, ctx->kvspath))
            || flux_kvs_lookup_get_dir (f, &dir) < 0
            || !(ctx->kvs = flux_kvsdir_copy (dir))) {
        wlog_fatal (ctx, 1, "flux_kvs_get_dir (%s): %s",
                   ctx->kvspath, flux_strerror (errno));
    }
    flux_future_destroy (f);

    if (flux_get_rank (ctx->flux, &ctx->noderank) < 0)
        wlog_fatal (ctx, 1, "flux_get_rank");

    if ((lua_pattern = flux_attr_get (ctx->flux, "wrexec.lua_pattern", NULL)))
        ctx->lua_pattern = lua_pattern;

    wlog_debug (ctx, "initializing from CMB: rank=%d", ctx->noderank);
    if (prog_ctx_load_lwj_info (ctx) < 0)
        wlog_fatal (ctx, 1, "Failed to load lwj info");

    /*
     *  Get current heartbeat-epoch. It will be updated by hb event
     *   listener locally, but we need initial value in case this job
     *   exits within one heartbeat.
     */
    ctx->epoch = flux_heartbeat_epoch (ctx->flux);

    return (0);
}

void close_task_fd_check (void *arg, int fd)
{
    struct task_info *t = arg;
    if (fd >= 3 && fd != t->pmi_fds[1])
        close (fd);
}

static int dup_fd (int fd, int newfd)
{
    assert (fd >= 0);
    assert (newfd >= 0);
    return dup2 (fd, newfd);
}

void child_io_setup (struct task_info *t)
{
    /*
     *  Close parent end of stdio fds in child
     */
    if (zio_close_dst_fd (t->zio [IN]) < 0
            || zio_close_src_fd (t->zio [OUT]) < 0
            || zio_close_src_fd (t->zio [ERR]) < 0)
        wlog_fatal (t->ctx, 1, "close: %s", flux_strerror (errno));

    /*
     *  Close parent end of PMI_FD
     */
    close (t->pmi_fds[0]);

    /*
     *  Dup appropriate fds onto child STDIN/STDOUT/STDERR
     */
    if (  (dup_fd (zio_src_fd (t->zio [IN]), STDIN_FILENO) < 0)
       || (dup_fd (zio_dst_fd (t->zio [OUT]), STDOUT_FILENO) < 0)
       || (dup_fd (zio_dst_fd (t->zio [ERR]), STDERR_FILENO) < 0))
        wlog_fatal (t->ctx, 1, "dup2: %s", flux_strerror (errno));

    fdwalk (close_task_fd_check, (void *)t);
}

void close_child_fds (struct task_info *t)
{
    if (zio_close_src_fd (t->zio [IN]) < 0
            || zio_close_dst_fd (t->zio [OUT]) < 0
            || zio_close_dst_fd (t->zio [ERR]) < 0)
        wlog_fatal (t->ctx, 1, "close: %s", flux_strerror (errno));
    close (t->pmi_fds[1]);
    t->pmi_fds[1] = -1;
}

void send_job_state_event (struct prog_ctx *ctx, const char *state)
{
    flux_msg_t *msg;
    char *topic = NULL;

    if (asprintf (&topic, "wreck.state.%s", state) < 0) {
        wlog_err (ctx, "failed to create state change event: %s", state);
        goto out;
    }

    if ((msg = flux_event_pack (topic, "{ s:I, s:s }",
                                "jobid", ctx->id,
                                "kvs_path", ctx->kvspath)) == NULL) {
        wlog_err (ctx, "flux_event_pack: %s", flux_strerror (errno));
        goto out;
    }

    if (flux_send (ctx->flux, msg, 0) < 0)
        wlog_err (ctx, "flux_send event: %s", flux_strerror (errno));

    flux_msg_destroy (msg);
out:
    free (topic);
}

int update_job_state (struct prog_ctx *ctx, const char *state)
{
    char buf [64];
    char *timestr = realtime_string (buf, sizeof (buf));
    char *key;

    assert (ctx->rankinfo.nodeid == 0);

    wlog_debug (ctx, "updating job state to %s", state);

    if (flux_kvsdir_pack (ctx->kvs, "state", "s", state) < 0)
        return (-1);

    if (asprintf (&key, "%s-time", state) < 0) {
        wlog_err (ctx, "update_job_state: asprintf: %s", strerror (errno));
        return (-1);
    }
    if (flux_kvsdir_pack (ctx->kvs, key, "s", timestr) < 0)
        return (-1);
    free (key);

    return (0);
}

int rexec_state_change (struct prog_ctx *ctx, const char *state)
{
    char *name;
    if (asprintf (&name, "lwj.%"PRId64".%s", ctx->id, state) < 0)
        wlog_fatal (ctx, 1, "rexec_state_change: asprintf: %s",
                    flux_strerror (errno));

    /* Rank 0 writes new job state */
    if ((ctx->rankinfo.nodeid == 0) && update_job_state (ctx, state) < 0)
        wlog_fatal (ctx, 1, "update_job_state");

    /* Wait for all wrexecds to finish and commit */
    if (flux_kvs_fence_anon (ctx->flux, name, ctx->nnodes, 0) < 0)
        wlog_fatal (ctx, 1, "flux_kvs_fence_anon");

    /* Also emit event to avoid racy flux_kvs_watch for clients */
    if (ctx->rankinfo.nodeid == 0)
        send_job_state_event (ctx, state);

    free (name);
    return (0);
}


int send_startup_message (struct prog_ctx *ctx)
{
    const char * state = "running";

    if (prog_ctx_getopt (ctx, "stop-children-in-exec"))
        state = "sync";

    if (rexec_state_change (ctx, state) < 0) {
        wlog_err (ctx, "rexec_state_change");
        return (-1);
    }

    return (0);
}


static int
exitstatus_watcher (const char *key, const char *str, void *arg, int err)
{
    struct prog_ctx *ctx = arg;
    flux_t *h = ctx->flux;
    int count;
    json_t *o;

    if (err || !(o = json_loads (str, 0, NULL))
            || (json_unpack (o, "{s:i}", "count", &count) < 0)) {
        if (err != ENOENT)
            flux_log (h, LOG_ERR, "exitstatus_watcher: %s",
                    err ? flux_strerror (err) : "Jfromstr failed");
        return (0);
    }

    /* Once count is fully populated, release the watch on the
     *  exit_status dir so reactor loop can exit
     */
    if (count == ctx->total_ntasks) {
        flux_kvs_unwatch (h, key);
        prog_ctx_remove_completion_ref (ctx, "exit_status");
    }

    json_decref (o);
    return (0);
}

static int wait_for_task_exit_aggregate (struct prog_ctx *ctx)
{
    int rc = 0;
    char *key = NULL;
    flux_t *h = ctx->flux;

    if (asprintf (&key, "%s.exit_status", ctx->kvspath) <= 0) {
        flux_log_error (h, "wait_for_aggregate: asprintf");
        return (-1);
    }

    /*  Add completion reference *before*  flux_kvs_watch() since
     *   callback may be called synchronously and thus the cb
     *   may attempt to unreference this completion ref before
     *   we return
     */
    prog_ctx_add_completion_ref (ctx, "exit_status");

    if ((rc = flux_kvs_watch (h, key, exitstatus_watcher, ctx)) < 0)
        flux_log_error (h, "flux_kvs_watch_dir");
    free (key);
    return (rc);
}

static int aggregator_push_task_exit (struct task_info *t)
{
    int n, rc = 0;
    char key [1024];
    char idstr [16];
    struct prog_ctx *ctx = t->ctx;
    flux_t *h = ctx->flux;
    flux_future_t *f;

    if ((n = snprintf (key, sizeof (key), "%s.exit_status", ctx->kvspath)) < 0
        || (n >= sizeof (key))
        || (n = snprintf (idstr, sizeof (idstr), "%d", t->globalid)) < 0
        || (n >= sizeof (idstr))) {
        flux_log_error (h, "aggregator_push_task_exit: snprintf");
        return (-1);
    }

    if (!(f = flux_rpc_pack (h, "aggregator.push", FLUX_NODEID_ANY, 0,
                                "{s:s,s:i,s:{s:i}}",
                                "key", key,
                                "total", ctx->total_ntasks,
                                "entries", idstr, t->status))) {
        flux_log_error (h, "flux_rpc");
        rc = -1;
    }

    if (f && flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "flux_future_get");
        rc = -1;
    }
    flux_future_destroy (f);

    if (t->ctx->noderank == 0 && t->id == 0)
        rc = wait_for_task_exit_aggregate (t->ctx);
    return (rc);
}

void prog_ctx_unsetenv (struct prog_ctx *ctx, const char *name)
{
    envz_remove (&ctx->envz, &ctx->envz_len, name);
}

int prog_ctx_setenv (struct prog_ctx *ctx, const char *name, const char *value)
{
    /* Overwrite: */
    prog_ctx_unsetenv (ctx, name);
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
        wlog_fatal (ctx, 1, "fork: %s", flux_strerror (errno));
    if (cpid == 0) {
        /* give each task its own process group so we can use killpg(2) */
        setpgrp ();

        child_io_setup (t);

        if (sigmask_unblock_all () < 0)
            fprintf (stderr, "sigprocmask: %s\n", flux_strerror (errno));

        /*
         *  Set current taskid and invoke rexecd_task_init
         */
        ctx->taskid = i;
        ctx->in_task = 1;
        lua_stack_call (ctx->lua_stack, "rexecd_task_init");

        prog_ctx_setenv  (ctx, "FLUX_URI", getenv ("FLUX_URI"));
        prog_ctx_setenvf (ctx, "FLUX_TASK_RANK", 1, "%d", t->globalid);
        prog_ctx_setenvf (ctx, "FLUX_TASK_LOCAL_ID", 1, "%d", i);

        if (ctx->pmi) {
            prog_ctx_setenvf (ctx, "PMI_FD", 1, "%d", t->pmi_fds[1]);
            prog_ctx_setenvf (ctx, "PMI_RANK", 1, "%d", t->globalid);
            prog_ctx_setenvf (ctx, "PMI_SIZE", 1, "%d", t->ctx->total_ntasks);
        }

        if (prog_ctx_getopt (ctx, "stop-children-in-exec")) {
            /* Stop process on exec with parent attached */
            ptrace (PTRACE_TRACEME, 0, NULL, 0);
        }

        /*
         *  Reassign environment:
         */
        environ = prog_ctx_env_create (ctx);
        if (execvp (ctx->argv [0], ctx->argv) < 0) {
            fprintf (stderr, "execvp: %s\n", flux_strerror (errno));
            exit (255);
        }
        exit (255);
    }

    /*
     *  Parent: Close child fds
     */
    close_child_fds (t);
    wlog_debug (ctx, "task%d: pid %d (%s): started", i, cpid, ctx->argv [0]);
    t->pid = cpid;


    return (0);
}

char *gtid_list_create (struct prog_ctx *ctx, char *buf, size_t len)
{
    int i, n = 0;
    int truncated = 0;

    memset (buf, 0, len);

    for (i = 0; i < ctx->rankinfo.ntasks; i++) {
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
            n += strlen (buf) + 1;
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

static flux_kvsdir_t *prog_ctx_kvsdir (struct prog_ctx *ctx)
{
    flux_t *h = prog_ctx_flux_handle (ctx);
    struct task_info *t;

    if (!ctx->in_task)
        return (ctx->kvs);

    t = prog_ctx_current_task (ctx);
    if (!t->kvs) {
        char *key = NULL;
        flux_future_t *f = NULL;
        const flux_kvsdir_t *dir;
        if (asprintf (&key, "%s.%d", ctx->kvspath, t->globalid) < 0
                    || !(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, key))
                    || flux_kvs_lookup_get_dir (f, &dir) < 0
                    || !(t->kvs = flux_kvsdir_copy (dir))) {
            if (errno != ENOENT)
                wlog_err (ctx, "flux_kvs_lookup (%s): %s",
                          key ? key : "?", flux_strerror (errno));
        }
        flux_future_destroy (f);
        free (key);
    }
    return (t->kvs);
}

static int l_wreck_log_msg (lua_State *L)
{
    struct prog_ctx *ctx = l_get_prog_ctx (L, 1);
    const char *msg;
    if (lua_gettop (L) > 2 && l_format_args (L, 2) < 0)
        return (2); /* error on stack from l_format_args */
    if (!(msg = lua_tostring (L, 2)))
        return lua_pusherror (L, "required arg to log_msg missing");
    wlog_msg (ctx, "%s", msg);
    return (0);
}

static int wreck_log_error (lua_State *L, int fatal)
{
    struct prog_ctx *ctx = l_get_prog_ctx (L, 1);
    const char *s;
    if (lua_gettop (L) > 2 && l_format_args (L, 2) < 0)
        return (2); /* error on stack from l_format_args */
    if (!(s = lua_tostring (L, 2)))
        return lua_pusherror (L, "required arg to die missing");
    wlog_error_kvs (ctx, fatal, "%s", s);
    return (0);
}

static int l_wreck_die (lua_State *L)
{
    return wreck_log_error (L, 1);
}

static int l_wreck_log_error (lua_State *L)
{
    return wreck_log_error (L, 0);
}

static int l_wreck_tasks_per_node (struct prog_ctx *ctx, lua_State *L)
{
    int i;
    int t;
    lua_newtable (L);
    t = lua_gettop (L);
    for (i = 0; i < ctx->nnodes; i++) {
        lua_pushnumber (L, i);
        lua_pushnumber (L, ctx->tasks_per_node [i]);
        lua_settable (L, t);
    }
    return (1);
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
	if (t == NULL)
            return lua_pusherror (L, "Not in task context");
        lua_pushnumber (L, t->globalid);
        return (1);
    }
    if (strcmp (key, "taskid") == 0) {
	if (t == NULL)
            return lua_pusherror (L, "Not in task context");
        lua_pushnumber (L, t->id);
        return (1);
    }
    if (strcmp (key, "kvsdir") == 0) {
        lua_push_kvsdir_external (L, ctx->kvs);
        return (1);
    }
    if (strcmp (key, "by_task") == 0) {
        flux_kvsdir_t *d;
        if (t == NULL)
            return lua_pusherror (L, "Not in task context");
        if (!(d = prog_ctx_kvsdir (ctx)))
            return lua_pusherror (L, (char *)flux_strerror (errno));
        lua_push_kvsdir_external (L, d);
        return (1);
    }
    if (strcmp (key, "flux") == 0) {
        lua_push_flux_handle_external (L, prog_ctx_flux_handle (ctx));
        return (1);
    }
    if (strcmp (key, "nodeid") == 0) {
        lua_pushnumber (L, ctx->rankinfo.nodeid);
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
    if (strcmp (key, "log_msg") == 0) {
        lua_pushcfunction (L, l_wreck_log_msg);
        return (1);
    }
    if (strcmp (key, "die") == 0) {
        lua_pushcfunction (L, l_wreck_die);
        return (1);
    }
    if (strcmp (key, "log_error") == 0) {
        lua_pushcfunction (L, l_wreck_log_error);
        return (1);
    }
    if (strcmp (key, "nnodes") == 0) {
        lua_pushnumber (L, ctx->nnodes);
        return (1);
    }
    if (strcmp (key, "tasks_per_node") == 0)
        return (l_wreck_tasks_per_node (ctx, L));
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
    luaL_setfuncs (L, wreck_methods, 0);
    luaL_newmetatable (L, "WRECK.environ");
    luaL_setfuncs (L, environ_methods, 0);
    l_push_prog_ctx (L, ctx);
    lua_setglobal (L, "wreck");
    wlog_debug (ctx, "reading lua files from %s", ctx->lua_pattern);
    lua_stack_append_file (ctx->lua_stack, ctx->lua_pattern);
    return (0);
}

int task_exit (struct task_info *t, int status)
{
    struct prog_ctx *ctx = t->ctx;

    wlog_debug (ctx, "task%d: pid %d (%s) exited with status 0x%04x",
            t->id, t->pid, ctx->argv [0], status);
    t->status = status;
    t->exited = 1;

    ctx->taskid = t->id;
    lua_stack_call (ctx->lua_stack, "rexecd_task_exit");


    if (aggregator_push_task_exit (t) < 0)
        wlog_err (ctx, "Sending exit message failed!");

    prog_ctx_remove_completion_ref (t->ctx, "task.%d.exit", t->id);

    return (0);
}

int start_trace_task (struct task_info *t)
{
    int status;
    pid_t pid = t->pid;
    struct prog_ctx *ctx = t->ctx;

    int rc = waitpid (pid, &status, WUNTRACED);
    if (rc < 0) {
        wlog_err (ctx, "start_trace: waitpid: %s", flux_strerror (errno));
        return (-1);
    }
    if (WIFSTOPPED (status)) {
        /*
         *  Send SIGSTOP and detach from process.
         */
        if (kill (pid, SIGSTOP) < 0) {
            wlog_err (ctx, "start_trace: kill: %s", flux_strerror (errno));
            return (-1);
        }
        if (ptrace (PTRACE_DETACH, pid, NULL, 0) < 0) {
            wlog_err (ctx, "start_trace: ptrace: %s", flux_strerror (errno));
            return (-1);
        }
        return (0);
    }

    /*
     *  Otherwise, did task exit?
     */
    if (WIFEXITED (status)) {
        wlog_err (ctx, "start_trace: task unexpectedly exited");
        task_exit (t, status);
    }
    else
        wlog_err (ctx, "start_trace: Unexpected status 0x%04x", status);

    return (-1);
}

int rexecd_init (struct prog_ctx *ctx)
{
    flux_future_t *f = NULL;
    char *key;
    int errnum = 0;
    char *name = NULL;
    int rc = asprintf (&name, "lwj.%"PRId64".init", (uintmax_t) ctx->id);
    if (rc < 0) {
        errno = ENOMEM;
        wlog_fatal (ctx, 1, "rexecd_init: asprintf: %s", flux_strerror (errno));
    }

    lua_stack_call (ctx->lua_stack, "rexecd_init");

    /*  Wait for all nodes to finish calling init plugins:
     */
    if (flux_kvs_fence_anon (ctx->flux, name, ctx->nnodes, 0) < 0)
        wlog_fatal (ctx, 1, "flux_kvs_fence_anon %s: %s",
                    name, flux_strerror (errno));

    /*  Now, check for `fatalerror` key in the kvs, which indicates
     *   one or more nodes encountered a fatal error and we should abort
     */
    key = flux_kvsdir_key_at (ctx->kvs, "fatalerror");
    if (!key || !(f = flux_kvs_lookup (ctx->flux, 0, key))
             || (flux_kvs_lookup_get_unpack (f, "i", &errnum) < 0 && errno != ENOENT)) {
        errnum = 1;
        wlog_msg (ctx, "Error: flux_kvsdir_get (fatalerror): %s\n", flux_strerror (errno));
    }
    flux_future_destroy (f);
    free (key);
    if (errnum) {
        /*  Only update job state and print initialization error message
         *   on rank 0.
         */
        if (rexec_state_change (ctx, "failed") < 0)
            wlog_err (ctx, "failed to update job state!");
        wlog_err (ctx, "Error in initialization, terminating job");
        ctx->errnum = errnum;
    }
    free (name);
    return (errnum ? -1 : 0);
}

int exec_commands (struct prog_ctx *ctx)
{
    char buf [4096];
    int i;
    int stop_children = 0;

    wreck_lua_init (ctx);
    if (rexecd_init (ctx) < 0)
        return (-1);

    prog_ctx_setenvf (ctx, "FLUX_JOB_ID",    1, "%d", ctx->id);
    prog_ctx_setenvf (ctx, "FLUX_JOB_NNODES",1, "%d", ctx->nnodes);
    prog_ctx_setenvf (ctx, "FLUX_NODE_ID",   1, "%d", ctx->rankinfo.nodeid);
    prog_ctx_setenvf (ctx, "FLUX_JOB_SIZE",  1, "%d", ctx->total_ntasks);
    prog_ctx_setenvf (ctx, "FLUX_JOB_KVSPATH", 1, "%s", ctx->kvspath);
    gtid_list_create (ctx, buf, sizeof (buf));
    prog_ctx_setenvf (ctx, "FLUX_LOCAL_RANKS",  1, "%s", buf);

    for (i = 0; i < ctx->rankinfo.ntasks; i++)
        exec_command (ctx, i);

    if (prog_ctx_getopt (ctx, "stop-children-in-exec"))
        stop_children = 1;
    for (i = 0; i < ctx->rankinfo.ntasks; i++) {
        if (stop_children)
            start_trace_task (ctx->task [i]);
    }

    return send_startup_message (ctx);
}

struct task_info *pid_to_task (struct prog_ctx *ctx, pid_t pid)
{
    int i;
    struct task_info *t = NULL;

    for (i = 0; i < ctx->rankinfo.ntasks; i++) {
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
        /* Ignore ECHILD (No child processes) */
        if (errno != ECHILD)
            wlog_err (ctx, "waitpid: %s", flux_strerror (errno));
        return (0);
    }

    if ((t = pid_to_task (ctx, wpid)) == NULL)
        return wlog_err (ctx, "Failed to find task for pid %d", wpid);

    task_exit (t, status);

    return (1);
}

int prog_ctx_signal (struct prog_ctx *ctx, int sig)
{
    int i;
    for (i = 0; i < ctx->rankinfo.ntasks; i++) {
        pid_t pid = ctx->task[i]->pid;
        /*  XXX: there is a race between a process starting and
         *   changing its process group, so killpg(2) may fail here
         *   if it happens to execute during that window. In that case,
         *   killing the individual task should work. Therefore,
         *   attempt kill after killpg.
         */
        if ((killpg (pid, sig) < 0) && (kill (pid, sig) < 0))
            wlog_err (ctx, "kill (%d): %s", (int) pid, flux_strerror (errno));
    }
    return (0);
}

int cleanup (struct prog_ctx *ctx)
{
    return prog_ctx_signal (ctx, SIGKILL);
}

void signal_cb (flux_reactor_t *r, flux_watcher_t *fdw, int revents, void *arg)
{
    struct prog_ctx *ctx = arg;
    int fd = flux_fd_watcher_get_fd (fdw);
    int n;
    struct signalfd_siginfo si;

    n = read (fd, &si, sizeof (si));
    if (n < 0) {
        wlog_err (ctx, "signal_cb: read: %s", flux_strerror (errno));
        return;
    }
    else if (n != sizeof (si)) {
        wlog_err (ctx, "signal_cb: partial read?");
        return;
    }

    if (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT) {
        cleanup (ctx);
        return; /* Continue, so we reap children */
    }

    /* SIGCHLD assumed */
    while (reap_child (ctx))
        ++ctx->exited;

    return;
}

void ev_cb (flux_t *f, flux_msg_handler_t *mw,
           const flux_msg_t *msg, struct prog_ctx *ctx)
{
    int base;
    const char *topic;

    if (flux_msg_get_topic (msg, &topic) < 0) {
        wlog_err (ctx, "flux_msg_get_topic: %s", flux_strerror (errno));
        return;
    }
    if (strcmp (topic, "hb") == 0) {
        /* ignore mangled hb messages */
        flux_heartbeat_decode (msg, &ctx->epoch);
        return;
    }
    base = strlen (ctx->topic);
    if (strcmp (topic+base, "kill") == 0) {
        /*  Default signal is 9 (SIGKILL) unless overidden by event payload */
        int sig = 9;
        flux_msg_unpack (msg, "{s:i}", "signal", &sig);
        wlog_msg (ctx, "Killing jobid %" PRIi64 " with signal %d", ctx->id, sig);
        prog_ctx_signal (ctx, sig);
    }
}

int task_info_io_setup (struct task_info *t)
{
    flux_t *f = t->ctx->flux;
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

    if (asprintf (&ctx->topic, "wreck.%"PRId64".", ctx->id) < 0)
           wlog_fatal (ctx, 1, "failed to create topic string: %s",
                      flux_strerror (errno));

    if (flux_event_subscribe (ctx->flux, ctx->topic) < 0)
        return wlog_err (ctx, "flux_event_subscribe (%s): %s",
                        ctx->topic, flux_strerror (errno));

    if (flux_event_subscribe (ctx->flux, "hb") < 0)
        return wlog_err (ctx, "flux_event_subscribe (hb): %s",
                        flux_strerror (errno));

    for (i = 0; i < ctx->rankinfo.ntasks; i++) {
        task_info_io_setup (ctx->task [i]);
        zio_flux_attach (ctx->task[i]->pmi_zio, ctx->flux);
        zio_flux_attach (ctx->task[i]->pmi_client, ctx->flux);
    }

    ctx->mw = flux_msg_handler_create (ctx->flux,
            FLUX_MATCH_EVENT,
            (flux_msg_handler_f) ev_cb,
            (void *) ctx);

    ctx->fdw = flux_fd_watcher_create (flux_get_reactor (ctx->flux),
            ctx->signalfd,
            FLUX_POLLIN | FLUX_POLLERR,
            signal_cb,
            (void *) ctx);

    flux_watcher_start (ctx->fdw);
    flux_msg_handler_start (ctx->mw);

    return (0);
}

static int wreck_pmi_kvs_put (void *arg, const char *kvsname,
        const char *key, const char *val)
{
    struct prog_ctx *ctx = arg;
    char *kvskey = NULL;
    int rc = -1;

    if (asprintf (&kvskey, "%s.%s", kvsname, key) < 0) {
        wlog_err (ctx, "pmi_kvs_put: asprintf: %s", strerror (errno));
        goto done;
    }
    if (!ctx->barrier_txn && !(ctx->barrier_txn = flux_kvs_txn_create ())) {
        wlog_err (ctx, "pmi_kvs_put: flux_kvs_txn_create: %s",
                  strerror (errno));
        goto done;
    }
    if (flux_kvs_txn_pack (ctx->barrier_txn, 0, kvskey, "s", val) < 0) {
        wlog_err (ctx, "pmi_kvs_put: flux_kvs_txn_pack: %s", strerror (errno));
        goto done;
    }
    rc = 0;
done:
    free (kvskey);
    return (rc);
}


static int wreck_pmi_kvs_get (void *arg, const char *kvsname, const char *key,
        char *val, int len)
{
    struct prog_ctx *ctx = arg;
    char *kvskey = NULL;
    const char *s = NULL;
    int rc = 0;
    flux_future_t *f;

    if (asprintf (&kvskey, "%s.%s", kvsname, key) < 0) {
        wlog_err (ctx, "pmi_kvs_get: asprintf: %s", strerror (errno));
        return (-1);
    }

    if (!(f = flux_kvs_lookup (ctx->flux, 0, kvskey))) {
        wlog_err (ctx, "pmi_kvs_get: flux_kvs_lookup: %s", strerror (errno));
        free (kvskey);
        return (-1);
    }
    if (flux_kvs_lookup_get_unpack (f, "s", &s) < 0) {
        if (errno != ENOENT)
            wlog_err (ctx, "pmi_kvs_get: flux_kvs_lookup_get_unpack (s,%s): %s",
                      kvskey, strerror (errno));
        free (kvskey);
        flux_future_destroy (f);
        return (-1);
    }

    if (strlen (s) >= len) {
        rc = -1;
        errno = ENOSPC;
    }
    else
        strcpy (val, s);

    free (kvskey);
    flux_future_destroy (f);

    return (rc);
}

static void wreck_barrier_next (struct prog_ctx *ctx)
{
    snprintf (ctx->barrier_name, sizeof (ctx->barrier_name), "lwj.%"PRId64".%u",
              ctx->id, ctx->barrier_sequence++);
}

static void wreck_barrier_complete (flux_future_t *f, void *arg)
{
    struct prog_ctx *ctx = arg;
    int rc = flux_future_get (f, NULL);
    pmi_simple_server_barrier_complete (ctx->pmi, rc);
    flux_future_destroy (f);
    wreck_barrier_next (ctx);
}

static int wreck_pmi_barrier_enter (void *arg)
{
    struct prog_ctx *ctx = arg;
    flux_future_t *f = NULL;

    if (!ctx->barrier_txn && !(ctx->barrier_txn = flux_kvs_txn_create ())) {
        wlog_err (ctx, "pmi_barrier_enter: flux_kvs_txn_create: %s",
                  strerror (errno));
        goto out;
    }
    if ((f = flux_kvs_fence (ctx->flux, 0, ctx->barrier_name,
                             ctx->nnodes, ctx->barrier_txn)) == NULL) {
        wlog_err (ctx, "pmi_barrier_enter: flux_kvs_fence: %s",
                  strerror (errno));
        goto out;
    }
    if (flux_future_then (f, -1., wreck_barrier_complete, ctx) < 0) {
        wlog_err (ctx, "pmi_barrier_enter: flux_future_then: %s",
                  strerror (errno));
        flux_future_destroy (f);
        f = NULL;
    }
    flux_kvs_txn_destroy (ctx->barrier_txn);
    ctx->barrier_txn = NULL;
out:
    return (f== NULL ? -1 : 0);
}

static void wreck_pmi_debug_trace (void *client, const char *buf)
{
    struct task_info *t = client;
    fprintf (stderr, "%d: %s", t->globalid, buf);
}

static int prog_ctx_initialize_pmi (struct prog_ctx *ctx)
{
    char *kvsname;
    struct pmi_simple_ops ops = {
        .kvs_put = wreck_pmi_kvs_put,
        .kvs_get = wreck_pmi_kvs_get,
        .barrier_enter = wreck_pmi_barrier_enter,
        .response_send = wreck_pmi_send,
        .debug_trace = wreck_pmi_debug_trace,
    };
    int flags = 0;
    if (asprintf (&kvsname, "%s.pmi", ctx->kvspath) < 0) {
        flux_log_error (ctx->flux, "initialize_pmi: asprintf");
        return (-1);
    }
    if (prog_ctx_getopt (ctx, "trace-pmi-server"))
        flags |= PMI_SIMPLE_SERVER_TRACE;
    ctx->barrier_sequence = 0;
    wreck_barrier_next (ctx);
    ctx->pmi = pmi_simple_server_create (ops, (int) ctx->id,
                                         ctx->total_ntasks,
                                         ctx->rankinfo.ntasks,
                                         kvsname,
                                         flags,
                                         ctx);
    if (!ctx->pmi)
        flux_log_error (ctx->flux, "pmi_simple_server_create");
    free (kvsname);
    return (ctx->pmi == NULL ? -1 : 0);
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

int prog_ctx_get_id (struct prog_ctx *ctx, optparse_t *p)
{
    const char *kvspath;
    const char *id;
    char *end;

    if (!optparse_getopt (p, "kvs-path", &kvspath))
        wlog_fatal (ctx, 1, "Required arg --kvs-path missing");
    if (!(ctx->kvspath = strdup (kvspath)))
        wlog_fatal (ctx, 1, "prog_ctx_get_id: strdup failed");

    if (!optparse_getopt (p, "lwj-id", &id)) {
        /* Assume lwj id is last component of kvs-path */
        if ((id = strrchr (kvspath, '.')) == NULL || *(++id) == '\0')
            wlog_fatal (ctx, 1, "Unable to get lwj id from kvs-path");
    }

    errno = 0;
    ctx->id = strtol (id, &end, 10);
    if (  (*end != '\0')
       || (ctx->id == 0 && errno == EINVAL)
       || (ctx->id == ULONG_MAX && errno == ERANGE))
           wlog_fatal (ctx, 1, "--lwj-id=%s invalid", id);

    return (0);
}

static int increase_nofile_limit (void)
{
    struct rlimit rlim;
    if (getrlimit (RLIMIT_NOFILE, &rlim) < 0)
        return (-1);
    rlim.rlim_cur = rlim.rlim_max;
    return (setrlimit (RLIMIT_NOFILE, &rlim));
}

static int do_hwloc_core_affinity (struct prog_ctx *ctx)
{
    int rc = -1;
    int depth, i;
    hwloc_topology_t topology;
    hwloc_cpuset_t coreset = NULL;
    hwloc_cpuset_t resultset = NULL;
    flux_t *h = ctx->flux;

    if (hwloc_topology_init (&topology) < 0) {
        flux_log_error (h, "hwloc_topology_init");
        return (-1);
    }
    if (hwloc_topology_load (topology) < 0) {
        flux_log_error (h, "hwloc_topology_load");
        goto out;
    }
    if (!(coreset = hwloc_bitmap_alloc ())
       || !(resultset = hwloc_bitmap_alloc ())) {
        flux_log_error (h, "hwloc_bitmap_alloc");
        goto out;
    }
    if (hwloc_bitmap_list_sscanf (coreset, ctx->rankinfo.cores) < 0) {
        flux_log_error (h, "hwloc_sscanf(%s)", ctx->rankinfo.cores);
        goto out;
    }
    depth = hwloc_get_type_depth (topology, HWLOC_OBJ_CORE);
    if (depth == HWLOC_TYPE_DEPTH_UNKNOWN
       || depth == HWLOC_TYPE_DEPTH_MULTIPLE) {
        flux_log_error (h, "hwloc_get_type_depth (CORE)");
        goto out;
    }
    i = hwloc_bitmap_first (coreset);
    while (i >= 0) {
        hwloc_obj_t core = hwloc_get_obj_by_depth (topology, depth, i);
        if (core)
            hwloc_bitmap_or (resultset, resultset, core->cpuset);
        else
            flux_log_error (h, "hwloc_get_obj_by_depth: core%d", i);
        i = hwloc_bitmap_next (coreset, i);
    }
    if ((rc = hwloc_set_cpubind (topology, resultset, 0)) < 0)
        flux_log_error (h, "hwloc_set_cpubind: %s", strerror (errno));
out:
    hwloc_bitmap_free (resultset);
    hwloc_bitmap_free (coreset);
    hwloc_topology_destroy (topology);
    return (0);
}

int main (int ac, char **av)
{
    int code = 0;
    int parent_fd = -1;
    int exec_rc = -1;
    struct prog_ctx *ctx = NULL;
    optparse_t *p;
    struct optparse_option opts [] = {
        { .name =    "lwj-id",
          .key =     1000,
          .has_arg = 1,
          .arginfo = "ID",
          .usage =   "Operate on LWJ id [ID]",
        },
       { .name =    "kvs-path",
          .has_arg = 1,
          .arginfo = "DIR",
          .usage =   "Operate on LWJ in DIR instead of lwj.<id>",
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
    if (optparse_set (p, OPTPARSE_FATALERR_FN, fatalerr) != OPTPARSE_SUCCESS)
        wlog_fatal (ctx, 1, "optparse_set FATALERR_FN");
    if (optparse_set (p, OPTPARSE_FATALERR_HANDLE, ctx) != OPTPARSE_SUCCESS)
        wlog_fatal (ctx, 1, "optparse_set FATALERR_HANDLE");
    if (optparse_add_option_table (p, opts) != OPTPARSE_SUCCESS)
        wlog_fatal (ctx, 1, "optparse_add_option_table");
    if (optparse_parse_args (p, ac, av) < 0)
        wlog_fatal (ctx, 1, "parse args");

    daemonize ();

    ctx = prog_ctx_create ();
    signalfd_setup (ctx);

    if (increase_nofile_limit () < 0)
        wlog_fatal (ctx, 1, "Failed to increase RLIMIT_NOFILE");

    if (prog_ctx_get_id (ctx, p) < 0)
        wlog_fatal (ctx, 1, "Failed to get lwj id from cmdline");

    if (prog_ctx_init_from_cmb (ctx) < 0) /* Nothing to do here */
        exit (0);

    if (prog_ctx_getopt (ctx, "cpu-affinity"))
        do_hwloc_core_affinity (ctx);

    if (rexec_state_change (ctx, "starting") < 0)
        wlog_fatal (ctx, 1, "rexec_state_change");

    if ((parent_fd = optparse_get_int (p, "parent-fd", -1)) >= 0)
        prog_ctx_signal_parent (parent_fd);
    prog_ctx_reactor_init (ctx);

    if (!prog_ctx_getopt (ctx, "no-pmi-server") && prog_ctx_initialize_pmi (ctx) < 0)
        wlog_fatal (ctx, 1, "failed to initialize pmi-server");

    exec_rc = exec_commands (ctx);

    if (exec_rc == 0 && flux_reactor_run (flux_get_reactor (ctx->flux), 0) < 0)
        wlog_err (ctx, "flux_reactor_run: %s", flux_strerror (errno));

    if (ctx->rankinfo.nodeid == 0) {
        /* At final job state, archive the completed lwj back to the
         * its final resting place in lwj.<id>
         */
        if (archive_lwj (ctx) < 0)
            wlog_err (ctx, "archive_lwj failed");
    }

    if (exec_rc == 0) {
        rexec_state_change (ctx, "completing");
        lua_stack_call (ctx->lua_stack, "rexecd_complete");

        rexec_state_change (ctx, "complete");
        wlog_msg (ctx, "job complete. exiting...");

        lua_stack_call (ctx->lua_stack, "rexecd_exit");
    }

    code = ctx->errnum;
    prog_ctx_destroy (ctx);
    return (code);
}

/*
 *  vi: ts=4 sw=4 expandtab
 */
