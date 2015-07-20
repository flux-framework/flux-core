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
#include <arraylist.h>

#include <flux/core.h>

#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/modules/libmrpc/mrpc.h"


struct rexec_ctx {
    int nodeid;
    zlist_t *session_list;
    flux_t h;
    char *wrexecd_path;
};

struct rexec_session {
    struct rexec_ctx *ctx;
    int64_t id;      /* LWJ id */
    int rank;
    int uid;

    json_object *jobinfo;

    char req_uri [1024];
    void *zs_req;   /* requests to client (zconnect) */

    char rep_uri [1024];
    void *zs_rep;   /* replies to client requests (zbind) */
};

static void freectx (void *arg)
{
    struct rexec_ctx *ctx = arg;
    zlist_destroy (&ctx->session_list);
    free (ctx);
}

static int wrexec_path_set (const char *key,
    const char *val, void *arg, int err)
{
    struct rexec_ctx *ctx = (struct rexec_ctx *) arg;
    if (ctx->wrexecd_path)
        free (ctx->wrexecd_path);
    if (err == ENOENT)
        ctx->wrexecd_path = strdup (WREXECD_PATH);
    else
        ctx->wrexecd_path = strdup (val);
    return (0);
}

static struct rexec_ctx *getctx (flux_t h)
{
    struct rexec_ctx *ctx = (struct rexec_ctx *)flux_aux_get (h, "wrexec");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->session_list = zlist_new ()))
            oom ();
        ctx->h = h;
        ctx->nodeid = flux_rank (h);
        flux_aux_set (h, "wrexec", ctx, freectx);
        kvs_watch_string (h, "config.wrexec.wrexecd_path",
            wrexec_path_set, (void *) ctx);
    }

    return ctx;
}

static void rexec_session_destroy (struct rexec_session *c)
{
    if (c->zs_req)
        zsocket_disconnect (c->zs_req, "%s", c->req_uri);
    if (c->zs_rep)
        zsocket_disconnect (c->zs_rep, "%s", c->rep_uri);
    if (c->jobinfo)
        json_object_put (c->jobinfo);
    free (c);
}

static int rexec_session_connect_to_helper (struct rexec_session *c)
{
    zctx_t *zctx = NULL;

    if (flux_opt_get (c->ctx->h, FLUX_OPT_ZEROMQ_CONTEXT, &zctx,
                                                          sizeof (zctx)) < 0)
        err_exit ("could not obtain zctx");

    snprintf (c->req_uri, sizeof (c->req_uri),
             "ipc:///tmp/cmb-%d-%d-rexec-req-%lu", c->rank, c->uid, c->id);
    if (!(c->zs_req = zsocket_new (zctx, ZMQ_DEALER)))
        err_exit ("zsocket_new");
    if (zsocket_connect (c->zs_req, "%s", c->req_uri) < 0)
        err_exit ("zsocket_connect: %s", c->req_uri);

    return (0);
}

static struct rexec_session * rexec_session_create (struct rexec_ctx *ctx, int64_t id)
{
    zctx_t *zctx = NULL;
    struct rexec_session *c = xzmalloc (sizeof (*c));

    if (flux_opt_get (ctx->h, FLUX_OPT_ZEROMQ_CONTEXT, &zctx,
                                                       sizeof (zctx)) < 0)
        msg_exit ("could not obtain zctx");

    c->ctx  = ctx;
    c->id   = id;
    c->rank = flux_rank (ctx->h);
    c->uid  = (int) geteuid (); /* runs as user for now */

    snprintf (c->rep_uri, sizeof (c->rep_uri),
             "ipc:///tmp/cmb-%d-%d-rexec-rep-%lu", c->rank, c->uid, c->id);

