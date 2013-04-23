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


static void _ping_respond (plugin_ctx_t *p, zmsg_t **zmsg)
{
    if (zmsg_send (zmsg, p->zs_out) < 0)
        err ("%s: zmsg_send", __FUNCTION__);
}

static void _plugin_poll (plugin_ctx_t *p)
{
    zmq_pollitem_t zpa[] = {
{ .socket = p->zs_in,        .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = p->zs_in_event,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = p->zs_req,       .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    zmsg_t *zmsg;
    struct timeval t1, t2, t;
    long elapsed, msec = -1;
    char *pingtag;
    msg_type_t type;

    if (asprintf (&pingtag, "%s.ping", p->plugin->name) < 0)
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
        if (zpa[0].revents & ZMQ_POLLIN) {
            zmsg = zmsg_recv (p->zs_in);
            if (!zmsg)
                err ("zmsg_recv");
            type = MSG_REQUEST;
        } else if (zpa[1].revents & ZMQ_POLLIN) {
            zmsg = zmsg_recv (p->zs_in_event);
            if (!zmsg)
                err ("zmsg_recv");
            type = MSG_EVENT;
        } else if (zpa[2].revents & ZMQ_POLLIN) {
            zmsg = zmsg_recv (p->zs_req);
            type = MSG_RESPONSE;
        } else
            zmsg = NULL;

        /* intercept and respond to ping requests for this plugin */
        if (zmsg && type == MSG_REQUEST && cmb_msg_match (zmsg, pingtag))
            _ping_respond (p, &zmsg);

        /* dispatch message to plugin's recvFn() */
        /*     recvFn () shouldn't free zmsg if it doesn't recognize the tag */
        if (zmsg && p->plugin->recvFn)
            p->plugin->recvFn (p, &zmsg, type);

        /* send a NAK response indicating plugin did not recognize tag */
        if (zmsg)
            cmb_msg_sendnak (&zmsg, p->zs_out);

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
    zsocket_destroy (p->srv->zctx, p->zs_out_event);
    zsocket_destroy (p->srv->zctx, p->zs_in_event);
    zsocket_destroy (p->srv->zctx, p->zs_out);
    zsocket_destroy (p->srv->zctx, p->zs_in);
    zsocket_destroy (p->srv->zctx, p->zs_req);

    /* server side */
    zsocket_destroy (p->srv->zctx, p->zs_plout);

    free (p);
}

static void _plugin_create (server_t *srv, conf_t *conf, plugin_t plugin)
{
    zctx_t *zctx = srv->zctx;
    plugin_ctx_t *p;
    int errnum;
    char *plout_uri = NULL;

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
    zconnect (zctx, &p->zs_req,       ZMQ_DEALER, ROUTER_URI, -1,
              (char *)plugin->name);
    zconnect (zctx, &p->zs_in,        ZMQ_PULL, plout_uri,        -1, NULL);
    zconnect (zctx, &p->zs_out,       ZMQ_PUSH, PLIN_URI,         -1, NULL);
    zconnect (zctx, &p->zs_in_event,  ZMQ_SUB,  PLOUT_EVENT_URI,  -1, NULL);
    zconnect (zctx, &p->zs_out_event, ZMQ_PUSH, PLIN_EVENT_URI,   -1, NULL);

    errnum = pthread_create (&p->t, NULL, _plugin_thread, p);
    if (errnum)
        errn_exit (errnum, "pthread_create\n");

    zhash_insert (srv->plugins, plugin->name, p);
    zhash_freefn (srv->plugins, plugin->name, _plugin_destroy);
    if (plout_uri)
        free (plout_uri);
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
                                            && cmb_hopcount (*zmsg) == 1)
        goto done;
    if (p->conf->verbose) {
        zmsg_dump (*zmsg);
        msg ("router->plout[%s]", p->plugin->name);
    }
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

    _plugin_create (srv, conf, &apisrv);
    if (!conf->treeout_uri) /* root (send on local bus even if no eventout) */
        _plugin_create (srv, conf, &syncsrv);
    if (conf->redis_server)
        _plugin_create (srv, conf, &kvssrv);
    _plugin_create (srv, conf, &barriersrv);
    //_plugin_create (srv, conf, &livesrv);
}

void plugin_fini (conf_t *conf, server_t *srv)
{
    zhash_destroy (&srv->plugins);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
