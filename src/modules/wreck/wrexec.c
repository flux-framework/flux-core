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
#include <sys/socket.h>
#include <zmq.h>
#include <czmq.h>
#if WITH_TCMALLOC
#if HAVE_GPERFTOOLS_HEAP_PROFILER_H
  #include <gperftools/heap-profiler.h>
#elif HAVE_GOOGLE_HEAP_PROFILER_H
  #include <google/heap-profiler.h>
#else
  #error gperftools headers not configured
#endif
#endif /* WITH_TCMALLOC */

#include <flux/core.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"


struct rexec_ctx {
    uint32_t nodeid;
    flux_t *h;
    const char *wrexecd_path;
    const char *local_uri;
};

struct rexec_session {
    struct rexec_ctx *ctx;
    int64_t id;      /* LWJ id */
    int rank;
    int uid;

    json_object *jobinfo;
};

static void freectx (void *arg)
{
    struct rexec_ctx *ctx = arg;
    free (ctx);
}

static struct rexec_ctx *getctx (flux_t *h)
{
    struct rexec_ctx *ctx = (struct rexec_ctx *)flux_aux_get (h, "wrexec");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        if (flux_get_rank (h, &ctx->nodeid) < 0) {
            flux_log_error (h, "getctx: flux_get_rank");
            goto fail;
        }
        if (!(ctx->local_uri = flux_attr_get (h, "local-uri", NULL))) {
            flux_log_error (h, "getctx: flux_attr_get local-uri");
            goto fail;
        }
        flux_aux_set (h, "wrexec", ctx, freectx);
    }

    return ctx;
fail:
    free (ctx);
    return (NULL);
}

static void closeall (int fd)
{
    int fdlimit = sysconf (_SC_OPEN_MAX);

    while (fd < fdlimit)
        close (fd++);
    return;
}


#if 0
static int handle_client_msg (struct rexec_session *c, zmsg_t *zmsg)
{
    char *tag;
    json_object *o;

    if (flux_msg_decode (zmsg, &tag, &o) < 0) {
        log_err ("bad msg from rexec sesion %lu", c->id);
        return (-1);
    }

    if (strcmp (tag, "rexec.exited") == 0) {
        int localid, globalid;
        int status;

        util_json_object_get_int (o, "id", &localid);
        util_json_object_get_int (o, "status", &status);
        json_object_put (o);
        globalid = c->rank + localid;

    }
    return (0);
}
#endif

static char ** wrexecd_args_create (struct rexec_ctx *ctx, uint64_t id)
{
    char buf [64];
    char **args;
    int nargs = 3;

    args = xzmalloc ((nargs + 1) * sizeof (char **));
    snprintf (buf, sizeof (buf) - 1, "--lwj-id=%lu", id);

    args [0] = strdup (ctx->wrexecd_path);
    args [1] = strdup (buf);
    args [2] = strdup ("--parent-fd=3");
    args [3] = NULL;

    return (args);
}

static void exec_handler (struct rexec_ctx *ctx, uint64_t id, int *pfds)
{
    char **args;
    pid_t pid, sid;

    args = wrexecd_args_create (ctx, id);

    if ((sid = setsid ()) < 0)
        log_err ("setsid");

    if ((pid = fork()) < 0)
        log_err_exit ("fork");
    else if (pid > 0)
        exit (0); /* parent of grandchild == child */

    /*
     *  Grandchild performs the exec
     */
    //dup2 (pfds[0], STDIN_FILENO);
    dup2 (pfds[0], 3);
    closeall (4);
    flux_log (ctx->h, LOG_DEBUG, "running %s %s %s", args[0], args[1], args[2]);
    if (setenv ("FLUX_URI", ctx->local_uri, 1) < 0)
        log_err_exit ("setenv");
    if (execvp (args[0], args) < 0) {
        close (3);
        log_err_exit ("execvp");
    }
    exit (255);
}

static int update_wrexecd_path (struct rexec_ctx *ctx)
{
    ctx->wrexecd_path = flux_attr_get (ctx->h, "wrexec.wrexecd_path", NULL);
    if (!ctx->wrexecd_path)
        return -1;
    return 0;
}

