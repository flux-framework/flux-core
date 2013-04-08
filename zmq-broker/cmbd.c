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
#include <zmq.h>

#include "zmq.h"
#include "cmbd.h"
#include "cmb.h"
#include "apisrv.h"
#include "barriersrv.h"
#include "syncsrv.h"
#include "kvssrv.h"
#include "util.h"

typedef struct {
    void *zctx;
    void *zs_eventout;
    void *zs_eventin;
    void *zs_treeout;
    void *zs_treein;
    void *zs_plout;
    void *zs_plin;
    void *zs_plin_event;
    void *zs_plin_tree;
} server_t;

#define OPTIONS "t:T:e:vs:r:"
static const struct option longopts[] = {
    {"event-uri",   required_argument,  0, 'e'},
    {"tree-in-uri", required_argument,  0, 't'},
    {"tree-out-uri",required_argument,  0, 'T'},
    {"redis-server",required_argument,  0, 'r'},
    {"verbose",           no_argument,  0, 'v'},
    {"syncperiod",  required_argument,  0, 's'},
    {0, 0, 0, 0},
};

#define PLOUT_URI           "inproc://plout"
#define PLIN_URI            "inproc://plin"
#define PLIN_EVENT_URI      "inproc://plin_event"
#define PLIN_TREE_URI       "inproc://plin_tree"

static void _cmb_init (conf_t *conf, server_t **srvp);
static void _cmb_fini (conf_t *conf, server_t *srv);
static void _cmb_poll (conf_t *conf, server_t *srv);

static void usage (conf_t *conf)
{
    fprintf (stderr, 
"Usage: %s OPTIONS\n"
" -e,--event-uri URI     Set event URI (in/out epgm)\n"
" -t,--tree-in-uri URI   Set tree-in URI\n"
" -T,--tree-out-uri URI  Set tree-out URI\n"
" -r,--redis-server HOST Set redis server hostname\n"
" -v,--verbose           Show bus traffic\n"
" -s,--syncperiod N      Set sync period in seconds\n"
            ,conf->prog);
    exit (1);
}

int main (int argc, char *argv[])
{
    int c;
    conf_t *conf;
    server_t *srv;

    conf = xzmalloc (sizeof (conf_t));
    conf->prog = basename (argv[0]);
    conf->plout_uri = PLOUT_URI;
    conf->plin_uri = PLIN_URI;
    conf->plin_event_uri = PLIN_EVENT_URI;
    conf->plin_tree_uri = PLIN_TREE_URI;
    conf->syncperiod_msec = 10*1000;
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
            default:
                usage (conf);
        }
    }
    if (optind < argc)
        usage (conf);

    _cmb_init (conf, &srv);
    for (;;)
        _cmb_poll (conf, srv);
    _cmb_fini (conf, srv);

    free (conf);

    return 0;
}

static void _cmb_init (conf_t *conf, server_t **srvp)
{
    server_t *srv;

    srv = xzmalloc (sizeof (server_t));

    srv->zctx = _zmq_init (1);

    if (conf->event_uri) {
        srv->zs_eventout = _zmq_socket (srv->zctx, ZMQ_PUB);
        _zmq_bind (srv->zs_eventout, conf->event_uri);
        srv->zs_eventin = _zmq_socket (srv->zctx, ZMQ_SUB);
        _zmq_connect (srv->zs_eventin, conf->event_uri);
        _zmq_subscribe_all (srv->zs_eventin);
    }
    if (conf->treeout_uri) {
        srv->zs_treeout = _zmq_socket (srv->zctx, ZMQ_PUSH);
        _zmq_connect (srv->zs_treeout, conf->treeout_uri);
    }
    if (conf->treein_uri) {
        srv->zs_treein = _zmq_socket (srv->zctx, ZMQ_PULL);
        _zmq_bind (srv->zs_treein, conf->treein_uri);
    }
    srv->zs_plout = _zmq_socket (srv->zctx, ZMQ_PUB);
    _zmq_bind (srv->zs_plout, conf->plout_uri);
    srv->zs_plin = _zmq_socket (srv->zctx, ZMQ_PULL);
    _zmq_bind (srv->zs_plin, conf->plin_uri);
    srv->zs_plin_tree = _zmq_socket (srv->zctx, ZMQ_PULL);
    _zmq_bind (srv->zs_plin_tree, conf->plin_tree_uri);
    srv->zs_plin_event = _zmq_socket (srv->zctx, ZMQ_PULL);
    _zmq_bind (srv->zs_plin_event, conf->plin_event_uri);

    apisrv_init (conf, srv->zctx, CMB_API_PATH);
    barriersrv_init (conf, srv->zctx);
    if (!conf->treeout_uri) /* root (send on local bus even if no eventout) */
        syncsrv_init (conf, srv->zctx);
    if (conf->redis_server)
        kvssrv_init (conf, srv->zctx);

    *srvp = srv;
}

