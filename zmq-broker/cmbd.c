/* cmbd.c - simple zmq message broker, to run on each node of a job */

#define _GNU_SOURCE /* vasprintf */
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <pwd.h>

#include <json/json.h>
#include <zmq.h>
#include <czmq.h>

#define GPL_LICENSED 1
#include <munge.h>

#include "log.h"
#include "zmsg.h"
#include "route.h"
#include "util.h"
#include "plugin.h"
#include "hljson.h"
#include "flux.h"
#include "handle.h"
#include "cmb_socket.h"

#if ZMQ_VERSION_MAJOR >= 4
#define HAVE_CURVE_SECURITY 1
#endif
#define HAVE_MUNGE_SECURITY 1

#define DEFAULT_ZAP_DOMAIN  "flux"

#define MAX_PARENTS 2
struct parent_struct {
    char *upreq_uri;
    char *dnreq_uri;
    int rank;
};

typedef struct {
    /* 0MQ
     */
    zctx_t *zctx;               /* zeromq context (MT-safe) */
#if HAVE_CURVE_SECURITY
    zauth_t *auth;
    zcert_t *srv_cert;
    zcert_t *cli_cert;
    struct passwd *pw;
#endif
#if HAVE_MUNGE_SECURITY
    munge_ctx_t mctx;
#endif
    bool security_disable;
    char *session_name;
    bool upev_mcast;

    void *zs_upreq_out;         /* DEALER - tree parent, upstream reqs */
    void *zs_dnreq_in;          /* rev DEALER - tree parent, downstream reqs */

    char *uri_upreq_in;         /* URI to listen for tree children */
    void *zs_upreq_in;          /* ROUTER - optional tree children + plugins */

    char *uri_dnreq_out;        /* URI to listen for tree children */
    void *zs_dnreq_out;         /* ROUTER - optional tree children + plugins */

    char *uri_upev_out;         /* URI to send external events */
    void *zs_upev_out;          /* PUB - publish external events */

    char *uri_upev_in;          /* URI to recv external events */
    void *zs_upev_in;           /* SUB - subscribe to external events */

    char *uri_dnev_out;         /* URI to listen for tree children */
    void *zs_dnev_out;          /* PUB - optional tree children + plugins */

    char *uri_dnev_in;          /* URI to listen for tree children */
    void *zs_dnev_in;           /* SUB - optional tree children + plugins */

    char *uri_snoop;
    void *zs_snoop;

    /* Wireup
     */
    struct parent_struct parent[MAX_PARENTS]; /* configured parents */
    int parent_len;             /* length of parent_struct array */
    int parent_cur;             /* current parent */
    bool parent_alive[MAX_PARENTS]; /* liveness state of parent nodes */
    /* Session parameters
     */
    bool treeroot;              /* true if we are the root of reduction tree */
    int rank;                   /* our rank in session */
    int size;                   /* session size */
    /* Plugins
     */
    char *plugin_path;          /* colon-separated list of directories */
    char *plugins;              /* comma-separated list of plugins to load */
    zhash_t *loaded_plugins;    /* hash of plugin handles by plugin name */
    zhash_t *kvs_arg;
    zhash_t *api_arg;
    /* Misc
     */
    bool verbose;               /* enable debug to stderr */
    route_ctx_t rctx;           /* routing table */
    int epoch;                  /* current sched trigger epoch */
    flux_t h;
} ctx_t;

static int snoop_cc (ctx_t *ctx, int type, zmsg_t *zmsg);
static int cmbd_upev_sendmsg (ctx_t *ctx, zmsg_t **zmsg);
static zmsg_t *cmbd_upev_recvmsg (ctx_t *ctx);

static void cmb_init (ctx_t *ctx);
static void cmb_fini (ctx_t *ctx);
static void cmb_poll (ctx_t *ctx);

