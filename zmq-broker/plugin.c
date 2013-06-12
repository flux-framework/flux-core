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

#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "zmq.h"
#include "cmbd.h"
#include "cmb.h"
#include "apisrv.h"
#include "barriersrv.h"
#include "syncsrv.h"
#include "kvssrv.h"
#include "livesrv.h"
#include "util.h"
#include "plugin.h"

static plugin_t plugins[] = {
    &kvssrv,
    &syncsrv,
    &barriersrv,
    &apisrv,
    &livesrv,
};
const int plugins_len = sizeof (plugins)/sizeof (plugins[0]);

/* pluginname.ping - return message to sender without change */
static void _plugin_ping(plugin_ctx_t *p, zmsg_t **zmsg)
{
    if (zmsg_send (zmsg, p->zs_out) < 0)
        err ("%s: zmsg_send", __FUNCTION__);
}

/* pluginname.stats - return generic plugin statistics */
static void _plugin_stats (plugin_ctx_t *p, zmsg_t **zmsg)
{
    json_object *no, *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (!(no = json_object_new_int (p->stats.req_count)))
        oom ();
    json_object_object_add (o, "req_count", no);
    if (!(no = json_object_new_int (p->stats.rep_count)))
        oom ();
    json_object_object_add (o, "rep_count", no);
    if (!(no = json_object_new_int (p->stats.event_count)))
        oom ();
    json_object_object_add (o, "event_count", no);
    if (cmb_msg_rep_json (*zmsg, o) < 0) {
        err ("%s: cmb_msg_rep_json", __FUNCTION__);
        goto done;
    }
    if (zmsg_send (zmsg, p->zs_out) < 0) {
        err ("%s: zmsg_send", __FUNCTION__);
        goto done;
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);    
}


