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
#include "cmb.h"
#include "cmbd.h"
#include "util.h"
#include "log.h"
#include "plugin.h"

#include "logsrv.h"

const int log_reduction_timeout_msec = 100;
const int log_circular_buffer_entries = 100000;
const int log_persist_priority = CMB_LOG_NOTICE;

typedef struct {
    char *fac;
    logpri_t pri_max;  /* the lower the number, the more filtering */
    logpri_t pri_min;
} subscription_t;

typedef struct {
    zmsg_t *zmsg;
    zlist_t *subscriptions;
} listener_t;

typedef struct {
    zhash_t *listeners;
    zlist_t *backlog;
    zlist_t *cirbuf;
    int cirbuf_size;
} ctx_t;

static void _log_external (json_object *o);

static bool _match_subscription (json_object *o, subscription_t *sub)
{
    json_object *no;

    if (!(no = json_object_object_get (o, "priority"))
            || json_object_get_int (no) > sub->pri_max
            || json_object_get_int (no) < sub->pri_min
            || !(no = json_object_object_get (o, "facility"))
            || strncasecmp (sub->fac, json_object_get_string (no),
                            strlen (sub->fac)) != 0)
        return false;
    return true;
}

static subscription_t *_create_subscription (char *arg)
{
    subscription_t *sub = xzmalloc (sizeof (*sub));
    char *nextptr;
    
    /* 'priority.facility' */
    sub->pri_min = CMB_LOG_EMERG;
    sub->pri_max = strtoul (arg, &nextptr, 10);
    sub->fac = *nextptr ? xstrdup (nextptr + 1) : xstrdup (nextptr);

    return sub;
}

static void _destroy_subscription (subscription_t *sub)
{
    free (sub->fac);
    free (sub);
}

/* Manage circular buffer.
 */

static void _log_save (plugin_ctx_t *p, json_object *ent)
{
    ctx_t *ctx = p->ctx;
    json_object *o;

    if (ctx->cirbuf_size == log_circular_buffer_entries) {
        o = zlist_pop (ctx->cirbuf);
        json_object_put (o);
        ctx->cirbuf_size--;
    }
    json_object_get (ent);
    zlist_append (ctx->cirbuf, ent);
    ctx->cirbuf_size++;
}

static void _recv_log_dump (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *o;
    zmsg_t *cpy;
    subscription_t *sub = _create_subscription (arg);

    o = zlist_first (ctx->cirbuf);
    while (o != NULL) {
        if (_match_subscription (o, sub)) {
            if (!(cpy = zmsg_dup (*zmsg)))
                oom ();
            plugin_send_response (p, &cpy, o);
        }
        o = zlist_next (ctx->cirbuf);
    }
    plugin_send_response_errnum (p, zmsg, ENOENT);
    _destroy_subscription (sub);
}

static void _priority_override (json_object *o)
{
    json_object *no;

    if (!(no = json_object_new_int (CMB_LOG_ERR)))
        oom ();
    json_object_object_add (o, "priority_override", no);
}

static void _recv_fault_event (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    subscription_t sub = {
        .pri_min = log_persist_priority,
        .pri_max = CMB_LOG_DEBUG,
        .fac = arg
    };
    json_object *o;
    zlist_t *temp;

    if (!(temp = zlist_new ()))
        oom ();
    while ((o = zlist_pop (ctx->cirbuf))) {
        if (_match_subscription (o, &sub)) {
            if (plugin_treeroot (p))
                _log_external (o);
            else {
                _priority_override (o);
                plugin_send_request (p, o, "log.msg");
            }
            json_object_put (o);
        } else
            zlist_append (temp, o);
    }
    zlist_destroy (&ctx->cirbuf);
    ctx->cirbuf = temp;
}

/* Manage listeners.
 */

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
    subscription_t *sub;

    zmsg_destroy (&lp->zmsg);
    while ((sub = zlist_pop (lp->subscriptions)))
        _destroy_subscription (sub);
    zlist_destroy (&lp->subscriptions);
    free (lp);
}

static void _listener_subscribe (listener_t *lp, char *arg)
{
    zlist_append (lp->subscriptions, _create_subscription (arg));    
}

