/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* cmbd.c - simple zmq message broker, to run on each node of a job */

#if HAVE_CONFIG_H
#include "config.h"
#endif
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
#include <sys/time.h>
#include <dlfcn.h>

#include <json/json.h>
#include <zmq.h>
#include <czmq.h>

#include "log.h"
#include "zmsg.h"
#include "util.h"
#include "plugin.h"
#include "flux.h"
#include "handle.h"
#include "security.h"
#include "nodeset.h"
#include "pmi.h"

#ifndef ZMQ_IMMEDIATE
#define ZMQ_IMMEDIATE           ZMQ_DELAY_ATTACH_ON_CONNECT
#define zsocket_set_immediate   zsocket_set_delay_attach_on_connect
#endif

#define ZLOOP_RETURN(p) \
    return ((p)->reactor_stop ? (-1) : (0))

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

    endpt_t *right;             /* DEALER - requests to rank overlay */

    void *zs_request;           /* ROUTER - requests from plugins */

    void *zs_event_out;         /* PUB - to plugins */
    endpt_t *gevent;            /* PUB for rank = 0, SUB for rank > 0 */
    endpt_t *gevent_relay;      /* PUB event relay for multiple cmbds/node */

    char *uri_snoop;            /* PUB - to flux-snoop (uri is generated) */
    void *zs_snoop;
    /* Session parameters
     */
    bool treeroot;              /* true if we are the root of reduction tree */
    int size;                   /* session size */
    int rank;                   /* our rank in session */
    char *rankstr;              /*   string version of above */
    char *rankstr_right;        /*   with "r" tacked on the end */
    char *sid;                  /* session id */
    /* Plugins
     */
    char *plugin_path;          /* colon-separated list of directories */
    zhash_t *modules;           /* hash of module_t's by name */
    /* Misc
     */
    bool verbose;               /* enable debug to stderr */
    flux_t h;
    pid_t pid;
    zhash_t *peer_idle;         /* peer (hopcount=1) hb idle time, by uuid */
    int hb_lastreq;             /* hb epoch of last upstream request */
    char *proctitle;
    pid_t shell_pid;
    char *shell_cmd;
    sigset_t default_sigset;
    /* Bootstrap
     */
    bool pmi_boot;
    int k_ary;
    /* Heartbeat
     */
    double heartrate;
    int heartbeat_tid;
    int hb_epoch;
    /* Shutdown
     */
    int shutdown_tid;
    int shutdown_exitcode;
} ctx_t;

typedef struct {
    plugin_ctx_t p;
    zhash_t *args;
    zlist_t *rmmod_reqs;
    ctx_t *ctx;
    char *path;
    int flags;
    nodeset_t ns;
} module_t;

typedef struct {
    int hb_lastseen;
    bool modflag;
} peer_t;

static int snoop_cc (ctx_t *ctx, int type, zmsg_t *zmsg);
static int relay_cc (ctx_t *ctx, zmsg_t *zmsg);

static int event_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int plugins_cb (zloop_t *zl, zmq_pollitem_t *item, module_t *mod);
static int parent_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int request_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int hb_cb (zloop_t *zl, int timer_id, ctx_t *ctx);
static int signal_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);

static void cmbd_fini (ctx_t *ctx);

static void cmbd_init_comms (ctx_t *ctx);
static void cmbd_init_socks (ctx_t *ctx);
static int cmbd_init_child (ctx_t *ctx, endpt_t *ep);
static int cmbd_init_gevent_pub (ctx_t *ctx, endpt_t *ep);

static module_t *module_create (ctx_t *ctx, const char *path, int flags);
static void module_destroy (module_t *mod);
static void module_unload (module_t *mod, zmsg_t **zmsg);
static bool module_select (module_t *mod, const char *nstr);
static module_t *module_prepare (ctx_t *ctx, const char *name);
static void module_prepare_list (ctx_t *ctx, const char *optarg);
static void module_loadall (ctx_t *ctx);

static int peer_idle (ctx_t *ctx, const char *uuid);
static void peer_update (ctx_t *ctx, const char *uuid);
static void peer_modcreate (ctx_t *ctx, const char *uuid);

static endpt_t *endpt_create (const char *fmt, ...);
static void endpt_destroy (endpt_t *ep);

static int cmb_pub_event (ctx_t *ctx, zmsg_t **event);

static void update_proctitle (ctx_t *ctx);
static void update_environment (ctx_t *ctx);
static void update_pidfile (ctx_t *ctx, bool force);
static void rank0_shell (ctx_t *ctx);
static void pmi_boot (ctx_t *ctx);
static void local_boot (ctx_t *ctx);

static const double min_heartrate = 0.01;   /* min seconds */
static const double max_heartrate = 30;     /* max seconds */
static const double dfl_heartrate = 2;

static const struct flux_handle_ops cmbd_handle_ops;

