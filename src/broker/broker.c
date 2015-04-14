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
#include <sys/param.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <dlfcn.h>
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/zdump.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/subprocess.h"

#include "module.h"
#include "boot_pmi.h"
#include "endpt.h"

#ifndef ZMQ_IMMEDIATE
#define ZMQ_IMMEDIATE           ZMQ_DELAY_ATTACH_ON_CONNECT
#define zsocket_set_immediate   zsocket_set_delay_attach_on_connect
#endif

#define ZLOOP_RETURN(p) \
    return ((p)->reactor_stop ? (-1) : (0))

const char *default_modules =
    "api,modctl,kvs,live,mecho,job[0],wrexec,resrc,barrier";

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

    endpt_t *modrequest;        /* ROUTER - requests from modules */
    endpt_t *modevent;          /* PUB - events to modules */

    endpt_t *gevent;            /* PUB for rank = 0, SUB for rank > 0 */
    endpt_t *gevent_relay;      /* PUB event relay for multiple brokers/node */

    endpt_t *snoop;             /* PUB - to flux-snoop (uri is generated) */
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
    char *module_searchpath;    /* colon-separated list of directories */
    zhash_t *modules;           /* hash of module_t's by name */
    /* Misc
     */
    bool verbose;
    bool quiet;
    flux_t h;
    pid_t pid;
    zhash_t *peer_idle;         /* peer (hopcount=1) hb idle time, by uuid */
    int hb_lastreq;             /* hb epoch of last upstream request */
    char *proctitle;
    sigset_t default_sigset;
    flux_conf_t cf;
    const char *secdir;
    int event_seq;
    bool event_active;          /* primary event source is active */
    /* Bootstrap
     */
    bool boot_pmi;
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

    /* Subprocess management
     */
    struct subprocess_manager *sm;

    char *shell_cmd;
    struct subprocess *shell;
} ctx_t;

/* Wrapper object for mod_ctx_t that allows 'rmmod' requests to be
 * queued up and responded to asynchronously.
 */
typedef struct {
    mod_ctx_t p;
    zlist_t *rmmod_reqs;
    ctx_t *ctx;
} module_t;

typedef struct {
    int hb_lastseen;            /* epoch peer was last heard from */
    bool modflag;               /* true if this peer is a comms module */
    bool event_mute;            /* stop CC'ing events over this connection */
} peer_t;

static int event_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int plugins_cb (zloop_t *zl, zmq_pollitem_t *item, module_t *mod);
static int parent_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int request_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);
static int hb_cb (zloop_t *zl, int timer_id, ctx_t *ctx);
static int signal_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx);

static void broker_init_comms (ctx_t *ctx);
static void broker_init_overlay (ctx_t *ctx);
static void broker_init_modsocks (ctx_t *ctx);
static int broker_init_child (ctx_t *ctx, endpt_t *ep);
static int broker_init_gevent_pub (ctx_t *ctx, endpt_t *ep);

static void load_modules (ctx_t *ctx, zlist_t *modules, zlist_t *modopts,
                          zhash_t *modexclude);

static int peer_idle (ctx_t *ctx, const char *uuid);
static void peer_update (ctx_t *ctx, const char *uuid);
static peer_t *peer_create (ctx_t *ctx, const char *uuid, bool modflag);

static int recv_event (ctx_t *ctx, zmsg_t **zmsg);
static int send_event (ctx_t *ctx, JSON o, const char *topic);
static int parent_send (ctx_t *ctx, zmsg_t **zmsg);
static void send_keepalive (ctx_t *ctx);

static void update_proctitle (ctx_t *ctx);
static void update_environment (ctx_t *ctx);
static void update_pidfile (ctx_t *ctx, bool force);
static void rank0_shell (ctx_t *ctx);
static void boot_pmi (ctx_t *ctx);
static void boot_local (ctx_t *ctx);
static int shell_exit_handler (struct subprocess *p, void *arg);

static const double min_heartrate = 0.01;   /* min seconds */
static const double max_heartrate = 30;     /* max seconds */
static const double dfl_heartrate = 2;

static const struct flux_handle_ops broker_handle_ops;

