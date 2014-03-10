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
#include <sys/prctl.h>
#include <sys/signalfd.h>
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

#include "log.h"
#include "zmsg.h"
#include "route.h"
#include "util.h"
#include "plugin.h"
#include "hljson.h"
#include "flux.h"
#include "handle.h"
#include "cmb_socket.h"
#include "security.h"

#ifndef ZMQ_IMMEDIATE
#define ZMQ_IMMEDIATE           ZMQ_DELAY_ATTACH_ON_CONNECT
#define zsocket_set_immediate   zsocket_set_delay_attach_on_connect
#endif

#define ZLOOP_RETURN(p) \
    return ((ctx)->reactor_stop ? (-1) : (0))

#define MAX_PARENTS 2
struct parent_struct {
    char *uri;
    int rank;
    char *id;
    bool alive;
};

typedef struct {
    /* 0MQ
     */
    zctx_t *zctx;               /* zeromq context (MT-safe) */
    zloop_t *zl;
    bool reactor_stop;
    int sigfd;
    flux_sec_t sec;
    bool security_clr;
    bool security_set;

    /* Sockets.
     */
    void *zs_parent;            /* DEALER - requests to parent */
    void *zs_request;           /* ROUTER - requests from plugins/downstream */
    void *zs_plugins;           /* rROUTER - requests to plugins */
    zlist_t *uri_request;

    char *uri_event_in;         /* SUB - to event module's ipc:// socket */
    void *zs_event_in;          /*       (event module takes care of epgm) */
    void *zs_event_out;         /* PUB - to plugins */

    char *uri_snoop;            /* PUB - to flux-snoop (uri is generated) */
    void *zs_snoop;

    /* Wireup
     */
    struct parent_struct parent[MAX_PARENTS]; /* configured parents */
    int parent_len;             /* length of parent_struct array */
    int parent_cur;             /* current parent */
    /* Session parameters
     */
    bool treeroot;              /* true if we are the root of reduction tree */
    int rank;                   /* our rank in session */
    char *id;
    int size;                   /* session size */
    char *session_name;
    /* Plugins
     */
    char *plugin_path;          /* colon-separated list of directories */
    char *plugins;              /* comma-separated list of plugins to load */
    zhash_t *loaded_plugins;    /* hash of plugin handles by plugin name */
    zhash_t *plugin_args;
    /* Misc
     */
    bool verbose;               /* enable debug to stderr */
    route_ctx_t rctx;           /* routing table */
    int epoch;                  /* current heartbeat epoch */
    flux_t h;
    pid_t pid;
    char hostname[HOST_NAME_MAX + 1];
} ctx_t;

static int snoop_cc (ctx_t *ctx, int type, zmsg_t *zmsg);

static int event_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int plugins_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int parent_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int request_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int signal_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);

static void cmb_event_geturi_request (ctx_t *ctx);

static void cmbd_init (ctx_t *ctx);
static void cmbd_fini (ctx_t *ctx);

#define OPTIONS "t:vR:S:p:P:L:H:N:nci"
static const struct option longopts[] = {
    {"session-name",    required_argument,  0, 'N'},
    {"no-security",     no_argument,        0, 'n'},
    {"request-uri",     required_argument,  0, 't'},
    {"verbose",         no_argument,        0, 'v'},
    {"plain-insecurity",no_argument,        0, 'i'},
    {"curve-security",  no_argument,        0, 'c'},
    {"rank",            required_argument,  0, 'R'},
    {"size",            required_argument,  0, 'S'},
    {"parent",          required_argument,  0, 'p'},
    {"plugins",         required_argument,  0, 'P'},
    {"logdest",         required_argument,  0, 'L'},
    {"hostlist",        required_argument,  0, 'H'},
    {0, 0, 0, 0},
};

static const struct flux_handle_ops cmbd_handle_ops;

