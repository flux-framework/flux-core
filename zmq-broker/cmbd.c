/* cmbd.c - simple zmq message broker, to run on each node of a job */

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

typedef struct {
    zctx_t *zctx;
    void *zs_eventout;
    void *zs_eventin;
    void *zs_treeout;
    void *zs_treein;
    void *zs_plout;
    void *zs_plout_event;
    void *zs_plin;
    void *zs_plin_event;
    void *zs_plin_tree;
    zlist_t *plugins;
} server_t;

#define OPTIONS "t:T:e:vs:r:R:S:"
static const struct option longopts[] = {
    {"event-uri",   required_argument,  0, 'e'},
    {"tree-in-uri", required_argument,  0, 't'},
    {"tree-out-uri",required_argument,  0, 'T'},
    {"redis-server",required_argument,  0, 'r'},
    {"verbose",           no_argument,  0, 'v'},
    {"syncperiod",  required_argument,  0, 's'},
    {"rank",        required_argument,  0, 'R'},
    {"size",        required_argument,  0, 'S'},
    {0, 0, 0, 0},
};

#define PLOUT_URI           "inproc://plout"
#define PLOUT_EVENT_URI     "inproc://plout_event"
#define PLIN_URI            "inproc://plin"
#define PLIN_EVENT_URI      "inproc://plin_event"
#define PLIN_TREE_URI       "inproc://plin_tree"

static void _cmb_init (conf_t *conf, server_t **srvp);
static void _cmb_fini (conf_t *conf, server_t *srv);
static void _cmb_poll (conf_t *conf, server_t *srv);

static void usage (void)
{
    fprintf (stderr, 
"Usage: cmbd OPTIONS\n"
" -e,--event-uri URI     Set event URI e.g. epgm://eth0;239.192.1.1:5555\n"
" -t,--tree-in-uri URI   Set tree-in URI, e.g. tcp://*:5556\n"
" -T,--tree-out-uri URI  Set tree-out URI, e.g. tcp://hostname:5556\n"
" -r,--redis-server HOST Set redis server hostname\n"
" -v,--verbose           Show bus traffic\n"
" -s,--syncperiod N      Set sync period in seconds\n"
" -R,--rank N            Set cmbd address\n"
" -S,--size N            Set number of ranks in session\n"
            );
    exit (1);
}

int main (int argc, char *argv[])
{
    int c;
    conf_t *conf;
    server_t *srv;

    log_init (basename (argv[0]));

    conf = xzmalloc (sizeof (conf_t));
    conf->plout_uri = PLOUT_URI;
    conf->plout_event_uri = PLOUT_EVENT_URI;
    conf->plin_uri = PLIN_URI;
    conf->plin_event_uri = PLIN_EVENT_URI;
    conf->plin_tree_uri = PLIN_TREE_URI;
    conf->syncperiod_msec = 10*1000;
    conf->size = 1;
    conf->apisockpath = CMB_API_PATH;
    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'e':   /* --event-uri URI */
                conf->event_uri = optarg;
                break;
            case 't':   /* --tree-in-uri URI */
                conf->treein_uri = optarg;
                break;
            case 'T':   /* --tree-out-uri URI */
                conf->treeout_uri = optarg;
                break;
            case 'v':   /* --verbose */
                conf->verbose = true;
                break;
            case 's':   /* --syncperiod sec */
                conf->syncperiod_msec = strtoul (optarg, NULL, 10) * 1000;
                break;
            case 'r':   /* --redis-server hostname */
                conf->redis_server = optarg;
                break;
            case 'R':   /* --rank N */
                conf->rank = strtoul (optarg, NULL, 10);
                break;
            case 'S':   /* --size N */
                conf->size = strtoul (optarg, NULL, 10);
                break;
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();

    _cmb_init (conf, &srv);
    for (;;)
        _cmb_poll (conf, srv);
    _cmb_fini (conf, srv);

    free (conf);

    return 0;
}