#define OPTIONS "t:vR:S:p:M:X:L:N:Pke:r:s:c:fnH:"
static const struct option longopts[] = {
    {"sid",             required_argument,  0, 'N'},
    {"child-uri",       required_argument,  0, 't'},
    {"parent-uri",      required_argument,  0, 'p'},
    {"right-uri",       required_argument,  0, 'r'},
    {"verbose",         no_argument,        0, 'v'},
    {"security",        required_argument,  0, 's'},
    {"rank",            required_argument,  0, 'R'},
    {"size",            required_argument,  0, 'S'},
    {"plugins",         required_argument,  0, 'M'},
    {"module-path",     required_argument,  0, 'X'},
    {"logdest",         required_argument,  0, 'L'},
    {"pmi-boot",        no_argument,        0, 'P'},
    {"k-ary",           required_argument,  0, 'k'},
    {"event-uri",       required_argument,  0, 'e'},
    {"command",         required_argument,  0, 'c'},
    {"noshell",         no_argument,        0, 'n'},
    {"heartrate",       required_argument,  0, 'H'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: cmbd OPTIONS [module:key=val ...]\n"
" -t,--child-uri URI           Set child URI to bind and receive requests\n"
" -p,--parent-uri URI          Set parent URI to connect and send requests\n"
" -e,--event-uri               Set event URI (pub: rank 0, sub: rank > 0)\n"
" -r,--right-uri               Set right (rank-request) URI\n"
" -v,--verbose                 Be chatty\n"
" -R,--rank N                  Set cmbd rank (0...size-1)\n"
" -S,--size N                  Set number of ranks in session\n"
" -N,--sid NAME                Set session id\n"
" -M,--plugins name[,name,...] Load the named modules (comma separated)\n"
" -X,--module-path PATH        Set module search path (colon separated)\n"
" -L,--logdest DEST            Log to DEST, can  be syslog, stderr, or file\n"
" -s,--security=plain|curve|none    Select security mode (default: curve)\n"
" -P,--pmi-boot                Bootstrap via PMI\n"
" -k,--k-ary K                 Wire up in a k-ary tree\n"
" -c,--command string          Run command on rank 0\n"
" -n,--noshell                 Do not spawn a shell even if on a tty\n"
" -f,--force                   Kill rival cmbd and start\n"
" -H,--heartrate SECS          Set heartrate in seconds (rank 0 only)\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int c, i;
    ctx_t ctx;
    endpt_t *ep;
    bool fopt = false;
    bool nopt = false;

    memset (&ctx, 0, sizeof (ctx));
    log_init (argv[0]);

    ctx.size = 1;
    if (!(ctx.modules = zhash_new ()))
        oom ();
    if (!(ctx.peer_idle = zhash_new ()))
        oom ();
    if (!(ctx.parents = zlist_new ()))
        oom ();
    ctx.k_ary = 2; /* binary TBON is default */

    ctx.pid = getpid();
    ctx.plugin_path = PLUGIN_PATH;
    ctx.heartrate = dfl_heartrate;
    ctx.shutdown_tid = -1;

    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'N':   /* --sid NAME */
                if (ctx.sid)
                    free (ctx.sid);
                ctx.sid = xstrdup (optarg);
                break;
            case 's':   /* --security=MODE */
                if (!strcmp (optarg, "none"))
                    ctx.security_clr = FLUX_SEC_TYPE_ALL;
                else if (!strcmp (optarg, "plain"))
                    ctx.security_set |= FLUX_SEC_TYPE_PLAIN;
                else if (!strcmp (optarg, "curve"))
                    ctx.security_set |= FLUX_SEC_TYPE_CURVE;
                else
                    msg_exit ("--security argument must be none|plain|curve");
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
            case 'R':   /* --rank N */
                ctx.rank = strtoul (optarg, NULL, 10);
                break;
            case 'S':   /* --size N */
                ctx.size = strtoul (optarg, NULL, 10);
                break;
            case 'M':   /* --plugins p1,p2,...,p3[nodeset],... */
                module_prepare_list (&ctx, optarg);
                break;
            case 'X':   /* --module-path PATH */
                ctx.plugin_path = optarg;
                break;
            case 'L':   /* --logdest DEST */
                log_set_dest (optarg);
                break;
            case 'P':   /* --pmi-boot */
                ctx.pmi_boot = true;
                break;
            case 'k':   /* --k-ary k */
                ctx.k_ary = strtoul (optarg, NULL, 10);
                if (ctx.k_ary < 0)
                    usage ();
                break;
            case 'e':   /* --event-uri */
                if (ctx.gevent)
                    endpt_destroy (ctx.gevent);
                ctx.gevent = endpt_create (optarg);
                break;
            case 'r':   /* --right-uri */
                if (ctx.right)
                    endpt_destroy (ctx.right);
                ctx.right = endpt_create (optarg);
                break;
            case 'c':   /* --command CMD */
                if (ctx.shell_cmd)
                    free (ctx.shell_cmd);
                ctx.shell_cmd = xstrdup (optarg);
                break;
            case 'n':   /* --noshell */
                nopt = true;
                break;
            case 'f':   /* --force */
                fopt = true;
                break;
            case 'H': { /* --heartrate SECS */
                char *endptr;
                ctx.heartrate = strtod (optarg, &endptr);
                if (ctx.heartrate == HUGE_VAL || endptr == optarg)
                    msg_exit ("error parsing heartrate");
                if (!strcasecmp (endptr, "s") || *endptr == '\0')
                    ;
                else if (!strcasecmp (endptr, "ms"))
                    ctx.heartrate /= 1000.0;
                else
                    msg_exit ("bad heartrate units: use s or ms");
                if (ctx.heartrate < min_heartrate
                                            || ctx.heartrate > max_heartrate)
                    msg_exit ("valid heartrate is %.0fms <= N <= %.0fs",
                              min_heartrate*1000, max_heartrate);
                break;
            }
            default:
                usage ();
        }
    }
    msg ("Heartrate: %0.1fs", ctx.heartrate);

    /* Create zeromq context, security context, zloop, etc.
     */
    cmbd_init_comms (&ctx);

    /* Sets rank, size, parent URI.
     * Initialize child socket.
     */
    if (ctx.pmi_boot) {
        if (ctx.child)
            msg_exit ("--child-uri should not be specified with --pmi-boot");
        if (zlist_size (ctx.parents) > 0)
            msg_exit ("--parent-uri should not be specified with --pmi-boot");
        if (ctx.gevent)
            msg_exit ("--event-uri should not be specified with --pmi-boot");
        if (ctx.sid)
            msg_exit ("--session-id should not be specified with --pmi-boot");
        pmi_boot (&ctx);
    }
    if (!ctx.sid)
        ctx.sid = xstrdup ("0");
    if (asprintf (&ctx.rankstr, "%d", ctx.rank) < 0)
        oom ();
    if (asprintf (&ctx.rankstr_right, "%dr", ctx.rank) < 0)
        oom ();
    if (ctx.rank == 0)
        ctx.treeroot = true;
    /* If we're missing the wiring, presume that the session is to be
     * started on a single node and compute appropriate ipc:/// sockets.
     */
    if (ctx.size > 1 && !ctx.gevent && !ctx.child
                                    && zlist_size (ctx.parents) == 0) {
        local_boot (&ctx);
    }
    if (ctx.treeroot && zlist_size (ctx.parents) > 0)
        msg_exit ("treeroot must NOT have parent");
    if (!ctx.treeroot && zlist_size (ctx.parents) == 0)
        msg_exit ("non-treeroot must have parents");
    if (ctx.size > 1 && !ctx.gevent)
        msg_exit ("--event-uri is required for size > 1");

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
    /* Ensure a minimal set of modules will be loaded.
     */
    module_prepare (&ctx, "api");
    module_prepare (&ctx, "kvs");
    module_prepare (&ctx, "modctl");
    //module_prepare (&ctx, "live");

    update_proctitle (&ctx);
    update_environment (&ctx);
    update_pidfile (&ctx, fopt);

    if (!nopt && ctx.rank == 0 && (isatty (0) || ctx.shell_cmd))
        rank0_shell (&ctx);

    cmbd_init_socks (&ctx);

    module_loadall (&ctx);

    /* install heartbeat timer
     */
    if (ctx.rank == 0) {
        unsigned long msec = ctx.heartrate * 1000;
        ctx.heartbeat_tid = zloop_timer (ctx.zl, msec, 0,
                                         (zloop_timer_fn *)hb_cb, &ctx);
        if (ctx.heartbeat_tid == -1)
            err_exit ("zloop_timer");
        msg ("Heartrate: T=%0.1fs", ctx.heartrate);
    }

    zloop_start (ctx.zl);

    /* remove heartbeat timer
     */
    if (ctx.rank == 0) {
        zloop_timer_end (ctx.zl, ctx.heartbeat_tid);
    }
    cmbd_fini (&ctx);

    while ((ep = zlist_pop (ctx.parents)))
        endpt_destroy (ep);
    zlist_destroy (&ctx.parents);
    if (ctx.child)
        endpt_destroy (ctx.child);
    if (ctx.rankstr)
        free (ctx.rankstr);
    if (ctx.rankstr_right)
        free (ctx.rankstr_right);
    zhash_destroy (&ctx.peer_idle);
    return 0;
}