static void usage (void)
{
    fprintf (stderr, 
"Usage: cmbd OPTIONS --plugins name[,name,...] [plugin:key=val ...]\n"
" -t,--request-uri URI[,...]   Set URIs to listen for requests\n"
" -p,--parent N,URI            Set parent rank,URI to connect for requests\n"
" -v,--verbose                 Be chatty\n"
" -H,--hostlist HOSTLIST       Set session hostlist (sets 'hosts' in the KVS)\n"
" -R,--rank N                  Set cmbd rank (0...size-1)\n"
" -S,--size N                  Set number of ranks in session\n"
" -N,--session-name NAME       Set session name (default: flux)\n"
" -P,--plugins name[,name,...] Load the named plugins (comma separated)\n"
" -X,--plugin-path PATH        Set plugin search path (colon separated)\n"
" -L,--logdest DEST            Log to DEST, can  be syslog, stderr, or file\n"
" -c,--curve-security          Use CURVE security (default)\n"
" -i,--plain-insecurity        Use PLAIN security instead of CURVE\n"
" -n,--no-security             Disable session security\n"
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
    if (!(ctx.plugin_args = zhash_new ()))
        oom ();
    if (!(ctx.uri_request = zlist_new ()))
        oom ();
    zlist_autofree (ctx.uri_request);
    ctx.session_name = "flux";
    if (gethostname (ctx.hostname, HOST_NAME_MAX) < 0)
        err_exit ("gethostname");
    ctx.pid = getpid();

    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'N':   /* --session-name NAME */
                ctx.session_name = optarg;
                break;
            case 'n':   /* --no-security */
                ctx.security_clr = FLUX_SEC_TYPE_ALL;
                break;
            case 'c':   /* --curve-security */
                ctx.security_set |= FLUX_SEC_TYPE_CURVE;
                break;
            case 'i':   /* --plain-insecurity */
                ctx.security_set |= FLUX_SEC_TYPE_PLAIN;
                break;
            case 't': { /* --request-uri URI[,URI,...] */
                char *cpy = xstrdup (optarg);
                char *uri, *saveptr, *a1 = cpy;

                while ((uri = strtok_r (a1, ",", &saveptr))) {
                    zlist_push (ctx.uri_request, xstrdup (uri));
                    a1 = NULL;
                }
                free (cpy);
                break;
            }
            case 'p': { /* --parent rank,uri */
                char *p = NULL, *ac = xstrdup (optarg);
                if (ctx.parent_len == MAX_PARENTS)
                    msg_exit ("too many --parent's, max %d", MAX_PARENTS);
                ctx.parent[ctx.parent_len].rank = strtoul (ac, &p, 10);
                ctx.parent[ctx.parent_len].id = ac; /* string form of rank */
                if (!p || *p != ',')
                    msg_exit ("malformed -p option");
                *p++ = '\0';
                ctx.parent[ctx.parent_len].uri = xstrdup (p);
                ctx.parent_len++;
                break;
            }
            case 'v':   /* --verbose */
                ctx.verbose = true;
                break;
            case 'H': { /* --hostlist hostlist */
                json_object *o = hostlist_to_json (optarg);
                const char *val = json_object_to_json_string (o);

                zhash_update (ctx.plugin_args, "kvs:hosts", xstrdup (val));
                zhash_freefn (ctx.plugin_args, "kvs:hosts", free);
                json_object_put (o);
                break;
            }
            case 'R':   /* --rank N */
                ctx.rank = strtoul (optarg, NULL, 10);
                ctx.id = xstrdup (optarg);
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
            default:
                usage ();
        }
    }
    /* Remaining args are for plugins.
     */
    for (i = optind; i < argc; i++) {
        char *p, *cpy = xstrdup (argv[i]);
        if ((p = strchr (cpy, '='))) {
            *p++ = '\0';
            zhash_update (ctx.plugin_args, cpy, xstrdup (p));
            zhash_freefn (ctx.plugin_args, cpy, free);
        }
        free (cpy);
    }
    if (!ctx.plugins)
        usage ();
    if (!ctx.plugin_path)
        ctx.plugin_path = PLUGIN_PATH; /* compiled in default */

    if (zlist_size (ctx.uri_request) == 0) {
        if (zlist_push (ctx.uri_request, xstrdup ("tcp://*:5556")) < 0)
            oom ();
    }

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

    char *proctitle;
    if (asprintf (&proctitle, "cmbd-%d", ctx.rank) < 0)
        oom ();
    (void)prctl (PR_SET_NAME, proctitle, 0, 0, 0);
    free (proctitle);

    cmbd_init (&ctx);

    zloop_start (ctx.zl);
    msg ("zloop reactor stopped");

    cmbd_fini (&ctx);

    for (i = 0; i < ctx.parent_len; i++) {
        free (ctx.parent[i].uri);
        free (ctx.parent[i].id);
    }
    if (ctx.id)
        free (ctx.id);
    if (ctx.plugin_args)
        zhash_destroy (&ctx.plugin_args);
    if (ctx.uri_request)
        zlist_destroy (&ctx.uri_request);

    return 0;
}

