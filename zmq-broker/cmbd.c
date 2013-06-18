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
#include "util.h"
#include "plugin.h"

#define OPTIONS "t:e:E:O:vs:r:R:S:p:c:P:L:"
static const struct option longopts[] = {
    {"event-uri",   required_argument,  0, 'e'},
    {"event-out-uri",required_argument, 0, 'O'},
    {"event-in-uri",required_argument,  0, 'E'},
    {"tree-in-uri", required_argument,  0, 't'},
    {"redis-server",required_argument,  0, 'r'},
    {"verbose",           no_argument,  0, 'v'},
    {"syncperiod",  required_argument,  0, 's'},
    {"rank",        required_argument,  0, 'R'},
    {"size",        required_argument,  0, 'S'},
    {"parent",      required_argument,  0, 'p'},
    {"children",    required_argument,  0, 'c'},
    {"plugins",     required_argument,  0, 'P'},
    {"logdest",     required_argument,  0, 'L'},
    {0, 0, 0, 0},
};

static void _cmb_init (conf_t *conf, server_t **srvp);
static void _cmb_fini (conf_t *conf, server_t *srv);
static void _cmb_poll (conf_t *conf, server_t *srv);

static void usage (void)
{
    fprintf (stderr, 
"Usage: cmbd OPTIONS\n"
" -e,--event-uri URI     Set event URI e.g. epgm://eth0;239.192.1.1:5555\n"
" -O,--event-out-uri URI Set event out URI (alternative to -e)\n"
" -E,--event-in-uri URI  Set event out URI (alternative to -e)\n"
" -t,--tree-in-uri URI   Set tree-in URI, e.g. tcp://*:5556\n"
" -p,--parent N,URI      Set parent rank,URI, e.g. 0,tcp://192.168.1.136:5556\n"
" -c,--children n,n,...  Set ranks of children, comma-sep\n"
" -r,--redis-server HOST Set redis server hostname\n"
" -v,--verbose           Show bus traffic\n"
" -s,--syncperiod N      Set sync period in seconds\n"
" -R,--rank N            Set cmbd address\n"
" -S,--size N            Set number of ranks in session\n"
" -P,--plugins p1,p2,... Load the named plugins (comma separated)\n"
" -L,--logdest DEST      Log to DEST, can  be syslog, stderr, or file\n"
            );
    exit (1);
}

int main (int argc, char *argv[])
{
    int c;
    conf_t *conf;
    server_t *srv;
    const int parent_max = sizeof (conf->parent) / sizeof (conf->parent[0]);

    log_init (basename (argv[0]));

    conf = xzmalloc (sizeof (conf_t));
    conf->syncperiod_msec = 2*1000;
    conf->size = 1;
    conf->apisockpath = CMB_API_PATH;
    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'e':   /* --event-uri URI */
                conf->event_in_uri = optarg;
                conf->event_out_uri = optarg;
                break;
            case 'E':   /* --event-in-uri URI */
                conf->event_in_uri = optarg;
                break;
            case 'O':   /* --event-out-uri URI */
                conf->event_out_uri = optarg;
                break;
            case 't':   /* --tree-in-uri URI */
                conf->treein_uri = optarg;
                break;
            case 'p': { /* --parent rank,URI */
                char *p;
                if (conf->parent_len == parent_max)
                    msg_exit ("too many --parent's, max %d", parent_max);
                if (!(p = strchr (optarg, ',')))
                    msg_exit ("malformed -p option");
                conf->parent[conf->parent_len].rank = strtoul (optarg, NULL,10);
                conf->parent[conf->parent_len].treeout_uri = p + 1;
                conf->parent_len++;
                break;
            }
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
            case 'c':   /* --children n,n,... */
                if (getints (optarg, &conf->children, &conf->children_len) < 0)
                    msg_exit ("out of memory");
                break;
            case 'P':   /* --plugins p1,p2,... */
                conf->plugins = optarg;
                break;
            case 'L':   /* --logdest DEST */
                log_set_dest (optarg);
                break;
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();
    if (!conf->plugins)
        msg_exit ("at least one plugin must be loaded");

    _cmb_init (conf, &srv);
    for (;;)
        _cmb_poll (conf, srv);
    _cmb_fini (conf, srv);

    if (conf->children)
        free (conf->children);
    free (conf);

    return 0;
}