static void _cmb_fini (conf_t *conf, server_t *srv)
{
    cmb_msg_send (srv->zs_plout, NULL, NULL, 0, "event.cmb.shutdown");
   
    if (conf->redis_server)
        kvssrv_fini ();
    if (!conf->treeout_uri) /* root */
        syncsrv_fini (); 
    barriersrv_fini ();
    apisrv_fini ();

    _zmq_close (srv->zs_plin_event);
    _zmq_close (srv->zs_plin_tree);
    _zmq_close (srv->zs_plin);
    _zmq_close (srv->zs_plout);
    if (srv->zs_treein)
        _zmq_close (srv->zs_treein);
    if (srv->zs_treeout)
        _zmq_close (srv->zs_treeout);
    if (srv->zs_eventin)
        _zmq_close (srv->zs_eventin);
    if (srv->zs_eventout)
        _zmq_close (srv->zs_eventout);

    _zmq_term (srv->zctx);

    free (srv);
}

static void _route_two (conf_t *conf, void *src, void *d1, void *d2, char *s)
{
    zmq_mpart_t msg, cpy;

    _zmq_mpart_init (&msg);
    _zmq_mpart_recv (src, &msg, 0);
    if (conf->verbose)
        cmb_msg_dump (s, &msg);
    _zmq_mpart_dup (&cpy, &msg);
    if (d2)
        _zmq_mpart_send (d2, &cpy, 0);
    else
        _zmq_mpart_close (&cpy);
    if (d1)
        _zmq_mpart_send (d1, &msg, 0);
    else
        _zmq_mpart_close (&msg);
}

static void _route_one (conf_t *conf, void *src, void *dest, char *s)
{
    zmq_mpart_t msg;

    _zmq_mpart_init (&msg);
    _zmq_mpart_recv (src, &msg, 0);
    if (conf->verbose)
        cmb_msg_dump (s, &msg);
    if (dest)
        _zmq_mpart_send (dest, &msg, 0);
    else
        _zmq_mpart_close (&msg);
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

    if ((zmq_poll(zpa, 5, -1)) < 0) {
        fprintf (stderr, "zmq_poll: %s\n", strerror (errno));
        exit (1);
    }
    if (zpa[0].revents & ZMQ_POLLIN)
        _route_one (conf, srv->zs_treein, srv->zs_plout, "treein->plout");
    if (zpa[1].revents & ZMQ_POLLIN)
        _route_one (conf, srv->zs_eventin, srv->zs_plout, "eventin->plout");
    if (zpa[2].revents & ZMQ_POLLIN)
        _route_one (conf, srv->zs_plin, srv->zs_plout, "plin->plout");
    if (zpa[3].revents & ZMQ_POLLIN)
        _route_two (conf, srv->zs_plin_event, srv->zs_eventout, srv->zs_plout,
                    "plin_event->eventout,plout");
    if (zpa[4].revents & ZMQ_POLLIN)
        _route_one (conf, srv->zs_plin_tree, srv->zs_treeout,
                    "plin_tree->treeout");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