static int load_plugin (ctx_t *ctx, char *name)
{
    char *uuid = uuid_generate_str ();
    plugin_ctx_t p;
    int rc = -1;

    if ((p = plugin_load (ctx->h, ctx->plugin_path, name, uuid,
                          ctx->plugin_args))) {
        if (zhash_insert (ctx->loaded_plugins, name, p) < 0) {
            plugin_unload (p);
            goto done;
        }
        route_add (ctx->rctx, name, uuid, NULL, ROUTE_FLAGS_PRIVATE);
        rc = 0;
    }
done:
    free (uuid);
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

void unload_plugins (ctx_t *ctx)
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

static void *cmbd_init_request (ctx_t *ctx)
{
    void *s;
    char *uri;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(s = zsocket_new (ctx->zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
    if (flux_sec_ssockinit (ctx->sec, s) < 0)
        msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ctx->sec));
    zsocket_set_hwm (s, 0);
    if (zsocket_bind (s, "%s", UPREQ_URI) < 0) /* always bind to inproc */
        err_exit ("%s", UPREQ_URI);
    uri = zlist_first (ctx->uri_request);
    while (uri) {
        if (check_uri (uri)) {
            if (zsocket_bind (s, "%s", uri) < 0)
                err_exit ("%s", uri);
        }
        uri = zlist_next (ctx->uri_request);
    }
    zp.socket = s;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)request_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return s;
}

static void *cmbd_init_plugins (ctx_t *ctx)
{
    void *s;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    /* Bind only to inproc://dnreq for sending requests to plugins.
     * No security.
     */
    if (!(s = zsocket_new (ctx->zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (s, 0);
    if (zsocket_bind (s, "%s", DNREQ_URI) < 0)
        err_exit ("%s", DNREQ_URI);
    zp.socket = s;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)plugins_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return s;
}

static void *cmbd_init_event_out (ctx_t *ctx)
{
    void *s;

    /* Bind only to inproc://event for publishing to plugins.
     * No security.
     */
    if (!(s = zsocket_new (ctx->zctx, ZMQ_PUB)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (s, 0);
    if (zsocket_bind (s, "%s", EVENT_URI) < 0)
        err_exit ("%s", EVENT_URI);
    return s;
}

static void *cmbd_init_snoop (ctx_t *ctx)
{
    void *s;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_PUB)))
        err_exit ("zsocket_new");
    if (flux_sec_ssockinit (ctx->sec, s) < 0)
        msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ctx->sec));
    /* Dynamically allocate the snoop URI in the ipc:// space.
     * Make it available to clients via the flux_getattr().
     */
    assert (ctx->uri_snoop == NULL);
    if (zsocket_bind (s, "%s", "ipc://*") < 0)
        err_exit ("%s", "ipc://*");
    ctx->uri_snoop = zsocket_last_endpoint (s); /* need to xstrdup? */
    return s;
}

static void *cmbd_init_event_in (ctx_t *ctx)
{
    void *s;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(s = zsocket_new (ctx->zctx, ZMQ_SUB)))
        err_exit ("zsocket_new");
    if (flux_sec_csockinit (ctx->sec, s) < 0)
        msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ctx->sec));
    zsocket_set_hwm (s, 0);
    if (zsocket_connect (s, "%s", ctx->uri_event_in) < 0)
        err_exit ("%s", ctx->uri_event_in);
    zsocket_set_subscribe (s, "");
    zp.socket = s;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)event_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return s;
}