#define OPTIONS "t:e:E:O:vs:R:S:p:P:L:T:A:d:D:H:N:n"
static const struct option longopts[] = {
    {"session-name",   required_argument,  0, 'N'},
    {"no-security",    no_argument,        0, 'N'},
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

static const struct flux_handle_ops cmbd_handle_ops;

static void usage (void)
{
    fprintf (stderr, 
"Usage: cmbd OPTIONS\n"
" -N,--session-name NAME     Set session name (default: flux)\n"
" -n,--no-security           Disable session security (default: %s, %s)\n"
" -e,--up-event-uri URI      Set upev URI, e.g. epgm://eth0;239.192.1.1:5555\n"
" -E,--up-event-in-uri URI   Set upev_in URI (alternative to -e)\n"
" -O,--up-event-out-uri URI  Set upev_out URI (alternative to -e)\n"
" -d,--dn-event-in-uri URI   Set dnev_in URI\n"
" -D,--dn-event-out-uri URI  Set dnev_out URI\n"
" -t,--up-req-in-uri URI     Set URI for upreq_in, e.g. tcp://*:5556\n"
" -T,--dn-req-out-uri URI    Set URI for dnreq_out, e.g. tcp://*:5557\n"
" -p,--parent N,URI,URI2     Set parent rank,URIs, e.g.\n"
"                            0,tcp://192.168.1.136:5556,tcp://192.168.1.136:557\n"
" -v,--verbose               Show bus traffic\n"
" -s,--set-conf key=val      Set plugin configuration key=val\n"
" -H,--set-conf-hostlist HOSTLIST Set session hostlist\n"
" -R,--rank N                Set cmbd address\n"
" -S,--size N                Set number of ranks in session\n"
" -P,--plugins p1,p2,...     Load the named plugins (comma separated)\n"
" -X,--plugin-path PATH      Set plugin search path (colon separated)\n"
" -L,--logdest DEST          Log to DEST, can  be syslog, stderr, or file\n"
" -A,--api-socket PATH       Listen for API connections on PATH\n",
#if HAVE_MUNGE_SECURITY
"MUNGE",
#else
"no MUNGE",
#endif
#if HAVE_CURVE_SECURITY
"CURVE"
#else
"no CURVE"
#endif
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int c, i;
    ctx_t ctx;

    memset (&ctx, 0, sizeof (ctx));
    log_init (basename (argv[0]));

    ctx.size = 1;
    if (!(ctx.kvs_arg = zhash_new ()))
        oom ();
    if (!(ctx.api_arg = zhash_new ()))
        oom ();
    ctx.session_name = "flux";
    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'N':   /* --session-name NAME */
                ctx.session_name = optarg;
                break;
            case 'n':   /* --no-security */
                ctx.security_disable = true;
                break;
            case 'e':   /* --up-event-uri URI */
                if (!strstr (optarg, "pgm://"))
                    msg_exit ("use -E, -O for non-multicast event socket");
                ctx.uri_upev_in = optarg;
                ctx.uri_upev_out = optarg;
                ctx.upev_mcast = true;
                break;
            case 'E':   /* --up-event-in-uri URI */
                ctx.uri_upev_in = optarg;
                break;
            case 'O':   /* --up-event-out-uri URI */
                ctx.uri_upev_out = optarg;
                break;
            case 'd':   /* --dn-event-in-uri URI */
                ctx.uri_dnev_in = optarg;
                break;
            case 'D':   /* --dn-event-out-uri URI */
                ctx.uri_dnev_out = optarg;
                break;
            case 't':   /* --up-req-in-uri URI */
                ctx.uri_upreq_in = optarg;
                break;
            case 'T':   /* --dn-req-out-uri URI */
                ctx.uri_dnreq_out = optarg;
                break;
            case 'p': { /* --parent rank,upreq-uri,dnreq-uri */
                char *p1, *p2, *ac = xstrdup (optarg);
                if (ctx.parent_len == MAX_PARENTS)
                    msg_exit ("too many --parent's, max %d", MAX_PARENTS);
                ctx.parent[ctx.parent_len].rank = strtoul (ac, NULL, 10);
                if (!(p1 = strchr (ac, ',')))
                    msg_exit ("malformed -p option");
                p1++;
                if (!(p2 = strchr (p1, ',')))
                    msg_exit ("malformed -p option");
                *p2++ = '\0';
                ctx.parent[ctx.parent_len].upreq_uri = xstrdup (p1);
                ctx.parent[ctx.parent_len].dnreq_uri = xstrdup (p2);
                ctx.parent_len++;
                free (ac);
                break;
            }
            case 'v':   /* --verbose */
                ctx.verbose = true;
                break;
            case 's': { /* --set-conf key=val */
                char *p, *cpy = xstrdup (optarg);
                if ((p = strchr (cpy, '='))) {
                    *p++ = '\0';
                    zhash_update (ctx.kvs_arg, cpy, xstrdup (p));
                    zhash_freefn (ctx.kvs_arg, cpy, free);
                }
                free (cpy);
                break;
            }
            case 'H': { /* set-conf-hostlist hostlist */
                json_object *o = hostlist_to_json (optarg);
                const char *val = json_object_to_json_string (o);

                zhash_update (ctx.kvs_arg, "hosts", xstrdup (val));
                zhash_freefn (ctx.kvs_arg, "hosts", free);
                json_object_put (o);
            }
            case 'R':   /* --rank N */
                ctx.rank = strtoul (optarg, NULL, 10);
                break;
            case 'S':   /* --size N */
                ctx.size = strtoul (optarg, NULL, 10);
                break;
            case 'P':   /* --plugins p1,p2,... */
                ctx.plugins = optarg;
                break;
            case 'X':   /* --plugin-path PATH */
                ctx.plugin_path = optarg;
                break;
            case 'L':   /* --logdest DEST */
                log_set_dest (optarg);
                break;
            case 'A': { /* --api-socket DEST */
                char *cpy = xstrdup (optarg);
                zhash_update (ctx.api_arg, "sockpath", cpy);
                zhash_freefn (ctx.api_arg, "sockpath", free);
                break;
            }
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();
    if (!ctx.plugins)
        msg_exit ("at least one plugin must be loaded");
    if (!ctx.plugin_path)
        ctx.plugin_path = PLUGIN_PATH; /* compiled in default */

    if (ctx.upev_mcast && strcmp (ctx.uri_upev_out, ctx.uri_upev_in) != 0)
        usage ();

    /* FIXME: hardwire rank 0 as root of the reduction tree.
     * Eventually we must allow for this role to migrate to other nodes
     * in case node 0 becomes unavailable.
     */
    if (ctx.rank == 0) {
        if (ctx.parent_len != 0)
            msg_exit ("rank 0 must not have parents");
        ctx.treeroot = true;
    } else {
        if (ctx.parent_len == 0)
            msg_exit ("rank > 0 must have parents");
        ctx.treeroot = false;
    }
    if (ctx.uri_upreq_in && !ctx.uri_dnreq_out)
        msg_exit ("if --up-req-in-uri is set, --dn-req-out-uri must be also");
    if (!ctx.uri_upreq_in && ctx.uri_dnreq_out)
        msg_exit ("if --dn-req-out-uri is set, --up-req-in-uri must be also");

    cmb_init (&ctx);
    for (;;)
        cmb_poll (&ctx);
    cmb_fini (&ctx);

    for (i = 0; i < ctx.parent_len; i++) {
        free (ctx.parent[i].upreq_uri);
        free (ctx.parent[i].dnreq_uri);
    }
    if (ctx.kvs_arg)
        zhash_destroy (&ctx.kvs_arg);
    if (ctx.api_arg)
        zhash_destroy (&ctx.api_arg);

    return 0;
}

static int load_plugin (ctx_t *ctx, char *name)
{
    int idlen = strlen (name) + 16;
    char *id = xzmalloc (idlen);
    plugin_ctx_t p;
    int rc = -1;
    zhash_t *args = NULL;

    snprintf (id, idlen, "%s-%d", name, ctx->rank);
    if (!strcmp (name, "kvs") && ctx->treeroot)
        args = ctx->kvs_arg;
    else if (!strcmp (name, "api"))
        args = ctx->api_arg;

    if ((p = plugin_load (ctx->h, ctx->plugin_path, name, id, args))) {
        if (zhash_insert (ctx->loaded_plugins, name, p) < 0) {
            plugin_unload (p);
            goto done;
        }
        route_add (ctx->rctx, id, id, NULL, ROUTE_FLAGS_PRIVATE);
        route_add (ctx->rctx, name, id, NULL, ROUTE_FLAGS_PRIVATE);
        rc = 0;
    }
done:
    free (id);
    return rc;
}

static void unload_plugin (ctx_t *ctx, plugin_ctx_t p)
{
    const char *name = plugin_name (p);
    const char *id = plugin_id (p);

    if (name && id) {
        route_del (ctx->rctx, id, id);
        route_del (ctx->rctx, name, id);
    }
    plugin_unload (p);
}

static void load_plugins (ctx_t *ctx)
{
    char *cpy = xstrdup (ctx->plugins);
    char *name, *saveptr, *a1 = cpy;

    while((name = strtok_r (a1, ",", &saveptr))) {
        if (load_plugin (ctx, name) < 0)
            err ("failed to load plugin %s", name);
        a1 = NULL;
    }
    free (cpy);
}

static void unload_plugins (ctx_t *ctx)
{
    zlist_t *keys = zhash_keys (ctx->loaded_plugins);
    plugin_ctx_t p;
    char *name;

    while ((name = zlist_pop (keys)))
        if ((p = zhash_lookup (ctx->loaded_plugins, name)))
            unload_plugin (ctx, p);
    zlist_destroy (&keys);
}

/* zeromq unlinks ipc:// paths before binding them.
 * Therefore checking them here is maybe not that useful.
 * N.B. ipc://@ designates abstract AF_LOCAL namespace (since zeromq4)
 */
static bool check_uri (const char *uri)
{
    bool ispath = false;

    if (uri && strncmp (uri, "ipc://", 6) == 0) {
#if ZMQ_VERSION_MAJOR >= 4
        if (strncmp (uri, "ipc://@", 7) != 0)
            ispath = true;
#else
        ispath = true;
#endif
    }
    if (ispath) {
        struct stat sb;
        const char *path = uri + 6;

        if (lstat (path, &sb) == 0) {
            if (S_ISLNK (sb.st_mode)) 
                msg_exit ("%s is a symlink", path);
            if (!S_ISSOCK (sb.st_mode))
                msg_exit ("%s is not a socket", path);
            if (sb.st_uid != geteuid ())
                msg_exit ("%s has wrong owner", path);
        }
    }
    return (uri != NULL);
}

static void *cmb_init_upreq_in (ctx_t *ctx)
{
    void *s;
    const char *uri = UPREQ_URI;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable) {
        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (ctx->srv_cert, s);
        zsocket_set_curve_server (s, 1);
    }
#endif
    zsocket_set_hwm (s, 0);
    if (check_uri (uri)) {
        if (zsocket_bind (s, "%s", uri) < 0)
            err_exit ("%s", uri);
    }
    if (check_uri (ctx->uri_upreq_in)) {
        if (zsocket_bind (s, "%s", ctx->uri_upreq_in) < 0)
            err_exit ("%s", ctx->uri_upreq_in);
    }
    return s;
}

static void *cmb_init_dnreq_out (ctx_t *ctx)
{
    void *s;
    const char *uri = DNREQ_URI;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable) {
        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (ctx->srv_cert, s);
        zsocket_set_curve_server (s, 1);
    }
#endif
    zsocket_set_hwm (s, 0);
    if (check_uri (uri)) {
        if (zsocket_bind (s, "%s", DNREQ_URI) < 0)
            err_exit ("%s", DNREQ_URI);
    }
    if (check_uri (ctx->uri_dnreq_out)) {
        if (zsocket_bind (s, "%s", ctx->uri_dnreq_out) < 0)
            err_exit ("%s", ctx->uri_dnreq_out);
    }
    return s;
}

