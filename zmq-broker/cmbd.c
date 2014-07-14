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
    void *zs;
    char *uri;
} endpt_t;

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
    zlist_t *parents;           /* DEALER - requests to parent */
                                /*   (reparent pushes new parent on head) */

    endpt_t *child;             /* ROUTER - requests from children */

    void *zs_request;           /* ROUTER - requests from plugins */

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
    zhash_t *modules;           /* hash of module_t's by name */
    /* Misc
     */
    bool verbose;               /* enable debug to stderr */
    flux_t h;
    pid_t pid;
    char hostname[HOST_NAME_MAX + 1];
    int hb_epoch;
    zhash_t *peer_idle;         /* peer (hopcount=1) hb idle time, by uuid */
    int hb_lastreq;             /* hb epoch of last upstream request */
} ctx_t;

typedef struct {
    plugin_ctx_t p;
    zhash_t *args;
    zlist_t *rmmod_reqs;
    ctx_t *ctx;
    char *path;
    int flags;
} module_t;

typedef struct {
    int hb_lastseen;
    bool modflag;
} peer_t;

static int snoop_cc (ctx_t *ctx, int type, zmsg_t *zmsg);

static int event_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int plugins_cb (zloop_t *zl, zmq_pollitem_t *item, module_t *mod);
static int parent_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int request_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int signal_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);

static void cmb_event_geturi_request (ctx_t *ctx);

static void cmbd_init (ctx_t *ctx);
static void cmbd_fini (ctx_t *ctx);

static module_t *module_create (ctx_t *ctx, const char *path, int flags);
static void module_destroy (module_t *mod);
static void module_unload (module_t *mod, zmsg_t **zmsg);
static char *modfind (const char *modpath, const char *name);

static int peer_idle (ctx_t *ctx, const char *uuid);
static void peer_update (ctx_t *ctx, const char *uuid);
static void peer_modcreate (ctx_t *ctx, const char *uuid);

static endpt_t *endpt_create (const char *uri);
static void endpt_destroy (endpt_t *ep);

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
    char *hosts = NULL;
    endpt_t *ep;

    memset (&ctx, 0, sizeof (ctx));
    log_init (basename (argv[0]));

    ctx.size = 1;
    if (!(ctx.modules = zhash_new ()))
        oom ();
    if (!(ctx.peer_idle = zhash_new ()))
        oom ();
    if (!(ctx.parents = zlist_new ()))
        oom ();
    ctx.session_name = "flux";
    if (gethostname (ctx.hostname, HOST_NAME_MAX) < 0)
        err_exit ("gethostname");
    ctx.pid = getpid();
    ctx.plugin_path = PLUGIN_PATH;

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
                if (ctx.child)
                    endpt_destroy (ctx.child);
                ctx.child = endpt_create (optarg);
                break;
            case 'p': { /* --parent-uri URI */
                endpt_t *ep = endpt_create (optarg);
                if (zlist_push (ctx.parents, ep) < 0)
                    oom ();
                break;
            }
            case 'v':   /* --verbose */
                ctx.verbose = true;
                break;
            case 'H': { /* --hostlist hostlist */
                json_object *o = hostlist_to_json (optarg);
                if (hosts)
                    free (hosts);
                hosts = xstrdup (json_object_to_json_string (o));
                json_object_put (o);
                break;
            }
            case 'R':   /* --rank N */
                ctx.rank = strtoul (optarg, NULL, 10);
                break;
            case 'S':   /* --size N */
                ctx.size = strtoul (optarg, NULL, 10);
                break;
            case 'P': { /* --plugins p1,p2,... */
                char *cpy = xstrdup (optarg);
                char *path, *name, *saveptr, *a1 = cpy;
                module_t *mod;
                int flags = 0;
                while((name = strtok_r (a1, ",", &saveptr))) {
                    if (!(path = modfind (ctx.plugin_path, name)))
                        err_exit ("module %s", name);
                    if (!(mod = module_create (&ctx, path, flags)))
                        err_exit ("module %s", name);
                    zhash_update (ctx.modules, name, mod);
                    zhash_freefn (ctx.modules, name,
                                  (zhash_free_fn *)module_destroy);
                    a1 = NULL;
                }
                free (cpy);
                break;
            }
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
    /* Remaining arguments are for modules: module:key=val
     */
    for (i = optind; i < argc; i++) {
        char *key, *val, *cpy = xstrdup (argv[i]);
        module_t *mod;
        if ((key = strchr (cpy, ':'))) {
            *key++ = '\0';
            if (!(mod = zhash_lookup (ctx.modules, cpy)))
                msg_exit ("module argument for unknown module: %s", cpy);
            if ((val = strchr (key, '='))) {
                *val++ = '\0';
                zhash_update (mod->args, key, xstrdup (val));
                zhash_freefn (mod->args, key, (zhash_free_fn *)free);
            }
        }
        free (cpy);
    }
    /* Special -H is really kvs:hosts=...
     */
    if (hosts) {
        module_t *mod;
        if (!(mod = zhash_lookup (ctx.modules, "kvs")))
            msg_exit ("kvs:hosts argument but kvs module not loaded");
        zhash_update (mod->args, "hosts", hosts);
        zhash_freefn (mod->args, "hosts", (zhash_free_fn *)free);
    }

    if (zhash_size (ctx.modules) == 0)
        usage ();

    if (asprintf (&ctx.rankstr, "%d", ctx.rank) < 0)
        oom ();
    if (ctx.rank == 0)
        ctx.treeroot = true;
    if (ctx.treeroot && zlist_size (ctx.parents) > 0)
        msg_exit ("treeroot must NOT have parent");
    if (!ctx.treeroot && zlist_size (ctx.parents) == 0)
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

    while ((ep = zlist_pop (ctx.parents)))
        endpt_destroy (ep);
    zlist_destroy (&ctx.parents);
    if (ctx.child)
        endpt_destroy (ctx.child);
    if (ctx.rankstr)
        free (ctx.rankstr);
    zhash_destroy (&ctx.peer_idle);
    return 0;
}

