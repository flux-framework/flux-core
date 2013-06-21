/* logsrv.c - aggregate log data */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmq.h"
#include "cmbd.h"
#include "util.h"
#include "log.h"
#include "plugin.h"

#include "logsrv.h"

typedef struct {
    zmsg_t *zmsg;
    zlist_t *subscriptions;
} listener_t;

typedef struct {
    zhash_t *listeners;
} ctx_t;

typedef struct {
    plugin_ctx_t *p;
    json_object *o;
} fwdarg_t;

static listener_t *_listener_create (zmsg_t *zmsg)
{
    listener_t *lp = xzmalloc (sizeof (listener_t));
    if (!(lp->zmsg = zmsg_dup (zmsg)))
        oom ();
    if (!(lp->subscriptions = zlist_new ()))
        oom ();
    return lp;
}

static void _listener_destroy (void *arg)
{
    listener_t *lp = arg;

    zmsg_destroy (&lp->zmsg);
    zlist_destroy (&lp->subscriptions);
    free (lp);
}

static int _listener_fwd (const char *key, void *item, void *arg)
{
    listener_t *lp = item;
    fwdarg_t *farg = arg;
    zmsg_t *zmsg;

    /* FIXME: match subscription */

    if (!(zmsg = zmsg_dup (lp->zmsg)))
        oom ();
    if (cmb_msg_rep_json (zmsg, farg->o) < 0)
        err_exit ("%s", __FUNCTION__);
    if (zmsg_send (&zmsg, farg->p->zs_dnreq) < 0)
        err ("zmsg_send");
    return 1;
}

static void _recv_log_subscribe (plugin_ctx_t *p, char *sub, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    char *sender = NULL;
    listener_t *lp;

    if (!(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__); 
        goto done;
    }
    if (!(lp = zhash_lookup (ctx->listeners, sender))) {
        lp = _listener_create (*zmsg);
        zhash_insert (ctx->listeners, sender, lp);
        zhash_freefn (ctx->listeners, sender, _listener_destroy);
    }

    /* FIXME: add subscription */
done:
    if (sender)
        free (sender);
    zmsg_destroy (zmsg);
}

static void _recv_log_unsubscribe (plugin_ctx_t *p, char *sub, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    listener_t *lp;
    char *sender = NULL;

    if (!(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__); 
        goto done;
    }
    lp = zhash_lookup (ctx->listeners, sender);

    /* FIXME: delete subscription */
done:
    if (sender)
        free (sender);
    zmsg_destroy (zmsg);
}

static void _recv_log_disconnect (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    char *sender;

    if (!(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__); 
        goto done;
    }
    zhash_delete (ctx->listeners, sender);
    free (sender);
done:
    zmsg_destroy (zmsg);
}

static void _recv_log_msg (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    fwdarg_t farg;
    json_object *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0)
        goto done;
    if (!o)
        goto done;

    /* add source if not present */
    if (!json_object_object_get (o, "source")) {
        char rankstr[16];
        json_object *no;

        snprintf (rankstr, sizeof (rankstr), "%d", p->conf->rank);
        if (!(no = json_object_new_string (rankstr)))
            oom ();
        json_object_object_add (o, "source", no);
    }

    /* FIXME: reduction */

    /* forward upstream */
    if (p->conf->rank != 0)
        cmb_msg_send_rt (p->zs_upreq, o, "log.msg");      

    /* forward message to listeners */
    farg.p = p;
    farg.o = o;
    zhash_foreach (ctx->listeners, _listener_fwd, &farg);
done:
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *arg = NULL;

    if (cmb_msg_match (*zmsg, "log.msg"))
        _recv_log_msg (p, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "log.subscribe.", &arg))
        _recv_log_subscribe (p, arg, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "log.unsubscribe.", &arg))
        _recv_log_unsubscribe (p, arg, zmsg);
    else if (cmb_msg_match (*zmsg, "log.disconnect"))
        _recv_log_disconnect (p, zmsg);

    if (arg)
        free (arg);
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    ctx->listeners = zhash_new ();
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    zhash_destroy (&ctx->listeners);
    free (ctx);
}

struct plugin_struct logsrv = {
    .name      = "log",
    .recvFn    = _recv,
    .initFn    = _init,
    .finiFn    = _fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