static void *cmb_init_dnev_out (ctx_t *ctx)
{
    void *s;
    const char *uri = DNEV_OUT_URI;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_PUB)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable) {
        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (ctx->srv_cert, s);
        zsocket_set_curve_server (s, 1);
    }
#endif
    zsocket_set_hwm (s, 0);
    if (check_uri (uri)) {
        if (zsocket_bind (s, "%s", uri) < 0)
            err_exit ("%s", uri);
    }
    if (check_uri (ctx->uri_dnev_out)) {
        if (zsocket_bind (s, "%s", ctx->uri_dnev_out) < 0)
            err_exit ("%s", ctx->uri_dnev_out);
    }
    return s;
}

static void *cmb_init_dnev_in (ctx_t *ctx)
{
    void *s;
    const char *uri = DNEV_IN_URI;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_SUB)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable) {
        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (ctx->srv_cert, s);
        zsocket_set_curve_server (s, 1);
    }
#endif
    zsocket_set_hwm (s, 0);
    if (check_uri (uri)) {
        if (zsocket_bind (s, "%s", uri) < 0)
            err_exit ("%s", uri);
    }
    if (check_uri (ctx->uri_dnev_in)) {
        if (zsocket_bind (s, "%s", ctx->uri_dnev_in) < 0)
            err_exit ("%s", ctx->uri_dnev_in);
    }
    zsocket_set_subscribe (s, "");
    return s;
}

static void *cmb_init_snoop (ctx_t *ctx)
{
    void *s;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_PUB)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable) {
        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (ctx->srv_cert, s);
        zsocket_set_curve_server (s, 1);
    }
#endif
    /* Dynamically allocate the snoop URI in the ipc:// space.
     * Make it available to clients via the flux_getattr().
     */
    assert (ctx->uri_snoop == NULL);
    if (zsocket_bind (s, "%s", "ipc://*") < 0)
        err_exit ("%s", "ipc://*");
    ctx->uri_snoop = zsocket_last_endpoint (s); /* need to xstrdup? */
    return s;
}

static void *cmb_init_upev_in (ctx_t *ctx)
{
    void *s;
    if (!(s = zsocket_new (ctx->zctx, ZMQ_SUB)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable) {
        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (ctx->cli_cert, s);
        char *srvkey = zcert_public_txt (ctx->srv_cert);
        zsocket_set_curve_serverkey (s, srvkey);
    }
#endif
    zsocket_set_hwm (s, 0);
    if (zsocket_connect (s, "%s", ctx->uri_upev_in) < 0)
        err_exit ("%s", ctx->uri_upev_in);
    zsocket_set_subscribe (s, "");
    return s;
}

static void *cmb_init_upev_out (ctx_t *ctx)
{
    void *s;
    if (!(s = zsocket_new (ctx->zctx, ZMQ_PUB)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable) {
        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (ctx->cli_cert, s);
        char *srvkey = zcert_public_txt (ctx->srv_cert);
        zsocket_set_curve_serverkey (s, srvkey);
    }
#endif
    zsocket_set_hwm (s, 0);
    if (zsocket_connect (s, "%s", ctx->uri_upev_out) < 0)
        err_exit ("%s", ctx->uri_upev_out);
    return s;
}

static void *cmb_init_upreq_out (ctx_t *ctx) {
    void *s;
    char id[16];
    char *uri = ctx->parent[ctx->parent_cur].upreq_uri;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_DEALER)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable) {
        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (ctx->cli_cert, s);
        char *srvkey = zcert_public_txt (ctx->srv_cert);
        zsocket_set_curve_serverkey (s, srvkey);
    }
#endif
    zsocket_set_hwm (s, 0);
    snprintf (id, sizeof (id), "%d", ctx->rank);
    zsocket_set_identity (s, id); 
    if (zsocket_connect (s, "%s", uri) < 0)
        err_exit ("%s", uri);
    return s;
}

