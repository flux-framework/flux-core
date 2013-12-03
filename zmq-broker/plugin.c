/* plugin.c - broker plugin interface */

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdarg.h>
#include <dlfcn.h>

#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "zmsg.h"
#include "util.h"
#include "plugin.h"

#include "flux.h"
#include "handle.h"

typedef struct {
    int upreq_send_count;
    int upreq_recv_count;
    int dnreq_send_count;
    int dnreq_recv_count;
    int event_send_count;
    int event_recv_count;
} plugin_stats_t;

typedef struct ptimeout_struct *ptimeout_t;

#define PLUGIN_MAGIC    0xfeefbe01
struct plugin_ctx_struct {
    int magic;
    void *zs_upreq; /* for making requests */
    void *zs_dnreq; /* for handling requests (reverse message flow) */
    void *zs_evin;
    void *zs_evout;
    void *zs_snoop;
    char *id;
    ptimeout_t timeout;
    pthread_t t;
    const struct plugin_ops *ops;
    plugin_stats_t stats;
    zloop_t *zloop;
    zlist_t *deferred_responses;
    void *zctx;
    flux_t h;
    char *name;
    void *dso;
    zhash_t *args;
    int rank;
    bool reactor_stop;
};

struct ptimeout_struct {
    plugin_ctx_t p;
    unsigned long msec;
};

static const struct flux_handle_ops plugin_handle_ops;

static int plugin_timer_cb (zloop_t *zl, zmq_pollitem_t *i, ptimeout_t t);

/**
 ** flux_t implementation
 **/

static int plugin_request_sendmsg (void *impl, zmsg_t **zmsg)
{
    plugin_ctx_t p = impl;
    int rc;

    assert (p->magic == PLUGIN_MAGIC);
    rc = zmsg_send (zmsg, p->zs_upreq);
    p->stats.upreq_send_count++;
    return rc;
}

static zmsg_t *plugin_request_recvmsg (void *impl, bool nb)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    return zmsg_recv (p->zs_dnreq); /* FIXME: ignores nb flag */
}


static int plugin_response_sendmsg (void *impl, zmsg_t **zmsg)
{
    int rc;
    plugin_ctx_t p = impl;

    assert (p->magic == PLUGIN_MAGIC);
    rc = zmsg_send (zmsg, p->zs_dnreq);
    p->stats.dnreq_send_count++;
    return rc;
}

static zmsg_t *plugin_response_recvmsg (void *impl, bool nb)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    return zmsg_recv (p->zs_upreq); /* FIXME: ignores nb flag */
}

static int plugin_response_putmsg (void *impl, zmsg_t **zmsg)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    if (zlist_append (p->deferred_responses, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    return 0;
}

static int plugin_event_sendmsg (void *impl, zmsg_t **zmsg)
{
    int rc;
    plugin_ctx_t p = impl;

    assert (p->magic == PLUGIN_MAGIC);
    rc = zmsg_send (zmsg, p->zs_evout);
    p->stats.event_send_count++;
    return rc;
}

static zmsg_t *plugin_event_recvmsg (void *impl, bool nb)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    return zmsg_recv (p->zs_evin); /* FIXME: ignores nb flag */
}

static int plugin_event_subscribe (void *impl, const char *topic)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    zsocket_set_subscribe (p->zs_evin, topic ? (char *)topic : "");
    return 0;
}

static int plugin_event_unsubscribe (void *impl, const char *topic)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    zsocket_set_unsubscribe (p->zs_evin, topic ? (char *)topic : "");
    return 0;
}

static zmsg_t *plugin_snoop_recvmsg (void *impl, bool nb)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    return zmsg_recv (p->zs_snoop); /* FIXME: ignores nb flag */
}

static int plugin_snoop_subscribe (void *impl, const char *topic)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    zsocket_set_subscribe (p->zs_snoop, topic ? (char *)topic : "");
    return 0;
}

