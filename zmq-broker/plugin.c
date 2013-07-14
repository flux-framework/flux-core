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
#include "zmq.h"
#include "route.h"
#include "cmb.h"
#include "cmbd.h"
#include "apisrv.h"
#include "barriersrv.h"
#include "syncsrv.h"
#include "kvssrv.h"
#include "livesrv.h"
#include "logsrv.h"
#include "confsrv.h"
#include "util.h"
#include "plugin.h"

static plugin_t plugins[] = {
    &kvssrv,
    &syncsrv,
    &barriersrv,
    &apisrv,
    &livesrv,
    &logsrv,
    &confsrv,
};
const int plugins_len = sizeof (plugins)/sizeof (plugins[0]);

bool plugin_treeroot (plugin_ctx_t *p)
{
    return (p->conf->treeroot);
}

void plugin_timeout_set (plugin_ctx_t *p, unsigned long val)
{
    p->timeout = val;
}

void plugin_timeout_clear (plugin_ctx_t *p)
{
    p->timeout = -1;
}

bool plugin_timeout_isset (plugin_ctx_t *p)
{
    return p->timeout == -1 ? false : true;
}

void plugin_send_request_raw (plugin_ctx_t *p, zmsg_t **zmsg)
{
    if (zmsg_send (zmsg, p->zs_upreq) < 0)
        err_exit ("%s: zmsg_send", __FUNCTION__);
    p->stats.upreq_send_count++;
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

void plugin_send_event_json (plugin_ctx_t *p, json_object *o,
                             const char *fmt, ...)
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
    zmsg = cmb_msg_encode (tag, o);
    free (tag);
    plugin_send_event_raw (p, &zmsg);
}

void plugin_send_request (plugin_ctx_t *p, json_object *o, const char *fmt, ...)
{
    va_list ap;
    zmsg_t *zmsg;
    char *tag;
    int n;
    json_object *empty = NULL;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0)
        err_exit ("vasprintf");

    if (!o) {
        if (!(empty = json_object_new_object ()))
            oom ();
        o = empty;
    }
    zmsg = cmb_msg_encode (tag, o);
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* delimiter frame */
        oom ();
    plugin_send_request_raw (p, &zmsg);

    if (empty)
        json_object_put (empty);
    free (tag);
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

void plugin_log (plugin_ctx_t *p, logpri_t pri, const char *fmt, ...)
{
    va_list ap;
    json_object *o;
   
    va_start (ap, fmt);
    o = util_json_vlog (pri, p->plugin->name, p->conf->rankstr, fmt, ap);
    va_end (ap);

    if (o) {
        plugin_send_request (p, o, "log.msg");
        json_object_put (o);
    }
}

static void _conf_add_watcher (plugin_ctx_t *p, const char *key,
                               ConfSetF *set, void *arg)
{
    conf_watcher_t *wp = xzmalloc (sizeof (conf_watcher_t));

    wp->arg = arg;
    wp->set = set;
    zhash_update (p->conf_watcher, key, wp);
    zhash_freefn (p->conf_watcher, key, free);
}

static json_object *_conf_get (plugin_ctx_t *p, const char *key, bool watch)
{
    json_object *vo = NULL, *o = util_json_object_new_object ();
    zmsg_t *zmsg = NULL;

    util_json_object_add_string (o, "key", key);
    util_json_object_add_boolean (o, "watch", watch);
    plugin_send_request (p, o, "conf.get");
    if (!(zmsg = zmsg_recv (p->zs_upreq))) {
        err ("%s: zmsg_recv", __FUNCTION__);
        goto done;
    }
    if (cmb_msg_decode (zmsg, NULL, &o) < 0) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }
    vo = json_object_object_get (o, "val");
    json_object_get (vo);
done:
    if (o)
        json_object_put (o);
    if (zmsg)
        zmsg_destroy (&zmsg);
    return vo;       
}

json_object *plugin_conf (plugin_ctx_t *p, const char *key)
{
    return _conf_get (p, key, false);
}

void plugin_conf_watch (plugin_ctx_t *p, const char *key,
                        ConfSetF *set, void *arg)
{
    set (key, _conf_get (p, key, true), arg);
    _conf_add_watcher (p, key, set, arg);    
}

