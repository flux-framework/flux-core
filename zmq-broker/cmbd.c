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

static void _route_response (conf_t *conf, server_t *srv, zmsg_t **zmsg,
                            bool dnsock);

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

    snprintf (conf->rankstr, sizeof (conf->rankstr), "%s", optarg);

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
    zbind (zctx, &srv->zs_upreq_in,    ZMQ_ROUTER, UPREQ_URI, -1);
    zbind (zctx, &srv->zs_dnreq_out,   ZMQ_ROUTER, DNREQ_URI, -1);
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
    if (conf->treein_uri2) { /* N.B. ditto */
        if (zsocket_bind (srv->zs_dnreq_out, "%s", conf->treein_uri2) < 0)
            err_exit ("zsocket_bind: %s", conf->treein_uri2);
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

    if (srv->zs_upreq_in)
        zsocket_destroy (srv->zctx, srv->zs_upreq_in);
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

static route_t *_route_create (const char *gw, int flags)
{
    route_t *rte = xzmalloc (sizeof (route_t));
    rte->gw = xstrdup (gw);
    rte->flags = flags;
    return rte;
}

static void _free_route (route_t *rte)
{
    free (rte->gw);
    free (rte);
}

int cmb_route_add_internal (server_t *srv, const char *dst, const char *gw, int flags)
{
    route_t *rte = _route_create (gw, flags);

    if (zhash_insert (srv->route, dst, rte) < 0) {
        _free_route (rte);
        return -1;
    }
    zhash_freefn (srv->route, dst, (zhash_free_fn *)_free_route);
    return 0;
}

void cmb_route_del_internal (server_t *srv, const char *dst, const char *gw)
{
    route_t *rte = zhash_lookup (srv->route, dst);

    if (rte && !strcmp (rte->gw, gw))
        zhash_delete (srv->route, dst);
}

/* helper to build array of routes as a json object */
static int _route_to_json_full (const char *dst, route_t *rte, json_object *o)
{
    json_object *oo, *dd, *go, *fo;

    if (!(oo = json_object_new_object ()))
        oom ();
    if (!(dd = json_object_new_string (dst)))
        oom ();
    if (!(go = json_object_new_string (rte->gw)))
        oom ();
    if (!(fo = json_object_new_int (rte->flags)))
        oom ();
    json_object_object_add (oo, "dst", dd);
    json_object_object_add (oo, "gw", dd);
    json_object_object_add (oo, "flags", fo);
    json_object_array_add (o, oo);
    return 0;
}
#if 0
/* helper to build array of known (public) destinations as a json object */
static int _route_to_json (const char *dst, route_t *rte, json_object *o)
{
    json_object *dd;

    if (!(rte->flags & ROUTE_FLAGS_PRIVATE)) {
        if (!(dd = json_object_new_string (dst)))
            oom ();
        json_object_array_add (o, dd);
    }
    return 0;
}
#endif

/**
 ** Request routing
 **/

/* cmb.* requests are handled by cmb internally.
 */
static void _cmb_message (conf_t *conf, server_t *srv, zmsg_t **zmsg)
{
    char *arg;

    if (cmb_msg_match_substr (*zmsg, "cmb.reparent.", &arg)) {
        int i, newrank = strtoul (arg, NULL, 10);
    
        for (i = 0; i < conf->parent_len; i++)
            if (conf->parent[i].rank == newrank)
                break;
        if (i < conf->parent_len) {
            if (zsocket_disconnect (srv->zs_upreq_out, "%s",
                                conf->parent[srv->parent_cur].treeout_uri) < 0)
                err_exit ("zsocket_disconnect");
            if (zsocket_disconnect (srv->zs_dnreq_in, "%s",
                                conf->parent[srv->parent_cur].treeout_uri2)< 0)
                err_exit ("zsocket_disconnect");
            if (zsocket_connect (srv->zs_upreq_out, "%s",
                                conf->parent[i].treeout_uri) < 0)
                err_exit ("zsocket_connect");
            if (zsocket_connect (srv->zs_dnreq_in, "%s",
                                conf->parent[i].treeout_uri2) < 0)
                err_exit ("zsocket_connect");
            srv->parent_cur = i;
        }    
        free (arg);
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.add.", &arg)) {
        json_object *gw, *o = NULL;

        if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) == 0
                    && o != NULL && (gw = json_object_object_get (o, "gw")))
            cmb_route_add_internal (srv, arg, json_object_get_string (gw), 0);
        if (o)
            json_object_put (o);
        free (arg);
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.del.", &arg)) {
        json_object *gw, *o = NULL;

        if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) == 0
                    && o != NULL && (gw = json_object_object_get (o, "gw")))
            cmb_route_del_internal (srv, arg, json_object_get_string (gw));
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match (*zmsg, "cmb.route.query")) {
        json_object *ao, *o = NULL;

        if (!(o = json_object_new_object ()))
            oom ();
        if (!(ao = json_object_new_array ()))
            oom ();
        zhash_foreach (srv->route, (zhash_foreach_fn *)_route_to_json_full,ao);
        json_object_object_add (o, "route", ao);
        if (cmb_msg_rep_json (*zmsg, o) == 0)
            _route_response (conf, srv, zmsg, true);
        json_object_put (o);
        if (*zmsg)
            zmsg_destroy (zmsg);
    }
}