static void *cmbd_init_parent (ctx_t *ctx) {
    void *s;
    char id[16];
    char *uri = ctx->parent[ctx->parent_cur].uri;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(s = zsocket_new (ctx->zctx, ZMQ_DEALER)))
        err_exit ("zsocket_new");
    if (flux_sec_csockinit (ctx->sec, s) < 0)
        msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ctx->sec));
    zsocket_set_hwm (s, 0);
    snprintf (id, sizeof (id), "%d", ctx->rank);
    zsocket_set_identity (s, id); 
    if (zsocket_connect (s, "%s", uri) < 0)
        err_exit ("%s", uri);
    zp.socket = s;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)parent_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return s;
}

/* signalfd + zloop example: https://gist.github.com/mhaberler/8426050
 */
static int cmbd_init_signalfd (ctx_t *ctx)
{
    sigset_t sigmask;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .socket = NULL };

    zsys_handler_set (NULL);
    sigemptyset(&sigmask);
    sigfillset(&sigmask);
    if (sigprocmask (SIG_SETMASK, &sigmask, NULL) < 0)
        err_exit ("sigprocmask");
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGHUP);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGQUIT);
    sigaddset(&sigmask, SIGTERM);
    sigaddset(&sigmask, SIGSEGV);
    sigaddset(&sigmask, SIGFPE);
    sigaddset(&sigmask, SIGCHLD);

    if ((zp.fd = signalfd(-1, &sigmask, SFD_NONBLOCK | SFD_CLOEXEC)) < 0)
        err_exit ("signalfd");
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)signal_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return zp.fd;
}

static void cmbd_init (ctx_t *ctx)
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
    if (!(ctx->zl = zloop_new ()))
        err_exit ("zloop_new");
    //if (ctx->verbose)
    //    zloop_set_verbose (ctx->zl, true);
    ctx->sigfd = cmbd_init_signalfd (ctx);

    /* Initialize security.
     */
    if (!(ctx->sec = flux_sec_create ()))
        err_exit ("flux_sec_create");
    if (ctx->security_clr && flux_sec_disable (ctx->sec, ctx->security_clr) < 0)
        err_exit ("flux_sec_disable");
    if (ctx->security_set && flux_sec_enable (ctx->sec, ctx->security_set) < 0)
        err_exit ("flux_sec_enable");
    if (flux_sec_zauth_init (ctx->sec, ctx->zctx, ctx->session_name) < 0)
        msg_exit ("flux_sec_zauth_init: %s", flux_sec_errstr (ctx->sec));
    if (flux_sec_munge_init (ctx->sec) < 0)
        msg_exit ("flux_sec_munge_init: %s", flux_sec_errstr (ctx->sec));

    /* Bind to downstream ports.
     */
    ctx->zs_request = cmbd_init_request (ctx);
    ctx->zs_plugins = cmbd_init_plugins (ctx);
    ctx->zs_event_out = cmbd_init_event_out (ctx);
    ctx->zs_snoop = cmbd_init_snoop (ctx);
#if 0
    /* Increase max number of sockets and number of I/O thraeds.
     * (N.B. must call zctx_underlying () only after first socket is created)
     */
    if (zmq_ctx_set (zctx_underlying (ctx->zctx), ZMQ_MAX_SOCKETS, 4096) < 0)
        err_exit ("zmq_ctx_set ZMQ_MAX_SOCKETS");
    if (zmq_ctx_set (zctx_underlying (ctx->zctx), ZMQ_IO_THREADS, 4) < 0)
        err_exit ("zmq_ctx_set ZMQ_IO_THREADS");
#endif
    /* Connect to upstream ports.
     */
    for (i = 0; i < ctx->parent_len; i++)
        ctx->parent[i].alive = true;

    if (ctx->parent_len > 0) {
        ctx->zs_parent = cmbd_init_parent (ctx);
    }

    /* create flux_t handle */
    ctx->h = handle_create (ctx, &cmbd_handle_ops, 0);
    flux_log_set_facility (ctx->h, "cmbd");

    /* Contact upstream.
     */
    if (ctx->zs_parent) {
        if (flux_request_send (ctx->h, NULL, "cmb.connect") < 0)
            err_exit ("error sending cmb.connect upstream");
        if (flux_request_send (ctx->h, NULL, "cmb.route.hello") < 0)
            err_exit ("error sending cmb.route.hello upstream");
    }

    ctx->loaded_plugins = zhash_new ();
    load_plugins (ctx);

    flux_log (ctx->h, LOG_INFO, "%s", flux_sec_confstr (ctx->sec));

    /* Start event initialization, complete it when response arrives.
     *   N.B. avoid flux_event_geturi () as zloop isn't running yet.
     */
    cmb_event_geturi_request (ctx);
}

