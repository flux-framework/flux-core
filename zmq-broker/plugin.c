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

#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "zmsg.h"
#include "route.h"
#include "cmbd.h"
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

typedef struct {
    conf_t *conf;
    void *zs_upreq; /* for making requests */
    void *zs_dnreq; /* for handling requests (reverse message flow) */
    void *zs_evin;
    void *zs_evout;
    void *zs_snoop;
    char *id;
    ptimeout_t timeout;
    pthread_t t;
    plugin_t plugin;
    server_t *srv;
    plugin_stats_t stats;
    zloop_t *zloop;
    zlist_t *deferred_responses;
    void *ctx;
    flux_t h;
} plugin_ctx_t;

struct ptimeout_struct {
    plugin_ctx_t *p;
    unsigned long msec;
};

static int plugin_timer_cb (zloop_t *zl, zmq_pollitem_t *i, ptimeout_t t);

struct plugin_struct apisrv;
struct plugin_struct barriersrv;
struct plugin_struct kvssrv;
struct plugin_struct livesrv;
struct plugin_struct logsrv;
struct plugin_struct syncsrv;
struct plugin_struct echosrv;
struct plugin_struct mechosrv;
struct plugin_struct jobsrv;
struct plugin_struct rexecsrv;
struct plugin_struct resrcsrv;

static plugin_t plugins[] = {
    &kvssrv,
    &syncsrv,
    &barriersrv,
    &apisrv,
    &livesrv,
    &logsrv,
    &echosrv,
    &mechosrv,
    &jobsrv,
    &rexecsrv,
    &resrcsrv
};
static const int plugins_len = sizeof (plugins)/sizeof (plugins[0]);

/**
 ** flux_t implementation
 **/

static int plugin_request_sendmsg (plugin_ctx_t *p, zmsg_t **zmsg)
{
    int rc;

    rc = zmsg_send (zmsg, p->zs_upreq);
    p->stats.upreq_send_count++;
    return rc;
}

static zmsg_t *plugin_request_recvmsg (plugin_ctx_t *p, bool nb)
{
    return zmsg_recv (p->zs_dnreq); /* FIXME: ignores nb flag */
}


static int plugin_response_sendmsg (plugin_ctx_t *p, zmsg_t **zmsg)
{
    int rc;

    rc = zmsg_send (zmsg, p->zs_dnreq);
    p->stats.dnreq_send_count++;
    return rc;
}

static zmsg_t *plugin_response_recvmsg (plugin_ctx_t *p, bool nb)
{
    return zmsg_recv (p->zs_upreq); /* FIXME: ignores nb flag */
}