#define OPTIONS "t:vqR:S:p:M:X:L:N:Pk:e:r:s:c:fnH:O:x:"
static const struct option longopts[] = {
    {"sid",             required_argument,  0, 'N'},
    {"child-uri",       required_argument,  0, 't'},
    {"parent-uri",      required_argument,  0, 'p'},
    {"right-uri",       required_argument,  0, 'r'},
    {"verbose",         no_argument,        0, 'v'},
    {"quiet",           no_argument,        0, 'q'},
    {"security",        required_argument,  0, 's'},
    {"rank",            required_argument,  0, 'R'},
    {"size",            required_argument,  0, 'S'},
    {"module",          required_argument,  0, 'M'},
    {"exclude",         required_argument,  0, 'x'},
    {"modopt",          required_argument,  0, 'O'},
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
"Usage: flux-broker OPTIONS [module:key=val ...]\n"
" -t,--child-uri URI           Set child URI to bind and receive requests\n"
" -p,--parent-uri URI          Set parent URI to connect and send requests\n"
" -e,--event-uri URI           Set event URI (pub: rank 0, sub: rank > 0)\n"
" -r,--right-uri URI           Set right (rank-request) URI\n"
" -v,--verbose                 Be annoyingly verbose\n"
" -q,--quiet                   Be mysteriously taciturn\n"
" -R,--rank N                  Set broker rank (0...size-1)\n"
" -S,--size N                  Set number of ranks in session\n"
" -N,--sid NAME                Set session id\n"
" -M,--module NAME             Load module NAME (may be repeated)\n"
" -x,--exclude NAME            Exclude module NAME\n"
" -O,--modopt NAME:key=val     Set option for module NAME (may be repeated)\n"
" -X,--module-path PATH        Set module search path (colon separated)\n"
" -L,--logdest DEST            Log to DEST, can  be syslog, stderr, or file\n"
" -s,--security=plain|curve|none    Select security mode (default: curve)\n"
" -P,--pmi-boot                Bootstrap via PMI\n"
" -k,--k-ary K                 Wire up in a k-ary tree\n"
" -c,--command string          Run command on rank 0\n"
" -n,--noshell                 Do not spawn a shell even if on a tty\n"
" -f,--force                   Kill rival broker and start\n"
" -H,--heartrate SECS          Set heartrate in seconds (rank 0 only)\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int c;
    ctx_t ctx;
    endpt_t *ep;
    bool fopt = false;
    bool nopt = false;
    zlist_t *modules, *modopts;
    zhash_t *modexclude;
    const char *confdir;

    memset (&ctx, 0, sizeof (ctx));
    log_init (argv[0]);

    if (!(modules = zlist_new ()))
        oom ();
    zlist_autofree (modules);
    if (!(modexclude = zhash_new ()))
        oom ();
    if (!(modopts = zlist_new ()))
        oom ();
    zlist_autofree (modopts);

    ctx.size = 1;
    if (!(ctx.modules = zhash_new ()))
        oom ();
    if (!(ctx.peer_idle = zhash_new ()))
        oom ();
    if (!(ctx.parents = zlist_new ()))
        oom ();
    ctx.k_ary = 2; /* binary TBON is default */

    ctx.pid = getpid();
    ctx.module_searchpath = getenv ("FLUX_MODULE_PATH");
    if (!ctx.module_searchpath)
        ctx.module_searchpath = MODULE_PATH;
    ctx.heartrate = dfl_heartrate;
    ctx.shutdown_tid = -1;

    if (!(ctx.sm = subprocess_manager_create ()))
        oom ();
    subprocess_manager_set (ctx.sm, SM_WAIT_FLAGS, WNOHANG);

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
                ctx.child = endpt_create ("%s", optarg);
                break;
            case 'p': { /* --parent-uri URI */
                endpt_t *ep = endpt_create ("%s", optarg);
                if (zlist_push (ctx.parents, ep) < 0)
                    oom ();
                break;
            }
            case 'v':   /* --verbose */
                ctx.verbose = true;
                break;
            case 'q':   /* --quiet */
                ctx.quiet = true;
                break;
            case 'R':   /* --rank N */
                ctx.rank = strtoul (optarg, NULL, 10);
                break;
            case 'S':   /* --size N */
                ctx.size = strtoul (optarg, NULL, 10);
                break;
            case 'M':   /* --module NAME[nodeset] */
                if (zlist_push (modules, optarg) < 0 )
                    oom ();
                break;
            case 'x':   /* --exclude NAME */
                zhash_update (modexclude, optarg, (void *)1);
                break;
            case 'O':   /* --modopt NAME:key=val */
                if (zlist_push (modopts, optarg) < 0)
                    oom ();
                break;
            case 'X':   /* --module-path PATH */
                ctx.module_searchpath = optarg;
                break;
            case 'L':   /* --logdest DEST */
                log_set_dest (optarg);
                break;
            case 'P':   /* --pmi-boot */
                ctx.boot_pmi = true;
                break;
            case 'k':   /* --k-ary k */
                ctx.k_ary = strtoul (optarg, NULL, 10);
                if (ctx.k_ary < 0)
                    usage ();
                break;
            case 'e':   /* --event-uri URI */
                if (ctx.gevent)
                    endpt_destroy (ctx.gevent);
                ctx.gevent = endpt_create ("%s", optarg);
                break;
            case 'r':   /* --right-uri */
                if (ctx.right)
                    endpt_destroy (ctx.right);
                ctx.right = endpt_create ("%s", optarg);
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
    if (argc != optind)
        usage ();

    /* Add default modules to user-specified module list
     */
    if (default_modules) {
        char *cpy = xstrdup (default_modules);
        char *name, *saveptr = NULL, *a1 = cpy;
        while ((name = strtok_r (a1, ",", &saveptr))) {
            if (zlist_append (modules, xstrdup (name)))
                oom ();
            a1 = NULL;
        }
        free (cpy);
    }

    /* Get the directory for CURVE keys.
     */
    if (!(ctx.secdir = getenv ("FLUX_SEC_DIRECTORY")))
        msg_exit ("FLUX_SEC_DIRECTORY is not set"); 

    /* Process config from the KVS if running in a session and not
     * forced to use a config file by the command line.
     */
    ctx.cf = flux_conf_create ();
    if (!(confdir = getenv ("FLUX_CONF_DIRECTORY")))
        msg_exit ("FLUX_CONF_DIRECTORY is not set");
    flux_conf_set_directory (ctx.cf, confdir);
    if (getenv ("FLUX_CONF_USEFILE")) {
        if (ctx.verbose)
            msg ("Loading config from %s", confdir);
        if (flux_conf_load (ctx.cf) < 0 && errno != ESRCH)
            err_exit ("%s", confdir);
    } else if (getenv ("FLUX_TMPDIR")) {
        flux_t h;
        if (ctx.verbose)
            msg ("Loading config from KVS");
        if (!(h = flux_api_open ()))
            err_exit ("flux_api_open");
        if (kvs_conf_load (h, ctx.cf) < 0)
            err_exit ("could not load config from KVS");
        flux_api_close (h);
    }

    /* Arrange to load config entries into kvs config.*
     */
    flux_conf_itr_t itr = flux_conf_itr_create (ctx.cf);
    const char *key;
    while ((key = flux_conf_next (itr))) {
        char *opt = xasprintf ("kvs:config.%s=%s",
                               key, flux_conf_get (ctx.cf, key));
        zlist_push (modopts, opt);
        free (opt);
    }
    flux_conf_itr_destroy (itr);

    /* Create zeromq context, security context, zloop, etc.
     */
    broker_init_comms (&ctx);

    /* Sets rank, size, parent URI.
     * Initialize child socket.
     */
    if (ctx.boot_pmi) {
        if (ctx.child)
            msg_exit ("--child-uri should not be specified with --pmi-boot");
        if (zlist_size (ctx.parents) > 0)
            msg_exit ("--parent-uri should not be specified with --pmi-boot");
        if (ctx.gevent)
            msg_exit ("--event-uri should not be specified with --pmi-boot");
        if (ctx.sid)
            msg_exit ("--session-id should not be specified with --pmi-boot");
        boot_pmi (&ctx);
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
        boot_local (&ctx);
    }
    if (ctx.treeroot && zlist_size (ctx.parents) > 0)
        msg_exit ("treeroot must NOT have parent");
    if (!ctx.treeroot && zlist_size (ctx.parents) == 0)
        msg_exit ("non-treeroot must have parents");
    if (ctx.size > 1 && !ctx.gevent)
        msg_exit ("--event-uri is required for size > 1");

    if (ctx.verbose) {
        endpt_t *ep = zlist_first (ctx.parents);
        if (ep)
            msg ("parent: %s", ep->uri);
        if (ctx.child)
            msg ("child: %s", ctx.child->uri);
        if (ctx.gevent)
            msg ("gevent: %s", ctx.gevent->uri);
        if (ctx.gevent_relay)
            msg ("gevent-relay: %s", ctx.gevent_relay->uri);
    }

    update_proctitle (&ctx);
    update_environment (&ctx);
    update_pidfile (&ctx, fopt);

    if (!nopt && ctx.rank == 0 && (isatty (STDIN_FILENO) || ctx.shell_cmd)) {
        ctx.shell = subprocess_create (ctx.sm);
        subprocess_set_callback (ctx.shell, shell_exit_handler, &ctx);
    }

    if (ctx.verbose)
        msg ("initializing overlay sockets");
    broker_init_overlay (&ctx); /* NOTE: after this, ctx->h is usable */

    if (ctx.verbose)
        msg ("initializing module sockets");
    broker_init_modsocks (&ctx);

    if (ctx.verbose)
        msg ("loading modules");
    if (ctx.verbose)
        msg ("module-path: %s", ctx.module_searchpath);
    load_modules (&ctx, modules, modopts, modexclude);

    /* install heartbeat timer
     */
    if (ctx.rank == 0) {
        unsigned long msec = ctx.heartrate * 1000;
        ctx.heartbeat_tid = zloop_timer (ctx.zl, msec, 0,
                                         (zloop_timer_fn *)hb_cb, &ctx);
        if (ctx.heartbeat_tid == -1)
            err_exit ("zloop_timer");
        if (ctx.verbose)
            msg ("installing session heartbeat: T=%0.1fs", ctx.heartrate);
    }

    /* Send an initial keepalive message to parent, if any.
     */
    if (ctx.rank > 0)
        send_keepalive (&ctx);

    /* Event loop
     */
    if (ctx.verbose)
        msg ("entering event loop");
    zloop_start (ctx.zl);
    if (ctx.verbose)
        msg ("exited event loop");

    /* remove heartbeat timer
     */
    if (ctx.rank == 0)
        zloop_timer_end (ctx.zl, ctx.heartbeat_tid);

    /* Unload modules.
     * FIXME: this will hang in pthread_join unless modules have been stopped.
     */
    if (ctx.verbose)
        msg ("unloading modules");
    zhash_destroy (&ctx.modules);

    if (ctx.verbose)
        msg ("cleaning up");
    if (ctx.sec)
        flux_sec_destroy (ctx.sec);
    zloop_destroy (&ctx.zl);
    zctx_destroy (&ctx.zctx);
    while ((ep = zlist_pop (ctx.parents)))
        endpt_destroy (ep);
    zlist_destroy (&ctx.parents);
    if (ctx.child)
        endpt_destroy (ctx.child);
    if (ctx.rankstr)
        free (ctx.rankstr);
    if (ctx.rankstr_right)
        free (ctx.rankstr_right);
    if (ctx.snoop)
        endpt_destroy (ctx.snoop);
    if (ctx.modrequest)
        endpt_destroy (ctx.modrequest);
    if (ctx.modevent)
        endpt_destroy (ctx.modevent);
    zhash_destroy (&ctx.peer_idle);

    zlist_destroy (&modules);
    zhash_destroy (&modexclude);
    zlist_destroy (&modopts);
    return 0;
}