static void cmbd_fini (ctx_t *ctx)
{
    //unload_plugins (ctx);  /* FIXME */
    zhash_destroy (&ctx->loaded_plugins);

    if (ctx->sec)
        flux_sec_destroy (ctx->sec);
    zloop_destroy (&ctx->zl);
    route_fini (ctx->rctx);
    zctx_destroy (&ctx->zctx); /* destorys all sockets created in ctx */
}

static void _reparent (ctx_t *ctx)
{
    int i;

    /* FIXME: possibly need to reconnect to event socket...
     */

    for (i = 0; i < ctx->parent_len; i++) {
        if (i != ctx->parent_cur && ctx->parent[i].alive) {
            flux_log (ctx->h, LOG_ALERT, "reparent %d->%d",
                      ctx->parent[ctx->parent_cur].rank, ctx->parent[i].rank);
            if (ctx->parent[ctx->parent_cur].alive) {
                if (flux_request_send (ctx->h, NULL, "cmb.route.goodbye.%d",
                                       ctx->rank) < 0)
                    err_exit ("flux_request_send");
            }
            if (ctx->verbose)
                msg ("%s: disconnect %s, connect %s", __FUNCTION__,
                    ctx->parent[ctx->parent_cur].uri,
                    ctx->parent[i].uri);
            if (zsocket_disconnect (ctx->zs_parent, "%s",
                              ctx->parent[ctx->parent_cur].uri) < 0)
                err_exit ("zsocket_disconnect");
            if (zsocket_connect (ctx->zs_parent, "%s", ctx->parent[i].uri) < 0)
                err_exit ("zsocket_connect");
            ctx->parent_cur = i;

            /* setsockopt ZMQ_IMMEDIATE should fix this -jg */
            usleep (1000*10); /* FIXME! */
            if (flux_request_send (ctx->h, NULL, "cmb.connect") < 0)
                err_exit ("flux_request_send");
            break;
        }
    }
}

static void cmb_internal_event (ctx_t *ctx, zmsg_t *zmsg)
{
    json_object *event = NULL;

    /* On receipt of a liveness state change, look to see if my parent
     * has changed state and possibly reconnect to a new parent:
     * If current parent goes down, connect to alternate parent.
     * If parent[0] returns to service, connect to that.
     */
    if (cmb_msg_match (zmsg, "live")) {
        bool alive;
        int rank, i;
        if (cmb_msg_decode (zmsg, NULL, &event) == 0 && event != NULL
                && util_json_object_get_boolean (event, "alive", &alive) == 0
                && util_json_object_get_int (event, "rank", &rank) == 0) {
            for (i = 0; i < ctx->parent_len; i++) {
                if (ctx->parent[i].rank == rank) {
                    if (alive) {
                        ctx->parent[i].alive = true;
                        if (i == 0 && ctx->parent_cur > 0)
                            _reparent (ctx);
                    } else {
                        ctx->parent[i].alive = false;
                        //route_del_subtree (ctx->rctx, rank);
                        if (i == ctx->parent_cur)
                            _reparent (ctx);
                    }
                }
            }
        }
    } else if (cmb_msg_match (zmsg, "route.update")) {
        if (ctx->zs_parent)
            if (flux_request_send (ctx->h, NULL, "cmb.route.hello") < 0)
                err_exit ("flux_request_send");
    } else if (cmb_msg_match (zmsg, "hb")) {
        if (cmb_msg_decode (zmsg, NULL, &event) == 0 && event!= NULL)
            (void)util_json_object_get_int (event, "epoch", &ctx->epoch);
    }
    if (event)
        json_object_put (event);
}

/* Send a request to event module for uri to subscribe to.
 * The request will go to local plugin if loaded, else upstream.
 */
static void cmb_event_geturi_request (ctx_t *ctx)
{
    json_object *request = util_json_object_new_object ();

    util_json_object_add_int (request, "pid", ctx->pid);
    util_json_object_add_string (request, "hostname", ctx->hostname);
    if (flux_request_send (ctx->h, request, "event.geturi") < 0)
        err_exit ("%s: flux_request_send", __FUNCTION__);
    json_object_put (request);
}

