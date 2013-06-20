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

#define OPTIONS "t:e:E:O:vs:r:R:S:p:c:P:L:T:"
static const struct option longopts[] = {
    {"event-uri",   required_argument,  0, 'e'},
    {"event-out-uri",required_argument, 0, 'O'},
    {"event-in-uri",required_argument,  0, 'E'},
    {"tree-in-uri", required_argument,  0, 't'},
    {"tree-in-uri2",required_argument,  0, 'T'},
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
" -t,--tree-in-uri URI   Set tree-in URI for upreq, e.g. tcp://*:5556\n"
" -T,--tree-in-uri2 URI  Set tree-in URI for dnreq, e.g. tcp://*:5557\n"
" -p,--parent N,URI,URI2 Set parent rank,URIs, e.g.\n"
"                        0,tcp://192.168.1.136:5556,tcp://192.168.1.136:557\n"
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
    int c, i;
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
            case 'T':   /* --tree-in-uri2 URI */
                conf->treein_uri2 = optarg;
                break;
            case 'p': { /* --parent rank,URI,URI2 */
                char *p1, *p2, *ac = xstrdup (optarg);
                if (conf->parent_len == parent_max)
                    msg_exit ("too many --parent's, max %d", parent_max);
                conf->parent[conf->parent_len].rank = strtoul (ac, NULL, 10);
                if (!(p1 = strchr (ac, ',')))
                    msg_exit ("malformed -p option");
                p1++;
                if (!(p2 = strchr (p1, ',')))
                    msg_exit ("malformed -p option");
                *p2++ = '\0';
                conf->parent[conf->parent_len].treeout_uri = xstrdup (p1);
                conf->parent[conf->parent_len].treeout_uri2 = xstrdup (p2);
                conf->parent_len++;
                free (ac);
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
    for (i = 0; i < conf->parent_len; i++) {
        free (conf->parent[i].treeout_uri);
        free (conf->parent[i].treeout_uri2);
    }
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
    zbind (zctx, &srv->zs_upreq_in,    ZMQ_ROUTER, ROUTER_URI, -1);
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
        zconnect (zctx, &srv->zs_upreq_out, ZMQ_DEALER,
                  conf->parent[srv->parent_cur].treeout_uri, -1, id);
        zconnect (zctx, &srv->zs_dnreq_in, ZMQ_DEALER,
                  conf->parent[srv->parent_cur].treeout_uri2, -1, id);
    }
    if (conf->treein_uri) { /* N.B. already given an inproc addr above */
        if (zsocket_bind (srv->zs_upreq_in, "%s", conf->treein_uri) < 0)
            err_exit ("zsocket_bind: %s", conf->treein_uri);
    }
    if (conf->treein_uri2)
        zbind (zctx, &srv->zs_dnreq_out, ZMQ_ROUTER, conf->treein_uri2, -1);

    if (!(srv->route = zhash_new ()))
        oom ();

    plugin_init (conf, srv);

    *srvp = srv;
}

