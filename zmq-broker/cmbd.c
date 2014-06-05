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

typedef struct {
    /* 0MQ
     */
    zctx_t *zctx;               /* zeromq context (MT-safe) */
    zloop_t *zl;
    bool reactor_stop;
    int sigfd;
    flux_sec_t sec;             /* security context (MT-safe) */
    bool security_clr;
    bool security_set;
    /* Sockets.
     */
    void *zs_parent;            /* DEALER - requests to parent */
    zlist_t *uri_parent;

    void *zs_child;             /* ROUTER - requests from plugins/downstream */
    zlist_t *uri_child;

    void *zs_plugins;           /* rROUTER - requests to plugins */

    char *uri_event_in;         /* SUB - to event module's ipc:// socket */
    void *zs_event_in;          /*       (event module takes care of epgm) */

    void *zs_event_out;         /* PUB - to plugins */

    char *uri_snoop;            /* PUB - to flux-snoop (uri is generated) */
    void *zs_snoop;
    /* Session parameters
     */
    bool treeroot;              /* true if we are the root of reduction tree */
    int size;                   /* session size */
    int rank;                   /* our rank in session */
    char *rankstr;              /*   string version of above */
    char *session_name;         /* Zauth "domain" (default "flux") */
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
    flux_t h;
    pid_t pid;
    char hostname[HOST_NAME_MAX + 1];
} ctx_t;

static int snoop_cc (ctx_t *ctx, int type, zmsg_t *zmsg);

static int event_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int plugins_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int parent_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int child_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int signal_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);

static void cmb_event_geturi_request (ctx_t *ctx);

static void cmbd_init (ctx_t *ctx);
static void cmbd_fini (ctx_t *ctx);

#define OPTIONS "t:vR:S:p:P:L:H:N:nci"
static const struct option longopts[] = {
    {"session-name",    required_argument,  0, 'N'},
    {"no-security",     no_argument,        0, 'n'},
    {"child-uri",       required_argument,  0, 't'},
    {"parent-uri",      required_argument,  0, 'p'},
    {"verbose",         no_argument,        0, 'v'},
    {"plain-insecurity",no_argument,        0, 'i'},
    {"curve-security",  no_argument,        0, 'c'},
    {"rank",            required_argument,  0, 'R'},
    {"size",            required_argument,  0, 'S'},
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
" -t,--child-uri URI           Set child URI to bind and receive requests\n"
" -p,--parent-uri URI          Set parent URI to connect and send requests\n"
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
    if (!(ctx.uri_child = zlist_new ()))
        oom ();
    zlist_autofree (ctx.uri_child);
    if (!(ctx.uri_parent = zlist_new ()))
        oom ();
    zlist_autofree (ctx.uri_parent);
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
            case 't':   /* --child-uri URI[,URI,...] */
                if (zlist_append (ctx.uri_child, xstrdup (optarg)) < 0)
                    oom ();
                break;
            case 'p':   /* --parent-uri URI */
                if (zlist_append (ctx.uri_parent, xstrdup (optarg)) < 0)
                    oom ();
                break;
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

    if (asprintf (&ctx.rankstr, "%d", ctx.rank) < 0)
        oom ();
    if (ctx.rank == 0)
        ctx.treeroot = true;
    if (ctx.treeroot && zlist_size (ctx.uri_parent) > 0)
        msg_exit ("treeroot must NOT have parents");
    if (!ctx.treeroot && zlist_size (ctx.uri_parent) == 0)
        msg_exit ("non-treeroot must have parents");

    char *proctitle;
    if (asprintf (&proctitle, "cmbd-%d", ctx.rank) < 0)
        oom ();
    (void)prctl (PR_SET_NAME, proctitle, 0, 0, 0);
    free (proctitle);

    cmbd_init (&ctx);

    zloop_start (ctx.zl);
    msg ("zloop reactor stopped");

    cmbd_fini (&ctx);

    if (ctx.rankstr)
        free (ctx.rankstr);
    if (ctx.plugin_args)
        zhash_destroy (&ctx.plugin_args);
    if (ctx.uri_child)
        zlist_destroy (&ctx.uri_child);
    if (ctx.uri_parent)
        zlist_destroy (&ctx.uri_parent);
    return 0;
}

static int load_plugin (ctx_t *ctx, char *name)
{
    zuuid_t *uuid;
    plugin_ctx_t p;
    int rc = -1;

    if (!(uuid = zuuid_new ()))
        oom ();

    if ((p = plugin_load (ctx->h, ctx->plugin_path, name, zuuid_str (uuid),
                          ctx->plugin_args))) {
        if (zhash_insert (ctx->loaded_plugins, name, p) < 0) {
            plugin_unload (p);
            goto done;
        }
        route_add (ctx->rctx, name, zuuid_str (uuid), NULL,
                   ROUTE_FLAGS_PRIVATE);
        rc = 0;
    }
done:
    zuuid_destroy (&uuid);
    return rc;
}