void plugin_watch_update (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *o = NULL;
    conf_watcher_t *wp; 
    const char *key;

    if (cmb_msg_decode (*zmsg, NULL, &o) == 0 && o != NULL
                        && util_json_object_get_string (o, "key", &key) == 0
                        && (wp = zhash_lookup (p->conf_watcher, key))) {
        wp->set (key, json_object_object_get (o, "val"), wp->arg);
        zmsg_destroy (zmsg);
    }
    if (o)
        json_object_put (o);
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


static void _plugin_poll (plugin_ctx_t *p)
{
    zmq_pollitem_t zpa[] = {
{ .socket = p->zs_upreq,     .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = p->zs_dnreq,     .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = p->zs_evin,      .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = p->zs_snoop,     .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    zmsg_t *zmsg;
    struct timeval t1, t2, t;
    long elapsed, msec = -1;
    char *pingtag, *statstag;
    zmsg_type_t type;

    if (asprintf (&pingtag, "%s.ping", p->plugin->name) < 0)
        err_exit ("asprintf");
    if (asprintf (&statstag, "%s.stats", p->plugin->name) < 0)
        err_exit ("asprintf");

    for (;;) {
        /* set up timeout */
        if (p->timeout > 0) {
            if (msec == -1) { 
                msec = p->timeout;
                xgettimeofday (&t1, NULL);
            }
        } else
            msec = -1;

        if (zmq_poll (zpa, sizeof (zpa) / sizeof (zpa[0]), msec) < 0)
            err_exit ("zmq_poll");

        /* process timeout */
        if (p->timeout > 0) {
            xgettimeofday (&t2, NULL);
            timersub (&t2, &t1, &t);
            elapsed = (t.tv_sec * 1000 + t.tv_usec / 1000);
            if (elapsed < p->timeout) {
                msec = p->timeout - elapsed;
            } else {
                if (p->plugin->timeoutFn)
                    p->plugin->timeoutFn (p);
                msec = -1;
            }
        }

        /* receive a message */
        if (zpa[0].revents & ZMQ_POLLIN) {   /* response on 'upreq' */
            zmsg = zmsg_recv (p->zs_upreq);
            type = ZMSG_RESPONSE;
            p->stats.upreq_recv_count++;
        } else if (zpa[1].revents & ZMQ_POLLIN) {   /* request on 'dnreq' */
            zmsg = zmsg_recv (p->zs_dnreq);
            if (!zmsg)
                err ("zmsg_recv");
            type = ZMSG_REQUEST;
            p->stats.dnreq_recv_count++;
        } else if (zpa[2].revents & ZMQ_POLLIN) {   /* event on 'in_event' */
            zmsg = zmsg_recv (p->zs_evin);
            if (!zmsg)
                err ("zmsg_recv");
            type = ZMSG_EVENT;
            p->stats.event_recv_count++;
        } else if (zpa[3].revents & ZMQ_POLLIN) {   /* debug on 'snoop' */
            zmsg = zmsg_recv (p->zs_snoop);
            type = ZMSG_SNOOP;
        } else
            zmsg = NULL;

        /* intercept and respond to ping requests for this plugin */
        if (zmsg && type == ZMSG_REQUEST && cmb_msg_match (zmsg, pingtag))
            plugin_ping_respond (p, &zmsg);

        /* intercept and respond to stats request for this plugin */
        if (zmsg && type == ZMSG_REQUEST && cmb_msg_match (zmsg, statstag))
            plugin_stats_respond (p, &zmsg);

        /* intercept "conf.get" response for watched conf values */
        if (zmsg && type == ZMSG_RESPONSE && cmb_msg_match (zmsg, "conf.get"))
            plugin_watch_update (p, &zmsg);

        /* dispatch message to plugin's recvFn() */
        /*     recvFn () shouldn't free zmsg if it doesn't recognize the tag */
        if (zmsg && p->plugin->recvFn)
            p->plugin->recvFn (p, &zmsg, type);

        /* send ENOSYS response indicating plugin did not recognize tag */
        if (zmsg && type == ZMSG_REQUEST)
            plugin_send_response_errnum (p, &zmsg, ENOSYS);

        if (zmsg)
            zmsg_destroy (&zmsg);
    }

    free (pingtag);
    free (statstag);
}

static void *_plugin_thread (void *arg)
{
    plugin_ctx_t *p = arg;

    if (p->plugin->initFn)
        p->plugin->initFn (p);
    if (p->plugin->pollFn)
        p->plugin->pollFn (p);
    else
        _plugin_poll (p);
    if (p->plugin->finiFn)
        p->plugin->finiFn (p);

    return NULL;
}    

static void _plugin_destroy (void *arg)
{
    plugin_ctx_t *p = arg;
    int errnum;

    route_del (p->srv->rctx, p->plugin->name, p->plugin->name);

    /* FIXME: no mechanism to tell thread to exit yet */
    errnum = pthread_join (p->t, NULL);
    if (errnum)
        errn_exit (errnum, "pthread_join\n");

    zsocket_destroy (p->srv->zctx, p->zs_snoop);
    zsocket_destroy (p->srv->zctx, p->zs_evout);
    zsocket_destroy (p->srv->zctx, p->zs_evin);
    zsocket_destroy (p->srv->zctx, p->zs_dnreq);
    zsocket_destroy (p->srv->zctx, p->zs_upreq);

    zhash_destroy (&p->conf_watcher);

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
    plugin_timeout_clear (p);

    if (!(p->conf_watcher = zhash_new ()))
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
        errn_exit (errnum, "pthread_create\n");

    zhash_insert (srv->plugins, plugin->name, p);
    zhash_freefn (srv->plugins, plugin->name, _plugin_destroy);

    return 0;
}

void plugin_init (conf_t *conf, server_t *srv)
{
    srv->plugins = zhash_new ();

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