static void update_proctitle (ctx_t *ctx)
{
    char *s;
    if (asprintf (&s, "cmbd-%d", ctx->rank) < 0)
        oom ();
    (void)prctl (PR_SET_NAME, s, 0, 0, 0);
    if (ctx->proctitle)
        free (ctx->proctitle);
    ctx->proctitle = s;
}

static void update_environment (ctx_t *ctx)
{
    const char *oldtmp = getenv ("FLUX_TMPDIR");
    static char tmpdir[PATH_MAX + 1];

    if (!oldtmp)
        oldtmp = getenv ("TMPDIR");
    if (!oldtmp)
        oldtmp = "/tmp";
    (void)snprintf (tmpdir, sizeof (tmpdir), "%s/flux-%s-%d",
                    oldtmp, ctx->sid, ctx->rank);
    if (mkdir (tmpdir, 0700) < 0 && errno != EEXIST)
        err_exit ("mkdir %s", tmpdir);
    if (setenv ("FLUX_TMPDIR", tmpdir, 1) < 0)
        err_exit ("setenv FLUX_TMPDIR");
}

static void update_pidfile (ctx_t *ctx, bool force)
{
    const char *tmpdir = getenv ("FLUX_TMPDIR");
    char *pidfile;
    pid_t pid;
    FILE *f;

    if (!tmpdir)
        tmpdir = getenv ("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";

    if (asprintf (&pidfile, "%s/cmbd.pid", tmpdir) < 0)
        oom ();
    if ((f = fopen (pidfile, "r"))) {
        if (fscanf (f, "%u", &pid) == 1 && kill (pid, 0) == 0) {
            if (force) {
                if (kill (pid, SIGKILL) < 0)
                    err_exit ("kill %d", pid);
                msg ("killed cmbd with pid %d", pid);
            } else
                msg_exit ("cmbd is already running in %s, pid %d", tmpdir, pid);
        }
        (void)fclose (f);
    }
    if (!(f = fopen (pidfile, "w+")))
        err_exit ("%s", pidfile);
    if (fprintf (f, "%u", getpid ()) < 0)
        err_exit ("%s", pidfile);
    if (fclose(f) < 0)
        err_exit ("%s", pidfile);
    free (pidfile);
}

static void rank0_shell (ctx_t *ctx)
{
    char *av[] = { getenv ("SHELL"), "-c", ctx->shell_cmd, NULL };

    if (!av[0])
        av[0] = "/bin/bash";
    if (!av[2])
        av[1] = NULL;

    msg ("%s-0: starting shell", ctx->sid);

    switch ((ctx->shell_pid = fork ())) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            if (sigprocmask (SIG_SETMASK, &ctx->default_sigset, NULL) < 0)
                err_exit ("sigprocmask");
            if (execv (av[0], av) < 0)
                err_exit ("execv %s", av[0]);
            break;
        default: /* parent */
            //close (2);
            close (1);
            close (0);
            break;
    }
}