static void update_proctitle (ctx_t *ctx)
{
    char *s;
    if (asprintf (&s, "flux-broker-%d", ctx->rank) < 0)
        oom ();
    (void)prctl (PR_SET_NAME, s, 0, 0, 0);
    if (ctx->proctitle)
        free (ctx->proctitle);
    ctx->proctitle = s;
}

static void update_environment (ctx_t *ctx)
{
    const char *oldtmp = flux_get_tmpdir ();
    static char tmpdir[PATH_MAX + 1];

    (void)snprintf (tmpdir, sizeof (tmpdir), "%s/flux-%s-%d",
                    oldtmp, ctx->sid, ctx->rank);
    if (mkdir (tmpdir, 0700) < 0 && errno != EEXIST)
        err_exit ("mkdir %s", tmpdir);
    if (ctx->verbose)
        msg ("FLUX_TMPDIR: %s", tmpdir);
    if (flux_set_tmpdir (tmpdir) < 0)
        err_exit ("flux_set_tmpdir");
}

static void update_pidfile (ctx_t *ctx, bool force)
{
    const char *tmpdir = flux_get_tmpdir ();
    char *pidfile;
    pid_t pid;
    FILE *f;

    if (asprintf (&pidfile, "%s/broker.pid", tmpdir) < 0)
        oom ();
    if ((f = fopen (pidfile, "r"))) {
        if (fscanf (f, "%u", &pid) == 1 && kill (pid, 0) == 0) {
            if (force) {
                if (kill (pid, SIGKILL) < 0)
                    err_exit ("kill %d", pid);
                msg ("killed broker with pid %d", pid);
            } else
                msg_exit ("broker is already running in %s, pid %d", tmpdir, pid);
        }
        (void)fclose (f);
    }
    if (!(f = fopen (pidfile, "w+")))
        err_exit ("%s", pidfile);
    if (fprintf (f, "%u", getpid ()) < 0)
        err_exit ("%s", pidfile);
    if (fclose(f) < 0)
        err_exit ("%s", pidfile);
    if (ctx->verbose)
        msg ("pidfile: %s", pidfile);
    free (pidfile);
}

/* If fd 0 gets recycled and used in a zloop, zmq 4.0.4 will assert
 */
void work_around_zmq_poll_bug (void)
{
    int fd = open ("/dev/null", O_RDONLY);
    if (fd < 0)
        err_exit ("/dev/null");
    if (fd != 0 && dup2 (fd, 0) < 0)
        msg ("failed to re-acquire stdin fileno - zmq_poll may be sad!");
    /* leak fd on purpose */
}

static void rank0_shell (ctx_t *ctx)
{
    const char *shell = getenv ("SHELL");

    if (!shell)
        shell = "/bin/bash";

    subprocess_argv_append (ctx->shell, shell);
    if (ctx->shell_cmd) {
        subprocess_argv_append (ctx->shell, "-c");
        subprocess_argv_append (ctx->shell, ctx->shell_cmd);
    }
    subprocess_set_environ (ctx->shell, environ);

    if (!ctx->quiet)
        msg ("%s-0: starting shell", ctx->sid);

    subprocess_run (ctx->shell);
}

/* N.B. If there are multiple nodes and multiple brokers per node, the
 * lowest rank in each clique will subscribe to the epgm:// socket
 * and relay events to an ipc:// socket for the other ranks in the
 * clique.  This is required due to a limitation of epgm.
 */
static void boot_pmi (ctx_t *ctx)
{
    pmi_t pmi = pmi_init (NULL);
    int relay_rank = pmi_relay_rank (pmi);
    int right_rank = pmi_right_rank (pmi);
    char ipaddr[HOST_NAME_MAX + 1];

    ctx->size = pmi_size (pmi);
    ctx->rank = pmi_rank (pmi);
    ctx->sid = xstrdup (pmi_sid (pmi));

    ipaddr_getprimary (ipaddr, sizeof (ipaddr));
    ctx->child = endpt_create ("tcp://%s:*", ipaddr);
    broker_init_child (ctx, ctx->child); /* obtain dyn port */
    pmi_put_uri (pmi, ctx->rank, ctx->child->uri);

    if (relay_rank >= 0 && ctx->rank == relay_rank) {
        ctx->gevent_relay = endpt_create ("ipc://*");
        broker_init_gevent_pub (ctx, ctx->gevent_relay); /* obtain dyn port */
        pmi_put_relay (pmi, ctx->rank, ctx->gevent_relay->uri);
    }

    pmi_fence (pmi);

    if (ctx->rank > 0) {
        int prank = ctx->k_ary == 0 ? 0 : (ctx->rank - 1) / ctx->k_ary;
        endpt_t *ep = endpt_create ("%s", pmi_get_uri (pmi, prank));
        if (zlist_push (ctx->parents, ep) < 0)
            oom ();
    }

    ctx->right = endpt_create ("%s", pmi_get_uri (pmi, right_rank));

    if (relay_rank >= 0 && ctx->rank != relay_rank) {
        ctx->gevent = endpt_create ("%s", pmi_get_relay (pmi, relay_rank));
    } else {
        int p = 5000 + pmi_jobid (pmi) % 1024;
        ctx->gevent = endpt_create ("epgm://%s;239.192.1.1:%d", ipaddr, p);
    }
    pmi_fini (pmi);
}

static void boot_local (ctx_t *ctx)
{
    const char *tmpdir = flux_get_tmpdir ();
    int rrank = ctx->rank == 0 ? ctx->size - 1 : ctx->rank - 1;

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

static module_t *module_create (ctx_t *ctx, const char *path)
{
    module_t *mod = xzmalloc (sizeof (*mod));

    if (!(mod->p = mod_create (ctx->zctx, ctx->rank, path)))
        goto done;
    if (!(mod->rmmod_reqs = zlist_new ()))
        oom ();
    mod->ctx = ctx;
done:
    return mod;
}

/* Registered as the zhash_free_fn for ctx->modules.
 * It will hang in pthread_join unless modules have been stopped.
 */
static void module_destroy (module_t *mod)
{
    zmsg_t *zmsg;

    if (mod->p) {
        zmq_pollitem_t zp;
        zp.socket = mod_sock (mod->p);
        zloop_poller_end (mod->ctx->zl, &zp);
        mod_destroy (mod->p); /* calls pthread_join */
    }
    while ((zmsg = zlist_pop (mod->rmmod_reqs)))
        flux_respond_errnum (mod->ctx->h, &zmsg, 0);
    zlist_destroy (&mod->rmmod_reqs);

    free (mod);
}

static void module_stop (module_t *mod, zmsg_t **zmsg)
{
    if (zmsg) {
        zlist_push (mod->rmmod_reqs, *zmsg);
        *zmsg = NULL;
    }
    mod_stop (mod->p);
}

static int module_start (ctx_t *ctx, module_t *mod)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    peer_create (ctx, mod_uuid (mod->p), true);
    zp.socket = mod_sock (mod->p);
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)plugins_cb, mod) < 0)
        err_exit ("zloop_poller");
    mod_start (mod->p);

    return 0;
}

