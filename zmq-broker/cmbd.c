/* cmbd.c - simple zmq message broker, to run on each node of a job */

#define _GNU_SOURCE /* vasprintf */
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
#include "zmsg.h"
#include "route.h"
#include "cmbd.h"
#include "util.h"
#include "plugin.h"
#include "hljson.h"
#include "flux.h"

#define OPTIONS "t:e:E:O:vs:R:S:p:P:L:T:A:d:D:H:"
static const struct option longopts[] = {
    {"up-event-uri",   required_argument,  0, 'e'},
    {"up-event-out-uri",required_argument, 0, 'O'},
    {"up-event-in-uri",required_argument,  0, 'E'},
    {"dn-event-in-uri",required_argument,  0, 'd'},
    {"dn-event-out-uri",required_argument,  0, 'D'},
    {"up-req-in-uri", required_argument,  0, 't'},
    {"dn-req-out-uri",required_argument,  0, 'T'},
    {"verbose",           no_argument,  0, 'v'},
    {"set-conf",    required_argument,  0, 's'},
    {"rank",        required_argument,  0, 'R'},
    {"size",        required_argument,  0, 'S'},
    {"parent",      required_argument,  0, 'p'},
    {"plugins",     required_argument,  0, 'P'},
    {"logdest",     required_argument,  0, 'L'},
    {"api-socket",  required_argument,  0, 'A'},
    {"set-conf-hostlist",required_argument,  0, 'H'},
    {0, 0, 0, 0},
};

static void _cmb_init (conf_t *conf, server_t **srvp);
static void _cmb_fini (conf_t *conf, server_t *srv);
static void _cmb_poll (conf_t *conf, server_t *srv);

static void _route_response (conf_t *conf, server_t *srv, zmsg_t **zmsg,
                            bool dnsock);

static int _request_send (conf_t *conf, server_t *srv,
                           json_object *o, const char *fmt, ...)
                           __attribute__ ((format (printf, 4, 5)));


static void usage (void)
{
    fprintf (stderr, 
"Usage: cmbd OPTIONS\n"
" -e,--up-event-uri URI      Set upev URI, e.g. epgm://eth0;239.192.1.1:5555\n"
" -E,--up-event-in-uri URI   Set upev_in URI (alternative to -e)\n"
" -O,--up-event-out-uri URI  Set upev_out URI (alternative to -e)\n"
" -d,--dn-event-in-uri URI   Set dnev_in URI\n"
" -D,--dn-event-out-uri URI  Set dnev_out URI\n"
" -t,--up-req-in-uri URI     Set URI for upreq_in, e.g. tcp://*:5556\n"
" -T,--dn-req-out-uri URI    Set URI for dnreq_out, e.g. tcp://*:5557\n"
" -p,--parent N,URI,URI2 Set parent rank,URIs, e.g.\n"
"                        0,tcp://192.168.1.136:5556,tcp://192.168.1.136:557\n"
" -v,--verbose           Show bus traffic\n"
" -s,--set-conf key=val  Set plugin configuration key=val\n"
" -H,--set-conf-hostlist HOSTLIST Set session hostlist\n"
" -R,--rank N            Set cmbd address\n"
" -S,--size N            Set number of ranks in session\n"
" -P,--plugins p1,p2,... Load the named plugins (comma separated)\n"
" -L,--logdest DEST      Log to DEST, can  be syslog, stderr, or file\n"
" -A,--api-socket PATH   Listen for API connections on PATH\n"
            );
    exit (1);
}