static bool checkpath (const char *path, const char *name)
{
    char *pname = plugin_getstring (path, "mod_name");
    bool res = (pname && !strcmp (name, pname));
    free (pname);
    return res;
}

static char *modfind (const char *modpath, const char *name)
{
    char *cpy = xstrdup (modpath);
    char *path = NULL, *dir, *saveptr, *a1 = cpy;
    char *ret = NULL;

    while (!ret && (dir = strtok_r (a1, ":", &saveptr))) {
        if (asprintf (&path, "%s/%s.so", dir, name) < 0)
            oom ();
        if (!checkpath (path, name))
            free (path);
        else
            ret = path;
        a1 = NULL;
    }
    free (cpy);
    if (!ret)
        errno = ENOENT;
    return ret;
}

static module_t *module_create (ctx_t *ctx, const char *path, int flags)
{
    module_t *mod = xzmalloc (sizeof (*mod));

    mod->path = xstrdup (path);
    if (!(mod->args = zhash_new ()))
        oom ();
    if (!(mod->rmmod_reqs = zlist_new ()))
        oom ();
    mod->ctx = ctx;
    mod->flags = flags;
    return mod;
}

static void module_destroy (module_t *mod)
{
    zmsg_t *zmsg;

    if (mod->p) {
        plugin_stop (mod->p);
        zmq_pollitem_t zp;
        zp.socket = plugin_sock (mod->p);
        zloop_poller_end (mod->ctx->zl, &zp);
        plugin_destroy (mod->p); /* calls pthread_join */
    }
    while ((zmsg = zlist_pop (mod->rmmod_reqs)))
        flux_respond_errnum (mod->ctx->h, &zmsg, 0);
    zlist_destroy (&mod->rmmod_reqs);
    zhash_destroy (&mod->args);

    free (mod->path);
    free (mod);
}

static void module_unload (module_t *mod, zmsg_t **zmsg)
{
    if (zmsg) {
        zlist_push (mod->rmmod_reqs, *zmsg);
        *zmsg = NULL;
    }
    plugin_stop (mod->p);
}

static int module_load (ctx_t *ctx, module_t *mod)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };
    int rc = -1;

    assert (mod->p == NULL);
    mod->p = plugin_create (ctx->h, mod->path, mod->args);
    if (mod->p) {
        peer_modcreate (ctx, plugin_uuid (mod->p));
        zp.socket = plugin_sock (mod->p);
        if (zloop_poller (ctx->zl, &zp, (zloop_fn *)plugins_cb, mod) < 0)
            err_exit ("zloop_poller");
        plugin_start (mod->p);
        rc = 0;
    }
    return rc;
}

