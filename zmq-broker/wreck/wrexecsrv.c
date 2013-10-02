
#include <sys/socket.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>
#include <json/arraylist.h>
#include <uuid/uuid.h>

#include "route.h"
#include "cmbd.h"
#include "util/zmsg.h"
#include "util/log.h"
#include "util/util.h"
#include "plugin.h"

#include "rexec-config.h"  /* For REXECD_PATH */


struct rexec_session {
    plugin_ctx_t *p;
    int64_t id;      /* LWJ id */
    int rank;
    int uid;

    json_object *jobinfo;

    char req_uri [1024];
    void *zs_req;   /* requests to client (zconnect) */

    char rep_uri [1024];
    void *zs_rep;   /* replies to client requests (zbind) */
};

struct rexec_ctx {
    zlist_t *session_list;
};


static void rexec_session_destroy (struct rexec_session *c)
{
    if (c->zs_req)
        zsocket_disconnect (c->zs_req, c->req_uri);
    if (c->zs_rep)
        zsocket_disconnect (c->zs_rep, c->rep_uri);
    if (c->jobinfo)
        json_object_put (c->jobinfo);
    free (c);
}

static int rexec_session_connect_to_helper (struct rexec_session *c)
{
    zctx_t *zctx = c->p->srv->zctx;

    snprintf (c->req_uri, sizeof (c->req_uri),
             "ipc:///tmp/cmb-%d-%d-rexec-req-%lu", c->rank, c->uid, c->id);
    zconnect (zctx, &c->zs_req, ZMQ_DEALER, c->req_uri, -1, NULL);
    return (0);
}

static json_object * rexec_session_json (struct rexec_session *c)
{
    json_object *o = json_object_new_object ();
    util_json_object_add_int (o, "nodeid", c->rank);
    util_json_object_add_int64 (o, "id", c->id);
    return (o);
}


static struct rexec_session * rexec_session_create (plugin_ctx_t *p, int64_t id)
{
    struct rexec_session *c = xzmalloc (sizeof (*c));
    zctx_t *zctx = p->srv->zctx;

    c->p    = p;
    c->id   = id;
    c->rank = p->conf->rank;
    c->uid  = (int) geteuid (); /* runs as user for now */

    snprintf (c->rep_uri, sizeof (c->rep_uri),
             "ipc:///tmp/cmb-%d-%d-rexec-rep-%lu", c->rank, c->uid, c->id);
    zbind (zctx, &c->zs_rep, ZMQ_ROUTER, c->rep_uri, -1);

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
    plugin_ctx_t *p = c->p;
    struct rexec_ctx *ctx = p->ctx;

    msg ("removing client %lu", c->id);

    zmq_pollitem_t zp = {
        .events = ZMQ_POLLIN | ZMQ_POLLERR,
        .socket = c->zs_rep
    };
    zloop_poller_end (p->zloop, &zp);
    zlist_remove (ctx->session_list, c);
    rexec_session_destroy (c);
    return (0);
}