int main (int argc, char *argv[])
{
    int c, i;
    conf_t *conf;
    server_t *srv;
    const int parent_max = sizeof (conf->parent) / sizeof (conf->parent[0]);
    static char apipath[PATH_MAX + 1];

    log_init (basename (argv[0]));

    conf = xzmalloc (sizeof (conf_t));
    conf->size = 1;
    snprintf (apipath, sizeof (apipath), CMB_API_PATH_TMPL, getuid ());
    conf->api_sockpath = apipath;
    if (!(conf->conf_hash = zhash_new ()))
        oom ();
    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'e':   /* --up-event-uri URI */
                if (!strstr (optarg, "pgm://"))
                    msg_exit ("use -E, -O for non-multicast event socket");
                conf->upev_in_uri = optarg;
                conf->upev_out_uri = optarg;
                break;
            case 'E':   /* --up-event-in-uri URI */
                conf->upev_in_uri = optarg;
                break;
            case 'O':   /* --up-event-out-uri URI */
                conf->upev_out_uri = optarg;
                break;
            case 'd':   /* --dn-event-in-uri URI */
                conf->dnev_in_uri = optarg;
                break;
            case 'D':   /* --dn-event-out-uri URI */
                conf->dnev_out_uri = optarg;
                break;
            case 't':   /* --up-req-in-uri URI */
                conf->upreq_in_uri = optarg;
                break;
            case 'T':   /* --dn-req-out-uri URI */
                conf->dnreq_out_uri = optarg;
                break;
            case 'p': { /* --parent rank,upreq-uri,dnreq-uri */
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
                conf->parent[conf->parent_len].upreq_uri = xstrdup (p1);
                conf->parent[conf->parent_len].dnreq_uri = xstrdup (p2);
                conf->parent_len++;
                free (ac);
                break;
            }
            case 'v':   /* --verbose */
                conf->verbose = true;
                break;
            case 's': { /* --set-conf key=val */
                char *p, *cpy = xstrdup (optarg);
                if ((p = strchr (cpy, '='))) {
                    *p++ = '\0';
                    zhash_update (conf->conf_hash, cpy, xstrdup (p));
                    zhash_freefn (conf->conf_hash, cpy, free);
                }
                free (cpy);
                break;
            }
            case 'H': { /* set-conf-hostlist hostlist */
                json_object *o = hostlist_to_json (optarg);
                const char *val = json_object_to_json_string (o);

                zhash_update (conf->conf_hash, "hosts", xstrdup (val));
                zhash_freefn (conf->conf_hash, "hosts", free);
                json_object_put (o);
            }
            case 'R':   /* --rank N */
                conf->rank = strtoul (optarg, NULL, 10);
                break;
            case 'S':   /* --size N */
                conf->size = strtoul (optarg, NULL, 10);
                break;
            case 'P':   /* --plugins p1,p2,... */
                conf->plugins = optarg;
                break;
            case 'L':   /* --logdest DEST */
                log_set_dest (optarg);
                break;
            case 'A':   /* --api-socket DEST */
                conf->api_sockpath = optarg;
                break;
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();
    if (!conf->plugins)
        msg_exit ("at least one plugin must be loaded");

    if (setenv ("CMB_API_PATH", conf->api_sockpath, 1) < 0)
        err_exit ("setenv (CMB_API_PATH=%s)", conf->api_sockpath);

    /* FIXME: hardwire rank 0 as root of the reduction tree.
     * Eventually we must allow for this role to migrate to other nodes
     * in case node 0 becomes unavailable.
     */
    if (conf->rank == 0) {
        if (conf->parent_len != 0)
            msg_exit ("rank 0 must not have parents");
        conf->treeroot = true;
    } else {
        if (conf->parent_len == 0)
            msg_exit ("rank > 0 must have parents");
        conf->treeroot = false;
    }
    if (conf->upreq_in_uri && !conf->dnreq_out_uri)
        msg_exit ("if --up-req-in-uri is set, --dn-req-out-uri must be also");
    if (!conf->upreq_in_uri && conf->dnreq_out_uri)
        msg_exit ("if --dn-req-out-uri is set, --up-req-in-uri must be also");

    snprintf (conf->rankstr, sizeof (conf->rankstr), "%d", conf->rank);

    _cmb_init (conf, &srv);
    for (;;)
        _cmb_poll (conf, srv);
    _cmb_fini (conf, srv);

    for (i = 0; i < conf->parent_len; i++) {
        free (conf->parent[i].upreq_uri);
        free (conf->parent[i].dnreq_uri);
    }
    if (conf->conf_hash)
        zhash_destroy (&conf->conf_hash);
    free (conf);

    return 0;
}

typedef struct {
    server_t *srv;
    conf_t *conf;
} kvs_put_one_arg_t;