static int spawn_exec_handler (struct rexec_ctx *ctx, int64_t id)
{
    int fds[2];
    char c;
    int n;
    int status;
    int rc = 0;
    pid_t pid;

    /*  Refresh wrexecd_path in case it was updated since the previous run */
    if (update_wrexecd_path (ctx) < 0)
        return (-1);

    if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        return (-1);

    if ((pid = fork ()) < 0) {
        flux_log_error (ctx->h, "spawn_exec_handler: fork");
        close (fds[0]);
        close (fds[1]);
        return (-1);
    }

    if (pid == 0) {
#if WITH_TCMALLOC
        /* Child: if heap profiling is running, stop it to avoid
         * triggering a dump when child exits.
         */
        if (IsHeapProfilerRunning ())
            HeapProfilerStop ();
#endif
        exec_handler (ctx, id, fds);
    }

    /*
     *  Wait for child to exit
     */
    if (waitpid (pid, &status, 0) < 0 && (errno != ECHILD))
        flux_log_error (ctx->h, "waitpid");

    /*
     *  Close child side of socketpair and wait for wrexecd
     */
    close (fds[0]);

    /* Blocking wait for exec helper to close fd */
    n = read (fds[1], &c, 1);
    if (n < 1) {
        flux_log_error (ctx->h, "reading status from rexecd (n=%d)", n);
        rc = -1;
    }
    close (fds[1]);
    return (rc);
}

static int64_t id_from_tag (const char *tag, char **endp)
{
    unsigned long l;

    errno = 0;
    l = strtoul (tag, endp, 10);
    if (l == 0 && errno == EINVAL)
        return (-1);
    else if (l == ULONG_MAX && errno == ERANGE)
        return (-1);
    return l;
}

int lwj_targets_this_node (struct rexec_ctx *ctx, int64_t id)
{
    kvsdir_t *tmp;
    /*
     *  If no 'rank' subdir exists for this lwj, then we are running
     *   without resource assignment so we run everywhere
     */
    if (kvs_get_dir (ctx->h, &tmp, "lwj.%ld.rank", id) < 0) {
        flux_log (ctx->h, LOG_INFO, "No dir lwj.%ld.rank: %s", id, strerror (errno));
        return (1);
    }

    kvsdir_destroy (tmp);
    if (kvs_get_dir (ctx->h, &tmp, "lwj.%ld.rank.%d", id, ctx->nodeid) < 0)
        return (0);
    kvsdir_destroy (tmp);
    return (1);
}

static void event_cb (flux_t *h, flux_msg_handler_t *w,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct rexec_ctx *ctx = arg;
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0) {
        flux_log_error (h, "event_cb: flux_msg_get_topic");
        return;
    }
    if (strncmp (topic, "wrexec.run", 10) == 0) {
        int64_t id = id_from_tag (topic + 11, NULL);
        if (id < 0)
            flux_log (h, LOG_ERR, "Invalid rexec tag `%s'", topic);
        if (lwj_targets_this_node (ctx, id))
            spawn_exec_handler (ctx, id);
    }
}

static void request_cb (flux_t *h,
			flux_msg_handler_t *w,
			const flux_msg_t *msg,
			void *arg)
{
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0)
        flux_log_error (h, "request_cb: flux_msg_get_topic");
    else if (!strcmp (topic, "wrexec.shutdown")) {
        flux_reactor_stop (flux_get_reactor (h));
        return;
    }
}

struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "*",        request_cb, NULL },
    { FLUX_MSGTYPE_EVENT,     "wrexec.*", event_cb,   NULL },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t *h, int argc, char **argv)
{
    struct rexec_ctx *ctx = getctx (h);
    if (ctx == NULL)
        return -1;

    flux_event_subscribe (h, "wrexec.run.");

    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        return -1;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_start");
        return -1;
    }
    flux_msg_handler_delvec (htab);
    return 0;
}

MOD_NAME ("wrexec");

/*
 * vi: ts=4 sw=4 expandtab
 */
