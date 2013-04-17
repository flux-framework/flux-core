/* plugin.c - broker plugin interface */

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

static void _plugin_poll (plugin_ctx_t *p)
{
    zmq_pollitem_t zpa[] = {
{ .socket = p->zs_in,        .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = p->zs_in_event,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    zmsg_t *zmsg;
    struct timeval t1, t2, t;
    long elapsed, msec = -1;

    for (;;) {
        if (p->timeout > 0) {
            if (msec == -1) { 
                msec = p->timeout;
                xgettimeofday (&t1, NULL);
            }
        } else
            msec = -1;

        zpoll(zpa, 2, msec);

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

        if (zpa[0].revents & ZMQ_POLLIN) {
            zmsg = zmsg_recv (p->zs_in);
            if (!zmsg)
                err ("zmsg_recv");
        } else if (zpa[1].revents & ZMQ_POLLIN) {
            zmsg = zmsg_recv (p->zs_in_event);
            if (!zmsg)
                err ("zmsg_recv");
        } else
            zmsg = NULL;

        if (zmsg) {
            if (p->plugin->recvFn)
                p->plugin->recvFn (p, zmsg);
            else
                zmsg_destroy (&zmsg);
        }
    }
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

static void _plugin_create (server_t *srv, conf_t *conf, plugin_t plugin)
{
    zctx_t *zctx = srv->zctx;
    plugin_ctx_t *p;
    int errnum;

    p = xzmalloc (sizeof (plugin_ctx_t));
    p->conf = conf;
    p->plugin = plugin;
    p->timeout = -1;

    zconnect (zctx, &p->zs_in,        ZMQ_SUB,  PLOUT_URI,       10000);
    zconnect (zctx, &p->zs_in_event,  ZMQ_SUB,  PLOUT_EVENT_URI, 1000);
    zconnect (zctx, &p->zs_out,       ZMQ_PUSH, PLIN_URI,        1000);
    zconnect (zctx, &p->zs_out_event, ZMQ_PUSH, PLIN_EVENT_URI,  1000);
    if (conf->treeout_uri)
        zconnect (zctx, &p->zs_out_tree, ZMQ_PUSH, PLIN_TREE_URI, 1000);

    errnum = pthread_create (&p->t, NULL, _plugin_thread, p);
    if (errnum)
        errn_exit (errnum, "pthread_create\n");

    zlist_append (srv->plugins, p);
}

static void _plugin_destroy (server_t *srv, plugin_ctx_t *p)
{
    int errnum;

    /* FIXME: no mechanism to tell thread to exit yet */
    errnum = pthread_join (p->t, NULL);
    if (errnum)
        errn_exit (errnum, "pthread_join\n");

    zsocket_destroy (srv->zctx, p->zs_out_tree);
    zsocket_destroy (srv->zctx, p->zs_out_event);
    zsocket_destroy (srv->zctx, p->zs_in_event);
    zsocket_destroy (srv->zctx, p->zs_in);
    zsocket_destroy (srv->zctx, p->zs_out);

    zlist_remove (srv->plugins, p);

    free (p);
}

void plugin_init (conf_t *conf, server_t *srv)
{
    srv->plugins = zlist_new ();
    _plugin_create (srv, conf, &apisrv);
    _plugin_create (srv, conf, &barriersrv);
    if (!conf->treeout_uri) /* root (send on local bus even if no eventout) */
        _plugin_create (srv, conf, &syncsrv);
    if (conf->redis_server)
        _plugin_create (srv, conf, &kvssrv);
    _plugin_create (srv, conf, &livesrv);
}

void plugin_fini (conf_t *conf, server_t *srv)
{
    plugin_ctx_t *p;

    while ((p = zlist_head (srv->plugins)))
        _plugin_destroy (srv, p);
    zlist_destroy (&srv->plugins); 
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