static int _kvs_put_one (const char *key, void *item, void *arg)
{
    kvs_put_one_arg_t *ca = arg;
    server_t *srv = ca->srv;
    conf_t *conf = ca->conf;
    zmsg_t *zmsg = NULL;
    json_object *no, *o = util_json_object_new_object ();

    if ((no = json_tokener_parse ((char *)item)))
        json_object_object_add (o, key, no);
    else
        util_json_object_add_string (o, (char *)key, (char *)item);
    if (_request_send (conf, srv, o, "kvs.put") < 0) {
        goto error;
    }
    json_object_put (o);
    o = NULL;
    zmsg = NULL;

    if (!(zmsg = zmsg_recv_unrouter (srv->zs_dnreq_out))) {
        goto error;
    }
    if (cmb_msg_decode (zmsg, NULL, &o) < 0 || !o
            || util_json_object_get_int (o, "errnum", &errno) < 0) {
        goto eproto;
    }
    if (errno != 0)
        goto error;
    zmsg_destroy (&zmsg);
    json_object_put (o);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (o)
        json_object_put (o);
    return 1;
}

static int _kvs_commit (server_t *srv, conf_t *conf)
{
    zmsg_t *zmsg = NULL;
    json_object *o = util_json_object_new_object ();
    char *commit_name = uuid_generate_str ();

    util_json_object_add_string (o, "name", commit_name);
    if (_request_send (conf, srv, o, "kvs.commit") < 0)
        goto error;
    json_object_put (o);
    o = NULL;

    if (!(zmsg = zmsg_recv_unrouter (srv->zs_dnreq_out)))
        goto error;
    if (cmb_msg_decode (zmsg, NULL, &o) < 0 || !o)
        goto eproto;
    if (util_json_object_get_int (o, "errnum", &errno) == 0)
        goto error;
    zmsg_destroy (&zmsg);
    json_object_put (o);
    free (commit_name);
    return 0;
eproto:
    errno = EPROTO;
error:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (o)
        json_object_put (o);
    if (commit_name)
        free (commit_name);
    return 1;
}