static int plugin_response_putmsg (plugin_ctx_t *p, zmsg_t **zmsg)
{
    if (zlist_append (p->deferred_responses, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    return 0;
}

static int plugin_event_sendmsg (plugin_ctx_t *p, zmsg_t **zmsg)
{
    int rc;

    rc = zmsg_send (zmsg, p->zs_evout);
    p->stats.event_send_count++;
    return rc;
}

static zmsg_t *plugin_event_recvmsg (plugin_ctx_t *p, bool nb)
{
    return zmsg_recv (p->zs_evin); /* FIXME: ignores nb flag */
}

static int plugin_event_subscribe (plugin_ctx_t *p, const char *topic)
{
    zsocket_set_subscribe (p->zs_evin, topic ? (char *)topic : "");
    return 0;
}

static int plugin_event_unsubscribe (plugin_ctx_t *p, const char *topic)
{
    zsocket_set_unsubscribe (p->zs_evin, topic ? (char *)topic : "");
    return 0;
}

static zmsg_t *plugin_snoop_recvmsg (plugin_ctx_t *p, bool nb)
{
    return zmsg_recv (p->zs_snoop); /* FIXME: ignores nb flag */
}

static int plugin_snoop_subscribe (plugin_ctx_t *p, const char *topic)
{
    zsocket_set_subscribe (p->zs_snoop, topic ? (char *)topic : "");
    return 0;
}

static int plugin_snoop_unsubscribe (plugin_ctx_t *p, const char *topic)
{
    zsocket_set_unsubscribe (p->zs_snoop, topic ? (char *)topic : "");
    return 0;
}

static int plugin_rank (plugin_ctx_t *p)
{
    return p->conf->rank;
}

static int plugin_size (plugin_ctx_t *p)
{
    return p->conf->size;
}

static bool plugin_treeroot (plugin_ctx_t *p)
{
    return (p->conf->treeroot);
}

/* N.B. zloop_timer() cannot be called repeatedly with the same
 * arg value to update the timeout of a free running (times = 0) timer.
 * Doing so creates a new timer, so you will have it going off at both
 * old and new times.  Also, zloop_timer_end() is deferred until the
 * bottom of the zloop poll loop, so we can't call zloop_timer_end() followed
 * immediately by zloop_timer() with the same arg value or the timer is
 * removed before it can go off.  Workaround: delete and readd but make
 * sure the arg value is different (malloc-before-free plugin_ctx_t *
 * wrapper struct shenanegens below).
 */
static int plugin_timeout_set (plugin_ctx_t *p, unsigned long msec)
{
    ptimeout_t t;

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

static int plugin_timeout_clear (plugin_ctx_t *p)
{
    if (p->timeout) {
        (void)zloop_timer_end (p->zloop, p->timeout);
        free (p->timeout);
        p->timeout = NULL;
    }
    return 0;
}

static bool plugin_timeout_isset (plugin_ctx_t *p)
{
    return p->timeout ? true : false;
}

static zloop_t *plugin_get_zloop (plugin_ctx_t *p)
{
    return p->zloop;
}

static zctx_t *plugin_get_zctx (plugin_ctx_t *p)
{
    return p->srv->zctx;
}

/**
 ** end of handle implementation
 **/

static void plugin_ping_respond (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o;
    char *s = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    s = zmsg_route_str (*zmsg, 2);
    util_json_object_add_string (o, "route", s);
    if (flux_respond (p->h, zmsg, o) < 0) {
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

void plugin_stats_respond (plugin_ctx_t *p, zmsg_t **zmsg)
{
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

    if (flux_respond (p->h, zmsg, o) < 0) {
        err ("%s: flux_respond", __FUNCTION__);
        goto done;
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);    
}

static void plugin_handle_response (plugin_ctx_t *p, zmsg_t *zmsg)
{
    char *tag;

    p->stats.upreq_recv_count++;

    /* Extract the tag from the message.
     */
    if (!(tag = cmb_msg_tag (zmsg, false))) {
        msg ("discarding malformed message");
        goto done;
    }
    /* Intercept and handle internal watch replies for keys of interest.
     * If no match, call the user's recv callback.
     */
    if (!strcmp (tag, "kvs.watch"))
        kvs_watch_response (p->h, &zmsg); /* consumes zmsg on match */
    if (zmsg && p->plugin->recvFn)
        p->plugin->recvFn (p->h, &zmsg, ZMSG_RESPONSE);
    if (zmsg)
        msg ("discarding unexpected response from %s", tag);
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (tag)
        free (tag);
}

/* Process any responses received during synchronous request-reply handling.
 * Call this after every plugin callback that may have invoked one of the
 * synchronous request-reply functions.
 */
static void plugin_handle_deferred_responses (plugin_ctx_t *p)
{
    zmsg_t *zmsg;

    while ((zmsg = zlist_pop (p->deferred_responses)))
        plugin_handle_response (p, zmsg);
}

/* Handle a response.
 */
static int upreq_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t *p)
{
    zmsg_t *zmsg = zmsg_recv (p->zs_upreq);

    plugin_handle_response (p, zmsg);

    plugin_handle_deferred_responses (p);

    return (0);
}

/* Handle a request.
 */
static int dnreq_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t *p)
{
    zmsg_t *zmsg = zmsg_recv (p->zs_dnreq);
    char *tag, *method;

    p->stats.dnreq_recv_count++;

    /* Extract the tag from the message.  The first part should match
     * the plugin name.  The rest is the "method" name.
     */
    if (!(tag = cmb_msg_tag (zmsg, false)) || !(method = strchr (tag, '.'))) {
        msg ("discarding malformed message");
        goto done;
    }
    method++;
    /* Intercept and handle internal "methods" for this plugin.
     * If no match, call the user's recv callback.
     */
    if (!strcmp (method, "ping"))
        plugin_ping_respond (p, &zmsg);
    else if (!strcmp (method, "stats"))
        plugin_stats_respond (p, &zmsg);
    else if (zmsg && p->plugin->recvFn)
        p->plugin->recvFn (p->h, &zmsg, ZMSG_REQUEST);
    /* If request wasn't handled above, NAK it.
     */
    if (zmsg) {
        if (flux_respond_errnum (p->h, &zmsg, ENOSYS) < 0) {
            err ("%s: flux_respond_errnum", __FUNCTION__);
            goto done;
        }
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (tag)
        free (tag);

    plugin_handle_deferred_responses (p);

    return (0);
}
static int event_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t *p)
{
    zmsg_t *zmsg = zmsg_recv (p->zs_evin);

    p->stats.event_recv_count++;

    if (zmsg && p->plugin->recvFn)
        p->plugin->recvFn (p->h, &zmsg, ZMSG_EVENT);

    if (zmsg)
        zmsg_destroy (&zmsg);

    plugin_handle_deferred_responses (p);

    return (0);
}
static int snoop_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t *p)
{
    zmsg_t *zmsg =  zmsg_recv (p->zs_snoop);

    if (zmsg && p->plugin->recvFn)
        p->plugin->recvFn (p->h, &zmsg, ZMSG_SNOOP);

    if (zmsg)
        zmsg_destroy (&zmsg);

    plugin_handle_deferred_responses (p);

    return (0);
}

static int plugin_timer_cb (zloop_t *zl, zmq_pollitem_t *i, ptimeout_t t)
{
    plugin_ctx_t *p = t->p;

    if (p->plugin->timeoutFn)
        p->plugin->timeoutFn (p->h);

    plugin_handle_deferred_responses (p);

    return (0);
}

static zloop_t * plugin_zloop_create (plugin_ctx_t *p)
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
    plugin_ctx_t *p = arg;
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

    if (p->plugin->initFn)
        p->plugin->initFn (p->h);
    zloop_start (p->zloop);
    if (p->plugin->finiFn)
        p->plugin->finiFn (p->h);

    zloop_destroy (&p->zloop);

    return NULL;
}

static void plugin_destroy (void *arg)
{
    plugin_ctx_t *p = arg;
    int errnum;
    zmsg_t *zmsg;

    route_del (p->srv->rctx, p->plugin->name, p->plugin->name);

    /* FIXME: no mechanism to tell thread to exit yet */
    errnum = pthread_join (p->t, NULL);
    if (errnum)
        errn_exit (errnum, "pthread_join");

    zsocket_destroy (p->srv->zctx, p->zs_snoop);
    zsocket_destroy (p->srv->zctx, p->zs_evout);
    zsocket_destroy (p->srv->zctx, p->zs_evin);
    zsocket_destroy (p->srv->zctx, p->zs_dnreq);
    zsocket_destroy (p->srv->zctx, p->zs_upreq);

    if (p->timeout)
        free (p->timeout);

    while ((zmsg = zlist_pop (p->deferred_responses)))
        zmsg_destroy (&zmsg);
    zlist_destroy (&p->deferred_responses);

    free (p->id);

    free (p);
}

static plugin_t lookup_plugin (char *name)
{
    int i;

    for (i = 0; i < plugins_len; i++)
        if (!strcmp (plugins[i]->name, name))
            return plugins[i];
    return NULL;
}

static int plugin_create (char *name, server_t *srv, conf_t *conf)
{
    zctx_t *zctx = srv->zctx;
    plugin_ctx_t *p;
    flux_t h;
    int errnum;
    plugin_t plugin;

    if (!(plugin = lookup_plugin (name))) {
        msg ("unknown plugin '%s'", name);
        return -1;
    }

    p = xzmalloc (sizeof (plugin_ctx_t));
    p->conf = conf;
    p->srv = srv;
    p->plugin = plugin;

    if (!(p->deferred_responses = zlist_new ()))
        oom ();

    p->id = xzmalloc (strlen (name) + 16);
    snprintf (p->id, strlen (name) + 16, "%s-%d", name, conf->rank);

    h = flux_handle_create (p, (FluxFreeFn *)plugin_destroy, 0);
    h->request_sendmsg = (FluxSendMsg *)plugin_request_sendmsg;
    h->request_recvmsg = (FluxRecvMsg *)plugin_request_recvmsg;
    h->response_sendmsg = (FluxSendMsg *)plugin_response_sendmsg;
    h->response_recvmsg = (FluxRecvMsg *)plugin_response_recvmsg;
    h->response_putmsg = (FluxPutMsg *)plugin_response_putmsg;
    h->event_sendmsg = (FluxSendMsg *)plugin_event_sendmsg;
    h->event_recvmsg = (FluxRecvMsg *)plugin_event_recvmsg;
    h->event_subscribe = (FluxSub *)plugin_event_subscribe;
    h->event_unsubscribe = (FluxSub *)plugin_event_unsubscribe;
    h->snoop_recvmsg = (FluxRecvMsg *)plugin_snoop_recvmsg;
    h->snoop_subscribe = (FluxSub *)plugin_snoop_subscribe;
    h->snoop_unsubscribe = (FluxSub *)plugin_snoop_unsubscribe;
    h->rank = (FluxGetInt *)plugin_rank;
    h->size = (FluxGetInt *)plugin_size;
    h->treeroot = (FluxGetBool *)plugin_treeroot;
    h->timeout_set = (FluxTimeoutSet *)plugin_timeout_set;
    h->timeout_clear = (FluxTimeoutClear *)plugin_timeout_clear;
    h->timeout_isset = (FluxGetBool *)plugin_timeout_isset;
    h->get_zloop = (FluxGetZloop *)plugin_get_zloop;
    h->get_zctx = (FluxGetZctx *)plugin_get_zctx;
    p->h = h;

    /* connect sockets in the parent, then use them in the thread */
    zconnect (zctx, &p->zs_upreq, ZMQ_DEALER, UPREQ_URI, -1, p->id);
    zconnect (zctx, &p->zs_dnreq, ZMQ_DEALER, DNREQ_URI, -1, p->id);
    zconnect (zctx, &p->zs_evin,  ZMQ_SUB, DNEV_OUT_URI, 0, NULL);
    zconnect (zctx, &p->zs_evout, ZMQ_PUB, DNEV_IN_URI, -1, NULL);
    zconnect (zctx, &p->zs_snoop, ZMQ_SUB, SNOOP_URI, -1, NULL);

    route_add (p->srv->rctx, p->id, p->id, NULL, ROUTE_FLAGS_PRIVATE);
    route_add (p->srv->rctx, name, p->id, NULL, ROUTE_FLAGS_PRIVATE);

    errnum = pthread_create (&p->t, NULL, plugin_thread, p);
    if (errnum)
        errn_exit (errnum, "pthread_create");

    zhash_insert (srv->plugins, plugin->name, p);
    zhash_freefn (srv->plugins, plugin->name, plugin_destroy);

    return 0;
}

void plugin_init (conf_t *conf, server_t *srv)
{
    srv->plugins = zhash_new ();

    if (mapstr (conf->plugins, (mapstrfun_t)plugin_create, srv, conf) < 0)
        exit (1);
}

void plugin_fini (conf_t *conf, server_t *srv)
{
    zhash_destroy (&srv->plugins);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