static void load_modules (ctx_t *ctx)
{
    zlist_t *keys = zhash_keys (ctx->modules);
    char *name;
    module_t *mod;

    name = zlist_first (keys);
    while (name) {
        mod = zhash_lookup (ctx->modules, name);
        assert (mod != NULL);
        if (module_load (ctx, mod) < 0)
            err_exit ("failed to load module %s", name);
        name = zlist_next (keys);
    }
    zlist_destroy (&keys);
}

static void *cmbd_init_request (ctx_t *ctx)
{
    void *s;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(s = zsocket_new (ctx->zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (s, 0);
    if (zsocket_bind (s, "%s", REQUEST_URI) < 0) /* always bind to inproc */
        err_exit ("%s", REQUEST_URI);
    zp.socket = s;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)request_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return s;
}

static void *cmbd_init_child (ctx_t *ctx, const char *uri)
{
    void *s;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(s = zsocket_new (ctx->zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
    if (flux_sec_ssockinit (ctx->sec, s) < 0)
        msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ctx->sec));
    zsocket_set_hwm (s, 0);
    if (zsocket_bind (s, "%s", uri) < 0)
        err_exit ("%s", uri);
    zp.socket = s;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)request_cb, ctx) < 0)
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

static void *cmbd_init_parent (ctx_t *ctx, const char *uri) {
    void *s = NULL;
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };
    int savederr;

    if (!(s = zsocket_new (ctx->zctx, ZMQ_DEALER)))
        goto error;
    if (flux_sec_csockinit (ctx->sec, s) < 0) {
        savederr = errno;
        msg ("flux_sec_csockinit: %s", flux_sec_errstr (ctx->sec));
        errno = savederr;
        goto error;
    }
    zsocket_set_hwm (s, 0);
    zsocket_set_identity (s, ctx->rankstr);
    if (zsocket_connect (s, "%s", uri) < 0)
        goto error;
    zp.socket = s;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)parent_cb, ctx) < 0)
        goto error;
    return s;
error:
    if (s) {
        savederr = errno;
        zsocket_destroy (ctx->zctx, s);
        errno = savederr;
    }
    return NULL;
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
    endpt_t *ep;
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
    ctx->zs_request = cmbd_init_request (ctx);
    ctx->zs_event_out = cmbd_init_event_out (ctx);
    ctx->zs_snoop = cmbd_init_snoop (ctx);
    if (ctx->child)
        ctx->child->zs = cmbd_init_child (ctx, ctx->child->uri);
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
    if ((ep = zlist_first (ctx->parents))) {
        if (!(ep->zs = cmbd_init_parent (ctx, ep->uri)))
            err_exit ("%s", ep->uri);
    }

    /* create flux_t handle */
    ctx->h = handle_create (ctx, &cmbd_handle_ops, 0);
    flux_log_set_facility (ctx->h, "cmbd");

    load_modules (ctx);

    flux_log (ctx->h, LOG_INFO, "%s", flux_sec_confstr (ctx->sec));

    /* Send request for event URI (maybe upstream, maybe plugin)
     * When the response is received we will subscribe to it.
     */
    cmb_event_geturi_request (ctx);
}

static void cmbd_fini (ctx_t *ctx)
{
    zhash_destroy (&ctx->modules);
    if (ctx->sec)
        flux_sec_destroy (ctx->sec);
    zloop_destroy (&ctx->zl);
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
    } else if (cmb_msg_match (*zmsg, "cmb.ping")) { /* ignore ping response */
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
    if (!strcmp (name, "cmbd-snoop-uri")) {
        val = ctx->uri_snoop;
    } else if (!strcmp (name, "cmbd-parent-uri")) {
        endpt_t *ep = zlist_first (ctx->parents);
        if (ep)
            val = ep->uri;
    } else if (!strcmp (name, "cmbd-request-uri")) {
        if (ctx->child)
            val = ctx->child->uri;
    }
    return val;
}

/* Modctl sets 'managed' flag for insert/remove.  flux-mod ins,rm does not.
 */
static int cmb_rmmod (ctx_t *ctx, const char *name, int flags, zmsg_t **zmsg)
{
    module_t *mod;
    if (!(mod = zhash_lookup (ctx->modules, name))) {
        errno = ENOENT;
        return -1;
    }
    if ((mod->flags & FLUX_MOD_FLAGS_MANAGED)
                                    != (flags & FLUX_MOD_FLAGS_MANAGED)) {
        errno = EINVAL;
        return -1;
    }
    module_unload (mod, zmsg);
    flux_log (ctx->h, LOG_INFO, "rmmod %s", name);
    return 0;
}