static void _cmb_fini (conf_t *conf, server_t *srv)
{
    plugin_fini (conf, srv);

    zhash_destroy (&srv->route);

    if (srv->zs_upreq_in)
        zsocket_destroy (srv->zctx, srv->zs_upreq_in);
    if (srv->zs_plin)
        zsocket_destroy (srv->zctx, srv->zs_plin);
    if (srv->zs_plout_event)
        zsocket_destroy (srv->zctx, srv->zs_plout_event);
    if (srv->zs_plin_event)
        zsocket_destroy (srv->zctx, srv->zs_plin_event);
    if (srv->zs_snoop)
        zsocket_destroy (srv->zctx, srv->zs_snoop);
    if (srv->zs_upreq_out)
        zsocket_destroy (srv->zctx, srv->zs_upreq_out);
    if (srv->zs_dnreq_out)
        zsocket_destroy (srv->zctx, srv->zs_dnreq_out);
    if (srv->zs_dnreq_in)
        zsocket_destroy (srv->zctx, srv->zs_dnreq_in);
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

static int _route_to_json (const char *rank, route_t *rte, json_object *o)
{
    json_object *oo, *ro;

    if (!(oo = json_object_new_object ()))
        oom ();
    if (!(ro = json_object_new_int (rte->gw)))
        oom ();
    json_object_object_add (oo, rank, ro);
    json_object_array_add (o, oo);
    return 0;
}

static void _cc_snoop (conf_t *conf, server_t *srv, zmsg_t *zmsg)
{
    if (zmsg) {
        zmsg_t *cpy = zmsg_dup (zmsg);
        if (!cpy)
            err_exit ("zmsg_dup");
        if (zmsg_send (&cpy, srv->zs_snoop) < 0)
            err_exit ("zmsg_send");
    } 
}

static void _cmb_message (conf_t *conf, server_t *srv, zmsg_t **zmsg)
{
    char *arg;

    /* FIXME: add replies to protocol */

    if (cmb_msg_match_substr (*zmsg, "cmb.reparent.", &arg)) {
        int i, newrank = strtoul (arg, NULL, 10);
    
        for (i = 0; i < conf->parent_len; i++)
            if (conf->parent[i].rank == newrank)
                break;
        if (i < conf->parent_len) {
            if (zsocket_disconnect (srv->zs_upreq_out, "%s",
                                conf->parent[srv->parent_cur].treeout_uri) < 0)
                err_exit ("zsocket_disconnect");
            if (zsocket_connect (srv->zs_upreq_out, "%s",
                                conf->parent[i].treeout_uri) < 0)
                err_exit ("zsocket_connect");
            srv->parent_cur = i;
        }    
        free (arg);
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.add.", &arg)) {
        int rank = strtoul (arg, NULL, 10);
        json_object *gw, *o = NULL;

        if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) == 0
                    && o != NULL && (gw = json_object_object_get (o, "gw")))
            _add_route (srv, rank, json_object_get_int (gw));
        if (o)
            json_object_put (o);
        free (arg);
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.del.", &arg)) {
        _del_route (srv, strtoul (arg, NULL, 10));
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match (*zmsg, "cmb.route.query")) {
        json_object *ao, *o = NULL;
        int rank;
        char key[16];

        if (!(o = json_object_new_object ()))
            oom ();
        if (!(ao = json_object_new_array ()))
            oom ();
        zhash_foreach (srv->route, (zhash_foreach_fn *)_route_to_json, ao);
        json_object_object_add (o, "route", ao);
        if (cmb_msg_rep_json (*zmsg, o) == 0) {
            /* FIXME: refactor */
            rank = cmb_msg_rep_rank (*zmsg);
            snprintf (key, sizeof (key), "%d", rank);
            if (rank == -1 || zhash_lookup (srv->route, key)) {
                if (srv->zs_upreq_in)
                    zmsg_send (zmsg, srv->zs_upreq_in);
            } else if (srv->zs_dnreq_in)
                zmsg_send (zmsg, srv->zs_dnreq_in);
        }
        json_object_put (o);
        if (*zmsg)
            zmsg_destroy (zmsg);
    }
}

static void _route_event (conf_t *conf, void *src, void *d1, void *d2)
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

/* Request tag with address ("N!tag") where N is NOT my rank.
 * Lookup route: if found, send downstream, else upstream
 */
static void _route_request_remote (conf_t *conf, server_t *srv,
                                   zmsg_t **zmsg, int rank, bool upstream_ok)
{
    char dst[16], gw[16], loc[16];
    route_t *rte;

    snprintf (dst, sizeof (dst), "%d", rank);
    rte = zhash_lookup (srv->route, dst);
    if (rte) {
        snprintf (loc, sizeof (loc), "%d", conf->rank);
        snprintf (gw, sizeof (gw), "%d", rte->gw);
        if (srv->zs_dnreq_out)
            zmsg_send_router_req (zmsg, srv->zs_dnreq_out, loc, gw);
    } else if (upstream_ok && srv->zs_upreq_out)
        zmsg_send (zmsg, srv->zs_upreq_out);
    /* if not sent, *zmsg will not be destroyed, fall through to NAK */
}

/* Request tag with address ("N!tag") where N is my rank.
 * Try to send it to the to local cmb or plugin.
 * If that doesn't work, NAK.
 */
static void _route_request_local (conf_t *conf, server_t *srv, zmsg_t **zmsg)
{
    _cmb_message (conf, srv, zmsg);
    if (*zmsg)
        plugin_send (srv, conf, zmsg);
    /* if not sent, *zmsg will not be destroyed, fall through to NAK */
}

/* Request tag with no address ("tag").
 * Try to send it to local cmb or plugin.
 * If that doesn't work, send it upstream.
 * If there is no upstream, NAK.
 */
static void _route_request_noaddr (conf_t *conf, server_t *srv, zmsg_t **zmsg)
{
    _cmb_message (conf, srv, zmsg);
    if (*zmsg)
        plugin_send (srv, conf, zmsg);
    if (*zmsg && srv->zs_upreq_out)
        zmsg_send (zmsg, srv->zs_upreq_out);
    /* if not sent, *zmsg will not be destroyed, fall through to NAK */
}

/* Message is ready on upreq_in.
 * This will be a request originating from either plugin or downstream node.
 */