/* N.B. If there are multiple nodes and multiple cmbds per node, the
 * lowest rank in each clique will subscribe to the epgm:// socket
 * and relay events to an ipc:// socket for the other ranks in the
 * clique.  This is required due to a limitation of epgm.
 */
static void pmi_boot (ctx_t *ctx)
{
    pmi_t pmi = pmi_init ("libpmi.so");
    bool relay_needed = (pmi_clique_size (pmi) > 1);
    int relay_rank = pmi_clique_minrank (pmi);
    int right_rank = pmi_rank (pmi) == 0 ? pmi_size (pmi) - 1
                                         : pmi_rank (pmi) - 1;
    char ipaddr[HOST_NAME_MAX + 1];

    ctx->size = pmi_size (pmi);
    ctx->rank = pmi_rank (pmi);
    ctx->sid = xstrdup (pmi_id (pmi));

    pmi_getip (pmi, ipaddr, sizeof (ipaddr));
    ctx->child = endpt_create ("tcp://%s:*", ipaddr);
    cmbd_init_child (ctx, ctx->child); /* obtain dyn port */
    pmi_kvs_put (pmi, ctx->child->uri, "cmbd.%d.uri", ctx->rank);

    if (relay_needed && ctx->rank == relay_rank) {
        ctx->gevent_relay = endpt_create ("ipc://*");
        cmbd_init_gevent_pub (ctx, ctx->gevent_relay); /* obtain dyn port */
        pmi_kvs_put (pmi, ctx->gevent_relay->uri, "cmbd.%d.relay", ctx->rank);
    }

    pmi_kvs_fence (pmi);

    if (ctx->rank > 0) {
        int prank = ctx->k_ary == 0 ? 0 : (ctx->rank - 1) / ctx->k_ary;
        endpt_t *ep = endpt_create (pmi_kvs_get (pmi, "cmbd.%d.uri", prank));
        if (zlist_push (ctx->parents, ep) < 0)
            oom ();
    }

    ctx->right = endpt_create (pmi_kvs_get (pmi, "cmbd.%d.uri", right_rank));

    if (relay_needed && ctx->rank != relay_rank) {
        const char *uri = pmi_kvs_get (pmi, "cmbd.%d.relay", relay_rank);
        ctx->gevent = endpt_create (uri);
    } else {
        int p = 5000 + pmi_appnum (pmi) % 1024;
        ctx->gevent = endpt_create ("epgm://%s;239.192.1.1:%d", ipaddr, p);
    }
    pmi_fini (pmi);
}