static json_object *cmb_lsmod (ctx_t *ctx)
{
    json_object *mo, *response = util_json_object_new_object ();
    zlist_t *keys;
    char *name;
    module_t *mod;

    if (!(keys = zhash_keys (ctx->modules)))
        oom ();
    name = zlist_first (keys);
    while (name) {
        mod = zhash_lookup (ctx->modules, name);
        assert (mod != NULL);
        mo = util_json_object_new_object ();
        util_json_object_add_string (mo, "name", plugin_name (mod->p));
        util_json_object_add_int (mo, "size", plugin_size (mod->p));
        util_json_object_add_string (mo, "digest", plugin_digest (mod->p));
        util_json_object_add_int (mo, "flags", mod->flags);
        util_json_object_add_int (mo, "idle",
                                    peer_idle (ctx, plugin_uuid (mod->p)));
        util_json_object_add_string (mo, "nodelist", ctx->rankstr);
        json_object_object_add (response, name, mo);
        name = zlist_next (keys);
    }
    zlist_destroy (&keys);
    return response;
}

static int cmb_insmod (ctx_t *ctx, const char *path, int flags,
                       json_object *args)
{
    int rc = -1;
    module_t *mod;
    json_object_iter iter;
    char *name = NULL;

    if (!(name = plugin_getstring (path, "mod_name"))) {
        errno = ENOENT;
        goto done;
    }
    if (zhash_lookup (ctx->modules, name)) {
        errno = EEXIST;
        goto done;
    }
    if (!(mod = module_create (ctx, path, flags)))
        goto done;
    json_object_object_foreachC (args, iter) {
        const char *val = json_object_get_string (iter.val);
        zhash_update (mod->args, iter.key, xstrdup (val));
        zhash_freefn (mod->args, iter.key, (zhash_free_fn *)free);
    }
    if (module_load (ctx, mod) < 0) {
        module_destroy (mod);
        goto done;
    }
    zhash_update (ctx->modules, name, mod);
    zhash_freefn (ctx->modules, name, (zhash_free_fn *)module_destroy);
    flux_log (ctx->h, LOG_INFO, "insmod %s %s", name, path);
    rc = 0;
done:
    if (name)
        free (name);
    return rc;
}

static json_object *peer_ls (ctx_t *ctx)
{
    json_object *po, *response = util_json_object_new_object ();
    zlist_t *keys;
    char *key;
    peer_t *p;

    if (!(keys = zhash_keys (ctx->peer_idle)))
        oom ();
    key = zlist_first (keys);
    while (key) {
        p = zhash_lookup (ctx->peer_idle, key);
        assert (p != NULL);
        if (!p->modflag) {
            po = util_json_object_new_object ();
            util_json_object_add_int (po, "idle", peer_idle (ctx, key));
            json_object_object_add (response, key, po);
        }
        key = zlist_next (keys);
    }
    zlist_destroy (&keys);
    return response;
}

static void peer_modcreate (ctx_t *ctx, const char *uuid)
{
    peer_t *p = xzmalloc (sizeof (*p));

    p->modflag = true;
    zhash_update (ctx->peer_idle, uuid, p);
    zhash_freefn (ctx->peer_idle, uuid, free);
}

static void peer_update (ctx_t *ctx, const char *uuid)
{
    peer_t *p;

    if (!(p = zhash_lookup (ctx->peer_idle, uuid))) {
        p = xzmalloc (sizeof (*p));
        zhash_update (ctx->peer_idle, uuid, p);
        zhash_freefn (ctx->peer_idle, uuid, free);
    }
    p->hb_lastseen = ctx->hb_epoch;
}

static int peer_idle (ctx_t *ctx, const char *uuid)
{
    peer_t *p;

    if (!(p = zhash_lookup (ctx->peer_idle, uuid)))
        return ctx->hb_epoch;
    return ctx->hb_epoch - p->hb_lastseen;
}

static bool peer_ismodule (ctx_t *ctx, const char *uuid)
{
    peer_t *p;

    if (!(p = zhash_lookup (ctx->peer_idle, uuid)))
        return false;
    return p->modflag;
}

static void self_update (ctx_t *ctx)
{
    ctx->hb_lastreq = ctx->hb_epoch;
}