static int plugin_snoop_unsubscribe (void *impl, const char *topic)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    zsocket_set_unsubscribe (p->zs_snoop, topic ? (char *)topic : "");
    return 0;
}
static int plugin_rank (void *impl)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    return p->rank;
}

static zctx_t *plugin_get_zctx (void *impl)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    return p->zctx;
}

static void plugin_reactor_stop (void *impl)
{
    plugin_ctx_t p = impl;
    p->reactor_stop = true;
}

static int fd_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t p)
{
    handle_event_fd (p->h, item->fd, item->revents);
    return (p->reactor_stop ? -1 : 0);
}

static int plugin_reactor_fd_add (void *impl, int fd, short events)
{
    plugin_ctx_t p = impl;
    zmq_pollitem_t item = { .fd = fd, .events = events };

    return zloop_poller (p->zloop, &item, (zloop_fn *)fd_cb, p);
}

static void plugin_reactor_fd_remove (void *impl, int fd, short events)
{
    plugin_ctx_t p = impl;
    zmq_pollitem_t item = { .fd = fd, .events = events };

    zloop_poller_end (p->zloop, &item); /* FIXME: 'events' are ignored */
}

static int zs_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t p)
{
    handle_event_zs (p->h, item->socket, item->revents);
    return (p->reactor_stop ? -1 : 0);
}

static int plugin_reactor_zs_add (void *impl, void *zs, short events)
{
    plugin_ctx_t p = impl;
    zmq_pollitem_t item = { .socket = zs, .events = events };

    return zloop_poller (p->zloop, &item, (zloop_fn *)zs_cb, p);
}

static void plugin_reactor_zs_remove (void *impl, void *zs, short events)
{
    plugin_ctx_t p = impl;
    zmq_pollitem_t item = { .socket = zs, .events = events };

    zloop_poller_end (p->zloop, &item); /* FIXME: 'events' are ignored */
}


/* N.B. zloop_timer() cannot be called repeatedly with the same
 * arg value to update the timeout of a free running (times = 0) timer.
 * Doing so creates a new timer, so you will have it going off at both
 * old and new times.  Also, zloop_timer_end() is deferred until the
 * bottom of the zloop poll loop, so we can't call zloop_timer_end() followed
 * immediately by zloop_timer() with the same arg value or the timer is
 * removed before it can go off.  Workaround: delete and readd but make
 * sure the arg value is different (malloc-before-free plugin_ctx_t
 * wrapper struct shenanegens below).
 */
static int plugin_reactor_timeout_set (void *impl, unsigned long msec)
{
    ptimeout_t t;
    plugin_ctx_t p = impl;

    assert (p->magic == PLUGIN_MAGIC);
    if (p->timeout)
        (void)zloop_timer_end (p->zloop, p->timeout);
    if (!(t = xzmalloc (sizeof (struct ptimeout_struct))))
        oom ();
    t->p = p;
    t->msec = msec;
    if (zloop_timer (p->zloop, msec, 0, (zloop_fn *)plugin_timer_cb, t) < 0)
        err_exit ("zloop_timer"); 
    if (p->timeout)
        free (p->timeout); /* free after xzmalloc - see comment above */
    p->timeout = t;
    return 0;
}

static int plugin_reactor_timeout_clear (void *impl)
{
    plugin_ctx_t p = impl;

    assert (p->magic == PLUGIN_MAGIC);
    if (p->timeout) {
        (void)zloop_timer_end (p->zloop, p->timeout);
        free (p->timeout);
        p->timeout = NULL;
    }
    return 0;
}

static bool plugin_reactor_timeout_isset (void *impl)
{
    plugin_ctx_t p = impl;
    assert (p->magic == PLUGIN_MAGIC);
    return p->timeout ? true : false;
}


/**
 ** end of handle implementation
 **/

static void ping_req_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    //plugin_ctx_t p = arg;
    json_object *o;
    char *s = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    s = zmsg_route_str (*zmsg, 2);
    util_json_object_add_string (o, "route", s);
    if (flux_respond (h, zmsg, o) < 0) {
        err ("%s: flux_respond", __FUNCTION__);
        goto done;
    }
