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
    char *fac;          /* FIXME: switch to regex */
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
    bool timer_armed;
} ctx_t;

static void add_backlog (ctx_t *ctx, json_object *o);
static void process_backlog (ctx_t *ctx);
static int timeout_cb (flux_t h, void *arg);

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

static subscription_t *create_subscription (const char *arg)
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

static int dump_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    zmsg_t *cpy;
    const char *fac;
    subscription_t *sub;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
                    || util_json_object_get_string (o, "fac", &fac) < 0)
        goto done;

    sub = create_subscription (fac);
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
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int fault_event_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    subscription_t sub = {
        .lev_min = ctx->log_persist_level,
        .lev_max = LOG_DEBUG,
    };
    json_object *o, *so;
    zlist_t *temp;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
       || util_json_object_get_string (o, "fac", (const char **)&sub.fac) < 0) {
        goto done;
    }
    if (!(temp = zlist_new ()))
        oom ();
    while ((so = zlist_pop (ctx->cirbuf))) {
        if (match_subscription (so, &sub)) {
            add_backlog (ctx, so);
            json_object_put (so);
        } else
            zlist_append (temp, so);
    }
    zlist_destroy (&ctx->cirbuf);
    ctx->cirbuf = temp;
    process_backlog (ctx);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
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

static void listener_subscribe (listener_t *lp, const char *arg)
{
    zlist_append (lp->subscriptions, create_subscription (arg));    
}

static void listener_unsubscribe (listener_t *lp, const char *fac)
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

static int subscribe_request_cb (flux_t h, int typemask, zmsg_t **zmsg,
                                 void *arg)
                                    
{
    ctx_t *ctx = arg;
    char *sender = NULL;
    listener_t *lp;
    const char *sub;
    json_object *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
                    || util_json_object_get_string (o, "sub", &sub) < 0
                    || !(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__); 
        goto done;
    }
    if (!(lp = zhash_lookup (ctx->listeners, sender))) {
        lp = listener_create (*zmsg);
        zhash_insert (ctx->listeners, sender, lp);
        zhash_freefn (ctx->listeners, sender, listener_destroy);
    }
    listener_subscribe (lp, sub);
done:
    if (sender)
        free (sender);
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
    return 0;
}

static int unsubscribe_request_cb (flux_t h, int typemask, zmsg_t **zmsg,
                                   void *arg)
{
    ctx_t *ctx = arg;
    listener_t *lp;
    char *sender = NULL;
    const char *sub;
    json_object *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
                    || util_json_object_get_string (o, "sub", &sub) < 0
                    || !(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__); 
        goto done;
    }
    lp = zhash_lookup (ctx->listeners, sender);
    if (lp)
        listener_unsubscribe (lp, sub);
done:
    if (sender)
        free (sender);
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int disconnect_request_cb (flux_t h, int typemask, zmsg_t **zmsg,
                                  void *arg)
{
    ctx_t *ctx = arg;
    char *sender;

    if (!(sender = cmb_msg_sender (*zmsg))) {
        err ("%s: protocol error", __FUNCTION__); 
        goto done;
    }
    zhash_delete (ctx->listeners, sender);
    free (sender);
done:
    zmsg_destroy (zmsg);
    return 0;
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

static int msg_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    fwdarg_t farg;
    json_object *o = NULL;
    int hopcount = 0, level = 0;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL)
        goto done;

    (void)util_json_object_get_int (o, "level", &level);
    (void)util_json_object_get_int (o, "hopcount", &hopcount);

    if (level <= ctx->log_persist_level || hopcount > 0) {
        add_backlog (ctx, o);
        if (!ctx->timer_armed) {
            if (flux_tmouthandler_add (h, ctx->log_reduction_timeout_msec,
                                       true, timeout_cb, ctx) < 0) {
                flux_log (h, LOG_ERR, "flux_tmouthandler_add: %s",
                          strerror (errno));
                goto done;
            }
            ctx->timer_armed = true;
        }
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
    return 0;
}

static int timeout_cb (flux_t h, void *arg)
{
    ctx_t *ctx = arg;
    ctx->timer_armed = false; /* one shot */
    process_backlog (ctx);
    return 0;
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

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "log.msg",          msg_request_cb },
    { FLUX_MSGTYPE_REQUEST,   "log.subscribe",    subscribe_request_cb },
    { FLUX_MSGTYPE_REQUEST,   "log.unsubscribe",  unsubscribe_request_cb },
    { FLUX_MSGTYPE_REQUEST,   "log.disconnect",   disconnect_request_cb },
    { FLUX_MSGTYPE_REQUEST,   "log.dump",         dump_request_cb },
    { FLUX_MSGTYPE_EVENT,     "fault.*",          fault_event_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);


int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (kvs_watch_dir (h, set_config, ctx, "conf.log") < 0) {
        err ("log: %s", "conf.log");
        return -1;
    }
    flux_event_subscribe (h, "fault.");
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

MOD_NAME ("log");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