static void *cmb_init_dnreq_in (ctx_t *ctx)
{
    void *s;
    char id[16];
    char *uri = ctx->parent[ctx->parent_cur].dnreq_uri;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_DEALER)))
        err_exit ("zsocket_new");
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable) {
        zsocket_set_zap_domain (s, DEFAULT_ZAP_DOMAIN);
        zcert_apply (ctx->cli_cert, s);
        char *srvkey = zcert_public_txt (ctx->srv_cert);
        zsocket_set_curve_serverkey (s, srvkey);
    }
#endif
    zsocket_set_hwm (s, 0);
    snprintf (id, sizeof (id), "%d", ctx->rank);
    zsocket_set_identity (s, id); 
    if (zsocket_connect (s, "%s", uri) < 0)
        err_exit ("%s", uri);
    return s;
}

#if HAVE_CURVE_SECURITY
/* cmbd instances wire up actively in the upstream direction; that is, we
 * have downstream facing sockets that call "bind" in the server role,
 * and upstream facing sockets that call "connect" in the client role.
 * As a result, cmbds may be acting as client, server, or both depending
 * on their position in the tree.
 *
 * All clients share a client cert.  All servers share a server cert.
 * For now, we load the keys out of the session owner's ~/.curve directory,
 * and require them to be generated in advance with "flux keygen".
 *
 * Although we use curve on all sockets, it only has an effect currently
 * sockets bound to tcp:// or ipc:// endpoints.  It is a no-op when
 * epgm:// and inproc:// are used.
 */
static zauth_t *cmb_init_curve (ctx_t *ctx)
{
    char *curve_path, *path;
    struct stat sb;
    zauth_t *auth;

    if (!(ctx->pw = getpwuid (geteuid ()))
            || (ctx->pw->pw_dir == NULL || strlen (ctx->pw->pw_dir) == 0))
        msg_exit ("could not determine home directory");
    if (asprintf (&curve_path, "%s/.curve", ctx->pw->pw_dir) < 0)
        oom ();
    if (lstat (curve_path, &sb) < 0) /* don't follow symlinks */
        err_exit ("%s", curve_path);
    if (!S_ISDIR (sb.st_mode))
        msg_exit ("%s: not a directory", curve_path);
    if ((sb.st_mode & (S_IRWXU|S_IRWXG|S_IRWXO)) != 0700)
        msg_exit ("%s: permissions not set to 0700", curve_path);
    if ((sb.st_uid != geteuid ()))
        msg_exit ("%s: invalid owner", curve_path);

    if (asprintf (&path, "%s/%s.client", curve_path, ctx->session_name) < 0)
        oom ();
    if (!(ctx->cli_cert = zcert_load (path)))
        err_exit ("%s", path);
    free (path);

    if (asprintf (&path, "%s/%s.server", curve_path, ctx->session_name) < 0)
        oom ();
    if (!(ctx->srv_cert = zcert_load (path)))
        err_exit ("%s", path);
    free (path);

    if (!(auth = zauth_new (ctx->zctx)))
        err_exit ("zauth_new");
    if (ctx->verbose)
        zauth_set_verbose (auth, true);
    //zauth_allow (auth, "127.0.0.1");
    zauth_configure_curve (auth, "*", curve_path);

    return auth;
}
#endif

static void cmb_init (ctx_t *ctx)
{
    int i;

    ctx->rctx = route_init (ctx->verbose);

    /* Set a restrictive umask so that zmq's unlink/bind doesn't create
     * a path that an unauthorized user else could connect to.
     * Not necessarily that useful when we are also doing CURVE auth.
     */
    (void)umask (077);

    ctx->zctx = zctx_new ();
    if (!ctx->zctx)
        err_exit ("zctx_new");
    zctx_set_linger (ctx->zctx, 5);
#if HAVE_CURVE_SECURITY
    if (!ctx->security_disable)
        ctx->auth = cmb_init_curve (ctx);
#endif
#if HAVE_MUNGE_SECURITY
    if (!ctx->security_disable) {
        munge_err_t e;
        if (!(ctx->mctx = munge_ctx_create ()))
            oom ();
        e = munge_ctx_set (ctx->mctx, MUNGE_OPT_UID_RESTRICTION, geteuid ());
        if (e != EMUNGE_SUCCESS)
            err_exit ("munge_ctx_set: %s", munge_strerror (e));
    }
#endif
    /* Bind to downstream ports.
     */
    ctx->zs_upreq_in = cmb_init_upreq_in (ctx);
    ctx->zs_dnreq_out = cmb_init_dnreq_out (ctx);
    ctx->zs_dnev_out = cmb_init_dnev_out (ctx);
    ctx->zs_dnev_in = cmb_init_dnev_in (ctx);
    ctx->zs_snoop = cmb_init_snoop (ctx);

    /* Connect to upstream ports.
     */
    if (ctx->uri_upev_in)
        ctx->zs_upev_in = cmb_init_upev_in (ctx);
    if (ctx->uri_upev_out)
        ctx->zs_upev_out = cmb_init_upev_out (ctx);

    for (i = 0; i < ctx->parent_len; i++)
        ctx->parent_alive[i] = true;

    if (ctx->parent_len > 0) {
        ctx->zs_upreq_out = cmb_init_upreq_out (ctx);
        ctx->zs_dnreq_in  = cmb_init_dnreq_in (ctx);
    }

    /* create flux_t handle */
    ctx->h = handle_create (ctx, &cmbd_handle_ops, 0);
    flux_log_set_facility (ctx->h, "cmbd");

    /* Contact upstream.
     */
    if (ctx->zs_upreq_out) {
        if (flux_request_send (ctx->h, NULL, "cmb.connect") < 0)
            err_exit ("error sending cmb.connect upstream");
        if (flux_request_send (ctx->h, NULL, "cmb.route.hello") < 0)
            err_exit ("error sending cmb.route.hello upstream");
    }

    ctx->loaded_plugins = zhash_new ();
    load_plugins (ctx);

    flux_log (ctx->h, LOG_INFO, "initialization complete");
}

