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

#include <json/json.h>
#include <zmq.h>

#include "zmq.h"
#include "cmbd.h"
#include "cmb.h"
#include "apisrv.h"
#include "barriersrv.h"
#include "syncsrv.h"
#include "kvssrv.h"

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

#define OPTIONS "rlvs:"
static const struct option longopts[] = {
    {"root-server",       no_argument,  0, 'r'},
    {"leaf-server",       no_argument,  0, 'l'},
    {"verbose",           no_argument,  0, 'v'},
    {"syncperiod",  required_argument,  0, 's'},
    {0, 0, 0, 0},
};

#define EVENTOUT_URI        "epgm://%s;239.192.1.1:5555"
#define EVENTIN_URI         "epgm://%s;239.192.1.1:5555"

#define TREEIN_URI          "tcp://*:5556"
#define TREEOUT_URI         "tcp://%s:5556"

#define PLOUT_URI           "inproc://plout"
#define PLIN_URI            "inproc://plin"
#define PLIN_EVENT_URI      "inproc://plin_event"
#define PLIN_TREE_URI       "inproc://plin_tree"

static void _pl_shutdown (conf_t *conf, server_t *srv);
static void _cmb_init (conf_t *conf, server_t **srvp);
static void _cmb_fini (conf_t *conf, server_t *srv);
static void _cmb_poll (conf_t *conf, server_t *srv);

static void _oom (void)
{
    fprintf (stderr, "out of memory\n");
    exit (1);
}

static void *_zmalloc (size_t size)
{
    void *new;

    new = malloc (size);
    if (!new)
        _oom ();
    memset (new, 0, size);
    return new;
}

static char *_strdup (char *s)
{
    char *cpy = strdup (s);
    if (!cpy)
        _oom ();
    return cpy;
}

static char *_msg2str (zmq_msg_t *msg)
{
    int len = zmq_msg_size (msg);
    char *s = _zmalloc (len + 1);

    memcpy (s, zmq_msg_data (msg), len);
    s[len] = '\0';

    return s;
}

static int _env_getint (char *name, int dflt)
{
    char *ev = getenv (name);
    return ev ? strtoul (ev, NULL, 10) : dflt;
}

static char *_env_getstr (char *name, char *dflt)
{
    char *ev = getenv (name);
    return ev ? _strdup (ev) : _strdup (dflt);
}

static void usage (conf_t *conf)
{
    fprintf (stderr, 
"Usage: %s OPTIONS\n"
" -r,--root-server    I am the root node\n"
" -l,--leaf-server    I am a leaf node\n"
" -v,--verbose        Show bus traffic\n"
" -s,--syncperiod N   Set sync period in seconds\n"
            ,conf->prog);
    exit (1);
}