static int cmb_internal_response (ctx_t *ctx, zmsg_t **zmsg)
{
    json_object *response = NULL;
    int rc = -1;

    if (cmb_msg_match (*zmsg, "event.geturi")) {
        const char *uri;
        if (cmb_msg_decode (*zmsg, NULL, &response) < 0 || !response
                || util_json_object_get_string (response, "uri", &uri) < 0) {
            flux_log (ctx->h, LOG_ERR, "mangled event.geturi response");
            errno = EPROTO;
            goto done; 
        }
        if (ctx->uri_event_in != NULL) {
            flux_log (ctx->h, LOG_ERR, "unexpected event.geturi response");
            errno = EINVAL;
            goto done;
        }
        ctx->uri_event_in = xstrdup (uri);        
        ctx->zs_event_in = cmbd_init_event_in (ctx);
        //flux_log (ctx->h, LOG_INFO, "subscribed to %s", ctx->uri_event_in);
        zmsg_destroy (zmsg);
        rc = 0;
    }
done:
    if (response)
        json_object_put (response);
    return rc;
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
        if (ctx->zs_parent)
            zmsg_send (zmsg, ctx->zs_parent); /* fwd upstream */
    } else if (cmb_msg_match_substr (*zmsg, "cmb.route.goodbye.", &arg)) {
        //route_del_subtree (ctx->rctx, arg);
        if (ctx->zs_parent)
            zmsg_send (zmsg, ctx->zs_parent); /* fwd upstream */
    } else if (cmb_msg_match (*zmsg, "cmb.connect")) {
        if (ctx->epoch > 2)
            flux_event_send (ctx->h, NULL, "route.update");
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
            response = rusage_to_json (&usage);
            if (flux_respond (ctx->h, zmsg, response) < 0)
                err_exit ("flux_respond");
            json_object_put (response);
        }
    } else if (cmb_msg_match (*zmsg, "cmb.disconnect")) {
        zmsg_destroy (zmsg); /* no response */
    } else
        handled = false;

    if (arg)
        free (arg);
    /* If zmsg is not destroyed, route_request() will respond with ENOSYS */
    if (handled && *zmsg)
        zmsg_destroy (zmsg);
}

static int request_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    zmsg_t *zmsg = zmsg_recv (ctx->zs_request);

    if (zmsg) {
        if (flux_request_sendmsg (ctx->h, &zmsg) < 0)
            (void)flux_respond_errnum (ctx->h, &zmsg, errno);
    }
    if (zmsg)
        zmsg_destroy (&zmsg);
    ZLOOP_RETURN(ctx);
}

static int parent_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    zmsg_t *zmsg = zmsg_recv (ctx->zs_parent);

    if (zmsg) {
        (void)flux_response_sendmsg (ctx->h, &zmsg);
    }
    if (zmsg)
        zmsg_destroy (&zmsg);
    ZLOOP_RETURN(ctx);
}

static int plugins_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    zmsg_t *zmsg = zmsg_recv_unrouter (ctx->zs_plugins);

    if (zmsg) {
        (void)flux_response_sendmsg (ctx->h, &zmsg);
    }
    if (zmsg)
        zmsg_destroy (&zmsg);
    ZLOOP_RETURN(ctx);
}

static int event_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    zmsg_t *zmsg = zmsg_recv (ctx->zs_event_in);
    if (zmsg) {
        cmb_internal_event (ctx, zmsg);
        snoop_cc (ctx, FLUX_MSGTYPE_EVENT, zmsg);
        zmsg_send (&zmsg, ctx->zs_event_out);
    }
    ZLOOP_RETURN(ctx);
}

static int signal_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    struct signalfd_siginfo fdsi;
    ssize_t n;

    if ((n = read (item->fd, &fdsi, sizeof (fdsi))) < 0) {
        if (errno != EWOULDBLOCK)
            err_exit ("read");
    } else if (n == sizeof (fdsi)){    
        msg ("signal %d (%s)", fdsi.ssi_signo, strsignal (fdsi.ssi_signo));
        ctx->reactor_stop = true;
    }
    ZLOOP_RETURN(ctx);
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

