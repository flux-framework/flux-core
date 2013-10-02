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
struct plugin_struct jobsrv;
struct plugin_struct rexecsrv;

static plugin_t plugins[] = {
    &kvssrv,
    &syncsrv,
    &barriersrv,
    &apisrv,
    &livesrv,
    &logsrv,
    &echosrv,
    &jobsrv,
    &rexecsrv
};
const int plugins_len = sizeof (plugins)/sizeof (plugins[0]);

bool plugin_treeroot (plugin_ctx_t *p)
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
void plugin_timeout_set (plugin_ctx_t *p, unsigned long msec)
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
}

void plugin_timeout_clear (plugin_ctx_t *p)
{
    if (p->timeout) {
        (void)zloop_timer_end (p->zloop, p->timeout);
        free (p->timeout);
        p->timeout = NULL;
    }
}

bool plugin_timeout_isset (plugin_ctx_t *p)
{
    return p->timeout ? true : false;
}

void plugin_send_request_raw (plugin_ctx_t *p, zmsg_t **zmsg)
{
    if (zmsg_send (zmsg, p->zs_upreq) < 0)
        err_exit ("%s: zmsg_send", __FUNCTION__);
    p->stats.upreq_send_count++;
}

zmsg_t *plugin_recv_response_raw (plugin_ctx_t *p)
{
    zmsg_t *zmsg = zmsg_recv (p->zs_upreq);

    if (!zmsg)
        err_exit ("%s: zmsg_recv", __FUNCTION__);
    return zmsg;
}

void plugin_send_response_raw (plugin_ctx_t *p, zmsg_t **zmsg)
{
    if (zmsg_send (zmsg, p->zs_dnreq) < 0)
        err_exit ("%s: zmsg_send", __FUNCTION__);
    p->stats.dnreq_send_count++;
}

void plugin_send_event_raw (plugin_ctx_t *p, zmsg_t **zmsg)
{
    if (zmsg_send (zmsg, p->zs_evout) < 0)
        err_exit ("%s: zmsg_send", __FUNCTION__);
    p->stats.event_send_count++;
}

void plugin_send_event (plugin_ctx_t *p, const char *fmt, ...)
{
    va_list ap;
    zmsg_t *zmsg;
    char *tag;
    int n;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0)
        err_exit ("%s: vasprintf", __FUNCTION__);
    zmsg = cmb_msg_encode (tag, NULL);
    free (tag);
    plugin_send_event_raw (p, &zmsg);
}

static void plugin_send_vrequest (plugin_ctx_t *p, json_object *o,
                                  const char *fmt, va_list ap)
{
    json_object *empty = NULL;
    char *tag;
    zmsg_t *zmsg;

    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();

    if (!o)
        o = empty = util_json_object_new_object ();
    zmsg = cmb_msg_encode (tag, o);
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* delimiter frame */
        oom ();
    plugin_send_request_raw (p, &zmsg);
    if (empty)
        json_object_put (empty);
    free (tag);
}                        

static json_object *plugin_recv_vresponse (plugin_ctx_t *p,
                                           const char *fmt, va_list ap)
{
    char *tag, *reply_tag;
    zmsg_t *zmsg;
    json_object *reply_obj;

    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();

    for (;;) {
        zmsg = plugin_recv_response_raw (p);
        if (cmb_msg_decode (zmsg, &reply_tag, &reply_obj) < 0) {
            err ("%s: dropping malformed reply", __FUNCTION__);
            zmsg_destroy (&zmsg);
            continue;
        }
        if (!reply_obj) {
            msg ("%s: dropping %s reply with no JSON", __FUNCTION__, reply_tag);
            free (reply_tag);
            zmsg_destroy (&zmsg);
            continue;
        }
        if (strcmp (tag, reply_tag) != 0) {
            json_object_put (reply_obj);
            free (reply_tag);
            if (zlist_append (p->deferred_responses, zmsg) < 0)
                oom ();
            continue;
        }
        zmsg_destroy (&zmsg);
        free (reply_tag);
        if (util_json_object_get_int (reply_obj, "errnum", &errno) == 0) {
            free (reply_obj);
            reply_obj = NULL;
        }
        break;
    }
    free (tag);
    return reply_obj;
}

void plugin_send_request (plugin_ctx_t *p, json_object *o, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    plugin_send_vrequest (p, o, fmt, ap);
    va_end (ap);
}

json_object *plugin_request (plugin_ctx_t *p, json_object *o,
                             const char *fmt, ...)
{
    va_list ap;
    json_object *reply;

    va_start (ap, fmt);
    plugin_send_vrequest (p, o, fmt, ap);
    va_end (ap);

    va_start (ap, fmt);
    reply = plugin_recv_vresponse (p, fmt, ap);
    va_end (ap);

    return reply;
}

void plugin_send_response (plugin_ctx_t *p, zmsg_t **req, json_object *o)
{
    if (cmb_msg_replace_json (*req, o) < 0)
        err_exit ("%s: cmb_msg_replace_json", __FUNCTION__);
    plugin_send_response_raw (p, req);
}

void plugin_send_response_errnum (plugin_ctx_t *p, zmsg_t **req, int errnum)
{
    if (cmb_msg_replace_json_errnum (*req, errnum) < 0)
        err_exit ("%s: cmb_msg_replace_json_errnum", __FUNCTION__);
    plugin_send_response_raw (p, req);
}