/* A plugin called 'foo' can send a request to 'foo[.anything]' and expect
 * the request to go upstream, not back to itself.  This is how instances
 * of the same plugin on different nodes accomplish reduction.
 * Compare the tag (dest) and the first frame pushed on the message by the
 * dealer socket (src).
 */
static bool _request_loop_detect (zmsg_t *zmsg)
{
    char *tag = cmb_msg_tag (zmsg, true); /* short tag name */
    char *sender = cmb_msg_sender (zmsg);
    bool res = false;

    if (!tag || !sender)
        goto done;
    if (strchr (tag, '!')) /* don't thwart explicit addressing */
        goto done;
    if (!strcmp (tag, sender))
        res = true;
done:
    if (tag)
        free (tag);
    if (sender)
        free (sender);
    return res;
}

/* Parse message request tag into addr, service.
 */
static int _parse_message_tag (zmsg_t *zmsg, char **ap, char **sp)
{
    char *p, *tag = NULL, *addr = NULL, *service = NULL;

    if (!(tag = cmb_msg_tag (zmsg, false)))
        goto error;
    if ((p = strchr (tag, '!'))) {
        addr = xstrdup (tag);
        addr[p - tag] = '\0';
        service = xstrdup (p + 1);
    } else
        service = xstrdup (tag);
    if ((p = strchr (service, '.')))
        *p = '\0';
    *ap = addr;
    *sp = service;
    free (tag);
    return 0;
error:
    if (tag)
        free (tag);
    return -1;
}