done:
    if (o)
        json_object_put (o);
    if (s)
        free (s);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void stats_req_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    plugin_ctx_t p = arg;
    json_object *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    util_json_object_add_int (o, "upreq_send_count", p->stats.upreq_send_count);
    util_json_object_add_int (o, "upreq_recv_count", p->stats.upreq_recv_count);
    util_json_object_add_int (o, "dnreq_send_count", p->stats.dnreq_send_count);
    util_json_object_add_int (o, "dnreq_recv_count", p->stats.dnreq_recv_count);
    util_json_object_add_int (o, "event_send_count", p->stats.event_send_count);
    util_json_object_add_int (o, "event_recv_count", p->stats.event_recv_count);

    if (flux_respond (h, zmsg, o) < 0) {
        err ("%s: flux_respond", __FUNCTION__);
        goto done;
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);    
}

static void plugin_handle_response (plugin_ctx_t p, zmsg_t *zmsg)
{
    p->stats.upreq_recv_count++;

    if (zmsg)
        handle_event_msg (p->h, FLUX_MSGTYPE_RESPONSE, &zmsg);
    if (zmsg && p->ops->recv)
        p->ops->recv (p->h, &zmsg, FLUX_MSGTYPE_RESPONSE);
    if (zmsg)
        zmsg_destroy (&zmsg);
}

/* Process any responses received during synchronous request-reply handling.
 * Call this after every plugin callback that may have invoked one of the
 * synchronous request-reply functions.
 */
static void plugin_handle_deferred_responses (plugin_ctx_t p)
{
    zmsg_t *zmsg;

    while ((zmsg = zlist_pop (p->deferred_responses)))
        plugin_handle_response (p, zmsg);
}

/* Handle a response.
 */
static int upreq_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t p)
{
    zmsg_t *zmsg = zmsg_recv (p->zs_upreq);

    plugin_handle_response (p, zmsg);
    plugin_handle_deferred_responses (p);

    return (0);
}

/* Handle a request.
 */
static int dnreq_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t p)
{
    zmsg_t *zmsg = zmsg_recv (p->zs_dnreq);

    p->stats.dnreq_recv_count++;

    if (zmsg)
        handle_event_msg (p->h, FLUX_MSGTYPE_REQUEST, &zmsg);
    if (zmsg && p->ops->recv)
        p->ops->recv (p->h, &zmsg, FLUX_MSGTYPE_REQUEST);
    if (zmsg) {
        if (flux_respond_errnum (p->h, &zmsg, ENOSYS) < 0) {
            err ("%s: flux_respond_errnum", __FUNCTION__);
            goto done;
        }
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    plugin_handle_deferred_responses (p);

    return (p->reactor_stop ? -1 : 0);
}

static int event_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t p)
{
    zmsg_t *zmsg = zmsg_recv (p->zs_evin);

    p->stats.event_recv_count++;

    if (zmsg)
        handle_event_msg (p->h, FLUX_MSGTYPE_EVENT, &zmsg);
    if (zmsg && p->ops->recv)
        p->ops->recv (p->h, &zmsg, FLUX_MSGTYPE_EVENT);

    if (zmsg)
        zmsg_destroy (&zmsg);

    plugin_handle_deferred_responses (p);

    return (p->reactor_stop ? -1 : 0);
}

static int snoop_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t p)
{
    zmsg_t *zmsg =  zmsg_recv (p->zs_snoop);

    if (zmsg)
        handle_event_msg (p->h, FLUX_MSGTYPE_SNOOP, &zmsg);
    if (zmsg && p->ops->recv)
        p->ops->recv (p->h, &zmsg, FLUX_MSGTYPE_SNOOP);

    if (zmsg)
        zmsg_destroy (&zmsg);

    plugin_handle_deferred_responses (p);

    return (p->reactor_stop ? -1 : 0);
}