static int self_idle (ctx_t *ctx)
{
    return ctx->hb_epoch - ctx->hb_lastreq;
}

static void hb_cb (ctx_t *ctx, zmsg_t *zmsg)
{
    json_object *event = NULL;

    if (!ctx->treeroot && self_idle (ctx) > 0) {
        json_object *request = util_json_object_new_object ();
        util_json_object_add_int (request, "seq", ctx->hb_epoch);
        flux_request_send (ctx->h, request, "cmb.ping");
        json_object_put (request);
    }

    if (cmb_msg_decode (zmsg, NULL, &event) < 0 || event == NULL
           || util_json_object_get_int (event, "epoch", &ctx->hb_epoch) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: bad hb message", __FUNCTION__);
        return;
    }
}

static endpt_t *endpt_create (const char *uri)
{
    endpt_t *ep = xzmalloc (sizeof (*ep));
    ep->uri = xstrdup (uri);
    return ep;
}

static void endpt_destroy (endpt_t *ep)
{
    free (ep->uri);
    free (ep);
}

/* Establish connection with a new parent and begin using it for all
 * upstream requests.  Leave old parent(s) wired in to zloop to make
 * it possible to transition off a healthy node without losing replies.
 */
static int cmb_reparent (ctx_t *ctx, const char *uri)
{
    endpt_t *ep;
    const char *comment = "";

    if (uri == NULL || !strstr (uri, "://")) {
        errno = EINVAL;
        return -1;
    }
    ep = zlist_first (ctx->parents);
    while (ep) {
        if (!strcmp (ep->uri, uri))
            break;
        ep = zlist_next (ctx->parents);
    }
    if (ep) {
        zlist_remove (ctx->parents, ep);
        comment = "restored";
    } else {
        ep = endpt_create (uri);
        if (!(ep->zs = cmbd_init_parent (ctx, ep->uri))) {
            endpt_destroy (ep);
            return -1;
        }
        comment = "new";
    }
    if (zlist_push (ctx->parents, ep) < 0)
        oom ();
    /* log after reparenting, not before */
    flux_log (ctx->h, LOG_INFO, "reparent %s (%s)", uri, comment);
    return 0;
}

static void cmb_internal_event (ctx_t *ctx, zmsg_t *zmsg)
{
    if (cmb_msg_match (zmsg, "hb"))
        hb_cb (ctx, zmsg);
}

static void cmb_internal_request (ctx_t *ctx, zmsg_t **zmsg)
{
    char *arg = NULL;
    bool handled = true;

    if (cmb_msg_match (*zmsg, "cmb.info")) {
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
            flux_respond_errnum (ctx->h, zmsg, ENOENT);
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
    } else if (cmb_msg_match (*zmsg, "cmb.rmmod")) {
        json_object *request = NULL;
        const char *name;
        int flags;
        if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
                || util_json_object_get_string (request, "name", &name) < 0
                || util_json_object_get_int (request, "flags", &flags) < 0) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else if (cmb_rmmod (ctx, name, flags, zmsg) < 0) {
            flux_respond_errnum (ctx->h, zmsg, errno);
        } /* else response is deferred until module returns EOF */
        if (request)
            json_object_put (request);
    } else if (cmb_msg_match (*zmsg, "cmb.insmod")) {
        json_object *args, *request = NULL;
        const char *path;
        int flags;
        if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
                || util_json_object_get_string (request, "path", &path) < 0
                || util_json_object_get_int (request, "flags", &flags) < 0
                || !(args = json_object_object_get (request, "args"))) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else if (cmb_insmod (ctx, path, flags, args) < 0) {
            flux_respond_errnum (ctx->h, zmsg, errno);
        } else {
            flux_respond_errnum (ctx->h, zmsg, 0);
        }
        if (request)
            json_object_put (request);
    } else if (cmb_msg_match (*zmsg, "cmb.lsmod")) {
        json_object *request = NULL;
        json_object *response = NULL;
        if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else if (!(response = cmb_lsmod (ctx))) {
            flux_respond_errnum (ctx->h, zmsg, errno);
        } else {
            flux_respond (ctx->h, zmsg, response);
        }
        if (request)
            json_object_put (request);
        if (response)
            json_object_put (response);
    } else if (cmb_msg_match (*zmsg, "cmb.lspeer")) {
        json_object *request = NULL;
        json_object *response = NULL;
        if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else if (!(response = peer_ls (ctx))) {
            flux_respond_errnum (ctx->h, zmsg, errno);
        } else {
            flux_respond (ctx->h, zmsg, response);
        }
        if (request)
            json_object_put (request);
        if (response)
            json_object_put (response);
    } else if (cmb_msg_match (*zmsg, "cmb.ping")) {
        json_object *request = NULL;
        char *s = NULL;
        if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else {
            s = zmsg_route_str (*zmsg, 1);
            util_json_object_add_string (request, "route", s);
            flux_respond (ctx->h, zmsg, request);
        }
        if (request)
            json_object_put (request);
        if (s)
            free (s);
    } else if (cmb_msg_match (*zmsg, "cmb.reparent")) {
        json_object *request = NULL;
        const char *uri;
        if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
                || util_json_object_get_string (request, "uri", &uri) < 0) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else if (cmb_reparent (ctx, uri) < 0) {
            flux_respond_errnum (ctx->h, zmsg, errno);
        } else {
            flux_respond_errnum (ctx->h, zmsg, 0);
        }
        if (request)
            json_object_put (request);
    } else if (cmb_msg_match (*zmsg, "cmb.panic")) {
        json_object *request = NULL;
        const char *s = NULL;
        if (cmb_msg_decode (*zmsg, NULL, &request) == 0 && request != NULL) {
            (void)util_json_object_get_string (request, "msg", &s);
            msg ("PANIC: %s", s ? s : "no reason");
            exit (1);
        }
        if (request)
            json_object_put (request);
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
    zmsg_t *zmsg = zmsg_recv (item->socket);

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
    zmsg_t *zmsg = zmsg_recv (item->socket);

    if (zmsg) {
        (void)flux_response_sendmsg (ctx->h, &zmsg);
    }
    if (zmsg)
        zmsg_destroy (&zmsg);
    ZLOOP_RETURN(ctx);
}