static void unload_plugin (ctx_t *ctx, plugin_ctx_t p)
{
    const char *name = plugin_name (p);
    const char *id = plugin_id (p);

    if (name && id)
        route_del (ctx->rctx, name, id);
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

static void *cmbd_init_child (ctx_t *ctx)
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
    uri = zlist_first (ctx->uri_child);
    for (; uri != NULL; uri = zlist_next (ctx->uri_child)) {
        if (zsocket_bind (s, "%s", uri) < 0)
            err_exit ("%s", uri);
    }
    zp.socket = s;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)child_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return s;
}

static void *cmbd_init_plugins (ctx_t *ctx)
{
    void *s;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

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
    assert (ctx->uri_snoop == NULL);
    if (zsocket_bind (s, "%s", "ipc://*") < 0)
        err_exit ("%s", "ipc://*");
    ctx->uri_snoop = zsocket_last_endpoint (s);
    return s;
}

/* This isn't called until we have a response to event.geturi request,
 * which fills in ctx->uri_event_in.
 */
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
    void *s = NULL;
    char *uri = zlist_first (ctx->uri_parent);
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (uri) {
        if (!(s = zsocket_new (ctx->zctx, ZMQ_DEALER)))
            err_exit ("zsocket_new");
        if (flux_sec_csockinit (ctx->sec, s) < 0)
            msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ctx->sec));
        zsocket_set_hwm (s, 0);
        zsocket_set_identity (s, ctx->rankstr); 
        if (zsocket_connect (s, "%s", uri) < 0)
            err_exit ("%s", uri);
        zp.socket = s;
        if (zloop_poller (ctx->zl, &zp, (zloop_fn *)parent_cb, ctx) < 0)
            err_exit ("zloop_poller");
    }
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
    ctx->rctx = route_init (ctx->verbose);

    //(void)umask (077); 

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
    ctx->zs_child = cmbd_init_child (ctx);
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
    /* Connect to upstream ports, if any
     */
    ctx->zs_parent = cmbd_init_parent (ctx);

    /* create flux_t handle */
    ctx->h = handle_create (ctx, &cmbd_handle_ops, 0);
    flux_log_set_facility (ctx->h, "cmbd");

    ctx->loaded_plugins = zhash_new ();
    load_plugins (ctx);

    flux_log (ctx->h, LOG_INFO, "%s", flux_sec_confstr (ctx->sec));

    /* Send request for event URI (maybe upstream, maybe plugin)
     * When the response is received we will subscribe to it.
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
    } else
        handled = false;

    if (arg)
        free (arg);
    /* If zmsg is not destroyed, route_request() will respond with ENOSYS */
    if (handled && *zmsg)
        zmsg_destroy (zmsg);
}

static int child_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    zmsg_t *zmsg = zmsg_recv (ctx->zs_child);

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
        snoop_cc (ctx, FLUX_MSGTYPE_EVENT, zmsg);
        zmsg_send (&zmsg, ctx->zs_event_out);
    }
    ZLOOP_RETURN(ctx);
}

static void reap_all_children (void)
{
    pid_t pid;
    int status;
    while ((pid = waitpid ((pid_t) -1, &status, WNOHANG)) > (pid_t)0)
        msg ("child %ld exited status 0x%04x\n", (long)pid, status);
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
        if (fdsi.ssi_signo == SIGCHLD)
            reap_all_children ();
        else
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

/* Helper for cmbd_request_sendmsg
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

    errno = 0;
    if ((hopcount = parse_request (*zmsg, &service, &lasthop)) < 0) {
        errno = EPROTO;
        goto done;
    }
    snoop_cc (ctx, FLUX_MSGTYPE_REQUEST, *zmsg);
    if (!strcmp (service, "cmb")) {
        if (hopcount > 0) {
            cmb_internal_request (ctx, zmsg);
        } else if (!ctx->treeroot) { /* we're sending so route upstream */
            if (zmsg_send (zmsg, ctx->zs_parent) < 0)
                err ("%s: zs_parent", __FUNCTION__);
        } else
            errno = EINVAL;
    } else {
        if ((gw = route_lookup (ctx->rctx, service))
                            && (!lasthop || strcmp (lasthop, gw) != 0)) {
            if (zmsg_send_unrouter (zmsg, ctx->zs_plugins, ctx->rankstr, gw))
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
    /* N.B. don't destroy zmsg on error as we use it to send errnum reply.
     */
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
    if (zmsg_send (zmsg, ctx->zs_child) < 0)
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