static int plugin_timer_cb (zloop_t *zl, zmq_pollitem_t *i, ptimeout_t t)
{
    plugin_ctx_t p = t->p;

    handle_event_tmout (p->h);

    if (p->ops->timeout)
        p->ops->timeout (p->h);

    plugin_handle_deferred_responses (p);

    return (p->reactor_stop ? -1 : 0);
}

static zloop_t * plugin_zloop_create (plugin_ctx_t p)
{
    int rc;
    zloop_t *zl;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(zl = zloop_new ()))
        err_exit ("zloop_new");

    zp.socket = p->zs_upreq;
    if ((rc = zloop_poller (zl, &zp, (zloop_fn *) upreq_cb, (void *) p)) != 0)
        err_exit ("zloop_poller: rc=%d", rc);
    zp.socket = p->zs_dnreq;
    if ((rc = zloop_poller (zl, &zp, (zloop_fn *) dnreq_cb, (void *) p)) != 0)
        err_exit ("zloop_poller: rc=%d", rc);
    zp.socket = p->zs_evin;
    if ((rc = zloop_poller (zl, &zp, (zloop_fn *) event_cb, (void *) p)) != 0)
        err_exit ("zloop_poller: rc=%d", rc);
    zp.socket = p->zs_snoop;
    if ((rc = zloop_poller (zl, &zp, (zloop_fn *) snoop_cb, (void *) p)) != 0)
        err_exit ("zloop_poller: rc=%d", rc);

    return (zl);
}

static void *plugin_thread (void *arg)
{
    plugin_ctx_t p = arg;
    sigset_t signal_set;
    int errnum;

    /* block all signals */
    if (sigfillset (&signal_set) < 0)
        err_exit ("sigfillset");
    if ((errnum = pthread_sigmask (SIG_BLOCK, &signal_set, NULL)) != 0)
        errn_exit (errnum, "pthread_sigmask");

    p->zloop = plugin_zloop_create (p);
    if (p->zloop == NULL)
        err_exit ("%s: plugin_zloop_create", p->id);

    /* Register callbacks for ping, stats which can be overridden
     * in p->ops->init() if desired.
     */
    if (flux_msghandler_add (p->h, FLUX_MSGTYPE_REQUEST, "*.ping",
                                                          ping_req_cb, p) < 0)
        err_exit ("%s: flux_msghandler_add *.ping", p->id);
    if (flux_msghandler_add (p->h, FLUX_MSGTYPE_REQUEST, "*.stats",
                                                          stats_req_cb, p) < 0)
        err_exit ("%s: flux_msghandler_add *.stats", p->id);

    if (p->ops->init) {
        if (p->ops->init (p->h, p->args) < 0) {
            err ("%s: init failed", p->name);
            goto done;
        }
    }

    zloop_start (p->zloop);
    if (p->ops->fini)
        p->ops->fini (p->h);
done:
    zloop_destroy (&p->zloop);

    return NULL;
}

const char *plugin_name (plugin_ctx_t p)
{
    return p->name;
}

const char *plugin_id (plugin_ctx_t p)
{
    return p->id;
}

void plugin_unload (plugin_ctx_t p)
{
    int errnum;
    zmsg_t *zmsg;

    /* FIXME: no mechanism to tell thread to exit yet */
    errnum = pthread_join (p->t, NULL);
    if (errnum)
        errn_exit (errnum, "pthread_join");

    zsocket_destroy (p->zctx, p->zs_snoop);
    zsocket_destroy (p->zctx, p->zs_evout);
    zsocket_destroy (p->zctx, p->zs_evin);
    zsocket_destroy (p->zctx, p->zs_dnreq);
    zsocket_destroy (p->zctx, p->zs_upreq);

    if (p->timeout)
        free (p->timeout);

    while ((zmsg = zlist_pop (p->deferred_responses)))
        zmsg_destroy (&zmsg);
    zlist_destroy (&p->deferred_responses);

    dlclose (p->dso);
    free (p->name);
    free (p->id);

    free (p);
}