static void _plugin_create (server_t *srv, conf_t *conf, plugin_poll_t pf)
{
    plugin_t *p;
    int errnum;

    p = xzmalloc (sizeof (plugin_t));
    p->conf = conf;

    p->zs_out = zsocket_new (srv->zctx, ZMQ_PUSH);
    if (zsocket_connect (p->zs_out, "%s", conf->plin_uri) < 0)
        err_exit ("zsocket_connect: %s", conf->plin_uri);

    p->zs_in = zsocket_new (srv->zctx, ZMQ_SUB);
    zsocket_set_hwm (p->zs_in, 10000); /* default is 1000 */
    if (zsocket_connect (p->zs_in, "%s", conf->plout_uri) < 0)
        err_exit ("zsocket_connect: %s", conf->plout_uri);

    p->zs_in_event = zsocket_new (srv->zctx, ZMQ_SUB);
    if (zsocket_connect (p->zs_in_event, "%s", conf->plout_event_uri) < 0)
        err_exit ("zsocket_connect: %s", conf->plout_event_uri);
        
    p->poll_fun = pf;
    errnum = pthread_create (&p->poll_thd, NULL, p->poll_fun, p);
    if (errnum)
        errn_exit (errnum, "pthread_create\n");

    zlist_append (srv->plugins, p);
}

static void _plugin_destroy (server_t *srv, plugin_t *p)
{
    int errnum;

    errnum = pthread_join (p->poll_thd, NULL);
    if (errnum)
        errn_exit (errnum, "pthread_join\n");

    zsocket_destroy (srv->zctx, p->zs_in_event);
    zsocket_destroy (srv->zctx, p->zs_in);
    zsocket_destroy (srv->zctx, p->zs_out);

    zlist_remove (srv->plugins, p);

    free (p);
}


static void _cmb_init (conf_t *conf, server_t **srvp)
{
    server_t *srv;

    srv = xzmalloc (sizeof (server_t));
    srv->zctx = zctx_new ();
    if (!srv->zctx)
        err_exit ("zctx_new");
    zctx_set_linger (srv->zctx, 5);
    
    if (conf->event_uri) {
        srv->zs_eventout = zsocket_new(srv->zctx, ZMQ_PUB);
        if (zsocket_bind (srv->zs_eventout, "%s", conf->event_uri) < 0)
            err_exit ("zsocket_bind: %s", conf->event_uri);

        srv->zs_eventin = zsocket_new (srv->zctx, ZMQ_SUB);
        if (zsocket_connect (srv->zs_eventin, conf->event_uri) < 0)
            err_exit ("zsocket_connect: %s", conf->event_uri);
        zsocket_set_subscribe (srv->zs_eventin, "");
    }
    if (conf->treeout_uri) {
        srv->zs_treeout = zsocket_new (srv->zctx, ZMQ_PUSH);
        if (zsocket_connect (srv->zs_treeout, "%s", conf->treeout_uri) < 0)
            err_exit ("zsocket_connect: %s", conf->treeout_uri);
    }
    if (conf->treein_uri) {
        srv->zs_treein = zsocket_new (srv->zctx, ZMQ_PULL);
        if (zsocket_bind (srv->zs_treein, "%s", conf->treein_uri) < 0)
            err_exit ("zsocket_bind: %s", conf->treein_uri);
    }
    srv->zs_plout = zsocket_new (srv->zctx, ZMQ_PUB);
    zsocket_set_hwm (srv->zs_plout, 10000); /* default is 1000 */
    if (zsocket_bind (srv->zs_plout, "%s", conf->plout_uri) < 0)
        err_exit ("zsocket_bind: %s", conf->plout_uri);

    srv->zs_plout_event = zsocket_new (srv->zctx, ZMQ_PUB);
    if (zsocket_bind (srv->zs_plout_event, "%s", conf->plout_event_uri) < 0)
        err_exit ("zsocket_bind: %s", conf->plout_event_uri);

    srv->zs_plin = zsocket_new (srv->zctx, ZMQ_PULL);
    if (zsocket_bind (srv->zs_plin, "%s", conf->plin_uri) < 0)
        err_exit ("zsocket_bind: %s", conf->plin_uri);

    srv->zs_plin_tree = zsocket_new (srv->zctx, ZMQ_PULL);
    if (zsocket_bind (srv->zs_plin_tree, "%s", conf->plin_tree_uri) < 0)
        err_exit ("zsocket_bind: %s", conf->plin_tree_uri);

    srv->zs_plin_event = zsocket_new (srv->zctx, ZMQ_PULL);
    if (zsocket_bind (srv->zs_plin_event, "%s", conf->plin_event_uri) < 0)
        err_exit ("zsocket_bind: %s", conf->plin_event_uri);

    /* Initialize plugins
     */
    srv->plugins = zlist_new ();
    _plugin_create (srv, conf, apisrv_poll);

#if 0
    apisrv_init (conf, srv->zctx);
    barriersrv_init (conf, srv->zctx);
    if (!conf->treeout_uri) /* root (send on local bus even if no eventout) */
        syncsrv_init (conf, srv->zctx);
    if (conf->redis_server)
        kvssrv_init (conf, srv->zctx);
    livesrv_init (conf, srv->zctx);
#endif
    *srvp = srv;
}