static void _route_upreq_request (conf_t *conf, server_t *srv)
{
    zmsg_t *zmsg;

    zmsg = zmsg_recv (srv->zs_upreq_in);
    _cc_snoop (conf, srv, zmsg);

    if (zmsg) {
        int rank = cmb_msg_req_rank (zmsg);

        if (rank == conf->rank)
            _route_request_local (conf, srv, &zmsg);
        else if (rank == -1)
            _route_request_noaddr (conf, srv, &zmsg);
        else
            _route_request_remote (conf, srv, &zmsg, rank, true);
    }
    if (zmsg)
        cmb_msg_send_errnum (&zmsg, srv->zs_upreq_in, ENOSYS, srv->zs_snoop);
}

/* Message is ready on dnreq_in.
 * This is a request heading downstream.
 * Continue downstream or deliver locally.  It cannot go upstream.
 */
static void _route_dnreq_request (conf_t *conf, server_t *srv)
{
    zmsg_t *zmsg;

    zmsg = zmsg_recv (srv->zs_dnreq_in);
    _cc_snoop (conf, srv, zmsg);

    if (zmsg) {
        int rank = cmb_msg_req_rank (zmsg);

        if (rank == conf->rank)
            _route_request_local (conf, srv, &zmsg);
        else
            _route_request_remote (conf, srv, &zmsg, rank, false);
    }
    if (zmsg)
        cmb_msg_send_errnum (&zmsg, srv->zs_dnreq_in, ENOSYS, srv->zs_snoop);
}

/* Message is ready on upreq_out.
 * This is a response heading downstream.
 * Route according to address envelope (never back upstream)
 */
static void _route_upreq_response (conf_t *conf, server_t *srv)
{
    zmsg_t *zmsg;

    zmsg = zmsg_recv (srv->zs_upreq_out);
    _cc_snoop (conf, srv, zmsg);

    if (zmsg && srv->zs_upreq_in)
        zmsg_send (&zmsg, srv->zs_upreq_in);
}

/* Message is ready on plin. 
 * This is a response from a local plugin.
 * Route according to address envelope.
 */
static void _route_plin_response (conf_t *conf, server_t *srv)
{
    zmsg_t *zmsg;
    char key[16];
    int rank;

    zmsg = zmsg_recv (srv->zs_plin);
    _cc_snoop (conf, srv, zmsg);

    if (zmsg) {
        rank = cmb_msg_rep_rank (zmsg);
        snprintf (key, sizeof (key), "%d", rank);
        if (rank == -1 || zhash_lookup (srv->route, key)) {
            if (srv->zs_upreq_in)
                zmsg_send (&zmsg, srv->zs_upreq_in);
        } else if (srv->zs_dnreq_in)
            zmsg_send (&zmsg, srv->zs_dnreq_in);
    }
}

/* Message is ready on dnreq_out.
 * This is a response heading upstream.
 * Route according to address envelope.
 */
static void _route_dnreq_response (conf_t *conf, server_t *srv)
{
    zmsg_t *zmsg;
    char key[16];
    int rank;

    zmsg = zmsg_recv_router_rep (srv->zs_dnreq_out);
    _cc_snoop (conf, srv, zmsg);

    if (zmsg) {
        rank = cmb_msg_rep_rank (zmsg);
        snprintf (key, sizeof (key), "%d", rank);
        if (rank == -1 || zhash_lookup (srv->route, key)) {
            if (srv->zs_upreq_in)
                zmsg_send (&zmsg, srv->zs_upreq_in);
        } else if (srv->zs_dnreq_in)
            zmsg_send (&zmsg, srv->zs_dnreq_in);
    }
}

static void _cmb_poll (conf_t *conf, server_t *srv)
{
    zmq_pollitem_t zpa[] = {
{ .socket = srv->zs_upreq_in,   .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_upreq_out,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_dnreq_in,   .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_dnreq_out,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_plin,       .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_plin_event, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_eventin,    .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };

    zpoll (zpa, sizeof (zpa) / sizeof (zpa[0]), -1);

    if (zpa[0].revents & ZMQ_POLLIN)
        _route_upreq_request (conf, srv);
    if (zpa[1].revents & ZMQ_POLLIN)
        _route_upreq_response (conf, srv);
    if (zpa[2].revents & ZMQ_POLLIN)
        _route_dnreq_request (conf, srv);
    if (zpa[3].revents & ZMQ_POLLIN)
        _route_dnreq_response (conf, srv);
    if (zpa[4].revents & ZMQ_POLLIN)
        _route_plin_response (conf, srv);
    if (zpa[5].revents & ZMQ_POLLIN)
        _route_event (conf, srv->zs_plin_event, srv->zs_plout_event, srv->zs_eventout);
    if (zpa[6].revents & ZMQ_POLLIN)
        _route_event (conf, srv->zs_eventin, srv->zs_plout_event, NULL);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