#if 0
static int handle_client_msg (struct rexec_session *c, zmsg_t *zmsg)
{
    char *tag;
    json_object *o;

    if (cmb_msg_decode (zmsg, &tag, &o) < 0) {
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

static int client_cb (zloop_t *zl, zmq_pollitem_t *zp, struct rexec_session *c)
{
    zmsg_t *new;

    if (zp->revents & ZMQ_POLLERR) {
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
    return (0);
}

static int rexec_session_add (plugin_ctx_t *p, struct rexec_session *c)
{
    zmq_pollitem_t zp = {
        .events = ZMQ_POLLIN | ZMQ_POLLERR,
        .socket = c->zs_rep
    };
    struct rexec_ctx *ctx = p->ctx;

    if (zlist_append (ctx->session_list, c) < 0)
        msg ("failed to insert %lu", c->id);
    zloop_poller (p->zloop, &zp, (zloop_fn *) client_cb, (void *) c);
    return (0);
}

static char ** rexec_session_args_create (struct rexec_session *s)
{
    char buf [64];
    char **args;
    int nargs = 3;

    args = xzmalloc ((nargs + 1) * sizeof (char **));
    snprintf (buf, sizeof (buf) - 1, "--lwj-id=%lu", s->id);

    args [0] = strdup (REXECD_PATH);
    args [1] = strdup (buf);
    args [2] = strdup ("--parent-fd=0");
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
    dup2 (pfds[0], STDIN_FILENO);
    dup2 (pfds[0], STDOUT_FILENO);
    closeall (3);
    msg ("running %s %s %s", args[0], args[1], args[2]);
    if (execvp (args[0], args) < 0) {
        close (STDOUT_FILENO);
        err_exit ("execvp");
    }
    exit (255);
}

static zmsg_t *rexec_session_handler_msg_create (struct rexec_session *s)
{
    json_object *o = rexec_session_json (s);
    zmsg_t *zmsg = cmb_msg_encode ("rexec.run", o);
    json_object_put (o);
    return (zmsg);
}

static int spawn_exec_handler (plugin_ctx_t *p, int64_t id)
{
    struct rexec_session *cli;
    zmsg_t *zmsg;
    int fds[2];
    char c;
    int n;
    int status;
    pid_t pid;

    if ((cli = rexec_session_create (p, id)) == NULL)
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
    zmsg = rexec_session_handler_msg_create (cli);
    zmsg_send_fd (fds[1], &zmsg);

    /* Blocking wait for exec helper to close fd */
    n = read (fds[1], &c, 1);
    close (fds[1]);

    if (n < 1) {
        msg ("Error reading status from rexecd");
        return (-1);
    }

    rexec_session_connect_to_helper (cli);
    rexec_session_add (p, cli);
    return (0);
}

static struct rexec_session *rexec_session_lookup (struct rexec_ctx *ctx, int64_t id)
{
    /* Warning: zlist has no search. linear search here */
    struct rexec_session *s;
    s = zlist_first (ctx->session_list);
    do {
        if (s->id == id)
            return (s);
    } while ((s = zlist_next (ctx->session_list)));
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

static int fwd_to_session (plugin_ctx_t *p, zmsg_t **zmsg, json_object *o)
{
    int rc;
    struct rexec_ctx *ctx = p->ctx;
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
    int rc;
    json_object *o = json_object_new_int (sig);
    zmsg_t * zmsg = cmb_msg_encode ("rexec.kill", o);
    zmsg_dump (zmsg);

    rc = zmsg_send (&zmsg, s->zs_req);
    if (rc < 0)
        err ("zmsg_send failed");

    json_object_put (o);
    return (rc);
}

static int rexec_kill (struct rexec_ctx *ctx, int64_t id, int sig)
{
    struct rexec_session *s = rexec_session_lookup (ctx, id);
    return rexec_session_kill (s, sig);
}

static void handle_event (plugin_ctx_t *p, zmsg_t **zmsg)
{
    char *tag = cmb_msg_tag (*zmsg, false);
    if (strncmp (tag, "event.rexec.run", 15) == 0) {
        int64_t id = id_from_tag (tag + 16, NULL);
        if (id < 0)
            err ("Invalid rexec tag `%s'", tag);
        spawn_exec_handler (p, id);
    }
    else if (strncmp (tag, "event.rexec.kill", 16) == 0) {
        int sig = SIGKILL;
        char *endptr = NULL;
        int64_t id = id_from_tag (tag + 17, &endptr);
        if (endptr && *endptr == '.')
            sig = atoi (endptr);
        rexec_kill (p->ctx, id, sig);
    }
    free (tag);
}

static void handle_request (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o;
    char *tag;

    if (cmb_msg_decode (*zmsg, &tag, &o) >= 0) {
        msg ("forwarding %s to session", tag);
        fwd_to_session (p, zmsg, o);
    }
    if (zmsg)
        zmsg_destroy (zmsg);
}

static void handle_recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    switch (type) {
        case ZMSG_REQUEST:
            handle_request (p, zmsg);
            break;
        case ZMSG_EVENT:
            handle_event (p, zmsg);
            break;
        default:
            break;
    }
}

static void rexec_init (plugin_ctx_t *p)
{
    struct rexec_ctx *ctx = xzmalloc (sizeof (*ctx));
    ctx->session_list = zlist_new ();
    p->ctx = (void *) ctx;

    zsocket_set_subscribe (p->zs_evin, "event.rexec.run.");
    zsocket_set_subscribe (p->zs_evin, "event.rexec.kill.");
}

struct plugin_struct rexecsrv = {
    .name = "rexec",
    .initFn = rexec_init,
    .recvFn = handle_recv,
};

/*
 * vi: ts=4 sw=4 expandtab
 */