/* Extract 'servicep' from message topic string.  Caller must free.
 * Return value is -1 (malformed packet), or the hopcount.
 * If retuern value is > 0, '*lasthopp' is set to a copy of the top
 * routing frame and must be freed by the caller.
 * (helper for cmbd_request_sendmsg)
 */
static int parse_request (zmsg_t *zmsg, char **servicep, char **lasthopp)
{
    char *p, *service = NULL, *lasthop = NULL;
    int nf = zmsg_size (zmsg);
    zframe_t *zf, *zf0 = zmsg_first (zmsg);
    int i, hopcount = 0;

    if (nf < 2)
        goto error;
    for (i = 0, zf = zf0; i < nf - 2; i++) {
        if (zframe_size (zf) == 0) { /* delimiter, if any */
            if (i != nf - 3)         /* expected position: zmsg[nf - 3] */
                goto error;
            hopcount = i;
        }
        zf = zmsg_next (zmsg);
    }
    if (!(service = zframe_strdup (zf)))
        goto error;
    if (hopcount > 0 && !(lasthop = zframe_strdup (zf0)))   
        goto error;
    if ((p = strchr (service, '.')))
        *p = '\0';
    *servicep = service;
    *lasthopp = lasthop;
    return hopcount;
error:
    if (service)
        free (service);
    if (lasthop)
        free (lasthop);
    return -1;
}

/**
 ** Cmbd's internal flux_t implementation.
 **    a bit limited, by design
 **/

static int cmbd_request_sendmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *ctx = impl;
    char *service = NULL, *lasthop = NULL;
    int hopcount;
    const char *gw;
    int rc = -1;

    /* FIXME: detect routing loop if hopcount > 2*tree_depth */

    errno = 0;
    if ((hopcount = parse_request (*zmsg, &service, &lasthop)) < 0) {
        errno = EPROTO;
        goto done;
    }

    /* FIXME: avoid sending request to socket without routing envelope */

    snoop_cc (ctx, FLUX_MSGTYPE_REQUEST, *zmsg);

    if (!strcmp (service, "cmb")) {
        if (hopcount > 0) {
            cmb_internal_request (ctx, zmsg);
        } else if (!ctx->treeroot) {
            if (zmsg_send (zmsg, ctx->zs_parent) < 0)
                err ("%s: zs_parent", __FUNCTION__);
        } else
            errno = EINVAL;
    } else {
        if ((gw = route_lookup (ctx->rctx, service))
                            && (!lasthop || strcmp (lasthop, gw) != 0)) {
            if (zmsg_send_unrouter (zmsg, ctx->zs_plugins, ctx->id, gw))
                err ("%s: zs_plugins", __FUNCTION__);
        } else if (!ctx->treeroot) {
            if (zmsg_send (zmsg, ctx->zs_parent) < 0)
                err ("%s: zs_parent", __FUNCTION__);
        } else
            errno = ENOSYS;
    }
done:
    if (*zmsg == NULL) {
        rc = 0;
    } else {
        if (errno == 0)
            errno = ENOSYS;
    }
    if (service)
        free (service);
    if (lasthop)
        free (lasthop);
    return rc;
}

static int cmbd_response_sendmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *ctx = impl;
    int rc = -1;
    char *sender;

    snoop_cc (ctx, FLUX_MSGTYPE_RESPONSE, *zmsg);
    if (!(sender = cmb_msg_nexthop (*zmsg))) {
        errno = EINVAL;
        goto done;
    }
    if (strlen (sender) == 0) { /* empty sender - must be me */
        rc = cmb_internal_response (ctx, zmsg);
        goto done;
    }
    if (zmsg_send (zmsg, ctx->zs_request) < 0)
        goto done;
    rc = 0;
done:
    if (sender)
        free (sender);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return rc;
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

static flux_sec_t cmbd_get_sec (void *impl)
{
    ctx_t *ctx = impl;
    return ctx->sec;
}

static const struct flux_handle_ops cmbd_handle_ops = {
    .request_sendmsg = cmbd_request_sendmsg,
    .response_sendmsg = cmbd_response_sendmsg,
    .rank = cmbd_rank,
    .get_zctx = cmbd_get_zctx,
    .get_sec = cmbd_get_sec,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