static void _cmb_init (conf_t *conf, server_t **srvp)
{
    server_t *srv;
    zctx_t *zctx;
    int i;

    srv = xzmalloc (sizeof (server_t));
    srv->zctx = zctx = zctx_new ();
    srv->rctx = route_init (conf->verbose);
    if (!srv->zctx)
        err_exit ("zctx_new");
    zctx_set_linger (srv->zctx, 5);

    /* bind: upreq_in */
    if (!(srv->zs_upreq_in = zsocket_new (zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (srv->zs_upreq_in, 0);
    if (zsocket_bind (srv->zs_upreq_in, "%s", UPREQ_URI) < 0)
        err_exit ("zsocket_bind %s", UPREQ_URI);
    if (zsocket_bind (srv->zs_upreq_in, UPREQ_IPC_URI_TMPL, getuid ()) < 0)
        err_exit (UPREQ_IPC_URI_TMPL, getuid ());
    if (conf->upreq_in_uri) {
        if (zsocket_bind (srv->zs_upreq_in, "%s", conf->upreq_in_uri) < 0)
            err_exit ("zsocket_bind (upreq_in): %s", conf->upreq_in_uri);
    }

    /* bind: dnreq_out */
    zbind (zctx, &srv->zs_dnreq_out,   ZMQ_ROUTER, DNREQ_URI, 0);
    if (zsocket_bind (srv->zs_dnreq_out, DNREQ_IPC_URI_TMPL, getuid ()) < 0)
        err_exit (DNREQ_IPC_URI_TMPL, getuid ());
    if (conf->dnreq_out_uri) {
        if (zsocket_bind (srv->zs_dnreq_out, "%s", conf->dnreq_out_uri) < 0)
            err_exit ("zsocket_bind (dnreq_out): %s", conf->dnreq_out_uri);
    }

    /* bind: dnev_out */
    zbind (zctx, &srv->zs_dnev_out,    ZMQ_PUB,    DNEV_OUT_URI, 0);
    if (zsocket_bind (srv->zs_dnev_out, EVOUT_IPC_URI_TMPL, getuid ()) < 0)
        err_exit (EVOUT_IPC_URI_TMPL, getuid ());
    if (conf->dnev_out_uri)
        if (zsocket_bind (srv->zs_dnev_out, "%s", conf->dnev_out_uri) < 0)
            err_exit ("zsocket_bind (dnev_out): %s", conf->dnev_out_uri);

    /* bind: dnev_in */
    zbind (zctx, &srv->zs_dnev_in,     ZMQ_SUB,    DNEV_IN_URI,  0);
    if (zsocket_bind (srv->zs_dnev_in, EVIN_IPC_URI_TMPL, getuid ()) < 0)
        err_exit (EVIN_IPC_URI_TMPL, getuid ());
    zsocket_set_subscribe (srv->zs_dnev_in, "");
    if (conf->dnev_in_uri)
        if (zsocket_bind (srv->zs_dnev_in, "%s", conf->dnev_in_uri) < 0)
            err_exit ("zsocket_bind (dnev_in): %s", conf->dnev_in_uri);

    /* bind: snoop */
    zbind (zctx, &srv->zs_snoop,       ZMQ_PUB,    SNOOP_URI, -1);
    if (zsocket_bind (srv->zs_snoop, SNOOP_IPC_URI_TMPL, getuid ()) < 0)
        err_exit (SNOOP_IPC_URI_TMPL, getuid ());

    /* connect: upev_in */
    if (conf->upev_in_uri) {
        zconnect (zctx, &srv->zs_upev_in,  ZMQ_SUB, conf->upev_in_uri, 0, NULL);
        zsocket_set_subscribe (srv->zs_upev_in, "");
    }

    /* connect: upev_out */
    if (conf->upev_out_uri)
        zconnect (zctx, &srv->zs_upev_out, ZMQ_PUB, conf->upev_out_uri,0, NULL);

    for (i = 0; i < conf->parent_len; i++)
        srv->parent_alive[i] = true;

    /* connect: upreq_out, dnreq_in */
    if (conf->parent_len > 0) {
        char id[16];
        snprintf (id, sizeof (id), "%d", conf->rank);
        zconnect (zctx, &srv->zs_upreq_out, ZMQ_DEALER,
                  conf->parent[srv->parent_cur].upreq_uri, 0, id);
        zconnect (zctx, &srv->zs_dnreq_in, ZMQ_DEALER,
                  conf->parent[srv->parent_cur].dnreq_uri, 0, id);
    }

    if (srv->zs_upreq_out) {
        _request_send (conf, srv, NULL, "cmb.connect");
        _request_send (conf, srv, NULL, "cmb.route.hello");
    }

    plugin_init (conf, srv);

    /* Now that conf plugin is loaded, initialize conf parameters from the
     * command line before entering poll loop, thus ensuring that
     * initialization has completed before it answers any queries.
     */
    if (conf->rank == 0) {
        kvs_put_one_arg_t ca = { .srv = srv, .conf = conf };
        if (zhash_foreach (conf->conf_hash, _kvs_put_one, &ca) != 0)
            err_exit ("failed to initialize conf store on rank 0");
        _kvs_commit (srv, conf);
    } else if (zhash_size (conf->conf_hash) > 0)
        err_exit ("set-conf should only be used on rank 0");

    *srvp = srv;
}

static void _cmb_fini (conf_t *conf, server_t *srv)
{
    plugin_fini (conf, srv);

    if (srv->zs_upreq_in)
        zsocket_destroy (srv->zctx, srv->zs_upreq_in);
    if (srv->zs_dnev_out)
        zsocket_destroy (srv->zctx, srv->zs_dnev_out);
    if (srv->zs_dnev_in)
        zsocket_destroy (srv->zctx, srv->zs_dnev_in);
    if (srv->zs_snoop)
        zsocket_destroy (srv->zctx, srv->zs_snoop);
    if (srv->zs_upreq_out)
        zsocket_destroy (srv->zctx, srv->zs_upreq_out);
    if (srv->zs_dnreq_out)
        zsocket_destroy (srv->zctx, srv->zs_dnreq_out);
    if (srv->zs_dnreq_in)
        zsocket_destroy (srv->zctx, srv->zs_dnreq_in);
    if (srv->zs_upev_in)
        zsocket_destroy (srv->zctx, srv->zs_upev_in);
    if (srv->zs_upev_out)
        zsocket_destroy (srv->zctx, srv->zs_upev_out);

    route_fini (srv->rctx);
    zctx_destroy (&srv->zctx);

    free (srv);
}

/* Send upstream request message.
 */
static int _request_send (conf_t *conf, server_t *srv,
                           json_object *o, const char *fmt, ...)
{
    va_list ap;
    zmsg_t *zmsg;
    char *tag, *p;
    const char *gw;
    int n, ret = 0;

    va_start (ap, fmt);
    n = vasprintf (&tag, fmt, ap);
    va_end (ap);
    if (n < 0)
        err_exit ("vasprintf");

    zmsg = cmb_msg_encode (tag, o);
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* message delimiter */
        oom ();
    if ((p = strchr (tag, '.')))
        *p = '\0';
    gw = route_lookup (srv->rctx, tag);
    if (gw)
        zmsg_send_unrouter (&zmsg, srv->zs_dnreq_out, conf->rankstr, gw);
    else if (srv->zs_upreq_out)
        zmsg_send (&zmsg, srv->zs_upreq_out);
    else  {
        errno = EHOSTUNREACH;
        ret = -1;
    }
    if (zmsg)
        zmsg_destroy (&zmsg);
    free (tag);
    return ret;
}

/* FIXME: duplicated from logcli.c
 *    (pending pending of cmbd flux_t handle)
 */
static json_object *log_create (int level, const char *fac, const char *src,
                                const char *fmt, va_list ap)
{
    json_object *o = util_json_object_new_object ();
    char *str = NULL;
    struct timeval tv;

    if (gettimeofday (&tv, NULL) < 0)
        err_exit ("gettimeofday");

    if (vasprintf (&str, fmt, ap) < 0)
        oom ();
    if (strlen (str) == 0) {
        errno = EINVAL;
        goto error;
    }
    util_json_object_add_int (o, "count", 1);
    util_json_object_add_string (o, "facility", fac);
    util_json_object_add_int (o, "level", level);
    util_json_object_add_string (o, "source", src);
    util_json_object_add_timeval (o, "timestamp", &tv);
    util_json_object_add_string (o, "message", str);
    free (str);
    return o;
error:
    if (str)
        free (str);
    json_object_put (o);
    return NULL;
}

void cmbd_log (conf_t *conf, server_t *srv, int lev, const char *fmt, ...)
{
    va_list ap;
    json_object *o;

    va_start (ap, fmt);
    o = log_create (lev, "cmb", conf->rankstr, fmt, ap);
    va_end (ap);

    if (o) {
        _request_send (conf, srv, o, "log.msg");
        json_object_put (o);
    }
}

static void _reparent (conf_t *conf, server_t *srv)
{
    int i;

    for (i = 0; i < conf->parent_len; i++) {
        if (i != srv->parent_cur && srv->parent_alive[i]) {
            cmbd_log (conf, srv, LOG_ALERT, "reparent %d->%d",
                  conf->parent[srv->parent_cur].rank, conf->parent[i].rank);
            if (srv->parent_alive[srv->parent_cur])
                _request_send (conf, srv, NULL, "cmb.route.goodbye.%d", conf->rank);
            if (conf->verbose)
                msg ("%s: disconnect %s, connect %s", __FUNCTION__,
                    conf->parent[srv->parent_cur].upreq_uri,
                    conf->parent[i].upreq_uri);
            if (zsocket_disconnect (srv->zs_upreq_out, "%s",
                                conf->parent[srv->parent_cur].upreq_uri) < 0)
                err_exit ("zsocket_disconnect");
            if (zsocket_disconnect (srv->zs_dnreq_in, "%s",
                                conf->parent[srv->parent_cur].dnreq_uri)< 0)
                err_exit ("zsocket_disconnect");
            if (zsocket_connect (srv->zs_upreq_out, "%s",
                                conf->parent[i].upreq_uri) < 0)
                err_exit ("zsocket_connect");
            if (zsocket_connect (srv->zs_dnreq_in, "%s",
                                conf->parent[i].dnreq_uri) < 0)
                err_exit ("zsocket_connect");
            srv->parent_cur = i;

            usleep (1000*10); /* FIXME: message is lost without this delay */
            _request_send (conf, srv, NULL, "cmb.connect");
            break;
        }
    }
}

static void _cmb_internal_event (conf_t *conf, server_t *srv, zmsg_t *zmsg)
{
    char *arg = NULL;

    if (cmb_msg_match_substr (zmsg, "event.live.down.", &arg)) {
        int i, rank = strtoul (arg, NULL, 10);

        for (i = 0; i < conf->parent_len; i++) {
            if (conf->parent[i].rank == rank) {
                srv->parent_alive[i] = false;
                if (i == srv->parent_cur)
                    _reparent (conf, srv);
            }
        }

        //route_del_subtree (srv->rctx, arg);
        free (arg);        
    } else if (cmb_msg_match_substr (zmsg, "event.live.up.", &arg)) {
        int i, rank = strtoul (arg, NULL, 10);
        
        for (i = 0; i < conf->parent_len; i++) {
            if (conf->parent[i].rank == rank) {
                srv->parent_alive[i] = true;
                if (i == 0 && srv->parent_cur > 0)
                    _reparent (conf, srv);
            }
        }
        free (arg);
    } else if (cmb_msg_match (zmsg, "event.route.update")) {
        if (srv->zs_upreq_out)
            _request_send (conf, srv, NULL, "cmb.route.hello");
    } else if (cmb_msg_match_substr (zmsg, "event.sched.trigger.", &arg)) {
        srv->epoch = strtoul (arg, NULL, 10);
        free (arg);
    }
}

static void _cmb_internal_request (conf_t *conf, server_t *srv, zmsg_t **zmsg)
{
    char *arg;

    if (cmb_msg_match_substr (*zmsg, "cmb.route.add.", &arg)) {
        json_object *o = NULL;
        const char *gw = NULL, *parent = NULL;
        int flags = 0;

        if (cmb_msg_decode (*zmsg, NULL, &o) == 0 && o != NULL) {
            (void)util_json_object_get_string (o, "gw", &gw);
            (void)util_json_object_get_string (o, "parent", &parent);
            (void)util_json_object_get_int (o, "flags", &flags);
            if (gw)
                route_add (srv->rctx, arg, gw, parent, flags);
        }
        if (o)
            json_object_put (o);
        zmsg_destroy (zmsg);
        free (arg);
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.del.", &arg)) {
        json_object *o = NULL;
        const char *gw = NULL;

        if (cmb_msg_decode (*zmsg, NULL, &o) == 0 && o != NULL) {
            (void)util_json_object_get_string (o, "gw", &gw);
            if (gw)
                route_del (srv->rctx, arg, gw);
        }
        if (o)
            json_object_put (o);
        zmsg_destroy (zmsg);
        free (arg);
    } else if (cmb_msg_match (*zmsg, "cmb.route.query")) {
        json_object *o = util_json_object_new_object ();

        json_object_object_add (o, "route", route_dump_json (srv->rctx, true));
        if (cmb_msg_replace_json (*zmsg, o) == 0)
            _route_response (conf, srv, zmsg, true);
        json_object_put (o);
        if (*zmsg)
            zmsg_destroy (zmsg);
    } else if (cmb_msg_match (*zmsg, "cmb.route.hello")) {
        route_add_hello (srv->rctx, *zmsg, 0);
        if (srv->zs_upreq_out)
            zmsg_send (zmsg, srv->zs_upreq_out); /* fwd upstream */
        if (*zmsg)
            zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.goodbye.", &arg)) {
        //route_del_subtree (srv->rctx, arg);
        if (srv->zs_upreq_out)
            zmsg_send (zmsg, srv->zs_upreq_out); /* fwd upstream */
        if (*zmsg)
            zmsg_destroy (zmsg);
        free (arg);
    } else if (cmb_msg_match (*zmsg, "cmb.connect")) {
        if (srv->epoch > 2) {
            zmsg_t *z = cmb_msg_encode ("event.route.update", NULL);
            if (srv->zs_upev_out)
                zmsg_cc (z, srv->zs_upev_out);
            if (srv->zs_dnev_out)
                zmsg_cc (z, srv->zs_dnev_out);
            zmsg_send (&z, srv->zs_snoop);
        }
        if (*zmsg)
            zmsg_destroy (zmsg);
    }
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
    char *nexthop = NULL;

    zmsg_cc (*zmsg, srv->zs_snoop);

    if (!(sender = cmb_msg_sender (*zmsg)))
        oom ();
    if (!(nexthop = cmb_msg_nexthop (*zmsg)))
        oom ();

    /* case 1: responses heading upward on the 'dnreq' flow can reverse
     * direction if they are traversing the tree.
     */
    if (dnsock) {
        if (route_lookup (srv->rctx, nexthop)) {
            if (conf->verbose)
                msg ("%s: dnsock: DOWN %s!...!%s", __FUNCTION__, nexthop, sender);
            zmsg_send (zmsg, srv->zs_upreq_in);
        } else if (srv->zs_dnreq_in) {
            if (conf->verbose)
                msg ("%s: dnsock: UP %s!...!%s", __FUNCTION__, nexthop, sender);
            zmsg_send (zmsg, srv->zs_dnreq_in);
        }
        if (conf->verbose && *zmsg)
            msg ("%s: dnsock: DROP %s!...!%s", __FUNCTION__, nexthop, sender);

    /* case 2: responses headed downward on 'upreq' flow must continue down.
     */
    } else {
        if (conf->verbose)
            msg ("%s: upsock: DOWN %s!...!%s", __FUNCTION__, nexthop, sender);
        zmsg_send (zmsg, srv->zs_upreq_in);
    }

    if (sender)
        free (sender);
    if (nexthop)
        free (nexthop);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void _route_request (conf_t *conf, server_t *srv, zmsg_t **zmsg,
                            bool dnsock)
{
    char *addr = NULL, *service = NULL, *lasthop = NULL;
    const char *gw;

    zmsg_cc (*zmsg, srv->zs_snoop);

    if (_parse_message_tag (*zmsg, &addr, &service) < 0)
        goto done;
    lasthop = cmb_msg_nexthop (*zmsg);

    /* case 1: request explicitly addressed to me, tag == mynode!service.
     * Lookup service in routing table and if found, route down, else NAK.
     * Handle tag == mynode!cmb as a special case.
     */
    if (addr && !strcmp (addr, conf->rankstr)) {
        if (!strcmp (service, "cmb")) {
            if (conf->verbose)
                msg ("%s: loc addr: internal %s!%s", __FUNCTION__, addr, service);
            _cmb_internal_request (conf, srv, zmsg);
        } else {
            gw = route_lookup (srv->rctx, service);
            if (gw) {
                if (conf->verbose)
                    msg ("%s: loc addr: DOWN %s!%s", __FUNCTION__, addr, service);
                zmsg_send_unrouter (zmsg, srv->zs_dnreq_out, conf->rankstr, gw);
            }
        }
        if (conf->verbose && *zmsg)
            msg ("%s: loc addr: NAK %s!%s", __FUNCTION__, addr, service);

    /* case 2: request explicitly addressed to a remote node, tag == N!service.
     * Lookup N and route down if found, route up if not found, or NAK at root.
     */
    } else if (addr) {
        gw = route_lookup (srv->rctx, addr);
        if (gw) {
            if (conf->verbose)       
                msg ("%s: remote addr: DOWN %s!%s", __FUNCTION__, addr, service);
            zmsg_send_unrouter (zmsg, srv->zs_dnreq_out, conf->rankstr, gw);
        } else if (!dnsock && srv->zs_upreq_out) {
            if (conf->verbose)
                msg ("%s: remote addr: UP %s!%s", __FUNCTION__, addr, service);
            zmsg_send (zmsg, srv->zs_upreq_out);
        }
        if (conf->verbose && *zmsg)
            msg ("%s: remote addr: NAK %s!%s", __FUNCTION__, addr, service);

    /* case 3: request not addressed, e.g. tag == service.
     * Lookup service and route down if found (and not looping back to sender).
     * Route up if not found (or loop), or NAK at root.
     * Handle tag == cmb as a special case.
     */
    } else {
        if (!strcmp (service, "cmb")) {
            if (conf->verbose)
                msg ("%s: no addr: internal %s", __FUNCTION__, service);
            _cmb_internal_request (conf, srv, zmsg);
        } else {
            gw = route_lookup (srv->rctx, service);
            if (gw && (!lasthop || strcmp (gw, lasthop)) != 0) {
                if (conf->verbose)
                    msg ("%s: no addr: DOWN %s", __FUNCTION__, service);
                zmsg_send_unrouter (zmsg, srv->zs_dnreq_out, conf->rankstr, gw);
            } else if (!dnsock && srv->zs_upreq_out) {
                if (conf->verbose)
                    msg ("%s: no addr: UP %s", __FUNCTION__, service);
                zmsg_send (zmsg, srv->zs_upreq_out);
            }
        }
        if (conf->verbose && *zmsg)
            msg ("%s: no addr: NAK %s", __FUNCTION__, service);
    }

    /* send NAK reply if message was not routed above */
    if (*zmsg) {
        if (cmb_msg_replace_json_errnum (*zmsg, ENOSYS) < 0)
            goto done;
        _route_response (conf, srv, zmsg, true);
    }
done:
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (addr)
        free (addr);
    if (service)
        free (service);
    if (lasthop)
        free (lasthop);
}

/*
  REQ REP           REQ REP
   ^   |             |   ^
   |   |             |   |
   |   v             v   |
  (dealer)          (dealer)     (pub)     (sub)
 +------------------------------+--------------------+
 | upreq_out         dnreq_in   | upev_out  upev_in  |
 |                              |                    |
 |             CMBD             |                    |
 |                              |                    |
 | upreq_in          dnreq_out  | dnev_in   dnev_out |
 +------------------------------+--------------------+
  (router)          (router)[1]  (sub)     (pub)
   ^   |             |   ^
   |   |             |   |
   |   v             v   |
  REQ REP           REQ REP

_______________
[1] Use zmsg_recv_unrouter()/zmsg_send_unrouter() on this socket
because it's being used "backwards" from normal dealer-router flow.
*/

static void _cmb_poll (conf_t *conf, server_t *srv)
{
    zmq_pollitem_t zpa[] = {
{ .socket = srv->zs_upreq_in,   .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_upreq_out,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_dnreq_in,   .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_dnreq_out,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_dnev_in,    .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = srv->zs_upev_in,    .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    zmsg_t *zmsg = NULL;

    if (zmq_poll (zpa, sizeof (zpa) / sizeof (zpa[0]), -1) < 0)
        err_exit ("zmq_poll");

    /* request on upreq_in */
    if (zpa[0].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_upreq_in);
        if (zmsg)
            _route_request (conf, srv, &zmsg, false);
    }
    /* response on upreq_out */
    if (zpa[1].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_upreq_out);
        if (zmsg)
            _route_response (conf, srv, &zmsg, false);
    }
    /* request on dnreq_in */
    if (zpa[2].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_dnreq_in);
        if (zmsg)
            _route_request (conf, srv, &zmsg, true);
    }
    /* repsonse on dnreq_out */
    if (zpa[3].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv_unrouter (srv->zs_dnreq_out);
        if (zmsg)
            _route_response (conf, srv, &zmsg, true);
    }
    /* event on dnev_in */
    if (zpa[4].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_dnev_in);
        if (zmsg) {
            _cmb_internal_event (conf, srv, zmsg);
            zmsg_cc (zmsg, srv->zs_snoop);
            if (srv->zs_upev_out)
                zmsg_cc (zmsg, srv->zs_upev_out);
            zmsg_send (&zmsg, srv->zs_dnev_out);
        }
    }
    /* event on upev_in */
    if (zpa[5].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (srv->zs_upev_in);
        if (zmsg) {
            _cmb_internal_event (conf, srv, zmsg);
            zmsg_cc (zmsg, srv->zs_snoop);
            zmsg_send (&zmsg, srv->zs_dnev_out);
        }
    }

    assert (zmsg == NULL);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