static void local_boot (ctx_t *ctx)
{
    const char *tmpdir = getenv ("FLUX_TMPDIR");
    int rrank = ctx->rank == 0 ? ctx->size - 1 : ctx->rank - 1;

    if (!tmpdir)
        tmpdir = getenv ("TMPDIR");
    if (!tmpdir)
        tmpdir = "/tmp";

    ctx->child = endpt_create ("ipc://%s/flux-%s-%d-req",
                               tmpdir, ctx->sid, ctx->rank);
    if (ctx->rank > 0) {
        int prank = ctx->k_ary == 0 ? 0 : (ctx->rank - 1) / ctx->k_ary;
        endpt_t *ep = endpt_create ("ipc://%s/flux-%s-%d-req",
                                    tmpdir, ctx->sid, prank);
        if (zlist_push (ctx->parents, ep) < 0)
            oom ();
    }
    ctx->gevent = endpt_create ("ipc://%s/flux-%s-event",
                                tmpdir, ctx->sid);
    ctx->right = endpt_create ("ipc://%s/flux-%s-%d-req",
                               tmpdir, ctx->sid, rrank);
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

    if (mod->ns)
        nodeset_destroy (mod->ns);
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

static void module_loadall (ctx_t *ctx)
{
    zlist_t *keys = zhash_keys (ctx->modules);
    char *name;
    module_t *mod;

    name = zlist_first (keys);
    while (name) {
        mod = zhash_lookup (ctx->modules, name);
        assert (mod != NULL);
        if (!mod->ns || nodeset_test_rank (mod->ns, ctx->rank)) {
            if (module_load (ctx, mod) < 0)
                err_exit ("failed to load module %s", name);
        } else {
            zhash_delete (ctx->modules, name);
        }
        name = zlist_next (keys);
    }
    zlist_destroy (&keys);
}

static bool module_select (module_t *mod, const char *nstr)
{
    if (!mod->ns)
        mod->ns = nodeset_new ();
    if (!mod->ns)
        oom ();
    return nodeset_add_str (mod->ns, nstr);
}

static module_t *module_prepare (ctx_t *ctx, const char *name)
{
    char *path;
    int flags = 0;
    module_t *mod;

    if (!(mod = zhash_lookup (ctx->modules, name))) {
        if (!(path = modfind (ctx->plugin_path, name)))
            err_exit ("module %s", name);
        if (!(mod = module_create (ctx, path, flags)))
           err_exit ("module %s", name);
        zhash_update (ctx->modules, name, mod);
        zhash_freefn (ctx->modules, name, (zhash_free_fn *)module_destroy);
    }
    return mod;
}

static void module_xsep (char *s, char oldsep, char newsep)
{
    bool inparen = false;
    char *p;

    for (p = s; *p != '\0'; p++) {
        if (*p == '[')
            inparen = true;
        else if (*p == ']')
            inparen = false;
        else if (inparen && *p == oldsep)
            *p = newsep;
    }
}

static void module_prepare_list (ctx_t *ctx, const char *optarg)
{
    char *cpy = xstrdup (optarg);
    char *name, *saveptr = NULL, *a1 = cpy;
    module_t *mod;

    module_xsep (cpy, ',', ';');
    while ((name = strtok_r (a1, ",", &saveptr))) {
        char *p, *nstr = NULL;
        if ((p = strchr (name, '['))) {
            nstr = xstrdup (p);
            *p = '\0';
        }
        mod = module_prepare (ctx, name);
        if (nstr) {
            module_xsep (nstr, ';', ',');
            if (!module_select (mod, nstr))
                msg_exit ("malformed module argument: %s%s", name, nstr);
            free (nstr);
        }
        a1 = NULL;
    }
    free (cpy);
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

static int cmbd_init_child (ctx_t *ctx, endpt_t *ep)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(ep->zs = zsocket_new (ctx->zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
    if (flux_sec_ssockinit (ctx->sec, ep->zs) < 0)
        msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ctx->sec));
    zsocket_set_hwm (ep->zs, 0);
    if (zsocket_bind (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    if (strchr (ep->uri, '*')) { /* capture dynamically assigned port */
        free (ep->uri);
        ep->uri = zsocket_last_endpoint (ep->zs);
    }
    zp.socket = ep->zs;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)request_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return 0;
}

static int cmbd_init_gevent_pub (ctx_t *ctx, endpt_t *ep)
{
    if (!(ep->zs = zsocket_new (ctx->zctx, ZMQ_PUB)))
        err_exit ("zsocket_new");
    if (flux_sec_ssockinit (ctx->sec, ep->zs) < 0) /* no-op for epgm */
        msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ctx->sec));
    zsocket_set_sndhwm (ep->zs, 0);
    if (zsocket_bind (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s: %s", __FUNCTION__, ep->uri);
    if (strchr (ep->uri, '*')) { /* capture dynamically assigned port */
        free (ep->uri);
        ep->uri = zsocket_last_endpoint (ep->zs);
    }
    return 0;
}

static int cmbd_init_gevent_sub (ctx_t *ctx, endpt_t *ep)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(ep->zs = zsocket_new (ctx->zctx, ZMQ_SUB)))
        err_exit ("zsocket_new");
    if (flux_sec_csockinit (ctx->sec, ep->zs) < 0) /* no-op for epgm */
        msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ctx->sec));
    zsocket_set_rcvhwm (ep->zs, 0);
    if (zsocket_connect (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    zsocket_set_subscribe (ep->zs, "");
    zp.socket = ep->zs;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)event_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return 0;
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

static int cmbd_init_parent (ctx_t *ctx, endpt_t *ep)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };
    int savederr;

    if (!(ep->zs = zsocket_new (ctx->zctx, ZMQ_DEALER)))
        goto error;
    if (flux_sec_csockinit (ctx->sec, ep->zs) < 0) {
        savederr = errno;
        msg ("flux_sec_csockinit: %s", flux_sec_errstr (ctx->sec));
        errno = savederr;
        goto error;
    }
    zsocket_set_hwm (ep->zs, 0);
    zsocket_set_identity (ep->zs, ctx->rankstr);
    if (zsocket_connect (ep->zs, "%s", ep->uri) < 0)
        goto error;
    zp.socket = ep->zs;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)parent_cb, ctx) < 0)
        goto error;
    return 0;