static void _listener_unsubscribe (listener_t *lp, char *fac)
{
    subscription_t *sub;

    do {
        sub = zlist_first (lp->subscriptions);
        while (sub) { 
            if (!strncasecmp (fac, sub->fac, strlen (fac))) {
                zlist_remove (lp->subscriptions, sub);
                _destroy_subscription (sub);
                break;
            }
            sub = zlist_next (lp->subscriptions);
        }
    } while (sub);
}

static void _recv_log_subscribe (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
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

    _listener_subscribe (lp, arg);
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

/* Handle a new log message
 */

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

    /* FIXME: Perform reduction,
     * e.g. aggregate similar messages here.
     */

    while ((o = zlist_pop (ctx->backlog))) {
        plugin_send_request (p, o, "log.msg");
        json_object_put (o);
    }
}

static bool _persistable (json_object *o)
{
    json_object *no;

    if ((no = json_object_object_get (o, "priority"))
            && json_object_get_int (no) <= log_persist_priority)
        return true;
    if ((no = json_object_object_get (o, "priority_override"))
            && json_object_get_int (no) <= log_persist_priority)
        return true;
    return false;
}

static const char *_logpri2str (logpri_t pri)
{
    switch (pri) {
        case CMB_LOG_EMERG: return "emerg";
        case CMB_LOG_ALERT: return "alert";
        case CMB_LOG_CRIT: return "crit";
        case CMB_LOG_ERR: return "err";
        case CMB_LOG_WARNING: return "warning";
        case CMB_LOG_NOTICE: return "notice";
        case CMB_LOG_INFO: return "info";
        case CMB_LOG_DEBUG: return "debug";
    }
    /*NOTREACHED*/
    return "unknown";
}

static void _log_external (json_object *o)
{
    json_object *no;
    logpri_t pri;
    const char *fac, *src, *ts, *message;
    char *endptr;
    struct timeval tv;

    if (!(no = json_object_object_get (o, "facility")))
        return;
    fac = json_object_get_string (no);
    if (!(no = json_object_object_get (o, "priority")))
        return;
    pri = json_object_get_int (no);
    if (!(no = json_object_object_get (o, "source")))
        return;
    src = json_object_get_string (no);
    if (!(no = json_object_object_get (o, "timestamp")))
        return;
    ts = json_object_get_string (no);
    tv.tv_sec = strtoul (ts, &endptr, 10);
    tv.tv_usec = *endptr ? strtoul (endptr + 1, NULL, 10) : 0;
    if (!(no = json_object_object_get (o, "message")))
        return;
    message = json_object_get_string (no);
    msg ("[%-.6lu.%-.6lu] %s.%s[%s]: %s", tv.tv_sec, tv.tv_usec,
         fac, _logpri2str (pri), src, message);
}


typedef struct {
    plugin_ctx_t *p;
    json_object *o;
} fwdarg_t;

static int _listener_fwd (const char *key, void *litem, void *arg)
{
    listener_t *lp = litem;
    fwdarg_t *farg = arg;
    zmsg_t *zmsg;
    subscription_t *sub;

    sub = zlist_first (lp->subscriptions);
    while (sub) {
        if (_match_subscription (farg->o, sub)) {
            if (!(zmsg = zmsg_dup (lp->zmsg)))
                oom ();
            plugin_send_response (farg->p, &zmsg, farg->o);
            break;
        }
        sub = zlist_next (lp->subscriptions);
    }
    return 1;
}

static void _recv_log_msg (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    fwdarg_t farg;
    json_object *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL)
        goto done;
    if (_persistable (o)) {
        if (plugin_treeroot (p)) {
            _log_external (o);
        } else {
            _add_backlog (p, o);
            if (!plugin_timeout_isset (p))
                plugin_timeout_set (p, log_reduction_timeout_msec);
        }
    }
    _log_save (p, o);

    farg.p = p;
    farg.o = o;
    zhash_foreach (ctx->listeners, _listener_fwd, &farg);
done:
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
}

/* Define plugin entry points.
 */

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
    else if (cmb_msg_match_substr (*zmsg, "log.dump.", &arg))
        _recv_log_dump (p, arg, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.fault.", &arg))
        _recv_fault_event (p, arg, zmsg);

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
    ctx->cirbuf = zlist_new ();

    zsocket_set_subscribe (p->zs_evin, "event.fault.");
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    zhash_destroy (&ctx->listeners);
    zlist_destroy (&ctx->backlog);
    zlist_destroy (&ctx->cirbuf);
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