int main (int argc, char *argv[])
{
    int c;
    conf_t *conf;
    server_t *srv;
    char *local_eth0_address;

    conf = _zmalloc (sizeof (conf_t));
    conf->prog = basename (argv[0]);
    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'r':   /* --root-server */
                conf->root_server = true;
                break;
            case 'l':   /* --leaf-server */
                conf->leaf_server = true;
                break;
            case 'v':   /* --verbose */
                conf->verbose = true;
                break;
            case 's':   /* --syncperiod sec */
                conf->syncperiod_msec = strtoul (optarg, NULL, 10) * 1000;
                break;
            default:
                usage (conf);
        }
    }
    if (optind < argc)
        usage (conf);

    conf->nnodes   = _env_getint ("SLURM_NNODES", 1);
    conf->rootnode = _env_getstr ("SLURM_LAUNCH_NODE_IPADDR", "127.0.0.1");

    /* FIXME - some zmq libraries assert on failure here, others just don't
     * pass messages, silently.
     */
    local_eth0_address = "eth0";

    snprintf (conf->eventout_uri, sizeof (conf->eventout_uri), EVENTOUT_URI,
	      local_eth0_address);
    snprintf (conf->eventin_uri, sizeof (conf->eventin_uri), EVENTIN_URI,
	      local_eth0_address);
    snprintf (conf->treeout_uri, sizeof (conf->treeout_uri), TREEOUT_URI,
              conf->rootnode);
    snprintf (conf->treein_uri, sizeof (conf->treein_uri),TREEIN_URI); 
    snprintf (conf->plout_uri, sizeof (conf->plout_uri), PLOUT_URI); 
    snprintf (conf->plin_uri, sizeof (conf->plin_uri), PLIN_URI); 
    snprintf (conf->plin_event_uri, sizeof (conf->plin_event_uri),
              PLIN_EVENT_URI); 
    snprintf (conf->plin_tree_uri, sizeof (conf->plin_tree_uri),
              PLIN_TREE_URI);

    if (!conf->leaf_server && conf->nnodes == 1)
        conf->root_server = true;

    if (conf->root_server && conf->syncperiod_msec == 0)
        conf->syncperiod_msec = 10*1000;

    if (conf->verbose) {
        if (conf->root_server)
            fprintf (stderr, "cmbd: root (%d nodes)\n", conf->nnodes);
        else
            fprintf (stderr, "cmbd: leaf (%d nodes)\n", conf->nnodes);
    }

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

    srv = _zmalloc (sizeof (server_t));

    srv->zctx = _zmq_init (1);

    srv->zs_eventout = _zmq_socket (srv->zctx, ZMQ_PUB);
    _zmq_bind (srv->zs_eventout, conf->eventout_uri);

    srv->zs_eventin = _zmq_socket (srv->zctx, ZMQ_SUB);
    //_zmq_connect (srv->zs_eventin, conf->eventin_uri);
    //_zmq_subscribe_all (srv->zs_eventin);

    srv->zs_treeout = _zmq_socket (srv->zctx, ZMQ_PUSH);
    if (!conf->root_server)
        _zmq_connect (srv->zs_treeout, conf->treeout_uri);

    srv->zs_treein = _zmq_socket (srv->zctx, ZMQ_PULL);
    if (conf->root_server)
        _zmq_bind (srv->zs_treein, conf->treein_uri);

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
    if (conf->root_server)
        syncsrv_init (conf, srv->zctx);
    kvssrv_init (conf, srv->zctx);

    *srvp = srv;
}

static void _cmb_fini (conf_t *conf, server_t *srv)
{
    _pl_shutdown (conf, srv);
   
    kvssrv_fini ();
    if (conf->root_server)
        syncsrv_fini (); 
    barriersrv_fini ();
    apisrv_fini ();

    _zmq_close (srv->zs_plin_event);
    _zmq_close (srv->zs_plin_tree);
    _zmq_close (srv->zs_plin);
    _zmq_close (srv->zs_plout);
    _zmq_close (srv->zs_treein);
    _zmq_close (srv->zs_treeout);
    _zmq_close (srv->zs_eventin);
    _zmq_close (srv->zs_eventout);

    _zmq_term (srv->zctx);

    free (srv);
}

static void _pl_shutdown (conf_t *conf, server_t *srv)
{
    zmq_2part_t msg;

    _zmq_2part_init_empty (&msg, "event.cmb.shutdown");
    _zmq_2part_send (srv->zs_plout, &msg, 0);
}

static void _dumpmsg (char *s, zmq_2part_t msg)
{
    int bodylen = (int)zmq_msg_size (&msg.body);
    char *tag = _msg2str (&msg.tag);

    fprintf (stderr, "%s: %s %d bytes\n", s, tag, bodylen);
    free (tag);
    if (bodylen > 0) {
        char *body = _msg2str (&msg.body);
        fprintf (stderr, "  %s\n", body);
        free (body);
    }
}

static void _route_two (conf_t *conf, void *src, void *d1, void *d2, char *s)
{
    zmq_2part_t msg, cpy;

    _zmq_2part_init (&msg);
    _zmq_2part_recv (src, &msg, 0);
    if (conf->verbose)
        _dumpmsg (s, msg);
    _zmq_2part_dup (&cpy, &msg);
    _zmq_2part_send (d2, &cpy, 0);
    _zmq_2part_send (d1, &msg, 0);
}

static void _route_one (conf_t *conf, void *src, void *dest, char *s)
{
    zmq_2part_t msg;

    _zmq_2part_init (&msg);
    _zmq_2part_recv (src, &msg, 0);
    if (conf->verbose)
        _dumpmsg (s, msg);
    _zmq_2part_send (dest, &msg, 0);
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