error:
    if (ep->zs) {
        savederr = errno;
        zsocket_destroy (ctx->zctx, ep->zs);
        ep->zs = NULL;
        errno = savederr;
    }
    return -1;
}

static int cmbd_init_right (ctx_t *ctx, endpt_t *ep)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(ep->zs = zsocket_new (ctx->zctx, ZMQ_DEALER)))
        err_exit ("zsocket_new");
    if (flux_sec_csockinit (ctx->sec, ep->zs) < 0)
        msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ctx->sec));
    zsocket_set_hwm (ep->zs, 0);
    zsocket_set_identity (ep->zs, ctx->rankstr_right);
    if (zsocket_connect (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    zp.socket = ep->zs;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)parent_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return 0;
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
    if (sigprocmask (SIG_SETMASK, &sigmask, &ctx->default_sigset) < 0)
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

static void cmbd_init_comms (ctx_t *ctx)
{
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
    if (flux_sec_zauth_init (ctx->sec, ctx->zctx, "flux") < 0)
        msg_exit ("flux_sec_zauth_init: %s", flux_sec_errstr (ctx->sec));
    if (flux_sec_munge_init (ctx->sec) < 0)
        msg_exit ("flux_sec_munge_init: %s", flux_sec_errstr (ctx->sec));
}

static void cmbd_init_socks (ctx_t *ctx)
{
    /* Bind to downstream ports.
     */
    ctx->zs_request = cmbd_init_request (ctx);
    ctx->zs_event_out = cmbd_init_event_out (ctx);
    ctx->zs_snoop = cmbd_init_snoop (ctx);

    if (ctx->rank == 0 && ctx->gevent)
        cmbd_init_gevent_pub (ctx, ctx->gevent);
    if (ctx->rank > 0 && ctx->gevent)
        cmbd_init_gevent_sub (ctx, ctx->gevent);
    if (ctx->child && !ctx->child->zs)      /* pmi_boot may have done this */
        cmbd_init_child (ctx, ctx->child);
    if (ctx->right)
        cmbd_init_right (ctx, ctx->right);
    /* N.B. pmi_boot may have created a gevent relay too - no work to do here */
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
    endpt_t *ep;
    if ((ep = zlist_first (ctx->parents))) {
        if (cmbd_init_parent (ctx, ep) < 0)
            err_exit ("%s", ep->uri);
    }

    /* create flux_t handle */
    ctx->h = handle_create (ctx, &cmbd_handle_ops, 0);
    flux_log_set_facility (ctx->h, "cmbd");
}

static void cmbd_fini (ctx_t *ctx)
{
    zhash_destroy (&ctx->modules);
    if (ctx->sec)
        flux_sec_destroy (ctx->sec);
    zloop_destroy (&ctx->zl);
    zctx_destroy (&ctx->zctx); /* destorys all sockets created in ctx */
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

static void cmb_heartbeat (ctx_t *ctx, zmsg_t *zmsg)
{
    json_object *event = NULL;

    if (ctx->rank == 0)
        return;

    if (cmb_msg_decode (zmsg, NULL, &event) < 0 || event == NULL
           || util_json_object_get_int (event, "epoch", &ctx->hb_epoch) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: bad hb message", __FUNCTION__);
    }

    /* If we've not sent anything to our parent, send a cmb.hb
     * to update our idle time.
     */
    if (self_idle (ctx) > 0)
        flux_request_send (ctx->h, NULL, "cmb.hb");
}

static endpt_t *endpt_create (const char *fmt, ...)
{
    endpt_t *ep = xzmalloc (sizeof (*ep));
    va_list ap;
    va_start (ap, fmt);
    if (vasprintf (&ep->uri, fmt, ap) < 0)
        oom ();
    va_end (ap);
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
        if (cmbd_init_parent (ctx, ep) < 0) {
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

static int shutdown_cb (zloop_t *zl, int timer_id, ctx_t *ctx)
{
    exit (ctx->shutdown_exitcode);
}

static void shutdown_recv (ctx_t *ctx, zmsg_t *zmsg)
{
    json_object *o = NULL;
    const char *reason;
    int grace, rank, exitcode;

    if (cmb_msg_decode (zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "reason", &reason) < 0
            || util_json_object_get_int (o, "grace", &grace) < 0
            || util_json_object_get_int (o, "exitcode", &exitcode) < 0
            || util_json_object_get_int (o, "rank", &rank) < 0) {
        msg ("ignoring mangled shutdown message");
    } else if (ctx->shutdown_tid == -1) {
        ctx->shutdown_tid = zloop_timer (ctx->zl, grace * 1000, 1,
                                        (zloop_timer_fn *)shutdown_cb, ctx);
        if (ctx->shutdown_tid == -1)
            err_exit ("zloop_timer");
        ctx->shutdown_exitcode = exitcode;
        if (ctx->rank == 0)
            msg ("%d: shutdown in %ds: %s", rank, grace, reason);
    }
    if (o)
        json_object_put (o);
}

static void shutdown_send (ctx_t *ctx, int grace, int rc, const char *fmt, ...)
{
    zmsg_t *event;
    json_object *o = util_json_object_new_object ();
    va_list ap;
    char *reason;

    va_start (ap, fmt);
    if (vasprintf (&reason, fmt, ap) < 0)
        oom ();
    va_end (ap);
    util_json_object_add_string (o, "reason", reason);
    util_json_object_add_int (o, "grace", grace);
    util_json_object_add_int (o, "rank", ctx->rank);
    util_json_object_add_int (o, "exitcode", rc);
    if (!(event = cmb_msg_encode ("shutdown", o)))
        oom ();
    (void)cmb_pub_event (ctx, &event);
    free (reason);
}

static void cmb_internal_event (ctx_t *ctx, zmsg_t *zmsg)
{
    if (cmb_msg_match (zmsg, "hb"))
        cmb_heartbeat (ctx, zmsg);
    else if (cmb_msg_match (zmsg, "shutdown"))
        shutdown_recv (ctx, zmsg);
}

static int cmb_pub_event (ctx_t *ctx, zmsg_t **event)
{
    int rc = -1;
    zmsg_t *cpy = NULL;

    /* Publish event globally (if configured)
    */
    if (ctx->gevent) {
        if (!(cpy = zmsg_dup (*event)))
            oom ();
        if (strstr (ctx->gevent->uri, "pgm://")) {
            if (flux_sec_munge_zmsg (ctx->sec, &cpy) < 0) {
                if (errno == 0)
                    errno = EIO;
                goto done;
            }
        }
        if (zmsg_send (&cpy, ctx->gevent->zs) < 0) {
            if (errno == 0)
                errno = EIO;
            goto done;
        }
    }
    /* Publish event locally
    */
    snoop_cc (ctx, FLUX_MSGTYPE_EVENT, *event);
    cmb_internal_event (ctx, *event);
    relay_cc (ctx, *event);
    if (zmsg_send (event, ctx->zs_event_out) < 0) {
        if (errno == 0)
            errno = EIO;
        goto done;
    }
    rc = 0;
done:
    if (cpy)
        zmsg_destroy (&cpy);
    return rc;
}

/* Unwrap event from cmb.pub request and publish.
 */
static int cmb_pub (ctx_t *ctx, zmsg_t **zmsg)
{
    json_object *payload, *request = NULL;
    const char *topic;
    zmsg_t *event = NULL;
    int rc = -1;

    assert (ctx->rank == 0);
    assert (ctx->zs_event_out != NULL);

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || !request) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (util_json_object_get_string (request, "topic", &topic) < 0
            || !(payload = json_object_object_get (request, "payload"))) {
        flux_respond_errnum (ctx->h, zmsg, EINVAL);
        goto done;
    }
    if (!(event = cmb_msg_encode ((char *)topic, payload))) {
        flux_respond_errnum (ctx->h, zmsg, EINVAL);
        goto done;
    }
    if (cmb_pub_event (ctx, &event) < 0) {
        flux_respond_errnum (ctx->h, zmsg, errno);
        goto done;
    }
    flux_respond_errnum (ctx->h, zmsg, 0);
    rc = 0;
done:
    if (request)
        json_object_put (request);
    if (event)
        zmsg_destroy (&event);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return rc;
}

static void rankfwd_rewrap (zmsg_t **zmsg, const char **tp, json_object **pp)
{
    zframe_t *zf[2];
    const char *s;
    int i;

    for (i = 0; i < 2; i++) {
        zf[i] = zmsg_last (*zmsg);
        assert (zf[i] != NULL);
        zmsg_remove (*zmsg, zf[i]);
    }
    if (zmsg_addstr (*zmsg, *tp) < 0)
        oom ();
    if (*pp && (s = json_object_to_json_string (*pp)))
        if (zmsg_addstr (*zmsg, s) < 0)
            oom ();
    for (i = 0; i < 2; i++) /* destroys *tp and *pp */
        zframe_destroy (&zf[i]);
    *pp = NULL;
    *tp = NULL;
}

static bool rankfwd_looped (ctx_t *ctx, zmsg_t *zmsg)
{
    zframe_t *zf;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) > 0) {
        if (zframe_streq (zf, ctx->rankstr_right))
            return true;
        zf = zmsg_next (zmsg);
    }
    return false;
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
    } else if (cmb_msg_match (*zmsg, "cmb.hb")) {
        /* no-op used to update peer idle time - no response */
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
    } else if (cmb_msg_match (*zmsg, "cmb.pub")) {
        if (ctx->rank > 0) {
            if (flux_request_sendmsg (ctx->h, zmsg) < 0)
                flux_respond_errnum (ctx->h, zmsg, errno);
        } else {
            if (cmb_pub (ctx, zmsg) < 0)
                flux_respond_errnum (ctx->h, zmsg, errno);
        }
    } else if (cmb_msg_match (*zmsg, "cmb.rankfwd")) {
        json_object *payload, *request = NULL;
        int rank;
        const char *topic;
        if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
                || util_json_object_get_int (request, "rank", &rank) < 0
                || util_json_object_get_string (request, "topic", &topic) < 0
                || !(payload = json_object_object_get (request, "payload"))) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else if (rank == ctx->rank) {
            char *p, *service = xstrdup (topic);
            module_t *mod;
            if ((p = strchr (service, '.')))
                *p = '\0'; 
            rankfwd_rewrap (zmsg, &topic, &payload);
            if (!strcmp (service, "cmb")) {
                cmb_internal_request (ctx, zmsg);
            } else if ((mod = zhash_lookup (ctx->modules, service))) {
                if (zmsg_send (zmsg, plugin_sock (mod->p)) < 0)
                    flux_respond_errnum (ctx->h, zmsg, errno);
            } else
                flux_respond_errnum (ctx->h, zmsg, EHOSTUNREACH);
            free (service);
        } else if (!ctx->right || rankfwd_looped (ctx, *zmsg)) {
            rankfwd_rewrap (zmsg, &topic, &payload);
            flux_respond_errnum (ctx->h, zmsg, EHOSTUNREACH);
        } else if (zmsg_send (zmsg, ctx->right->zs) < 0) {
            flux_respond_errnum (ctx->h, zmsg, errno);
        }
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
    zmsg_t *zmsg = zmsg_recv (item->socket);
    if (zmsg) {
        if (strstr (ctx->gevent->uri, "pgm://")) {
            if (flux_sec_unmunge_zmsg (ctx->sec, &zmsg) < 0) {
                zmsg_destroy (&zmsg);
                goto done;
            }
        }
        relay_cc (ctx, zmsg);
        snoop_cc (ctx, FLUX_MSGTYPE_EVENT, zmsg);
        cmb_internal_event (ctx, zmsg);
        zmsg_send (&zmsg, ctx->zs_event_out);
    }
done:
    ZLOOP_RETURN(ctx);
}