void plugin_log (plugin_ctx_t *p, int lev, const char *fmt, ...)
{
    va_list ap;
    json_object *request;
   
    va_start (ap, fmt);
    request = util_json_vlog (lev, p->plugin->name, p->conf->rankstr, fmt, ap);
    va_end (ap);

    if (!request) {
        err ("%s", __FUNCTION__);
        goto done;
    }
    plugin_send_request (p, request, "log.msg");
done:
    if (request)
        json_object_put (request);
}

void plugin_ping_respond (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o;
    char *s = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    s = zmsg_route_str (*zmsg, 2);
    util_json_object_add_string (o, "route", s);
    plugin_send_response (p, zmsg, o);
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

    plugin_send_response (p, zmsg, o);
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
        kvs_watch_response (p, &zmsg); /* consumes zmsg on match */
    if (zmsg && p->plugin->recvFn)
        p->plugin->recvFn (p, &zmsg, ZMSG_RESPONSE);
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
        p->plugin->recvFn (p, &zmsg, ZMSG_REQUEST);
    /* If request wasn't handled above, NAK it.
     */
    if (zmsg)
        plugin_send_response_errnum (p, &zmsg, ENOSYS);
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
        p->plugin->recvFn (p, &zmsg, ZMSG_EVENT);

    if (zmsg)
        zmsg_destroy (&zmsg);

    plugin_handle_deferred_responses (p);

    return (0);
}
static int snoop_cb (zloop_t *zl, zmq_pollitem_t *item, plugin_ctx_t *p)
{
    zmsg_t *zmsg =  zmsg_recv (p->zs_snoop);

    if (zmsg && p->plugin->recvFn)
        p->plugin->recvFn (p, &zmsg, ZMSG_SNOOP);

    if (zmsg)
        zmsg_destroy (&zmsg);

    plugin_handle_deferred_responses (p);

    return (0);
}

static int plugin_timer_cb (zloop_t *zl, zmq_pollitem_t *i, ptimeout_t t)
{
    plugin_ctx_t *p = t->p;

    if (p->plugin->timeoutFn)
        p->plugin->timeoutFn (p);

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

static void *_plugin_thread (void *arg)
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
        p->plugin->initFn (p);
    zloop_start (p->zloop);
    if (p->plugin->finiFn)
        p->plugin->finiFn (p);

    zloop_destroy (&p->zloop);

    return NULL;
}

static kvsctx_t _get_kvs_ctx (void *h)
{
    plugin_ctx_t *p = h;

    return p->kvs_ctx;
}

static void _plugin_destroy (void *arg)
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

    kvs_ctx_destroy (p->kvs_ctx);

    while ((zmsg = zlist_pop (p->deferred_responses)))
        zmsg_destroy (&zmsg);
    zlist_destroy (&p->deferred_responses);

    free (p->id);

    free (p);
}

static plugin_t _lookup_plugin (char *name)
{
    int i;

    for (i = 0; i < plugins_len; i++)
        if (!strcmp (plugins[i]->name, name))
            return plugins[i];
    return NULL;
}

static int _plugin_create (char *name, server_t *srv, conf_t *conf)
{
    zctx_t *zctx = srv->zctx;
    plugin_ctx_t *p;
    int errnum;
    plugin_t plugin;

    if (!(plugin = _lookup_plugin (name))) {
        msg ("unknown plugin '%s'", name);
        return -1;
    }

    p = xzmalloc (sizeof (plugin_ctx_t));
    p->conf = conf;
    p->srv = srv;
    p->plugin = plugin;

    p->kvs_ctx = kvs_ctx_create (p);

    if (!(p->deferred_responses = zlist_new ()))
        oom ();

    p->id = xzmalloc (strlen (name) + 16);
    snprintf (p->id, strlen (name) + 16, "%s-%d", name, conf->rank);

    /* connect sockets in the parent, then use them in the thread */
    zconnect (zctx, &p->zs_upreq, ZMQ_DEALER, UPREQ_URI, -1, p->id);
    zconnect (zctx, &p->zs_dnreq, ZMQ_DEALER, DNREQ_URI, -1, p->id);
    zconnect (zctx, &p->zs_evin,  ZMQ_SUB, DNEV_OUT_URI, 0, NULL);
    zconnect (zctx, &p->zs_evout, ZMQ_PUB, DNEV_IN_URI, -1, NULL);
    zconnect (zctx, &p->zs_snoop, ZMQ_SUB, SNOOP_URI, -1, NULL);

    route_add (p->srv->rctx, p->id, p->id, NULL, ROUTE_FLAGS_PRIVATE);
    route_add (p->srv->rctx, name, p->id, NULL, ROUTE_FLAGS_PRIVATE);

    errnum = pthread_create (&p->t, NULL, _plugin_thread, p);
    if (errnum)
        errn_exit (errnum, "pthread_create");

    zhash_insert (srv->plugins, plugin->name, p);
    zhash_freefn (srv->plugins, plugin->name, _plugin_destroy);

    return 0;
}

void plugin_init (conf_t *conf, server_t *srv)
{
    srv->plugins = zhash_new ();

    kvs_reqfun_set ((KVSReqF *)plugin_request);
    kvs_barrierfun_set (NULL); /* kvs_fence not used in plugin context as yet */
    kvs_getctxfun_set ((KVSGetCtxF *)_get_kvs_ctx);

    if (mapstr (conf->plugins, (mapstrfun_t)_plugin_create, srv, conf) < 0)
        exit (1);
}

void plugin_fini (conf_t *conf, server_t *srv)
{
    zhash_destroy (&srv->plugins);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