static void _cmb_fini (conf_t *conf, server_t *srv)
{
    plugin_t *p;
#if 0
    apisrv_fini ();
    livesrv_fini ();   
    if (conf->redis_server)
        kvssrv_fini ();
    if (!conf->treeout_uri) /* root */
        syncsrv_fini (); 
    barriersrv_fini ();
#endif
    while ((p = zlist_head (srv->plugins)))
        _plugin_destroy (srv, p);
    zlist_destroy (&srv->plugins); 

    zsocket_destroy (srv->zctx, srv->zs_plin_event);
    zsocket_destroy (srv->zctx, srv->zs_plin_tree);
    zsocket_destroy (srv->zctx, srv->zs_plin);
    zsocket_destroy (srv->zctx, srv->zs_plout);
    zsocket_destroy (srv->zctx, srv->zs_plout_event);
    if (srv->zs_treein)
        zsocket_destroy (srv->zctx, srv->zs_treein);
    if (srv->zs_treeout)
        zsocket_destroy (srv->zctx, srv->zs_treeout);
    if (srv->zs_eventin)
        zsocket_destroy (srv->zctx, srv->zs_eventin);
    if (srv->zs_eventout)
        zsocket_destroy (srv->zctx, srv->zs_eventout);

    zctx_destroy (&srv->zctx);

    free (srv);
}

static void _route (conf_t *conf, void *src, void *d1, void *d2, char *s)
{
    zmsg_t *msg, *cpy;

    msg = zmsg_recv (src);
    if (!msg)
        return;

    if (conf->verbose)
        zmsg_dump (msg);
    if (d2) {
        cpy = zmsg_dup (msg);
        if (!cpy)
            err_exit ("zmsg_dup");
        if (zmsg_send (&cpy, d2) < 0)
            err_exit ("zmsg_send");
    }
    if (d1) {
        if (zmsg_send (&msg, d1) < 0)
            err_exit ("zmsg_send");
    } else {
        zmsg_destroy (&msg);
    }
}

static void _cmb_poll (conf_t *conf, server_t *srv)
{
    zmq_pollitem_t zpa[] = {
{ .socket = srv->zs_treein,     .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_eventin,    .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_plin,       .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_plin_event, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_plin_tree,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 }
    };

    _zmq_poll(zpa, 5, -1);

    if (zpa[0].revents & ZMQ_POLLIN)
        _route (conf, srv->zs_treein, srv->zs_plout, NULL, "treein->plout");
    if (zpa[1].revents & ZMQ_POLLIN)
        _route (conf, srv->zs_eventin, srv->zs_plout_event, NULL, "eventin->plout_event");
    if (zpa[2].revents & ZMQ_POLLIN)
        _route (conf, srv->zs_plin, srv->zs_plout, NULL, "plin->plout");
    if (zpa[3].revents & ZMQ_POLLIN)
        _route (conf, srv->zs_plin_event, srv->zs_eventout, srv->zs_plout_event, "plin_event->(eventout,plout_event)");
    if (zpa[4].revents & ZMQ_POLLIN)
        _route (conf, srv->zs_plin_tree, srv->zs_treeout, NULL, "plin_tree->treeout");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
