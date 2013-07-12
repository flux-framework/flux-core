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

typedef struct {
    char *fac;         /* FIXME: switch to regex */
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
    int log_reduction_timeout_msec;
    int log_circular_buffer_entries;
    int log_persist_priority;
} ctx_t;

static void _add_backlog (plugin_ctx_t *p, json_object *o);
static void _process_backlog (plugin_ctx_t *p);


static bool _match_subscription (json_object *o, subscription_t *sub)
{
    int pri;
    const char *fac;

    if (util_json_object_get_int (o, "priority", &pri) < 0
     || util_json_object_get_string (o, "facility", &fac) < 0
     || pri > sub->pri_max
     || pri < sub->pri_min
     || strncasecmp (sub->fac, fac, strlen (sub->fac)) != 0)
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

static void _log_save (plugin_ctx_t *p, json_object *o)
{
    ctx_t *ctx = p->ctx;

    if (ctx->cirbuf_size == ctx->log_circular_buffer_entries) {
        json_object *tmp = zlist_pop (ctx->cirbuf);
        json_object_put (tmp);
        ctx->cirbuf_size--;
    }
    json_object_get (o);
    zlist_append (ctx->cirbuf, o);
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

static void _recv_fault_event (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    subscription_t sub = {
        .pri_min = ctx->log_persist_priority,
        .pri_max = CMB_LOG_DEBUG,
        .fac = arg
    };
    json_object *o;
    zlist_t *temp;

    if (!(temp = zlist_new ()))
        oom ();
    while ((o = zlist_pop (ctx->cirbuf))) {
        if (_match_subscription (o, &sub)) {
            _add_backlog (p, o);
            json_object_put (o);
        } else
            zlist_append (temp, o);
    }
    zlist_destroy (&ctx->cirbuf);
    ctx->cirbuf = temp;
    _process_backlog (p);
}

static void _resize_cirbuf (plugin_ctx_t *p, int new_size)
{
    ctx_t *ctx = p->ctx;

    while (ctx->cirbuf_size > new_size) {
        json_object *tmp = zlist_pop (ctx->cirbuf);
        json_object_put (tmp);
        ctx->cirbuf_size--;
    }
    ctx->log_circular_buffer_entries = new_size;
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

static void _log_external (json_object *o)
{
    const char *fac, *src, *message;
    struct timeval tv;
    int pri, count;

    if (util_json_object_get_string (o, "facility", &fac) == 0
     && util_json_object_get_int (o, "priority", &pri) == 0
     && util_json_object_get_string (o, "source", &src) == 0
     && util_json_object_get_timeval (o, "timestamp", &tv) == 0 
     && util_json_object_get_string (o, "message", &message) == 0
     && util_json_object_get_int (o, "count", &count) == 0) {
        msg ("[%-.6lu.%-.6lu] %dx %s.%s[%s]: %s", tv.tv_sec, tv.tv_usec, count,
             fac, util_logpri_str (pri), src, message);
    } /* FIXME: expose iface in log.[ch] to pass syslog facility, priority */
}

static bool _match_reduce (json_object *o1, json_object *o2)
{
    int pri1, pri2;
    const char *fac1, *fac2;
    const char *msg1, *msg2;

    if (util_json_object_get_int (o1, "priority", &pri1) < 0
     || util_json_object_get_int (o2, "priority", &pri2) < 0
     || pri1 != pri2)
        return false;
    if (util_json_object_get_string (o1, "facility", &fac1) < 0
     || util_json_object_get_string (o2, "facility", &fac2) < 0
     || strcmp (fac1, fac2) != 0)
        return false;
    if (util_json_object_get_string (o1, "message", &msg1) < 0
     || util_json_object_get_string (o2, "message", &msg2) < 0
     || strcmp (msg1, msg2) != 0)
        return false;

    return true;
}

static void _combine_reduce (json_object *o1, json_object *o2)
{
    int count1 = 0, count2 = 0;

    (void)util_json_object_get_int (o1, "count", &count1);
    (void)util_json_object_get_int (o2, "count", &count2);
    util_json_object_add_int (o1, "count", count1 + count2);
}

static void _process_backlog_one (plugin_ctx_t *p, json_object **op)
{
    int hopcount = 0;

    if (plugin_treeroot (p))
        _log_external (*op);
    else {
        /* Increment hopcount each time a message is forwarded upstream.
         */
        (void)util_json_object_get_int (*op, "hopcount", &hopcount);
        hopcount++;
        util_json_object_add_int (*op, "hopcount", hopcount);
        plugin_send_request (p, *op, "log.msg");
    }
    json_object_put (*op);
    *op = NULL;
}

static bool _timestamp_compare (void *item1, void *item2)
{
    json_object *o1 = item1;
    json_object *o2 = item2;
    struct timeval tv1, tv2;

    util_json_object_get_timeval (o1, "timestamp", &tv1);
    util_json_object_get_timeval (o2, "timestamp", &tv2);

    return timercmp (&tv1, &tv2, >);
}

static void _process_backlog (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    json_object *o, *lasto = NULL;

    zlist_sort (ctx->backlog, _timestamp_compare);
    while ((o = zlist_pop (ctx->backlog))) {
        if (!lasto) {
            lasto = o;
        } else if (_match_reduce (lasto, o)) {
            _combine_reduce (lasto, o);
            json_object_put (o);
        } else {
            _process_backlog_one (p, &lasto);
            lasto = o;
        }
    }
    if (lasto)
        _process_backlog_one (p, &lasto);
}

static void _add_backlog (plugin_ctx_t *p, json_object *o)
{
    ctx_t *ctx = p->ctx;

    json_object_get (o);
    zlist_append (ctx->backlog, o);
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
    int hopcount = 0, priority = 0;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL)
        goto done;

    (void)util_json_object_get_int (o, "priority", &priority);
    (void)util_json_object_get_int (o, "hopcount", &hopcount);

    if (priority <= ctx->log_persist_priority || hopcount > 0) {
        _add_backlog (p, o);
        if (!plugin_timeout_isset (p))
            plugin_timeout_set (p, ctx->log_reduction_timeout_msec);
    }

    if (hopcount == 0)
        _log_save (p, o);
   
    farg.p = p; farg.o = o;
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
    _process_backlog (p);
    plugin_timeout_clear (p);
}

static void _set_log_reduction_timeout_msec (const char *key, json_object *o,
                                             void *arg)
{
    plugin_ctx_t *p = arg;
    ctx_t *ctx = p->ctx;
    int i;

    if (!o)
        msg_exit ("live: %s is not set", key);
    i = json_object_get_int (o);
    if (i < 0)
        msg_exit ("live: bad %s value: %d", key, i);
    ctx->log_reduction_timeout_msec = i;
}

static void _set_log_circular_buffer_entries (const char *key, json_object *o,
                                              void *arg)
{
    plugin_ctx_t *p = arg;
    int i;

    if (!o)
        msg_exit ("live: %s is not set", key);
    i = json_object_get_int (o);
    if (i < 0)
        msg_exit ("live: bad %s value: %d", key, i);
    _resize_cirbuf (p, i);
}

static void _set_log_persist_priority (const char *key, json_object *o,
                                       void *arg)
{
    plugin_ctx_t *p = arg;
    ctx_t *ctx = p->ctx;
    int i;

    if (!o)
        msg_exit ("live: %s is not set", key);
    i = json_object_get_int (o);
    if (i < CMB_LOG_EMERG || i > CMB_LOG_DEBUG)
        msg_exit ("live: bad %s value: %d", key, i);
    ctx->log_persist_priority = i;
}


static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    ctx->listeners = zhash_new ();
    ctx->backlog = zlist_new ();
    ctx->cirbuf = zlist_new ();

    plugin_conf_watch (p, "log.reduction.timeout.msec",
                      _set_log_reduction_timeout_msec, p);
    plugin_conf_watch (p, "log.circular.buffer.entries",
                      _set_log_circular_buffer_entries, p);
    plugin_conf_watch (p, "log.persist.priority",
                      _set_log_persist_priority, p);

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