static void _plugin_poll (plugin_ctx_t *p)
{
    zmq_pollitem_t zpa[] = {
{ .socket = p->zs_in,        .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = p->zs_in_event,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = p->zs_req,       .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
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

        zpoll(zpa, sizeof (zpa) / sizeof (zpa[0]), msec);

        /* process timeout */
        if (p->timeout > 0) {
            xgettimeofday (&t2, NULL);
            timersub (&t2, &t1, &t);
            elapsed = (t.tv_sec * 1000 + t.tv_usec / 1000);
            if (elapsed < p->timeout) {
                msec -= elapsed;
            } else {
                if (p->plugin->timeoutFn)
                    p->plugin->timeoutFn (p);
                msec = -1;
            }
        }

        /* receive a message */
        if (zpa[0].revents & ZMQ_POLLIN) {          /* request on 'in' */
            zmsg = zmsg_recv (p->zs_in);
            if (!zmsg)
                err ("zmsg_recv");
            type = ZMSG_REQUEST;
            p->stats.req_count++;
        } else if (zpa[1].revents & ZMQ_POLLIN) {   /* event on 'in_event' */
            zmsg = zmsg_recv (p->zs_in_event);
            if (!zmsg)
                err ("zmsg_recv");
            type = ZMSG_EVENT;
            p->stats.event_count++;
        } else if (zpa[2].revents & ZMQ_POLLIN) {   /* response on 'req' */
            zmsg = zmsg_recv (p->zs_req);
            type = ZMSG_RESPONSE;
            p->stats.rep_count++;
        } else if (zpa[3].revents & ZMQ_POLLIN) {   /* debug on 'snoop' */
            zmsg = zmsg_recv (p->zs_snoop);
            type = ZMSG_SNOOP;
        } else
            zmsg = NULL;

        /* intercept and respond to ping requests for this plugin */
        if (zmsg && type == ZMSG_REQUEST && cmb_msg_match (zmsg, pingtag))
            _plugin_ping (p, &zmsg);

        /* intercept and respond to stats request for this plugin */
        if (zmsg && type == ZMSG_REQUEST && cmb_msg_match (zmsg, statstag))
            _plugin_stats (p, &zmsg);

        /* dispatch message to plugin's recvFn() */
        /*     recvFn () shouldn't free zmsg if it doesn't recognize the tag */
        if (zmsg && p->plugin->recvFn)
            p->plugin->recvFn (p, &zmsg, type);

        /* send ENOSYS response indicating plugin did not recognize tag */
        if (zmsg && type == ZMSG_REQUEST)
            cmb_msg_send_errnum (&zmsg, p->zs_out, ENOSYS, NULL);

        if (zmsg)
            zmsg_destroy (&zmsg);


        if (zmsg)
            zmsg_destroy (&zmsg);
        assert (zmsg == NULL);
    }

    free (pingtag);
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

    /* FIXME: no mechanism to tell thread to exit yet */
    errnum = pthread_join (p->t, NULL);
    if (errnum)
        errn_exit (errnum, "pthread_join\n");

    /* plugin side */
    zsocket_destroy (p->srv->zctx, p->zs_snoop);
    zsocket_destroy (p->srv->zctx, p->zs_out_event);
    zsocket_destroy (p->srv->zctx, p->zs_in_event);
    zsocket_destroy (p->srv->zctx, p->zs_out);
    zsocket_destroy (p->srv->zctx, p->zs_in);
    zsocket_destroy (p->srv->zctx, p->zs_req);

    /* server side */
    zsocket_destroy (p->srv->zctx, p->zs_plout);

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
    char *plout_uri = NULL;
    plugin_t plugin;

    if (!(plugin = _lookup_plugin (name))) {
        msg ("unknown plugin '%s'", name);
        return -1;
    }

    p = xzmalloc (sizeof (plugin_ctx_t));
    p->conf = conf;
    p->srv = srv;
    p->plugin = plugin;
    p->timeout = -1;

    if (asprintf (&plout_uri, PLOUT_URI_TMPL, plugin->name) < 0)
        err_exit ("asprintf");

    /* server side */
    zbind (zctx, &p->zs_plout,        ZMQ_PUSH, plout_uri,        -1);

    /* plugin side */
    zconnect (zctx, &p->zs_req,       ZMQ_DEALER, ROUTER_URI,     -1,
              (char *)plugin->name);
    zconnect (zctx, &p->zs_in,        ZMQ_PULL, plout_uri,        -1, NULL);
    zconnect (zctx, &p->zs_out,       ZMQ_PUSH, PLIN_URI,         -1, NULL);
    zconnect (zctx, &p->zs_in_event,  ZMQ_SUB,  PLOUT_EVENT_URI,  -1, NULL);
    zconnect (zctx, &p->zs_out_event, ZMQ_PUSH, PLIN_EVENT_URI,   -1, NULL);
    zconnect (zctx, &p->zs_snoop,     ZMQ_SUB,  SNOOP_URI,        -1, NULL);

    errnum = pthread_create (&p->t, NULL, _plugin_thread, p);
    if (errnum)
        errn_exit (errnum, "pthread_create\n");

    zhash_insert (srv->plugins, plugin->name, p);
    zhash_freefn (srv->plugins, plugin->name, _plugin_destroy);
    if (plout_uri)
        free (plout_uri);

    return 0;
}

static int _send_match (const char *key, void *item, void *arg)
{
    plugin_ctx_t *p = item;
    zmsg_t **zmsg = arg;
    char *ptag = NULL;
    int rc = 0;

    if (!*zmsg)
        goto done;
    if (asprintf (&ptag, "%s.", p->plugin->name) < 0)
        err_exit ("asprintf");
    if (!cmb_msg_match_substr (*zmsg, ptag, NULL))
        goto done;
    if (cmb_msg_match_sender (*zmsg, p->plugin->name)
                                            && cmb_msg_hopcount (*zmsg) == 1)
        goto done; /* msg originating from this plugin, this broker */
    if (zmsg_send (zmsg, p->zs_plout) >= 0)
        rc = 1; /* makes zhash_foreach stop iterating and return 1 */
done:
    if (ptag)
        free (ptag);
    return rc;
}

/* Send messsage to first matching plugin */
void plugin_send (server_t *srv, conf_t *conf, zmsg_t **zmsg)
{
    zhash_foreach (srv->plugins, _send_match, zmsg);
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