static void cmb_fini (ctx_t *ctx)
{
    unload_plugins (ctx);
    zhash_destroy (&ctx->loaded_plugins);

    if (ctx->zs_upreq_in)
        zsocket_destroy (ctx->zctx, ctx->zs_upreq_in);
    if (ctx->zs_dnev_out)
        zsocket_destroy (ctx->zctx, ctx->zs_dnev_out);
    if (ctx->zs_dnev_in)
        zsocket_destroy (ctx->zctx, ctx->zs_dnev_in);
    if (ctx->zs_snoop)
        zsocket_destroy (ctx->zctx, ctx->zs_snoop);
    if (ctx->zs_upreq_out)
        zsocket_destroy (ctx->zctx, ctx->zs_upreq_out);
    if (ctx->zs_dnreq_out)
        zsocket_destroy (ctx->zctx, ctx->zs_dnreq_out);
    if (ctx->zs_dnreq_in)
        zsocket_destroy (ctx->zctx, ctx->zs_dnreq_in);
    if (ctx->zs_upev_in)
        zsocket_destroy (ctx->zctx, ctx->zs_upev_in);
    if (ctx->zs_upev_out)
        zsocket_destroy (ctx->zctx, ctx->zs_upev_out);
#if HAVE_CURVE_SECURITY
    if (ctx->auth)
        zauth_destroy (&ctx->auth);
#endif
#if HAVE_MUNGE_SECURITY
    if (ctx->mctx)
        munge_ctx_destroy (ctx->mctx);
#endif
    route_fini (ctx->rctx);
    zctx_destroy (&ctx->zctx);
}

static void _reparent (ctx_t *ctx)
{
    int i;

    for (i = 0; i < ctx->parent_len; i++) {
        if (i != ctx->parent_cur && ctx->parent_alive[i]) {
            flux_log (ctx->h, LOG_ALERT, "reparent %d->%d",
                  ctx->parent[ctx->parent_cur].rank,
                  ctx->parent[i].rank);
            if (ctx->parent_alive[ctx->parent_cur]) {
                if (flux_request_send (ctx->h, NULL, "cmb.route.goodbye.%d",
                                       ctx->rank) < 0)
                    err_exit ("flux_request_send");
            }
            if (ctx->verbose)
                msg ("%s: disconnect %s, connect %s", __FUNCTION__,
                    ctx->parent[ctx->parent_cur].upreq_uri,
                    ctx->parent[i].upreq_uri);
            if (zsocket_disconnect (ctx->zs_upreq_out, "%s",
                              ctx->parent[ctx->parent_cur].upreq_uri) < 0)
                err_exit ("zsocket_disconnect");
            if (zsocket_disconnect (ctx->zs_dnreq_in, "%s",
                                ctx->parent[ctx->parent_cur].dnreq_uri)< 0)
                err_exit ("zsocket_disconnect");
            if (zsocket_connect (ctx->zs_upreq_out, "%s",
                                ctx->parent[i].upreq_uri) < 0)
                err_exit ("zsocket_connect");
            if (zsocket_connect (ctx->zs_dnreq_in, "%s",
                                ctx->parent[i].dnreq_uri) < 0)
                err_exit ("zsocket_connect");
            ctx->parent_cur = i;

            usleep (1000*10); /* FIXME: message is lost without this delay */
            if (flux_request_send (ctx->h, NULL, "cmb.connect") < 0)
                err_exit ("flux_request_send");
            break;
        }
    }
}

static void cmb_internal_event (ctx_t *ctx, zmsg_t *zmsg)
{
    char *arg = NULL;

    if (cmb_msg_match_substr (zmsg, "event.live.down.", &arg)) {
        int i, rank = strtoul (arg, NULL, 10);

        for (i = 0; i < ctx->parent_len; i++) {
            if (ctx->parent[i].rank == rank) {
                ctx->parent_alive[i] = false;
                if (i == ctx->parent_cur)
                    _reparent (ctx);
            }
        }

        //route_del_subtree (ctx->rctx, arg);
        free (arg);        
    } else if (cmb_msg_match_substr (zmsg, "event.live.up.", &arg)) {
        int i, rank = strtoul (arg, NULL, 10);
        
        for (i = 0; i < ctx->parent_len; i++) {
            if (ctx->parent[i].rank == rank) {
                ctx->parent_alive[i] = true;
                if (i == 0 && ctx->parent_cur > 0)
                    _reparent (ctx);
            }
        }
        free (arg);
    } else if (cmb_msg_match (zmsg, "event.route.update")) {
        if (ctx->zs_upreq_out)
            if (flux_request_send (ctx->h, NULL, "cmb.route.hello") < 0)
                err_exit ("flux_request_send");
    } else if (cmb_msg_match_substr (zmsg, "event.sched.trigger.", &arg)) {
        ctx->epoch = strtoul (arg, NULL, 10);
        free (arg);
    }
}

static char *cmb_getattr (ctx_t *ctx, const char *name)
{
    char *val = NULL;
    if (!strcmp (name, "cmbd-snoop-uri"))
        val = ctx->uri_snoop;
    return val;
}