static int plugins_cb (zloop_t *zl, zmq_pollitem_t *item, module_t *mod)
{
    ctx_t *ctx = mod->ctx;
    zmsg_t *zmsg = zmsg_recv (item->socket);

    if (zmsg) {
        if (zmsg_content_size (zmsg) == 0) /* EOF */
            zhash_delete (ctx->modules, plugin_name (mod->p));
        else {
            (void)flux_response_sendmsg (ctx->h, &zmsg);
            peer_update (ctx, plugin_uuid (mod->p));
        }
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
        cmb_internal_event (ctx, zmsg);
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
    char *typestr, *tag;

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
    if (zmsg_pushstr (cpy, tag) < 0)
        oom ();
    free (tag);

    return zmsg_send (&cpy, ctx->zs_snoop);
}

static int parent_send (ctx_t *ctx, zmsg_t **zmsg)
{
    endpt_t *ep = zlist_first (ctx->parents);

    assert (ep != NULL);
    assert (ep->zs != NULL);
    if (zmsg_send (zmsg, ep->zs) < 0) {
        err ("%s: %s: %s", __FUNCTION__, ep->uri, strerror (errno));
        return -1;
    }
    self_update (ctx);
    return 0;
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
    module_t *mod;
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
            parent_send (ctx, zmsg);
        } else
            errno = EINVAL;
    } else {
        if ((mod = zhash_lookup (ctx->modules, service))
                 && (!lasthop || strcmp (lasthop, plugin_uuid (mod->p)) != 0)) {
            if (zmsg_send (zmsg, plugin_sock (mod->p)) < 0)
                err ("%s: %s", __FUNCTION__, service);

        } else if (!ctx->treeroot) {
            parent_send (ctx, zmsg);
        } else
            errno = ENOSYS;
    }
    if (hopcount > 0)
        peer_update (ctx, lasthop);
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
    zframe_t *zf;
    char *uuid = NULL;

    if (!(zf = zmsg_first (*zmsg))) {
        errno = EINVAL;
        goto done; /* drop message with no frames */
    }
    snoop_cc (ctx, FLUX_MSGTYPE_RESPONSE, *zmsg);
    if (zframe_size (zf) == 0) {
        rc = cmb_internal_response (ctx, zmsg);
    } else if ((uuid = zframe_strdup (zf)) && peer_ismodule (ctx, uuid)) {
        rc = zmsg_send (zmsg, ctx->zs_request);
    } else if (ctx->child) {
        rc = zmsg_send (zmsg, ctx->child->zs);
    }
done:
    if (uuid)
        free (uuid);
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