static bool nodeset_suffix_member (char *name, uint32_t rank)
{
    char *s;
    nodeset_t ns;
    bool member = true;

    if ((s = strchr (name, '['))) {
        if (!(ns = nodeset_new_str (s)))
            msg_exit ("malformed nodeset suffix in '%s'", name);
        *s = '\0'; /* side effect: truncate nodeset suffix */
        if (!nodeset_test_rank (ns, rank))
            member = false;
        nodeset_destroy (ns);
    }
    return member;
}

/* Load command line/default comms modules.
 * Walk through 'modules' list, creating ctx->modules hash entry for
 * each entry that is not excluded by a nodeset suffix, excluded by
 * presence in 'modexclude' list, or already in the hash.
 * Handle either a .so path (contains one or more '/' characters)
 * or a module name.
 */
static void load_modules (ctx_t *ctx, zlist_t *modules, zlist_t *modopts,
                          zhash_t *modexclude)
{
    char *s;
    module_t *mod;

    /* Create the module hash entries.
     */
    s = zlist_first (modules);
    while (s) {
        char *name = NULL;
        char *path = NULL;
        if (!nodeset_suffix_member (s, ctx->rank))
            goto next;
        if (strchr (s, '/')) {
            if (!(name = flux_modname (s)))
                msg_exit ("%s", dlerror ());
            path = s;
        } else {
            if (!(path = flux_modfind (ctx->module_searchpath, s)))
                msg_exit ("%s: not found in module search path", s);
            name = s;
        }
        if (modexclude && zhash_lookup (modexclude, name))
            goto next;
        if ((mod = zhash_lookup (ctx->modules, name)))
            goto next;
        if (!(mod = module_create (ctx, path)))
           err_exit ("module %s", name);
        zhash_update (ctx->modules, name, mod);
        zhash_freefn (ctx->modules, name, (zhash_free_fn *)module_destroy);
next:
        if (name != s)
            free (name);
        if (path != s)
            free (path);
        s = zlist_next (modules);
    }

    /* Add module options to module hash entries.
     */
    s = zlist_first (modopts);
    while (s) {
        char *arg = strchr (s, ':');
        if (!arg)
            msg_exit ("malformed module option: %s", s);
        *arg++ = '\0';
        module_t *mod = zhash_lookup (ctx->modules, s);
        if (!mod)
            msg_exit ("module argument for unknown module: %s", s);
        mod_add_arg (mod->p, arg);
        s = zlist_next (modopts);
    }

    /* Now start all the modules.
     */
    zlist_t *keys = zhash_keys (ctx->modules);
    s = zlist_first (keys);
    while (s) {
        mod = zhash_lookup (ctx->modules, s);
        assert (mod != NULL);
        if (module_start (ctx, mod) < 0)
            err_exit ("failed to start module %s", s);
        s = zlist_next (keys);
    }
    zlist_destroy (&keys);
}

/* Bind to ROUTER socket used by comms modules to send requests to the broker.
 */