static void cmb_internal_request (ctx_t *ctx, zmsg_t **zmsg)
{
    char *arg = NULL;
    bool handled = true;

    if (cmb_msg_match_substr (*zmsg, "cmb.route.add.", &arg)) {
        json_object *request = NULL;
        const char *gw = NULL, *parent = NULL;
        int flags = 0;

        if (cmb_msg_decode (*zmsg, NULL, &request) == 0 && request != NULL) {
            (void)util_json_object_get_string (request, "gw", &gw);
            (void)util_json_object_get_string (request, "parent", &parent);
            (void)util_json_object_get_int (request, "flags", &flags);
            if (gw)
                route_add (ctx->rctx, arg, gw, parent, flags);
        }
        if (request)
            json_object_put (request);
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.del.", &arg)) {
        json_object *request = NULL;
        const char *gw = NULL;

        if (cmb_msg_decode (*zmsg, NULL, &request) == 0 && request != NULL) {
            (void)util_json_object_get_string (request, "gw", &gw);
            if (gw)
                route_del (ctx->rctx, arg, gw);
        }
        if (request)
            json_object_put (request);
    } else if (cmb_msg_match (*zmsg, "cmb.route.query")) {
        json_object *response = util_json_object_new_object ();

        json_object_object_add (response , "route",
                                route_dump_json (ctx->rctx, true));
        if (flux_respond (ctx->h, zmsg, response) < 0)
            err_exit ("flux_respond");
        json_object_put (response);
    } else if (cmb_msg_match (*zmsg, "cmb.route.hello")) {
        route_add_hello (ctx->rctx, *zmsg, 0);
        if (ctx->zs_upreq_out)
            zmsg_send (zmsg, ctx->zs_upreq_out); /* fwd upstream */
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.goodbye.", &arg)) {
        //route_del_subtree (ctx->rctx, arg);
        if (ctx->zs_upreq_out)
            zmsg_send (zmsg, ctx->zs_upreq_out); /* fwd upstream */
    } else if (cmb_msg_match (*zmsg, "cmb.connect")) {
        if (ctx->epoch > 2)
            flux_event_send (ctx->h, NULL, "event.route.update");
    } else if (cmb_msg_match (*zmsg, "cmb.info")) {
        json_object *response = util_json_object_new_object ();

        util_json_object_add_int (response, "rank", ctx->rank);
        util_json_object_add_int (response, "size", ctx->size);
        util_json_object_add_boolean (response, "treeroot", ctx->treeroot);
        if (flux_respond (ctx->h, zmsg, response) < 0)
            err_exit ("flux_respond");
        json_object_put (response);
    } else if (cmb_msg_match (*zmsg, "cmb.getattr")) {
        json_object *request = NULL;
        json_object *response = util_json_object_new_object ();
        const char *name = NULL;
        char *val = NULL;

        if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
                || util_json_object_get_string (request, "name", &name) < 0) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else if (!(val = cmb_getattr (ctx, name))) {
            flux_respond_errnum (ctx->h, zmsg, ESRCH);
        } else {
            util_json_object_add_string (response, (char *)name, val);
            flux_respond (ctx->h, zmsg, response);
        }
        if (request)
            json_object_put (request);
        if (response)
            json_object_put (response);
    } else if (cmb_msg_match (*zmsg, "cmb.rusage")) {
        json_object *response;
        struct rusage usage;

        if (getrusage (RUSAGE_THREAD, &usage) < 0) {
            if (flux_respond_errnum (ctx->h, zmsg, errno) < 0)
                err_exit ("flux_respond_errnum");
        } else {
            response = util_json_object_new_object ();
            util_json_object_add_timeval (response, "utime", &usage.ru_utime);
            util_json_object_add_timeval (response, "stime", &usage.ru_stime);
            util_json_object_add_int64 (response, "maxrss", usage.ru_maxrss);
            util_json_object_add_int64 (response, "ixrss", usage.ru_ixrss);
            util_json_object_add_int64 (response, "idrss", usage.ru_idrss);
            util_json_object_add_int64 (response, "isrss", usage.ru_isrss);
            util_json_object_add_int64 (response, "minflt", usage.ru_minflt);
            util_json_object_add_int64 (response, "majflt", usage.ru_majflt);
            util_json_object_add_int64 (response, "nswap", usage.ru_nswap);
            util_json_object_add_int64 (response, "inblock", usage.ru_inblock);
            util_json_object_add_int64 (response, "oublock", usage.ru_oublock);
            util_json_object_add_int64 (response, "msgsnd", usage.ru_msgsnd);
            util_json_object_add_int64 (response, "msgrcv", usage.ru_msgrcv);
            util_json_object_add_int64 (response, "nsignals", usage.ru_nsignals);
            util_json_object_add_int64 (response, "nvcsw", usage.ru_nvcsw);
            util_json_object_add_int64 (response, "nivcsw", usage.ru_nivcsw);
            if (flux_respond (ctx->h, zmsg, response) < 0)
                err_exit ("flux_respond");
            json_object_put (response);
        }
    } else
        handled = false;

    if (arg)
        free (arg);
    /* If zmsg is not destroyed, route_request() will respond with ENOSYS */
    if (handled && *zmsg)
        zmsg_destroy (zmsg);
}

/* Parse message request tag into addr, service.
 */
static int parse_message_tag (zmsg_t *zmsg, char **ap, char **sp)
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

static void route_response (ctx_t *ctx, zmsg_t **zmsg, bool dnsock)
{
    snoop_cc (ctx, FLUX_MSGTYPE_RESPONSE, *zmsg);

    /* case 1: responses heading upward on the 'dnreq' flow can reverse
     * direction if they are traversing the tree.
     * Need to consult routing tables.  flux_response_sendmsg() does that.
     */
    if (dnsock) {
        if (flux_response_sendmsg (ctx->h, zmsg) < 0)
            err ("%s: flux_response_sendmsg", __FUNCTION__);

    /* case 2: responses headed downward on 'upreq' flow must continue down.
     * Ignore routing tables.
     */
    } else if (ctx->zs_upreq_in) {
        if (zmsg_send (zmsg, ctx->zs_upreq_in) < 0)
            err ("%s: zmsg_send(zs_upreq_in)", __FUNCTION__);

    } else
        errn (EHOSTUNREACH, "%s", __FUNCTION__); /* DROP */

    if (*zmsg)
        zmsg_destroy (zmsg);
}