static void *plugin_dlopen (const char *searchpath, const char *name)
{
    char *cpy = xstrdup (searchpath);
    char *path, *dir, *saveptr, *a1 = cpy;
    void *dso = NULL;

    while ((dir = strtok_r (a1, ":", &saveptr))) {
        if (asprintf (&path, "%s/%ssrv.so", dir, name) < 0)
            oom ();
        dso = dlopen (path, RTLD_NOW | RTLD_LOCAL);
        free (path);
        if (dso)
            break;
        a1 = NULL;
    }
    free (cpy);
    return dso;
}

plugin_ctx_t plugin_load (flux_t h, const char *searchpath,
                          char *name, char *id, zhash_t *args)
{
    plugin_ctx_t p;
    int errnum;
    const struct plugin_ops *ops;
    void *dso;
    char *errstr;
        
    if (!(dso = plugin_dlopen (searchpath, name))) {
        msg ("plugin `%s' not found in search path (%s)", name, searchpath);
        return NULL;
    }
    dlerror ();
    ops = (const struct plugin_ops *)dlsym (dso, "ops");
    if ((errstr = dlerror ()) != NULL) {
        err ("%s", errstr);
        dlclose (dso);
        return NULL;
    }

    p = xzmalloc (sizeof (*p));
    p->magic = PLUGIN_MAGIC;
    p->zctx = flux_get_zctx (h);
    p->args = args;
    p->ops = ops;
    p->dso = dso;
    p->name = xstrdup (name);
    p->id = xstrdup (id);
    p->rank = flux_rank (h);
    if (!(p->deferred_responses = zlist_new ()))
        oom ();

    p->h = handle_create (p, &plugin_handle_ops, 0);
    flux_log_set_facility (p->h, name);

    /* connect sockets in the parent, then use them in the thread */
    zconnect (p->zctx, &p->zs_upreq, ZMQ_DEALER, UPREQ_URI, -1, id);
    zconnect (p->zctx, &p->zs_dnreq, ZMQ_DEALER, DNREQ_URI, -1, id);
    zconnect (p->zctx, &p->zs_evin,  ZMQ_SUB, DNEV_OUT_URI, 0, NULL);
    zconnect (p->zctx, &p->zs_evout, ZMQ_PUB, DNEV_IN_URI, -1, NULL);
    zconnect (p->zctx, &p->zs_snoop, ZMQ_SUB, SNOOP_URI, -1, NULL);

    errnum = pthread_create (&p->t, NULL, plugin_thread, p);
    if (errnum)
        errn_exit (errnum, "pthread_create");

    return p;
}

static const struct flux_handle_ops plugin_handle_ops = {
    .request_sendmsg = plugin_request_sendmsg,
    .request_recvmsg = plugin_request_recvmsg,
    .response_sendmsg = plugin_response_sendmsg,
    .response_recvmsg = plugin_response_recvmsg,
    .response_putmsg = plugin_response_putmsg,
    .event_sendmsg = plugin_event_sendmsg,
    .event_recvmsg = plugin_event_recvmsg,
    .event_subscribe = plugin_event_subscribe,
    .event_unsubscribe = plugin_event_unsubscribe,
    .snoop_recvmsg = plugin_snoop_recvmsg,
    .snoop_subscribe = plugin_snoop_subscribe,
    .snoop_unsubscribe = plugin_snoop_unsubscribe,
    .rank = plugin_rank,
    .get_zctx = plugin_get_zctx,
    .reactor_stop = plugin_reactor_stop,
    .reactor_fd_add = plugin_reactor_fd_add,
    .reactor_fd_remove = plugin_reactor_fd_remove,
    .reactor_zs_add = plugin_reactor_zs_add,
    .reactor_zs_remove = plugin_reactor_zs_remove,
    .reactor_timeout_set = plugin_reactor_timeout_set,
    .reactor_timeout_clear = plugin_reactor_timeout_clear,
    .reactor_timeout_isset = plugin_reactor_timeout_isset,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
