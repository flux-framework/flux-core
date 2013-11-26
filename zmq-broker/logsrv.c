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

#include "zmsg.h"
#include "util.h"
#include "log.h"
#include "plugin.h"

typedef struct {
    char *fac;         /* FIXME: switch to regex */
    int lev_max;        /* the lower the number, the more filtering */
    int lev_min;
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
    int log_persist_level;
    bool disabled;
    flux_t h;
} ctx_t;

static void add_backlog (ctx_t *ctx, json_object *o);
static void process_backlog (ctx_t *ctx);

static void freectx (ctx_t *ctx)
{
    zhash_destroy (&ctx->listeners);
    zlist_destroy (&ctx->backlog);
    zlist_destroy (&ctx->cirbuf);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "logsrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->listeners = zhash_new ();
        ctx->backlog = zlist_new ();
        ctx->cirbuf = zlist_new ();
        ctx->disabled = false;
        ctx->h = h;
        flux_aux_set (h, "logsrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

static bool match_subscription (json_object *o, subscription_t *sub)
{
    int lev;
    const char *fac;

    if (util_json_object_get_int (o, "level", &lev) < 0
     || util_json_object_get_string (o, "facility", &fac) < 0
     || lev > sub->lev_max
     || lev < sub->lev_min
     || strncasecmp (sub->fac, fac, strlen (sub->fac)) != 0)
        return false;
    return true;
}

static subscription_t *create_subscription (char *arg)
{
    subscription_t *sub = xzmalloc (sizeof (*sub));
    char *nextptr;
    
    /* 'level.facility' */
    sub->lev_min = LOG_EMERG;
    sub->lev_max = strtoul (arg, &nextptr, 10);
    sub->fac = *nextptr ? xstrdup (nextptr + 1) : xstrdup (nextptr);

    return sub;
}

static void destroy_subscription (subscription_t *sub)
{
    free (sub->fac);
    free (sub);
}

/* Manage circular buffer.
 */

static void log_save (ctx_t *ctx, json_object *o)
{
    if (ctx->cirbuf_size == ctx->log_circular_buffer_entries) {
        json_object *tmp = zlist_pop (ctx->cirbuf);
        json_object_put (tmp);
        ctx->cirbuf_size--;
    }
    json_object_get (o);
    zlist_append (ctx->cirbuf, o);
    ctx->cirbuf_size++;
}

static void recv_log_dump (ctx_t *ctx, char *arg, zmsg_t **zmsg)
{
    json_object *o;
    zmsg_t *cpy;
    subscription_t *sub = create_subscription (arg);

    o = zlist_first (ctx->cirbuf);
    while (o != NULL) {
        if (match_subscription (o, sub)) {
            if (!(cpy = zmsg_dup (*zmsg)))
                oom ();
            flux_respond (ctx->h, &cpy, o);
        }
        o = zlist_next (ctx->cirbuf);
    }
    flux_respond_errnum (ctx->h, zmsg, ENOENT);
    destroy_subscription (sub);
}

static void recv_fault_event (ctx_t *ctx, char *arg, zmsg_t **zmsg)
{
    subscription_t sub = {
        .lev_min = ctx->log_persist_level,
        .lev_max = LOG_DEBUG,
        .fac = arg
    };
    json_object *o;
    zlist_t *temp;

    if (!(temp = zlist_new ()))
        oom ();
    while ((o = zlist_pop (ctx->cirbuf))) {
        if (match_subscription (o, &sub)) {
            add_backlog (ctx, o);
            json_object_put (o);
        } else
            zlist_append (temp, o);
    }
    zlist_destroy (&ctx->cirbuf);
    ctx->cirbuf = temp;
    process_backlog (ctx);
}

static bool resize_cirbuf (ctx_t *ctx, int new_size)
{
    bool ret = false;

    if (new_size > 0) {
        while (ctx->cirbuf_size > new_size) {
            json_object *tmp = zlist_pop (ctx->cirbuf);
            json_object_put (tmp);
            ctx->cirbuf_size--;
        }
        ctx->log_circular_buffer_entries = new_size;
        ret = true;
    }
    return ret;
}

/* Manage listeners.
 */

static listener_t *listener_create (zmsg_t *zmsg)
{
    listener_t *lp = xzmalloc (sizeof (listener_t));
    if (!(lp->zmsg = zmsg_dup (zmsg)))
        oom ();
    if (!(lp->subscriptions = zlist_new ()))
        oom ();
    return lp;
}

static void listener_destroy (void *arg)
{
    listener_t *lp = arg;
    subscription_t *sub;

    zmsg_destroy (&lp->zmsg);
    while ((sub = zlist_pop (lp->subscriptions)))
        destroy_subscription (sub);
    zlist_destroy (&lp->subscriptions);
    free (lp);
}

static void listener_subscribe (listener_t *lp, char *arg)
{
    zlist_append (lp->subscriptions, create_subscription (arg));    
}

static void listener_unsubscribe (listener_t *lp, char *fac)
{
    subscription_t *sub;

    do {
        sub = zlist_first (lp->subscriptions);
        while (sub) { 
            if (!strncasecmp (fac, sub->fac, strlen (fac))) {
                zlist_remove (lp->subscriptions, sub);
                destroy_subscription (sub);
                break;
            }
            sub = zlist_next (lp->subscriptions);
        }
    } while (sub);
}

static void recv_log_subscribe (ctx_t *ctx, char *arg, zmsg_t **zmsg)
{
    char *sender = NULL;
    listener_t *lp;
    
    if (!(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__); 
        goto done;
    }
    if (!(lp = zhash_lookup (ctx->listeners, sender))) {
        lp = listener_create (*zmsg);
        zhash_insert (ctx->listeners, sender, lp);
        zhash_freefn (ctx->listeners, sender, listener_destroy);
    }
    listener_subscribe (lp, arg);
done:
    if (sender)
        free (sender);
    zmsg_destroy (zmsg);
}

static void recv_log_unsubscribe (ctx_t *ctx, char *sub, zmsg_t **zmsg)
{
    listener_t *lp;
    char *sender = NULL;

    if (!(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__); 
        goto done;
    }
    lp = zhash_lookup (ctx->listeners, sender);
    if (lp)
        listener_unsubscribe (lp, sub);
done:
    if (sender)
        free (sender);
    zmsg_destroy (zmsg);
}

static void recv_log_disconnect (ctx_t *ctx, zmsg_t **zmsg)
{
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

static void log_external (json_object *o)
{
    const char *fac, *src, *message;
    struct timeval tv;
    int lev, count;

    if (util_json_object_get_string (o, "facility", &fac) == 0
     && util_json_object_get_int (o, "level", &lev) == 0
     && util_json_object_get_string (o, "source", &src) == 0
     && util_json_object_get_timeval (o, "timestamp", &tv) == 0 
     && util_json_object_get_string (o, "message", &message) == 0
     && util_json_object_get_int (o, "count", &count) == 0) {
        const char *levstr = log_leveltostr (lev);
        msg ("[%-.6lu.%-.6lu] %dx %s.%s[%s]: %s", tv.tv_sec, tv.tv_usec, count,
             fac, levstr ? levstr : "unknown", src, message);
    } /* FIXME: expose iface in log.[ch] to pass syslog facility, level */
}

static bool match_reduce (json_object *o1, json_object *o2)
{
    int lev1, lev2;
    const char *fac1, *fac2;
    const char *msg1, *msg2;

    if (util_json_object_get_int (o1, "level", &lev1) < 0
     || util_json_object_get_int (o2, "level", &lev2) < 0
     || lev1 != lev2)
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

static void combine_reduce (json_object *o1, json_object *o2)
{
    int count1 = 0, count2 = 0;

    (void)util_json_object_get_int (o1, "count", &count1);
    (void)util_json_object_get_int (o2, "count", &count2);
    util_json_object_add_int (o1, "count", count1 + count2);
}

static void process_backlog_one (ctx_t *ctx, json_object **op)
{
    int hopcount = 0;

    if (flux_treeroot (ctx->h))
        log_external (*op);
    else {
        /* Increment hopcount each time a message is forwarded upstream.
         */
        (void)util_json_object_get_int (*op, "hopcount", &hopcount);
        hopcount++;
        util_json_object_add_int (*op, "hopcount", hopcount);
        flux_request_send (ctx->h, *op, "log.msg");
    }
    json_object_put (*op);
    *op = NULL;
}

static bool timestamp_compare (void *item1, void *item2)
{
    json_object *o1 = item1;
    json_object *o2 = item2;
    struct timeval tv1, tv2;

    util_json_object_get_timeval (o1, "timestamp", &tv1);
    util_json_object_get_timeval (o2, "timestamp", &tv2);

    return timercmp (&tv1, &tv2, >);
}

static void process_backlog (ctx_t *ctx)
{
    json_object *o, *lasto = NULL;

    zlist_sort (ctx->backlog, timestamp_compare);
    while ((o = zlist_pop (ctx->backlog))) {
        if (!lasto) {
            lasto = o;
        } else if (match_reduce (lasto, o)) {
            combine_reduce (lasto, o);
            json_object_put (o);
        } else {
            process_backlog_one (ctx, &lasto);
            lasto = o;
        }
    }
    if (lasto)
        process_backlog_one (ctx, &lasto);
}

static void add_backlog (ctx_t *ctx, json_object *o)
{
    json_object_get (o);
    zlist_append (ctx->backlog, o);
}

typedef struct {
    ctx_t *ctx;
    json_object *o;
} fwdarg_t;

static int listener_fwd (const char *key, void *litem, void *arg)
{
    listener_t *lp = litem;
    fwdarg_t *farg = arg;
    ctx_t *ctx = farg->ctx;
    zmsg_t *zmsg;
    subscription_t *sub;

    sub = zlist_first (lp->subscriptions);
    while (sub) {
        if (match_subscription (farg->o, sub)) {
            if (!(zmsg = zmsg_dup (lp->zmsg)))
                oom ();
            flux_respond (ctx->h, &zmsg, farg->o);
            break;
        }
        sub = zlist_next (lp->subscriptions);
    }
    return 1;
}

static void recv_log_msg (ctx_t *ctx, zmsg_t **zmsg)
{
    fwdarg_t farg;
    json_object *o = NULL;
    int hopcount = 0, level = 0;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL)
        goto done;

    (void)util_json_object_get_int (o, "level", &level);
    (void)util_json_object_get_int (o, "hopcount", &hopcount);

    if (level <= ctx->log_persist_level || hopcount > 0) {
        add_backlog (ctx, o);
        if (!flux_timeout_isset (ctx->h))
            flux_timeout_set (ctx->h, ctx->log_reduction_timeout_msec);
    }

    if (hopcount == 0)
        log_save (ctx, o);
   
    farg.ctx = ctx;
    farg.o = o;
    zhash_foreach (ctx->listeners, listener_fwd, &farg);
done:
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
}

static void logsrv_recv (flux_t h, zmsg_t **zmsg, int typemask)
{
    ctx_t *ctx = getctx (h);
    char *arg = NULL;

    if (ctx->disabled)
        return;

    if (cmb_msg_match (*zmsg, "log.msg"))
        recv_log_msg (ctx, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "log.subscribe.", &arg))
        recv_log_subscribe (ctx, arg, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "log.unsubscribe.", &arg))
        recv_log_unsubscribe (ctx, arg, zmsg);
    else if (cmb_msg_match (*zmsg, "log.disconnect"))
        recv_log_disconnect (ctx, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "log.dump.", &arg))
        recv_log_dump (ctx, arg, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.fault.", &arg))
        recv_fault_event (ctx, arg, zmsg);

    if (arg)
        free (arg);
}

static void logsrv_timeout (flux_t h)
{
    process_backlog (getctx (h));
    flux_timeout_clear (h);
}

static void set_config (const char *path, kvsdir_t dir, void *arg, int errnum)
{
    ctx_t *ctx = arg;
    char *s, *key;
    int val;

    if (errnum != 0) {
        err ("log: %s", path);
        goto invalid;
    }

    key = kvsdir_key_at (dir, "reduction-timeout-msec");
    if (kvs_get_int (ctx->h, key, &val) < 0) {
        err ("log: %s", key);
        goto invalid; 
    }
    if ((ctx->log_reduction_timeout_msec = val) < 0) {
        msg ("log: %s must be >= 0", key);
        goto invalid; 
    }
    free (key);

    key = kvsdir_key_at (dir, "circular-buffer-entries");
    if (kvs_get_int (ctx->h, key, &val) < 0) {
        err ("log: %s", key);
        goto invalid;
    }
    if (!resize_cirbuf (ctx, val)) {
        msg ("log: %s must be > 0", key);
        goto invalid;
    }
    free (key);

    key = kvsdir_key_at (dir, "persist-level");
    if (kvs_get_string (ctx->h, key, &s) < 0) {
        err ("log: %s", key);
        goto invalid;
    }
    if ((ctx->log_persist_level = log_strtolevel (s)) < 0) {
        msg ("log: %s invalid level string", key);
        goto invalid;
    }
    free (key);

    if (ctx->disabled) {
        msg ("log: %s values OK, logging resumed", path);
        ctx->disabled = false;
    }
    return;
invalid:
    if (!ctx->disabled) {
        msg ("log: %s values invalid, logging suspended", path);
        ctx->disabled = true;
    }
}

static int logsrv_init (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (kvs_watch_dir (h, set_config, ctx, "conf.log") < 0) {
        err ("log: %s", "conf.log");
        return -1;
    }
    flux_event_subscribe (h, "event.fault.");
    return 0;
}

static void logsrv_fini (flux_t h)
{
    //ctx_t *ctx = getctx (h);

    flux_event_unsubscribe (h, "event.fault.");
}

const struct plugin_ops ops = {
    .recv    = logsrv_recv,
    .init    = logsrv_init,
    .fini    = logsrv_fini,
    .timeout = logsrv_timeout,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