static int hb_cb (zloop_t *zl, int timer_id, ctx_t *ctx)
{
    zmsg_t *zmsg = NULL;
    json_object *o = NULL;

    assert (ctx->rank == 0);
    assert (timer_id == ctx->heartbeat_tid);

    o = util_json_object_new_object ();
    util_json_object_add_int (o, "epoch", ++ctx->hb_epoch);
    if (!(zmsg = cmb_msg_encode ("hb", o))) {
        err ("cmb_msg_encode failed");
        goto done;
    }
    if (cmb_pub_event (ctx, &zmsg) < 0) {
        err ("cmb_pub_event failed");
        goto done;
    }
done:
    if (o)
        json_object_put (o);
    if (zmsg)
        zmsg_destroy (&zmsg);
    ZLOOP_RETURN(ctx);
}

static char *decode_status (ctx_t *ctx, const char *name, pid_t pid,
                            int status, int *rcp)
{
    int rc = 0;
    char *s;

    if (WIFEXITED (status)) {
        rc = WEXITSTATUS (status);
        if (asprintf (&s, "%s (pid %d) exited with rc=%d", name, pid, rc) < 0)
            oom ();
    } else if (WIFSIGNALED (status)) {
        if (asprintf (&s, "%s (pid %d) terminated by %s",
                      name, pid, strsignal (WTERMSIG (status))) < 0)
            oom ();
        rc = 128 + WTERMSIG (status); /* POSIX 2008, Vol. 3, p 74314 */
    } else {
        if (asprintf (&s, "%s (pid %d) wait status=%d", name, pid, status) < 0)
            oom ();
    }
    *rcp = rc;
    return s;
}