static endpt_t *broker_init_modrequest (ctx_t *ctx)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };
    endpt_t *ep = endpt_create (MODREQUEST_INPROC_URI);

    if (!(ep->zs = zsocket_new (ctx->zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (ep->zs, 0);
    if (zsocket_bind (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    zp.socket = ep->zs;
    if (zloop_poller (ctx->zl, &zp, (zloop_fn *)request_cb, ctx) < 0)
        err_exit ("zloop_poller");
    return ep;
}

/* Bind to PUB socket used by comms modules to receive events from broker.
 */
static endpt_t *broker_init_modevent (ctx_t *ctx)
{
    endpt_t *ep = endpt_create (MODEVENT_INPROC_URI);

    if (!(ep->zs = zsocket_new (ctx->zctx, ZMQ_PUB)))
        err_exit ("zsocket_new");
    zsocket_set_hwm (ep->zs, 0);
    if (zsocket_bind (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    return ep;
}

static int broker_init_child (ctx_t *ctx, endpt_t *ep)
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

static int broker_init_gevent_pub (ctx_t *ctx, endpt_t *ep)
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

static int broker_init_gevent_sub (ctx_t *ctx, endpt_t *ep)
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

static endpt_t *broker_init_snoop (ctx_t *ctx)
{
    char *uri;
    endpt_t *ep = endpt_create ("ipc://*");

    if (!(ep->zs = zsocket_new (ctx->zctx, ZMQ_PUB)))
        err_exit ("zsocket_new");
    if (flux_sec_ssockinit (ctx->sec, ep->zs) < 0)
        msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ctx->sec));
    if (zsocket_bind (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", "ipc://*");
    if ((uri = zsocket_last_endpoint (ep->zs))) {
        free (ep->uri);
        ep->uri = xstrdup (uri);
    }
    return ep;
}

static int broker_init_parent (ctx_t *ctx, endpt_t *ep)
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

static int broker_init_right (ctx_t *ctx, endpt_t *ep)
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
static int broker_init_signalfd (ctx_t *ctx)
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

static void broker_init_comms (ctx_t *ctx)
{
    //(void)umask (077);

    ctx->zctx = zctx_new ();
    if (!ctx->zctx)
        err_exit ("zctx_new");
    zctx_set_linger (ctx->zctx, 5);
    if (!(ctx->zl = zloop_new ()))
        err_exit ("zloop_new");
    ctx->sigfd = broker_init_signalfd (ctx);

    /* Initialize security.
     */
    if (!(ctx->sec = flux_sec_create ()))
        err_exit ("flux_sec_create");
    flux_sec_set_directory (ctx->sec, ctx->secdir);
    if (ctx->security_clr && flux_sec_disable (ctx->sec, ctx->security_clr) < 0)
        err_exit ("flux_sec_disable");
    if (ctx->security_set && flux_sec_enable (ctx->sec, ctx->security_set) < 0)
        err_exit ("flux_sec_enable");
    if (flux_sec_zauth_init (ctx->sec, ctx->zctx, "flux") < 0)
        msg_exit ("flux_sec_zauth_init: %s", flux_sec_errstr (ctx->sec));
    if (flux_sec_munge_init (ctx->sec) < 0)
        msg_exit ("flux_sec_munge_init: %s", flux_sec_errstr (ctx->sec));
}

static void broker_init_overlay (ctx_t *ctx)
{
    if (ctx->rank == 0 && ctx->gevent) {
        broker_init_gevent_pub (ctx, ctx->gevent);
        ctx->event_active = true;
    }
    if (ctx->rank > 0 && ctx->gevent)
        broker_init_gevent_sub (ctx, ctx->gevent);

    if (ctx->child && !ctx->child->zs)      /* boot_pmi may have done this */
        broker_init_child (ctx, ctx->child);
    if (ctx->right)
        broker_init_right (ctx, ctx->right);

    /* Connect to parent(s), if any
     */
    endpt_t *ep;
    if ((ep = zlist_first (ctx->parents))) {
        if (broker_init_parent (ctx, ep) < 0)
            err_exit ("%s", ep->uri);
    }

    /* Create broker's flux_t handle.
     */
    ctx->h = flux_handle_create (ctx, &broker_handle_ops, 0);
    flux_log_set_facility (ctx->h, "broker");
    if (ctx->rank == 0)
        flux_log_set_redirect (ctx->h, true);
}

static void broker_init_modsocks (ctx_t *ctx)
{
    ctx->modrequest = broker_init_modrequest (ctx);
    ctx->modevent = broker_init_modevent (ctx);

    ctx->snoop = broker_init_snoop (ctx);
}

static char *cmb_getattr (ctx_t *ctx, const char *name)
{
    char *val = NULL;
    if (!strcmp (name, "snoop-uri")) {
        if (ctx->snoop)
            val = ctx->snoop->uri;
    } else if (!strcmp (name, "parent-uri")) {
        endpt_t *ep = zlist_first (ctx->parents);
        if (ep)
            val = ep->uri;
    } else if (!strcmp (name, "request-uri")) {
        if (ctx->child)
            val = ctx->child->uri;
    }
    return val;
}

static int cmb_rmmod (ctx_t *ctx, const char *name, zmsg_t **zmsg)
{
    module_t *mod;
    if (!(mod = zhash_lookup (ctx->modules, name))) {
        errno = ENOENT;
        return -1;
    }
    module_stop (mod, zmsg);
    flux_log (ctx->h, LOG_INFO, "rmmod %s", name);
    return 0;
}

/* Build lsmod response payload per RFC 5.
 */
static JSON cmb_lsmod (ctx_t *ctx)
{
    JSON o = flux_lsmod_json_create ();
    zlist_t *keys;
    char *name;
    module_t *mod;

    if (!(keys = zhash_keys (ctx->modules)))
        oom ();
    name = zlist_first (keys);
    while (name) {
        mod = zhash_lookup (ctx->modules, name);
        assert (mod != NULL);
        if (mod && flux_lsmod_json_append (o, mod_name (mod->p),
                                mod_size (mod->p),
                                mod_digest (mod->p),
                                peer_idle (ctx, mod_uuid (mod->p))) < 0) {
            Jput (o);
            o = NULL;
            goto done;
        }
        name = zlist_next (keys);
    }
done:
    zlist_destroy (&keys);
    return o;
}

static int cmb_insmod (ctx_t *ctx, char *path, char *argz, size_t argz_len)
{
    module_t *mod;
    char *name = NULL;
    char *arg;
    int rc = -1;

    if (!(name = flux_modname (path))) {
        errno = ENOENT;
        goto done;
    }
    if (zhash_lookup (ctx->modules, name)) {
        errno = EEXIST;
        goto done;
    }
    if (!(mod = module_create (ctx, path)))
        goto done;
    arg = argz_next (argz, argz_len, NULL);
    while (arg) {
        mod_add_arg (mod->p, arg);
        arg = argz_next (argz, argz_len, arg);
    }
    if (module_start (ctx, mod) < 0) {
        module_destroy (mod);
        goto done;
    }
    zhash_update (ctx->modules, name, mod);
    zhash_freefn (ctx->modules, name, (zhash_free_fn *)module_destroy);
    flux_log (ctx->h, LOG_INFO, "insmod %s", name);
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

static void peer_destroy (peer_t *p)
{
    free (p);
}

static peer_t *peer_create (ctx_t *ctx, const char *uuid, bool modflag)
{
    peer_t *p = xzmalloc (sizeof (*p));
    p->modflag = modflag;
    zhash_update (ctx->peer_idle, uuid, p);
    zhash_freefn (ctx->peer_idle, uuid, (zhash_free_fn *)peer_destroy);
    return p;
}

static void peer_update (ctx_t *ctx, const char *uuid)
{
    peer_t *p;

    if (!(p = zhash_lookup (ctx->peer_idle, uuid)))
        p = peer_create (ctx, uuid, false);
    p->hb_lastseen = ctx->hb_epoch;
}

static int peer_idle (ctx_t *ctx, const char *uuid)
{
    peer_t *p;

    if (!(p = zhash_lookup (ctx->peer_idle, uuid)))
        return ctx->hb_epoch; /* nonexistent: maximum idle */
    return ctx->hb_epoch - p->hb_lastseen;
}

static bool peer_ismodule (ctx_t *ctx, const char *uuid)
{
    peer_t *p;

    if (!(p = zhash_lookup (ctx->peer_idle, uuid)))
        return false;
    return p->modflag;
}

static int child_cc (void *sock, const char *id, zmsg_t *zmsg)
{
    zmsg_t *cpy;
    int rc = -1;

    if (!(cpy = zmsg_dup (zmsg)))
        oom ();
    if (flux_msg_enable_route (cpy) < 0)
        goto done;
    if (flux_msg_push_route (cpy, id) < 0)
        goto done;
    rc = zmsg_send (&cpy, sock);
done:
    zmsg_destroy (&cpy);
    return rc;
}

/* Cc events to downstream peers, until they have their primary event
 * source wired.  This works around the race described in issue 38.
 */
static void child_cc_all (ctx_t *ctx, zmsg_t *zmsg)
{
    zlist_t *keys;
    char *key;
    peer_t *p;

    if (!ctx->child || !ctx->child->zs)
        return;
    if (!(keys = zhash_keys (ctx->peer_idle)))
        oom ();
    key = zlist_first (keys);
    while (key) {
        if ((p = zhash_lookup (ctx->peer_idle, key))) {
            if (!p->modflag && !p->event_mute)
                child_cc (ctx->child->zs, key, zmsg);
        }
        key = zlist_next (keys);
    }
    zlist_destroy (&keys);
}

static int peer_mute (ctx_t *ctx, const char *id)
{
    peer_t *p;
    if (!(p = zhash_lookup (ctx->peer_idle, id)))
        return -1;
    p->event_mute = true;
    return 0;
}

static void self_update (ctx_t *ctx)
{
    ctx->hb_lastreq = ctx->hb_epoch;
}

static int self_idle (ctx_t *ctx)
{
    return ctx->hb_epoch - ctx->hb_lastreq;
}

static void send_keepalive (ctx_t *ctx)
{
    zmsg_t *zmsg = NULL;

    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_KEEPALIVE)))
        goto done;
    if (flux_msg_enable_route (zmsg) < 0)
        goto done;
    if (parent_send (ctx, &zmsg) < 0)
        goto done;
done:
    zmsg_destroy (&zmsg);
}

static void cmb_heartbeat (ctx_t *ctx, zmsg_t *zmsg)
{
    json_object *event = NULL;

    if (ctx->rank == 0)
        return;

    if (flux_msg_decode (zmsg, NULL, &event) < 0 || event == NULL
           || util_json_object_get_int (event, "epoch", &ctx->hb_epoch) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: bad hb message", __FUNCTION__);
    }

    /* If we've not sent anything to our parent, send a keepalive
     * to update our idle time.
     */
    if (self_idle (ctx) > 0)
        send_keepalive (ctx);
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
        ep = endpt_create ("%s", uri);
        if (broker_init_parent (ctx, ep) < 0) {
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
    if (ctx->verbose)
        msg ("shutdown timer expired: exiting");
    exit (ctx->shutdown_exitcode);
}

static void shutdown_recv (ctx_t *ctx, zmsg_t *zmsg)
{
    json_object *o = NULL;
    const char *reason;
    int grace, rank, exitcode;

    if (flux_msg_decode (zmsg, NULL, &o) < 0 || o == NULL
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
        if ((ctx->rank == 0 && !ctx->quiet) || ctx->verbose)
            msg ("%d: shutdown in %ds: %s", rank, grace, reason);
    }
    if (o)
        json_object_put (o);
}

static int shutdown_send (ctx_t *ctx, int grace, int rc, const char *fmt, ...)
{
    JSON o = Jnew ();
    va_list ap;
    char *reason;
    int ret;

    va_start (ap, fmt);
    reason = xvasprintf (fmt, ap);
    va_end (ap);

    util_json_object_add_string (o, "reason", reason);
    util_json_object_add_int (o, "grace", grace);
    util_json_object_add_int (o, "rank", ctx->rank);
    util_json_object_add_int (o, "exitcode", rc);
    ret = send_event (ctx, o, "shutdown");
    Jput (o);
    if (reason)
        free (reason);
    return ret;
}

static void cmb_internal_event (ctx_t *ctx, zmsg_t *zmsg)
{
    if (flux_msg_match (zmsg, "hb"))
        cmb_heartbeat (ctx, zmsg);
    else if (flux_msg_match (zmsg, "shutdown"))
        shutdown_recv (ctx, zmsg);
    else if (flux_msg_match (zmsg, "live.ready")) {
        if (ctx->shell)
            rank0_shell (ctx);
    }
}

/* helper for send_event() */
static int send_event_zmsg (ctx_t *ctx, zmsg_t **event)
{
    int rc = -1;
    zmsg_t *cpy = NULL;

    assert (ctx->rank == 0);
    assert (ctx->modevent != NULL);
    assert (ctx->modevent->zs != NULL);

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
    /* Publish event to downstream peers.
     */
    child_cc_all (ctx, *event);
    /* Publish event locally
    */
    endpt_cc (*event, ctx->snoop);
    cmb_internal_event (ctx, *event);
    endpt_cc (*event, ctx->gevent_relay);
    if (zmsg_send (event, ctx->modevent->zs) < 0) {
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

/* Send an event (call on rank 0 only)
 */
static int send_event (ctx_t *ctx, JSON o, const char *topic)
{
    int rc = -1;
    zmsg_t *zmsg;

    assert (ctx->rank == 0);

    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        oom ();
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto done;
    if (o) {
        const char *s = json_object_to_json_string (o);
        int len = strlen (s);
        if (flux_msg_set_payload (zmsg, FLUX_MSGFLAG_JSON, (char *)s, len) < 0)
            goto done;
    }
    if (flux_msg_set_seq (zmsg, ++ctx->event_seq) < 0) /* start with seq=1 */
        goto done;
    if (send_event_zmsg (ctx, &zmsg) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

/* Unwrap event from cmb.pub request and publish.
 */
static int cmb_pub (ctx_t *ctx, zmsg_t **zmsg)
{
    JSON payload, o = NULL;
    const char *topic;
    int rc = -1;

    assert (ctx->rank == 0);

    if (flux_msg_decode (*zmsg, NULL, &o) < 0 || !o) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (!Jget_str (o, "topic", &topic) || !Jget_obj (o, "payload", &payload)) {
        flux_respond_errnum (ctx->h, zmsg, EINVAL);
        goto done;
    }
    if (send_event (ctx, payload, topic) < 0) {
        flux_respond_errnum (ctx->h, zmsg, errno);
        goto done;
    }
    flux_respond_errnum (ctx->h, zmsg, 0);
    rc = 0;
done:
    Jput (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return rc;
}

static json_object *
subprocess_json_resp (ctx_t *ctx, struct subprocess *p)
{
    json_object *resp = util_json_object_new_object ();

    assert (ctx != NULL);
    assert (resp != NULL);

    util_json_object_add_int (resp, "rank", ctx->rank);
    util_json_object_add_int (resp, "pid", subprocess_pid (p));
    util_json_object_add_string (resp, "state", subprocess_state_string (p));
    return (resp);
}

static int child_exit_handler (struct subprocess *p, void *arg)
{
    int n;

    ctx_t *ctx = (ctx_t *) arg;
    zmsg_t *zmsg = (zmsg_t *) subprocess_get_context (p);
    json_object *resp;

    assert (ctx != NULL);
    assert (zmsg != NULL);

    resp = subprocess_json_resp (ctx, p);
    util_json_object_add_int (resp, "status", subprocess_exit_status (p));
    util_json_object_add_int (resp, "code", subprocess_exit_code (p));
    if ((n = subprocess_signaled (p)))
        util_json_object_add_int (resp, "signal", n);

    flux_json_respond (ctx->h, resp, &zmsg);
    json_object_put (resp);
    return (0);
}

/*
 *  Create a subprocess described in the zmsg argument.
 */
static int cmb_create_subprocess (ctx_t *ctx, zmsg_t **zmsg)
{
    json_object *request = NULL;
    json_object *response = NULL;
    json_object *o;
    struct subprocess *p;
    zmsg_t *copy;
    int i, argc;
    int rc = -1;

    if (flux_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL) {
        errno = EPROTO;
        return -1;
    }

    if (!json_object_object_get_ex (request, "cmdline", &o)
        || o == NULL
        || (json_object_get_type (o) != json_type_array)) {
        errno = EPROTO;
        goto out_free;
    }

    if ((argc = json_object_array_length (o)) < 0) {
        errno = EPROTO;
        goto out_free;
    }

    p = subprocess_create (ctx->sm);
    subprocess_set_callback (p, child_exit_handler, ctx);

    for (i = 0; i < argc; i++) {
        json_object *ox = json_object_array_get_idx (o, i);
        if (json_object_get_type (ox) == json_type_string)
            subprocess_argv_append (p, json_object_get_string (ox));
    }

    if (json_object_object_get_ex (request, "env", &o) && o != NULL) {
        json_object_iter iter;
        json_object_object_foreachC (o, iter) {
            const char *val = json_object_get_string (iter.val);
            if (val != NULL)
                subprocess_setenv (p, iter.key, val, 1);
        }
        /*
         *  Override key FLUX environment variables in env array
         */
        subprocess_setenv (p, "FLUX_TMPDIR", getenv ("FLUX_TMPDIR"), 1);
    }
    else
        subprocess_set_environ (p, environ);

    if (json_object_object_get_ex (request, "cwd", &o) && o != NULL) {
        const char *dir = json_object_get_string (o);
        if (dir != NULL)
            subprocess_set_cwd (p, dir);
    }

    if ((rc = subprocess_run (p)) < 0) {
        subprocess_destroy (p);
        goto out_free;
    }

    /*
     * Save a copy of zmsg for future messages
     */
    copy = zmsg_dup (*zmsg);
    subprocess_set_context (p, (void *) copy);

    /*
     *  Send response, destroys original zmsg.
     */
    response = subprocess_json_resp (ctx, p);
    flux_json_respond (ctx->h, response, zmsg);
out_free:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    return (rc);
}

static int cmb_internal_request (ctx_t *ctx, zmsg_t **zmsg)
{
    int rc = 0;

    if (flux_msg_match (*zmsg, "cmb.info")) {
        json_object *response = util_json_object_new_object ();

        util_json_object_add_int (response, "rank", ctx->rank);
        util_json_object_add_int (response, "size", ctx->size);
        util_json_object_add_boolean (response, "treeroot", ctx->treeroot);

        if (flux_respond (ctx->h, zmsg, response) < 0)
            err_exit ("flux_respond");
        json_object_put (response);
    } else if (flux_msg_match (*zmsg, "cmb.getattr")) {
        json_object *request = NULL;
        json_object *response = util_json_object_new_object ();
        const char *name = NULL;
        char *val = NULL;
        if (flux_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
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
    } else if (flux_msg_match (*zmsg, "cmb.rusage")) {
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
    } else if (flux_msg_match (*zmsg, "cmb.rmmod")) {
        char *name = NULL;
        int errnum = 0;
        if (flux_rmmod_request_decode (*zmsg, &name) < 0)
            errnum = errno;
        else if (cmb_rmmod (ctx, name, zmsg) < 0) /* responds on success */
            errnum = errno;
        if (errnum && flux_err_respond (ctx->h, errnum, zmsg) < 0)
            flux_log (ctx->h, LOG_ERR, "%s: flux_err_respond: %s",
                      __FUNCTION__, strerror (errno));
        if (name)
            free (name);
    } else if (flux_msg_match (*zmsg, "cmb.insmod")) {
        char *path = NULL;
        char *argz = NULL;
        size_t argz_len = 0;
        int errnum = 0;
        if (flux_insmod_request_decode (*zmsg, &path, &argz, &argz_len) < 0)
            errnum = errno;
        else if (cmb_insmod (ctx, path, argz, argz_len) < 0)
            errnum = errno;
        if (flux_err_respond (ctx->h, errnum, zmsg) < 0)
            flux_log (ctx->h, LOG_ERR, "%s: flux_err_respond: %s",
                      __FUNCTION__, strerror (errno));
        if (path)
            free (path);
        if (argz)
            free (argz);
    } else if (flux_msg_match (*zmsg, "cmb.lsmod")) {
        if (flux_lsmod_request_decode (*zmsg) < 0) {
            if (flux_err_respond (ctx->h, errno, zmsg) < 0)
                flux_log (ctx->h, LOG_ERR, "%s: flux_err_respond: %s",
                          __FUNCTION__, strerror (errno));
        } else {
            JSON out = cmb_lsmod (ctx);
            if (!out) {
                if (flux_err_respond (ctx->h, errno, zmsg) < 0)
                    flux_log (ctx->h, LOG_ERR, "%s: flux_err_respond: %s",
                              __FUNCTION__, strerror (errno));
            } else {
                if (flux_json_respond (ctx->h, out, zmsg) < 0)
                    flux_log (ctx->h, LOG_ERR, "%s: flux_json_respond: %s",
                              __FUNCTION__, strerror (errno));
            }
            Jput (out);
        }
    } else if (flux_msg_match (*zmsg, "cmb.lspeer")) {
        json_object *response = NULL;
        if (!(response = peer_ls (ctx))) {
            flux_respond_errnum (ctx->h, zmsg, errno);
        } else {
            flux_respond (ctx->h, zmsg, response);
        }
        if (response)
            json_object_put (response);
    } else if (flux_msg_match (*zmsg, "cmb.ping")) {
        json_object *request = NULL;
        char *s = NULL;
        if (flux_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else {
            s = zdump_routestr (*zmsg, 1);
            util_json_object_add_string (request, "route", s);
            flux_respond (ctx->h, zmsg, request);
        }
        if (request)
            json_object_put (request);
        if (s)
            free (s);
    } else if (flux_msg_match (*zmsg, "cmb.reparent")) {
        json_object *request = NULL;
        const char *uri;
        if (flux_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
                || util_json_object_get_string (request, "uri", &uri) < 0) {
            flux_respond_errnum (ctx->h, zmsg, EPROTO);
        } else if (cmb_reparent (ctx, uri) < 0) {
            flux_respond_errnum (ctx->h, zmsg, errno);
        } else {
            flux_respond_errnum (ctx->h, zmsg, 0);
        }
        if (request)
            json_object_put (request);
    } else if (flux_msg_match (*zmsg, "cmb.panic")) {
        json_object *request = NULL;
        const char *s = NULL;
        if (flux_msg_decode (*zmsg, NULL, &request) == 0 && request != NULL) {
            (void)util_json_object_get_string (request, "msg", &s);
            msg ("PANIC: %s", s ? s : "no reason");
            exit (1);
        }
        if (request)
            json_object_put (request);
    } else if (flux_msg_match (*zmsg, "cmb.log")) {
        if (ctx->rank > 0)
            (void)parent_send (ctx, zmsg);
        else
            flux_log_zmsg (*zmsg);
    } else if (flux_msg_match (*zmsg, "cmb.pub")) {
        if (ctx->rank > 0) {
            if (parent_send (ctx, zmsg) < 0)
                flux_respond_errnum (ctx->h, zmsg, errno);
        } else {
            if (cmb_pub (ctx, zmsg) < 0)
                flux_respond_errnum (ctx->h, zmsg, errno);
        }
    } else if (flux_msg_match (*zmsg, "cmb.event-mute")) {
        char *id = NULL;
        if (flux_msg_get_route_last (*zmsg, &id) == 0)
            peer_mute (ctx, id);
        if (id)
            free (id);
    } else if (flux_msg_match (*zmsg, "cmb.exec")) {
        if (cmb_create_subprocess (ctx, zmsg) < 0)
            flux_respond_errnum (ctx->h, zmsg, errno);
    } else {
        rc = -1;
        errno = ENOSYS;
    }
    return rc;
}

static int request_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    zmsg_t *zmsg = zmsg_recv (item->socket);
    int type;
    char *id;

    if (!zmsg)
        goto done;
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_KEEPALIVE:
            if (flux_msg_get_route_last (zmsg, &id) < 0)
                goto done;
            endpt_cc (zmsg, ctx->snoop);
            peer_update (ctx, id);
            free (id);
            break;
        case FLUX_MSGTYPE_REQUEST:
            assert (zmsg != NULL);
            if (flux_sendmsg (ctx->h, &zmsg) < 0) {
                assert (zmsg != NULL);
                if (flux_respond_errnum (ctx->h, &zmsg, errno) < 0)
                    goto done;
            }
            break;
    }
done:
    zmsg_destroy (&zmsg);
    ZLOOP_RETURN(ctx);
}

static void send_mute_request (ctx_t *ctx, void *sock)
{
    zmsg_t *zmsg;

    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (zmsg, "cmb.event-mute") < 0)
        goto done;
    if (flux_msg_enable_route (zmsg))
        goto done;
    if (zmsg_send (&zmsg, sock) < 0)
        flux_log (ctx->h, LOG_ERR, "failed to send mute request: %s",
                  strerror (errno));
    /* No response will be sent */
done:
    zmsg_destroy (&zmsg);
}

static int parent_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    zmsg_t *zmsg = zmsg_recv (item->socket);
    int type;

    if (!zmsg)
        goto done;
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            if (flux_sendmsg (ctx->h, &zmsg) < 0)
                goto done;
            break;
        case FLUX_MSGTYPE_EVENT:
            if (ctx->event_active) {
                send_mute_request (ctx, item->socket);
                goto done;
            }
            if (flux_msg_clear_route (zmsg) < 0) {
                flux_log (ctx->h, LOG_ERR, "dropping malformed event");
                goto done;
            }
            if (recv_event (ctx, &zmsg) < 0)
                goto done;
            break;
    }
done:
    zmsg_destroy (&zmsg);
    ZLOOP_RETURN(ctx);
}

static int plugins_cb (zloop_t *zl, zmq_pollitem_t *item, module_t *mod)
{
    ctx_t *ctx = mod->ctx;
    zmsg_t *zmsg = zmsg_recv (item->socket);

    if (zmsg) {
        if (zmsg_content_size (zmsg) == 0) /* EOF */
            zhash_delete (ctx->modules, mod_name (mod->p));
        else {
            (void)flux_sendmsg (ctx->h, &zmsg);
            peer_update (ctx, mod_uuid (mod->p));
        }
    }
    if (zmsg)
        zmsg_destroy (&zmsg);
    ZLOOP_RETURN(ctx);
}

static int recv_event (ctx_t *ctx, zmsg_t **zmsg)
{
    int i;
    uint32_t seq;

    if (flux_msg_get_seq (*zmsg, &seq) < 0) {
        flux_log (ctx->h, LOG_ERR, "dropping malformed event");
        return -1;
    }
    if (seq <= ctx->event_seq) {
        //flux_log (ctx->h, LOG_INFO, "duplicate event");
        return -1;
    }
    if (ctx->event_seq > 0) { /* don't log initial missed evnets */
        for (i = ctx->event_seq + 1; i < seq; i++)
            flux_log (ctx->h, LOG_ERR, "lost event %d", i);
    }
    ctx->event_seq = seq;
    endpt_cc (*zmsg, ctx->gevent_relay);
    endpt_cc (*zmsg, ctx->snoop);
    child_cc_all (ctx, *zmsg); /* to downstream peers */
    cmb_internal_event (ctx, *zmsg);

    assert (ctx->modevent != NULL);
    assert (ctx->modevent->zs != NULL);
    return zmsg_send (zmsg, ctx->modevent->zs);
}

static int event_cb (zloop_t *zl, zmq_pollitem_t *item, ctx_t *ctx)
{
    zmsg_t *zmsg = zmsg_recv (item->socket);

    if (zmsg) {
        if (strstr (ctx->gevent->uri, "pgm://")) {
            if (flux_sec_unmunge_zmsg (ctx->sec, &zmsg) < 0) {
                flux_log (ctx->h, LOG_ERR, "dropping malformed event: %s",
                          flux_sec_errstr (ctx->sec));
                goto done;
            }
        }
        ctx->event_active = true;
        if (recv_event (ctx, &zmsg) < 0)
            goto done;
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    ZLOOP_RETURN(ctx);
}

static int hb_cb (zloop_t *zl, int timer_id, ctx_t *ctx)
{
    JSON o = Jnew ();

    assert (ctx->rank == 0);
    assert (timer_id == ctx->heartbeat_tid);

    Jadd_int (o, "epoch", ++ctx->hb_epoch);
    if (send_event (ctx, o, "hb") < 0)
        err ("send_event failed");
    Jput (o);
    ZLOOP_RETURN(ctx);
}

/* See POSIX 2008 Volume 3 Shell and Utilities, Issue 7
 * Section 2.8.2 Exit status for shell commands (page 2315)
 */
static int shell_exit_handler (struct subprocess *p, void *arg)
{
    int rc;
    ctx_t *ctx = (ctx_t *) arg;
    assert (ctx != NULL);

    if (subprocess_signaled (p))
        rc = 128 + subprocess_signaled (p);
    else
        rc = subprocess_exit_code (p);
    return shutdown_send (ctx, 2, rc, subprocess_state_string (p));
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
            subprocess_manager_reap_all (ctx->sm);
        else {
            (void)shutdown_send (ctx, 2, 0, "signal %d (%s) %d",
                                 fdsi.ssi_signo, strsignal (fdsi.ssi_signo));
        }
    }
    ZLOOP_RETURN(ctx);
}

static int parent_send (ctx_t *ctx, zmsg_t **zmsg)
{
    endpt_t *ep = zlist_first (ctx->parents);
    int rc = -1;

    if (!ep || !ep->zs) {
        errno = ctx->treeroot ? ENOSYS : EHOSTUNREACH;
        goto done;
    }
    self_update (ctx);
    rc = zmsg_send (zmsg, ep->zs);
done:
    return rc;
}

static int rank_send (ctx_t *ctx, zmsg_t **zmsg)
{
    zframe_t *zf;

    if (!ctx->right || !ctx->right->zs)
        goto unreach;
    zf = zmsg_first (*zmsg);
    while (zf && zframe_size (zf) > 0) {
        if (zframe_streq (zf, ctx->rankstr_right)) /* cycle detected! */
            goto unreach;
        zf = zmsg_next (*zmsg);
    }
    return zmsg_send (zmsg, ctx->right->zs);

unreach:
    errno = EHOSTUNREACH;
    return -1;
}

/* Try to dispatch message to a local service: built-in broker service,
 * or loaded comms module.
 * If 'loopback_ok' is true, allow a service other than 'cmb' to loop a
 * request message back to itself on the same rank.  Otherwise we return
 * ENOSYS in this case as an indication to caller that request should be
 * routed to parent.
 */
static int service_send (ctx_t *ctx, zmsg_t **zmsg, char *lasthop, int hopcount,
                         bool loopback_ok)
{
    char *service = flux_msg_tag_short (*zmsg);
    int rc = -1;
    module_t *mod;

    if (!service) {
        errno = EPROTO;
        goto done;
    }
    if (!strcmp (service, "cmb")) {
        if (hopcount == 0) { /* no cmb-cmb loopback (ignore loopback_ok) */
            errno = ENOSYS;
            goto done;
        }
        rc = cmb_internal_request (ctx, zmsg);
    } else {
        if (!(mod = zhash_lookup (ctx->modules, service))) {
            errno = ENOSYS;
            goto done;
        }
        if (!loopback_ok) {
            if (lasthop && !strcmp (lasthop, mod_uuid (mod->p))) {
                errno = ENOSYS;
                goto done;
            }
        }
        rc = zmsg_send (zmsg, mod_sock (mod->p));
    }
done:
    if (service)
        free (service);
    return rc;
}

/**
 ** Broker's internal flux_t implementation.
 **    a bit limited, by design
 **/

static int broker_request_sendmsg (ctx_t *ctx, zmsg_t **zmsg)
{
    char *lasthop = flux_msg_nexthop (*zmsg);
    int hopcount = flux_msg_hopcount (*zmsg);
    uint32_t nodeid;
    int rc = -1;

    if (flux_msg_get_nodeid (*zmsg, &nodeid) < 0) {
        errno = EPROTO;
        goto done;
    }
    endpt_cc (*zmsg, ctx->snoop);
    if (hopcount > 0 && lasthop)
        peer_update (ctx, lasthop);
    if (nodeid == FLUX_NODEID_ANY) {
        rc = service_send (ctx, zmsg, lasthop, hopcount, false);
        if (rc < 0 && errno == ENOSYS)
            rc = parent_send (ctx, zmsg);
    } else if (nodeid == ctx->rank) {
        rc = service_send (ctx, zmsg, lasthop, hopcount, true);
    } else if (nodeid == 0) {
        rc = parent_send (ctx, zmsg);
    } else {
        rc = rank_send (ctx, zmsg);
    }
done:
    /* N.B. don't destroy zmsg on error as we use it to send errnum reply.
     */
    if (lasthop)
        free (lasthop);
    return rc;
}

static int broker_response_sendmsg (ctx_t *ctx, zmsg_t **zmsg)
{
    char *nexthop = flux_msg_nexthop (*zmsg);
    int rc = -1;

    endpt_cc (*zmsg, ctx->snoop);

    if (!nexthop)                             /* local: reply to ourselves? */
        rc = -1;
    else if (peer_ismodule (ctx, nexthop))    /* send to a module */
        rc = zmsg_send (zmsg, ctx->modrequest->zs);
    else if (ctx->child)                      /* send to downstream peer */
        rc = zmsg_send (zmsg, ctx->child->zs);
    if (nexthop)
        free (nexthop);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return rc;
}

static int broker_sendmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *ctx = impl;
    int type;
    int rc = -1;
    if (flux_msg_get_type (*zmsg, &type) < 0)
        goto done;
    if (type == FLUX_MSGTYPE_REQUEST)
        rc = broker_request_sendmsg (ctx, zmsg);
    else if (type == FLUX_MSGTYPE_RESPONSE)
        rc = broker_response_sendmsg (ctx, zmsg);
    else
        errno = EINVAL;
done:
    return rc;
}

static const struct flux_handle_ops broker_handle_ops = {
    .sendmsg = broker_sendmsg,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