static void _route_response (conf_t *conf, server_t *srv, zmsg_t **zmsg,
                            bool dnsock)
{
    char *sender = NULL;
    route_t *rte;

    zmsg_cc (*zmsg, srv->zs_snoop);

    /* case 1: responses heading upward on the 'dnreq' flow can reverse
     * direction if they are traversing the tree.
     */
    if (dnsock) {
        sender = cmb_msg_sender (*zmsg);
        if (!sender)
            oom ();
        rte = zhash_lookup (srv->route, sender);
        if (rte)
            zmsg_send (zmsg, srv->zs_dnreq_in);
        else
            zmsg_send (zmsg, srv->zs_upreq_in);

    /* case 2: responses headed downward on 'upreq' flow must continue down.
     */
    } else
        zmsg_send (zmsg, srv->zs_upreq_in);

    if (sender)
        free (sender);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void _route_request (conf_t *conf, server_t *srv, zmsg_t **zmsg,
                            bool dnsock)
{
    char *addr = NULL, *service = NULL;
    route_t *rte;

    zmsg_cc (*zmsg, srv->zs_snoop);

    if (_parse_message_tag (*zmsg, &addr, &service) < 0)
        goto done;

    /* case 1: request explicitly addressed to me, tag == mynode!service.
     * Lookup service and if rte->gw == service, route down, else NAK.
     * Handle tag == mynode!cmb as a special case.
     */
    if (addr && !strcmp (addr, conf->rankstr)) {
        if (!strcmp (service, "cmb"))
            _cmb_message (conf, srv, zmsg);
        else {
            rte = zhash_lookup (srv->route, service);
            if (rte && !strcmp (rte->gw, service))
                zmsg_send_unrouter (zmsg, srv->zs_dnreq_out,
                                    conf->rankstr, rte->gw);
        }

    /* case 2: request explicitly addressed to a remote node, tag == N!service.
     * Lookup N and route down if found, route up if not found, or NAK at root.
     */
    } else if (addr) {
        rte = zhash_lookup (srv->route, addr);
        if (rte)
            zmsg_send_unrouter (zmsg, srv->zs_dnreq_out,
                                conf->rankstr, rte->gw);
        else if (!dnsock && srv->zs_upreq_out)
            zmsg_send (zmsg, srv->zs_upreq_out);
            

    /* case 3: request not addressed, e.g. tag == service.
     * Lookup service and route down if found (and not a loop).
     * Route up if not found or loop, or NAK at root.
     * Handle tag == cmb as a special case.
     */
    } else {
        if (!strcmp (service, "cmb"))
            _cmb_message (conf, srv, zmsg);
        else {
            rte = zhash_lookup (srv->route, service);
            if (rte && !_request_loop_detect (*zmsg)) {
                zmsg_send_unrouter (zmsg, srv->zs_dnreq_out,
                                    conf->rankstr, rte->gw);
            } else if (!dnsock && srv->zs_upreq_out)
                zmsg_send (zmsg, srv->zs_upreq_out);
        }
    }

    /* send NAK reply if message was not routed above */
    if (*zmsg) {
        if (cmb_msg_rep_errnum (*zmsg, ENOSYS) < 0)
            goto done;
        zmsg_cc (*zmsg, srv->zs_snoop);
        _route_response (conf, srv, zmsg, true);
    }
done:
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (addr)
        free (addr);
    if (service)
        free (service);
}


static void _cmb_poll (conf_t *conf, server_t *srv)
{
    zmq_pollitem_t zpa[] = {
{ .socket = srv->zs_upreq_in,   .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_upreq_out,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_dnreq_in,   .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_dnreq_out,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_plin_event, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_eventin,    .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    zmsg_t *zmsg = NULL;

    zpoll (zpa, sizeof (zpa) / sizeof (zpa[0]), -1);

    if (zpa[0].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_upreq_in);
        if (zmsg)
            _route_request (conf, srv, &zmsg, false);
    }
    if (zpa[1].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_upreq_out);
        if (zmsg)
            _route_response (conf, srv, &zmsg, false);
    }
    if (zpa[2].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_dnreq_in);
        if (zmsg)
            _route_request (conf, srv, &zmsg, true);
    }
    if (zpa[3].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv_unrouter (srv->zs_dnreq_out);
        if (zmsg)
            _route_response (conf, srv, &zmsg, true);
    }

    if (zpa[4].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_plin_event);
        if (zmsg) {
            zmsg_cc (zmsg, srv->zs_snoop);
            if (srv->zs_eventout)
                zmsg_cc (zmsg, srv->zs_eventout);
            zmsg_send (&zmsg, srv->zs_plout_event);
        }
    }
    if (zpa[5].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_eventin);
        if (zmsg) {
            zmsg_cc (zmsg, srv->zs_snoop);
            zmsg_send (&zmsg, srv->zs_plout_event);
        }
    }

    assert (zmsg == NULL);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