static void _cmb_init (conf_t *conf, server_t **srvp)
{
    server_t *srv;
    zctx_t *zctx;

    srv = xzmalloc (sizeof (server_t));
    srv->zctx = zctx = zctx_new ();
    if (!srv->zctx)
        err_exit ("zctx_new");
    zctx_set_linger (srv->zctx, 5);

    /* broker side of plugin sockets */
    /* N.B. each plugin has a private zs_plout */
    zbind (zctx, &srv->zs_router,      ZMQ_ROUTER, ROUTER_URI, -1);
    zbind (zctx, &srv->zs_plin,        ZMQ_PULL, PLIN_URI,        -1);
    zbind (zctx, &srv->zs_plout_event, ZMQ_PUB,  PLOUT_EVENT_URI, -1);
    zbind (zctx, &srv->zs_plin_event,  ZMQ_PULL, PLIN_EVENT_URI,  -1);
    zbind (zctx, &srv->zs_snoop,       ZMQ_PUB,  SNOOP_URI, -1);

    /* external sockets */
    if (conf->event_in_uri) {
        zbind (zctx, &srv->zs_eventin,  ZMQ_SUB, conf->event_in_uri, -1);
        zsocket_set_subscribe (srv->zs_eventin, "");
    }
    if (conf->event_out_uri)
        zbind (zctx, &srv->zs_eventout, ZMQ_PUB, conf->event_out_uri, -1);
    if (conf->parent_len > 0) {
        char id[16];
        snprintf (id, sizeof (id), "%d", conf->rank);
        zconnect (zctx, &srv->zs_upreq, ZMQ_DEALER,
                  conf->parent[srv->parent_cur].treeout_uri, -1, id);
    }
    if (conf->treein_uri) {
        if (zsocket_bind (srv->zs_router, "%s", conf->treein_uri) < 0)
            err_exit ("zsocket_bind: %s", conf->treein_uri);
    }

    if (!(srv->route = zhash_new ()))
        oom ();

    plugin_init (conf, srv);

    *srvp = srv;
}

static void _cmb_fini (conf_t *conf, server_t *srv)
{
    plugin_fini (conf, srv);

    zhash_destroy (&srv->route);

    zsocket_destroy (srv->zctx, srv->zs_router);
    zsocket_destroy (srv->zctx, srv->zs_plin);
    zsocket_destroy (srv->zctx, srv->zs_plout_event);
    zsocket_destroy (srv->zctx, srv->zs_plin_event);
    zsocket_destroy (srv->zctx, srv->zs_snoop);

    if (srv->zs_upreq)
        zsocket_destroy (srv->zctx, srv->zs_upreq);
    if (srv->zs_eventin)
        zsocket_destroy (srv->zctx, srv->zs_eventin);
    if (srv->zs_eventout)
        zsocket_destroy (srv->zctx, srv->zs_eventout);

    zctx_destroy (&srv->zctx);

    free (srv);
}

static int _add_route (server_t *srv, int rank, int gw)
{
    route_t *rte = xzmalloc (sizeof (route_t));
    char key[16];

    rte->gw = gw;
    snprintf (key, sizeof (key), "%d", rank);
    if (zhash_insert (srv->route, key, rte) < 0) {
        free (rte);
        return -1;
    }
    zhash_freefn (srv->route, key, (zhash_free_fn *) free);
    return 0;
}

static void _del_route (server_t *srv, int rank)
{
    char key[16];

    snprintf (key, sizeof (key), "%d", rank);
    zhash_delete (srv->route, key);
}

static void _cmb_message (conf_t *conf, server_t *srv, zmsg_t **zmsg)
{
    char *arg;

    if (cmb_msg_match_substr (*zmsg, "cmb.reparent.", &arg)) {
        int i, newrank = strtoul (arg, NULL, 10);
    
        for (i = 0; i < conf->parent_len; i++)
            if (conf->parent[i].rank == newrank)
                break;
        if (i < conf->parent_len) {
            if (zsocket_disconnect (srv->zs_upreq, "%s",
                                conf->parent[srv->parent_cur].treeout_uri) < 0)
                err_exit ("zsocket_disconnect");
            if (zsocket_connect (srv->zs_upreq, "%s",
                                conf->parent[i].treeout_uri) < 0)
                err_exit ("zsocket_connect");
            srv->parent_cur = i;
        }    
        free (arg);
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.add.", &arg)) {
        int rank = strtoul (arg, NULL, 10);
        json_object *gw, *o = NULL;

        if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0) 
            goto done;
        if (!o || !(gw = json_object_object_get (o, "gw")))
            goto done;
        _add_route (srv, rank, json_object_get_int (gw));
done:
        if (o)
            json_object_put (o);
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.delete.", &arg)) {
        int rank = strtoul (arg, NULL, 10);
        _del_route (srv, rank);
        zmsg_destroy (zmsg);
    }
}

static void _route_event (conf_t *conf, void *src, void *d1, void *d2, char *s)
{
    zmsg_t *m, *cpy;

    m = zmsg_recv (src);
    if (!m)
        return;

    if (d2) {
        cpy = zmsg_dup (m);
        if (!cpy)
            err_exit ("zmsg_dup");
        if (zmsg_send (&cpy, d2) < 0)
            err_exit ("zmsg_send");
    }
    if (d1) {
        if (zmsg_send (&m, d1) < 0)
            err_exit ("zmsg_send");
    } else {
        zmsg_destroy (&m);
    }
}