static void reap_all_children (ctx_t *ctx)
{
    pid_t pid;
    int status, rc;
    char *s;
    while ((pid = waitpid ((pid_t) -1, &status, WNOHANG)) > (pid_t)0) {
        if (pid == ctx->shell_pid) {
            s = decode_status (ctx, "shell", pid, status, &rc);
            shutdown_send (ctx, 2, rc, "%s", s);
        } else {
            s = decode_status (ctx, "child", pid, status, &rc);
            msg ("%s", s);
        }
        free (s);
    }
}

static int signal_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    struct signalfd_siginfo fdsi;
    ssize_t n;

    if ((n = read (item->fd, &fdsi, sizeof (fdsi))) < 0) {
        if (errno != EWOULDBLOCK)
            err_exit ("read");
    } else if (n == sizeof (fdsi)) {
        if (fdsi.ssi_signo == SIGCHLD)
            reap_all_children (ctx);
        else {
            shutdown_send (ctx, 2, 0, "signal %d (%s) %d",
                           fdsi.ssi_signo, strsignal (fdsi.ssi_signo));
        }
    }
    ZLOOP_RETURN(ctx);
}

static int relay_cc (ctx_t *ctx, zmsg_t *zmsg)
{
    zmsg_t *cpy;
    if (!zmsg || !ctx->gevent_relay)
        return 0;
    if (!(cpy = zmsg_dup (zmsg)))
        err_exit ("zmsg_dup");
    return zmsg_send (&cpy, ctx->gevent_relay->zs);
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
    if (zframe_size (zf) == 0) { /* response addressed to me */
        rc = -1;                 /*   add hook here if we need it */
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