    if (!(c->zs_rep = zsocket_new (zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
    if (zsocket_bind (c->zs_rep, "%s", c->rep_uri) < 0)
        err_exit ("zsocket_bind: %s", c->rep_uri);


    return (c);
}

static void closeall (int fd)
{
    int fdlimit = sysconf (_SC_OPEN_MAX);

    while (fd < fdlimit)
        close (fd++);
    return;
}

static int rexec_session_remove (struct rexec_session *c)
{
    struct rexec_ctx *ctx = c->ctx;

    msg ("removing client %lu", c->id);

    flux_zshandler_remove (ctx->h, c->zs_rep, ZMQ_POLLIN | ZMQ_POLLERR);

    zlist_remove (ctx->session_list, c);
    rexec_session_destroy (c);
    return (0);
}

#if 0
static int handle_client_msg (struct rexec_session *c, zmsg_t *zmsg)
{
    char *tag;
    json_object *o;

    if (flux_msg_decode (zmsg, &tag, &o) < 0) {
        err ("bad msg from rexec sesion %lu", c->id);
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

static int client_cb (flux_t h, void *zs, short revents, void *arg)
{
    struct rexec_session *c = arg;
    zmsg_t *new;

    if (revents & ZMQ_POLLERR) {
        rexec_session_remove (c);
    }
    new = zmsg_recv (c->zs_rep);
    if (new) {
        free (zmsg_popstr (new)); /* remove dealer id */
        //client_forward (c, new);  /* forward to rexec client */
        zmsg_destroy (&new);
    }
    else
        err ("client_cb: zmsg_recv");
    return 0;
}

static int rexec_session_add (struct rexec_ctx *ctx, struct rexec_session *c)
{
    if (zlist_append (ctx->session_list, c) < 0)
        msg ("failed to insert %lu", c->id);
    if (flux_zshandler_add (ctx->h, c->zs_rep, ZMQ_POLLIN | ZMQ_POLLERR,
                                                    client_cb, c) < 0)
        err ("failed to insert %lu", c->id);
    return (0);
}

static char ** rexec_session_args_create (struct rexec_session *s)
{
    char buf [64];
    char **args;
    int nargs = 3;

    args = xzmalloc ((nargs + 1) * sizeof (char **));
    snprintf (buf, sizeof (buf) - 1, "--lwj-id=%lu", s->id);

    args [0] = strdup (s->ctx->wrexecd_path);
    args [1] = strdup (buf);
    args [2] = strdup ("--parent-fd=3");
    args [3] = NULL;

    return (args);
}

static void exec_handler (struct rexec_session *s, int *pfds)
{
    char **args;
    pid_t pid, sid;

    args = rexec_session_args_create (s);

    if ((sid = setsid ()) < 0)
        err ("setsid");

    if ((pid = fork()) < 0)
        err_exit ("fork");
    else if (pid > 0)
        exit (0); /* parent of grandchild == child */

    /*
     *  Grandchild performs the exec
     */
    //dup2 (pfds[0], STDIN_FILENO);
    dup2 (pfds[0], 3);
    closeall (4);
    flux_log (s->ctx->h, LOG_DEBUG, "running %s %s %s", args[0], args[1], args[2]);
    if (execvp (args[0], args) < 0) {
        close (3);
        err_exit ("execvp");
    }
    exit (255);
}

static int spawn_exec_handler (struct rexec_ctx *ctx, int64_t id)
{
    struct rexec_session *cli;
    int fds[2];
    char c;
    int n;
    int status;
    pid_t pid;

    if ((cli = rexec_session_create (ctx, id)) == NULL)
        return (-1);

    if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) < 0)
        return (-1);

    if ((pid = fork ()) < 0)
        err_exit ("fork");

    if (pid == 0)
        exec_handler (cli, fds);

    /*
     *  Wait for child to exit
     */
    waitpid (pid, &status, 0);

    /*
     *  Close child side of socketpair and send zmsg to (grand)child
     */
    close (fds[0]);

    /* Blocking wait for exec helper to close fd */
    n = read (fds[1], &c, 1);
    if (n < 1) {
        msg ("Error reading status from rexecd: %s", strerror (errno));
        return (-1);
    }
    close (fds[1]);

    rexec_session_connect_to_helper (cli);
    rexec_session_add (ctx, cli);
    return (0);
}

static struct rexec_session *rexec_session_lookup (struct rexec_ctx *ctx, int64_t id)
{
    /* Warning: zlist has no search. linear search here */
    struct rexec_session *s;
    s = zlist_first (ctx->session_list);
    while (s) {
        if (s->id == id)
            return (s);
        s = zlist_next (ctx->session_list);
    }
    return (NULL);
}

static int64_t json_to_session_id (struct rexec_ctx *ctx, json_object *o)
{
    int64_t id = -1;
    if (util_json_object_get_int64 (o, "id", &id) < 0)
        return (-1);
    return (id);
}

static struct rexec_session * rexec_json_to_session (struct rexec_ctx *ctx,
    json_object *o)
{
    int64_t id = json_to_session_id (ctx, o);
    if (id < 0)
        return (NULL);
    return rexec_session_lookup (ctx, id);
}

static int fwd_to_session (struct rexec_ctx *ctx, zmsg_t **zmsg, json_object *o)
{
    int rc;
    struct rexec_session *s = rexec_json_to_session (ctx, o);
    if (s == NULL) {
        //plugin_send_response_errnum (p, &s->zmsg, ENOSYS);
        return (-1);
    }
    msg ("sending message to session %lu", s->id);
    rc = zmsg_send (zmsg, s->zs_req);
    if (rc < 0)
        err ("zmsg_send"); 
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

static int rexec_session_kill (struct rexec_session *s, int sig)
{
    int rc = -1;
    json_object *o = json_object_new_int (sig);
    const char *json_str = json_object_to_json_string (o);
    zmsg_t * zmsg = NULL;

    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (zmsg, "wrexec.kill") < 0)
        goto done;
    if (flux_msg_set_payload_json (zmsg, json_str) < 0)
        goto done;

    zmsg_dump (zmsg);

    rc = zmsg_send (&zmsg, s->zs_req);
    if (rc < 0)
        err ("zmsg_send failed");
done:
    json_object_put (o);
    zmsg_destroy (&zmsg);
    return (rc);
}

static int rexec_kill (struct rexec_ctx *ctx, int64_t id, int sig)
{
    struct rexec_session *s = rexec_session_lookup (ctx, id);
    if (s == NULL)
        return (0);
    return rexec_session_kill (s, sig);
}

static int mrpc_respond_errnum (flux_mrpc_t mrpc, int errnum)
{
    json_object *o = json_object_new_object ();
    util_json_object_add_int (o, "errnum", errnum);
    flux_mrpc_put_outarg (mrpc, o);
    json_object_put (o);
    return (0);
}

static int mrpc_handler (struct rexec_ctx *ctx, zmsg_t *zmsg)
{
    int64_t id;
    const char *method;
    const char *json_str;
    json_object *inarg = NULL;
    json_object *request = NULL;
    int rc = -1;
    flux_t f = ctx->h;
    flux_mrpc_t mrpc;

    if (flux_event_decode (zmsg, NULL, &json_str) < 0
                || !(request = Jfromstr (json_str)) ) {
        flux_log (f, LOG_ERR, "flux_event_decode: %s", strerror (errno));
        return (0);
    }
    mrpc = flux_mrpc_create_fromevent (f, request);
    if (mrpc == NULL) {
        if (errno != EINVAL) /* EINVAL == not addressed to me */
            flux_log (f, LOG_ERR, "flux_mrpc_create_fromevent: %s",
                      strerror (errno));
        return (0);
    }
    if (flux_mrpc_get_inarg (mrpc, &inarg) < 0) {
        flux_log (f, LOG_ERR, "flux_mrpc_get_inarg: %s", strerror (errno));
        goto done;
    }
    if (util_json_object_get_int64 (inarg, "id", &id) < 0) {
        mrpc_respond_errnum (mrpc, errno);
        flux_log (f, LOG_ERR, "wrexec mrpc failed to get arg `id'");
        goto done;
    }
    if (util_json_object_get_string (inarg, "method", &method) < 0) {
        mrpc_respond_errnum (mrpc, errno);
        flux_log (f, LOG_ERR, "wrexec mrpc failed to get arg `id'");
        goto done;
    }

    if (strcmp (method, "run") == 0) {
        rc = spawn_exec_handler (ctx, id);
    }
    else if (strcmp (method, "kill") == 0) {
        int sig = -1;
        util_json_object_get_int (inarg, "signal", &sig);
        if (sig == -1)
            sig = 9;
        rc = rexec_kill (ctx, id, sig);
    }
    else {
        mrpc_respond_errnum (mrpc, EINVAL);
        flux_log (f, LOG_ERR, "rexec mrpc failed to get arg `id'");
    }

done:
    flux_mrpc_respond (mrpc);
    flux_mrpc_destroy (mrpc);

    if (request)
        json_object_put (request);
    if (inarg)
        json_object_put (inarg);
    if (mrpc)
        flux_mrpc_destroy (mrpc);
    return (rc);
}

int lwj_targets_this_node (struct rexec_ctx *ctx, int64_t id)
{
    kvsdir_t tmp;
    /*
     *  If no 'rank' subdir exists for this lwj, then we are running
     *   without resource assignment so we run everywhere
     */
    if (kvs_get_dir (ctx->h, &tmp, "lwj.%ld.rank", id) < 0) {
        flux_log (ctx->h, LOG_INFO, "No dir lwj.%ld.rank: %s\n", id, strerror (errno));
        return (1);
    }

    kvsdir_destroy (tmp);
    if (kvs_get_dir (ctx->h, &tmp, "lwj.%ld.rank.%d", id, ctx->nodeid) < 0)
        return (0);
    kvsdir_destroy (tmp);
    return (1);
}

static int event_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    struct rexec_ctx *ctx = arg;
    const char *topic;
    if (flux_msg_get_topic (*zmsg, &topic) < 0)
        goto done;
    if (strncmp (topic, "wrexec.run", 10) == 0) {
        int64_t id = id_from_tag (topic + 11, NULL);
        if (id < 0)
            err ("Invalid rexec tag `%s'", topic);
        if (lwj_targets_this_node (ctx, id))
            spawn_exec_handler (ctx, id);
    }
    else if (strncmp (topic, "wrexec.kill", 12) == 0) {
        int sig = SIGKILL;
        char *endptr = NULL;
        int64_t id = id_from_tag (topic + 12, &endptr);
        if (endptr && *endptr == '.')
            sig = atoi (endptr);
        rexec_kill (ctx, id, sig);
    }
    else if (strncmp (topic, "mrpc.wrexec", 11) == 0) {
        mrpc_handler (ctx, *zmsg);
    }
done:
    if (zmsg && *zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    struct rexec_ctx *ctx = arg;
    const char *json_str;
    json_object *o = NULL;
    const char *topic;

    if (flux_msg_get_topic (*zmsg, &topic) < 0)
        goto done;
    if (!strcmp (topic, "wrexec.shutdown")) {
        flux_reactor_stop (h);
        return 0;
    }
    if (flux_msg_get_payload_json (*zmsg, &json_str) < 0
            || (json_str && !(o = json_tokener_parse (json_str))))
        goto done;
    msg ("forwarding %s to session", topic);
    fwd_to_session (ctx, zmsg, o);
done:
    zmsg_destroy (zmsg);
    if (o)
        json_object_put (o);
    return 0;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "*",          request_cb },
    { FLUX_MSGTYPE_EVENT,     "wrexec.*", event_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    struct rexec_ctx *ctx = getctx (h);

    flux_event_subscribe (h, "wrexec.run.");
    flux_event_subscribe (h, "wrexec.kill.");
    flux_event_subscribe (h, "mrpc.wrexec");

    if (flux_msghandler_addvec (h, htab, htablen, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_addvec: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("wrexec");

/*
 * vi: ts=4 sw=4 expandtab
 */