static void _route_response (conf_t *conf, server_t *srv, void *sock)
{
    zmsg_t *zmsg = zmsg_recv (sock);

    /* feed snoop socket */
    if (zmsg) {
        zmsg_t *cpy = zmsg_dup (zmsg);
        if (!cpy)
            err_exit ("zmsg_dup");
        if (zmsg_send (&cpy, srv->zs_snoop) < 0)
            err_exit ("zmsg_send");
    } 
    zmsg_send (&zmsg, srv->zs_router);
}

/* Request tag with address ("N!tag") where N is NOT my rank.
 * Lookup route: if found, prepend next hop and send downstream, else upstream.
 * If there is no upstream, NAK.
 */
static void _route_remote_request (conf_t *conf, server_t *srv, zmsg_t **zmsg,
                                   int rank)
{
    char key[16];
    route_t *rte;

    snprintf (key, sizeof (key), "%d", rank);
    rte = zhash_lookup (srv->route, key);

    if (rte) {
        zframe_t *zf = zframe_new (key, strlen (key));
        if (!zf)
            oom ();
        if (zmsg_push (*zmsg, zf) < 0)
            oom ();
        zmsg_send (zmsg, srv->zs_router);
    } else if (srv->zs_upreq)
        zmsg_send (zmsg, srv->zs_upreq);
    else
        cmb_msg_send_errnum (zmsg, srv->zs_router, ENOSYS, srv->zs_snoop);
}

/* Request tag with address ("N!tag") where N is my rank.
 * Try to send it to the to local cmb or plugin.
 * If that doesn't work, NAK.
 */
static void _route_local_request (conf_t *conf, server_t *srv, zmsg_t **zmsg)
{
    _cmb_message (conf, srv, zmsg);
    if (*zmsg)
        plugin_send (srv, conf, zmsg);
    if (*zmsg)
        cmb_msg_send_errnum (zmsg, srv->zs_router, ENOSYS, srv->zs_snoop);
}

/* Request tag with no address ("tag").
 * Try to send it to local cmb or plugin.
 * If that doesn't work, send it upstream.
 * If there is no upstream, NAK.
 */
static void _route_noaddr_request (conf_t *conf, server_t *srv, zmsg_t **zmsg)
{
    _cmb_message (conf, srv, zmsg);
    if (*zmsg)
        plugin_send (srv, conf, zmsg);
    if (*zmsg && srv->zs_upreq)
        zmsg_send (zmsg, srv->zs_upreq);
    if (*zmsg)
        cmb_msg_send_errnum (zmsg, srv->zs_router, ENOSYS, srv->zs_snoop);
}

static void _route_request (conf_t *conf, server_t *srv)
{
    zmsg_t *zmsg;

    zmsg = zmsg_recv (srv->zs_router);

    /* feed snoop socket */
    if (zmsg) {
        zmsg_t *cpy = zmsg_dup (zmsg);
        if (!cpy)
            err_exit ("zmsg_dup");
        if (zmsg_send (&cpy, srv->zs_snoop) < 0)
            err_exit ("zmsg_send");
    }

    if (zmsg) {
        int rank = cmb_msg_tag_addr (zmsg);

        if (rank == conf->rank)
            _route_local_request (conf, srv, &zmsg);
        else if (rank == -1)
            _route_noaddr_request (conf, srv, &zmsg);
        else
            _route_remote_request (conf, srv, &zmsg, rank);
    }
    assert (zmsg == NULL);
}

static void _cmb_poll (conf_t *conf, server_t *srv)
{
    zmq_pollitem_t zpa[] = {
{ .socket = srv->zs_router,     .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_upreq,      .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_plin,       .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_plin_event, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_eventin,    .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };

    zpoll (zpa, sizeof (zpa) / sizeof (zpa[0]), -1);

    if (zpa[0].revents & ZMQ_POLLIN) /* router */
        _route_request (conf, srv);
    if (zpa[1].revents & ZMQ_POLLIN) /* upreq (upstream responding to req) */
        _route_response (conf, srv, srv->zs_upreq);
    if (zpa[2].revents & ZMQ_POLLIN) /* plin (plugin responding to req) */
        _route_response (conf, srv, srv->zs_plin);

    if (zpa[3].revents & ZMQ_POLLIN) /* plin_event (plugin sending event) */
        _route_event (conf, srv->zs_plin_event, srv->zs_plout_event,
                      srv->zs_eventout, "plin_event->plout_event,eventout");
    if (zpa[4].revents & ZMQ_POLLIN) /* eventin (external event input) */
        _route_event (conf, srv->zs_eventin, srv->zs_plout_event,
                      NULL, "eventin->plout_event");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
