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
#include "route.h"
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
    zlist_t *backlog;
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
    zlist_autofree (lp->subscriptions);
    return lp;
}

static void _listener_destroy (void *arg)
{
    listener_t *lp = arg;

    zmsg_destroy (&lp->zmsg);
    zlist_destroy (&lp->subscriptions);
    free (lp);
}

static char *_match_item (zlist_t *zl, const char *s, bool substr)
{
    char *item = zlist_first (zl);

    while (item && strcmp (item, s) != 0
                    && (!substr || strncmp (item, s, strlen (item)) != 0))
        item = zlist_next (zl);
    return item;
}

static void _listener_subscribe (listener_t *lp, char *sub)
{
    char *item = _match_item (lp->subscriptions, sub, false);

    if (!item)
        zlist_append (lp->subscriptions, xstrdup (sub));    
}

static void _listener_unsubscribe (listener_t *lp, char *sub)
{
    char *item = _match_item (lp->subscriptions, sub, false);

    if (item)
        zlist_remove (lp->subscriptions, item);
}

static int _listener_fwd (const char *key, void *litem, void *arg)
{
    listener_t *lp = litem;
    fwdarg_t *farg = arg;
    zmsg_t *zmsg;
    json_object *o = json_object_object_get (farg->o, "tag");
    const char *tag = o ? json_object_get_string (o) : "";
    char *item = _match_item (lp->subscriptions, tag, true);

    if (item) {
        if (!(zmsg = zmsg_dup (lp->zmsg)))
            oom ();
        plugin_send_response (farg->p, &zmsg, farg->o);
    }
    return 1;
}


static void _add_backlog (plugin_ctx_t *p, json_object *o)
{
    ctx_t *ctx = p->ctx;

    json_object_get (o);
    zlist_append (ctx->backlog, o);
}

static void _send_backlog (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    json_object *o;

    /* FIXME: aggregate similar messages */
    while ((o = zlist_pop (ctx->backlog))) {
        plugin_send_request (p, o, "log.msg");
        json_object_put (o);
    }

    assert (zlist_size (ctx->backlog) == 0);
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

    _listener_subscribe (lp, sub);
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
    if (lp)
        _listener_unsubscribe (lp, sub);
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
    json_object *no, *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0)
        goto done;
    if (!o)
        goto done;

    /* add source if not present */
    if (!json_object_object_get (o, "source")) {
        if (!(no = json_object_new_string (p->conf->rankstr)))
            oom ();
        json_object_object_add (o, "source", no);
    }

    if (p->conf->rank != 0) {
        _add_backlog (p, o);
        if (!plugin_timeout_isset (p))
            plugin_timeout_set (p, 100); /* 100ms - then send backlog up */
    }

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

static void _timeout (plugin_ctx_t *p)
{
    _send_backlog (p);
    plugin_timeout_clear (p);
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    ctx->listeners = zhash_new ();
    ctx->backlog = zlist_new ();
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    zhash_destroy (&ctx->listeners);
    zlist_destroy (&ctx->backlog);
    free (ctx);
}

struct plugin_struct logsrv = {
    .name      = "log",
    .recvFn    = _recv,
    .initFn    = _init,
    .finiFn    = _fini,
    .timeoutFn = _timeout,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