static void route_request (ctx_t *ctx, zmsg_t **zmsg, bool dnsock)
{
    char *myaddr = NULL, *addr = NULL, *service = NULL, *lasthop = NULL;
    const char *gw;

    if (asprintf (&myaddr, "%d", ctx->rank) < 0)
        oom ();
    snoop_cc (ctx, FLUX_MSGTYPE_REQUEST, *zmsg);

    if (parse_message_tag (*zmsg, &addr, &service) < 0) {
        errn (EPROTO, "%s: parse_message_tag failed", __FUNCTION__);
        goto done;
    }
    if (!(lasthop = cmb_msg_nexthop (*zmsg))) {
        errn (EPROTO, "%s: cmb_msg_nexthop failed", __FUNCTION__);
        goto done;
    }

    /* case 1: request explicitly addressed to me, tag == mynode!service.
     * Lookup service in routing table and if found, route down, else NAK.
     * Handle tag == mynode!cmb as a special case.
     */
    if (addr && !strcmp (addr, myaddr)) {
        if (!strcmp (service, "cmb")) {
            cmb_internal_request (ctx, zmsg);
        } else if ((gw = route_lookup (ctx->rctx, service))) {
            if (zmsg_send_unrouter (zmsg, ctx->zs_dnreq_out, ctx->rank, gw) < 0)
                err ("%s: zmsg_send_unrouter(zs_dnreq_out)", __FUNCTION__);
        } /* else (silently) NAK */

    /* case 2: request explicitly addressed to a remote node, tag == N!service.
     * Lookup N and route down if found, route up if not found, or NAK at root.
     */
    } else if (addr) {
        if ((gw = route_lookup (ctx->rctx, addr))) {
            if (zmsg_send_unrouter (zmsg, ctx->zs_dnreq_out, ctx->rank, gw) < 0)
                err ("%s: zmsg_send_unrouter(zs_dnreq_out)", __FUNCTION__);
        } else if (!dnsock && ctx->zs_upreq_out) {
            if (zmsg_send (zmsg, ctx->zs_upreq_out) < 0)
                err ("%s: zmsg_send(zs_upreq_out", __FUNCTION__);
        } /* else (silently) NAK */

    /* case 3: request not addressed, e.g. tag == service.
     * Lookup service and route down if found (and not looping back to sender).
     * Route up if not found (or loop), or NAK at root.
     * Handle tag == cmb as a special case.
     */
    } else {
        if (!strcmp (service, "cmb")) {
            cmb_internal_request (ctx, zmsg);
        } else {
            gw = route_lookup (ctx->rctx, service);
            if (gw && (!lasthop || strcmp (gw, lasthop)) != 0) {
                if (zmsg_send_unrouter (zmsg, ctx->zs_dnreq_out, ctx->rank, gw) < 0)
                    err ("%s: zmsg_send_unrouter(zs_dnreq_out)", __FUNCTION__);
            } else if (!dnsock && ctx->zs_upreq_out) {
                if (zmsg_send (zmsg, ctx->zs_upreq_out) < 0)
                    err ("%s: zmsg_send(zs_upreq_out)", __FUNCTION__);
            } /* else (silently) NAK */
        }
    }

    /* send NAK reply if message was not routed above */
    if (*zmsg) {
        if (flux_respond_errnum (ctx->h, zmsg, ENOSYS) < 0)
            err_exit ("%s: flux_respond", __FUNCTION__);
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
    if (myaddr)
        free (myaddr);
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

static void cmb_poll (ctx_t *ctx)
{
    zmq_pollitem_t zpa[] = {
{ .socket = ctx->zs_upreq_in,   .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = ctx->zs_upreq_out,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = ctx->zs_dnreq_in,   .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = ctx->zs_dnreq_out,  .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = ctx->zs_dnev_in,    .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = ctx->zs_upev_in,    .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    zmsg_t *zmsg = NULL;

    if (zmq_poll (zpa, sizeof (zpa) / sizeof (zpa[0]), -1) < 0)
        err_exit ("zmq_poll");

    /* request on upreq_in */
    if (zpa[0].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (ctx->zs_upreq_in);
        if (zmsg)
            route_request (ctx, &zmsg, false);
    }
    /* response on upreq_out */
    if (zpa[1].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (ctx->zs_upreq_out);
        if (zmsg)
            route_response (ctx, &zmsg, false);
    }
    /* request on dnreq_in */
    if (zpa[2].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (ctx->zs_dnreq_in);
        if (zmsg)
            route_request (ctx, &zmsg, true);
    }
    /* repsonse on dnreq_out */
    if (zpa[3].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv_unrouter (ctx->zs_dnreq_out);
        if (zmsg)
            route_response (ctx, &zmsg, true);
    }
    /* event on dnev_in */
    if (zpa[4].revents & ZMQ_POLLIN) {
        zmsg = zmsg_recv (ctx->zs_dnev_in);
        if (zmsg) {
            cmb_internal_event (ctx, zmsg);
            snoop_cc (ctx, FLUX_MSGTYPE_EVENT, zmsg);
            zmsg_cc (zmsg, ctx->zs_dnev_out);
            cmbd_upev_sendmsg (ctx, &zmsg);
        }
    }
    /* event on upev_in */
    if (zpa[5].revents & ZMQ_POLLIN) {
        zmsg = cmbd_upev_recvmsg (ctx);
        if (zmsg) {
            cmb_internal_event (ctx, zmsg);
            snoop_cc (ctx, FLUX_MSGTYPE_EVENT, zmsg);
            zmsg_send (&zmsg, ctx->zs_dnev_out);
        }
    }

    assert (zmsg == NULL);
}

#if HAVE_MUNGE_SECURITY
/* Events sent on the PGM multicast overlay plane are encrypted with MUNGE.
 * Start with two-part zmsg: Tag + json.
 * Serialize it to string form with zmsg_encode().
 * Transform that (as payload) to a MUNGE cred string with munge_encode().
 * Now replace the json frame in the original message with the munge cred.
 * Thus the final message is cleartext tag + munge cred.
 * N.B.: Confidentiality of payload is obtained in addition to the usual
 * authentication, since MUNGE context was created with
 * MUNGE_OPT_UID_RESTRICTION set to cmbd's effective UID.
 */
static void zmsg_repl_zf2 (zmsg_t *zmsg, zframe_t *zf)
{
    zframe_t *lf = zmsg_last (zmsg);
    assert (lf != NULL);
    zmsg_remove (zmsg, lf);
    zframe_destroy (&lf);
    if (zmsg_add (zmsg, zf))
        oom ();
}

static int event_encode (ctx_t *ctx, zmsg_t *zmsg)
{
    char *buf = NULL, *cr = NULL;
    munge_err_t e;
    zframe_t *zf;
    size_t len;
    int rc = -1;

    assert (zmsg != NULL);
    assert (zmsg_size (zmsg) == 2); /* tag + json */

    if ((len  = zmsg_encode (zmsg, (byte **)&buf)) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: zmsg_encode failed", __FUNCTION__);
        goto done;
    }
    if ((e = munge_encode (&cr, ctx->mctx, buf, len)) != EMUNGE_SUCCESS) {
        flux_log (ctx->h, LOG_ERR, "%s: munge_encode failed: %s",
                  __FUNCTION__, munge_strerror (e));
        goto done;
    }
    if (!(zf = zframe_new (cr, strlen (cr) + 1)))
        oom ();
    zmsg_repl_zf2 (zmsg, zf);
    assert (zmsg_size (zmsg) == 2);
    rc = 0;
done:
    if (buf)
        free (buf);
    if (cr)
        free (cr);
    return rc;
}

static int event_decode (ctx_t *ctx, zmsg_t *zmsg)
{
    char *buf = NULL, *tag = NULL, *tag2 = NULL;
    zmsg_t *zmsg2 = NULL;
    munge_err_t e;
    zframe_t *zf, *zf2;
    int len;
    int rc = -1;

    if (zmsg_size (zmsg) != 2 || !(tag = cmb_msg_tag (zmsg, false))
           || !(zf = zmsg_last (zmsg)) || zframe_size (zf) < 1
           || (*(zframe_data (zf) + zframe_size (zf) - 1) != '\0')) {
        flux_log (ctx->h, LOG_ERR, "%s: malformed message", __FUNCTION__);
        goto done;
    }
    if ((e = munge_decode ((char *)zframe_data (zf), ctx->mctx,
                    (void *)&buf, &len, NULL, NULL)) != EMUNGE_SUCCESS) {
        flux_log (ctx->h, LOG_ERR, "%s: munge_decode failed: %s",
                  __FUNCTION__, munge_strerror (e));
        goto done;
    }
    if (!(zmsg2 = zmsg_decode ((byte *)buf, len))) {
        flux_log (ctx->h, LOG_ERR, "%s: zmsg_decode failed", __FUNCTION__);
        goto done;
    }
    if (zmsg_size (zmsg2) != 2 || !(tag2 = cmb_msg_tag (zmsg2, false))
            || strcmp (tag, tag2) != 0 || !(zf2 = zmsg_last (zmsg2))) {
        flux_log (ctx->h, LOG_ERR, "%s: malformed payload", __FUNCTION__);
        goto done;
    }
    zmsg_remove (zmsg2, zf2);
    zmsg_repl_zf2 (zmsg, zf2);
    rc = 0;
done:
    if (buf)
        free (buf);
    if (tag)
        free (tag);
    if (tag2)
        free (tag2);
    if (zmsg2)
        zmsg_destroy (&zmsg2); 
    return rc;
}
#endif

static int cmbd_upev_sendmsg (ctx_t *ctx, zmsg_t **zmsg)
{
    int rc = -1;

    if (ctx->zs_upev_out) {
#if HAVE_MUNGE_SECURITY
        if (!ctx->security_disable && ctx->upev_mcast) {
            if (event_encode (ctx, *zmsg) < 0)
                goto done;
        }
#endif
        rc = zmsg_send (zmsg, ctx->zs_upev_out);
    }
done:
    if (*zmsg)
        zmsg_destroy (zmsg);
    return rc;
}

static zmsg_t *cmbd_upev_recvmsg (ctx_t *ctx)
{
    zmsg_t *zmsg = NULL;

    if (ctx->zs_upev_in) {
        if (!(zmsg = zmsg_recv (ctx->zs_upev_in)))
            goto done;
#if HAVE_MUNGE_SECURITY
        if (!ctx->security_disable && ctx->upev_mcast) {
            if (event_decode (ctx, zmsg) < 0) {
                zmsg_destroy (&zmsg);
                goto done;
            }
        }
#endif
    }
done:
    return zmsg;
}

static int snoop_cc (ctx_t *ctx, int type, zmsg_t *zmsg)
{
    zmsg_t *cpy;
    char *typestr, *tag, *tagp;

    if (!zmsg)
        return 0;
    if (!(cpy = zmsg_dup (zmsg)))
        err_exit ("zmsg_dup");
    if (asprintf (&typestr, "%d", type) < 0)
        oom ();
    if (zmsg_pushstr (cpy, typestr) < 0)
        oom ();
    free (typestr);
    tag = cmb_msg_tag (zmsg, false);
    tagp = strchr (tag, '!');
    if (zmsg_pushstr (cpy, tagp ? tagp + 1 : tag) < 0)
        oom ();
    free (tag);

    return zmsg_send (&cpy, ctx->zs_snoop);
}


/* flux_t handle operations
 * (routing logic needs to access sockets directly, but handle is still
 * useful in handling internal "services" and making some higher level
 * calls such as flux_log().
 * NOTE: Avoid making blocking RPC calls in the cmbd poll loop!
 */

static int cmbd_request_sendmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *ctx = impl;
    char *addr = NULL, *service = NULL;
    const char *gw;
    int rc = -1;

    if (parse_message_tag (*zmsg, &addr, &service) < 0)
        goto done;
    if (addr) { /* FIXME: N!tag style addressing currently unsupported here */
        errno = EHOSTUNREACH;
        goto done;
    }
    gw = route_lookup (ctx->rctx, service);
    if (gw) {
        if (zmsg_send_unrouter (zmsg, ctx->zs_dnreq_out, ctx->rank, gw) < 0)
            goto done;
    } else if (ctx->zs_upreq_out) {
        if (zmsg_send (zmsg, ctx->zs_upreq_out) < 0)
            goto done;
    } else  {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = 0;
done:
    if (addr)
        free (addr);
    if (service)
        free (service);
    return rc;
}

static int cmbd_response_sendmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *ctx = impl;
    int rc = -1;
    char *nexthop;

    if (!(nexthop = cmb_msg_nexthop (*zmsg))) {
        if (errno == 0)
            errno = EPROTO;
        goto done;
    }
    if (route_lookup (ctx->rctx, nexthop)) {
        if (zmsg_send (zmsg, ctx->zs_upreq_in) < 0)
            goto done;
    } else if (ctx->zs_dnreq_in) {
        if (zmsg_send (zmsg, ctx->zs_dnreq_in) < 0)
            goto done;
    } else {
        errno = EHOSTUNREACH;
        goto done; /* DROP */
    }
    rc = 0;
done:
    if (nexthop)
        free (nexthop);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return rc;
}

static int cmbd_event_sendmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *ctx = impl;

    snoop_cc (ctx, FLUX_MSGTYPE_EVENT, *zmsg);
    if (ctx->zs_dnev_out)
        zmsg_cc (*zmsg, ctx->zs_dnev_out);
    return cmbd_upev_sendmsg (ctx, zmsg);
}

static int cmbd_rank (void *impl)
{
    ctx_t *ctx = impl;
    return ctx->rank;
}

static zctx_t *cmbd_get_zctx (void *impl)
{
    ctx_t *ctx = impl;
    return ctx->zctx;
}

static const struct flux_handle_ops cmbd_handle_ops = {
    .request_sendmsg = cmbd_request_sendmsg,
    .response_sendmsg = cmbd_response_sendmsg,
    .event_sendmsg = cmbd_event_sendmsg,
    .rank = cmbd_rank,
    .get_zctx = cmbd_get_zctx,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
