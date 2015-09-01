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
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/shortjson.h"
#include "src/modules/libsubprocess/subprocess.h"
#include "src/modules/libzio/zio.h"

#include "heartbeat.h"
#include "module.h"
#include "boot_pmi.h"
#include "overlay.h"
#include "snoop.h"
#include "service.h"
#include "hello.h"
#include "shutdown.h"

#ifndef ZMQ_IMMEDIATE
#define ZMQ_IMMEDIATE           ZMQ_DELAY_ATTACH_ON_CONNECT
#define zsocket_set_immediate   zsocket_set_delay_attach_on_connect
#endif

const char *default_modules =
    "connector-local,modctl,kvs,live,mecho,job[0],wrexec,barrier,resource-hwloc";

typedef struct {
    /* 0MQ
     */
    zctx_t *zctx;               /* zeromq context (MT-safe) */
    flux_sec_t sec;             /* security context (MT-safe) */

    /* Reactor
     */
    flux_t h;
    flux_fd_watcher_t *signalfd_w;

    /* Sockets.
     */
    overlay_t *overlay;
    snoop_t *snoop;

    /* Session parameters
     */
    uint32_t size;              /* session size */
    uint32_t rank;              /* our rank in session */
    char *sid;                  /* session id */
    char *socket_dir;
    char *local_uri;
    char *parent_uri;

    /* Modules
     */
    modhash_t *modhash;
    /* Misc
     */
    bool verbose;
    bool quiet;
    pid_t pid;
    char *proctitle;
    sigset_t default_sigset;
    flux_conf_t cf;
    int event_seq;
    bool event_active;          /* primary event source is active */
    svchash_t *services;
    svchash_t *eventsvc;
    heartbeat_t *heartbeat;
    shutdown_t *shutdown;
    double shutdown_grace;
    /* Bootstrap
     */
    bool boot_pmi;
    int k_ary;
    hello_t *hello;
    double hello_timeout;

    /* Subprocess management
     */
    struct subprocess_manager *sm;

    char *shell_cmd;
    struct subprocess *shell;
} ctx_t;

static void broker_log (void *ctx, const char *facility, int level,
                        int rank, struct timeval tv, const char *msg);

static int broker_event_sendmsg (ctx_t *ctx, zmsg_t **zmsg);
static int broker_response_sendmsg (ctx_t *ctx, const flux_msg_t *msg);
static int broker_request_sendmsg (ctx_t *ctx, zmsg_t **zmsg);

static void event_cb (overlay_t *ov, void *sock, void *arg);
static void parent_cb (overlay_t *ov, void *sock, void *arg);
static void child_cb (overlay_t *ov, void *sock, void *arg);
static void heartbeat_cb (heartbeat_t *h, void *arg);
static void module_cb (module_t *p, void *arg);
static void rmmod_cb (module_t *p, void *arg);
static void hello_update_cb (hello_t *h, void *arg);
static void shutdown_cb (shutdown_t *s, void *arg);
static void signalfd_cb (flux_t h, flux_fd_watcher_t *w,
                         int fd, int revents, void *arg);

static void broker_init_signalfd (ctx_t *ctx);

static void broker_add_services (ctx_t *ctx);

static void load_modules (ctx_t *ctx, zlist_t *modules, zlist_t *modopts,
                          zhash_t *modexclude, const char *modpath);

static void update_proctitle (ctx_t *ctx);
static void create_rankdir (ctx_t *ctx);
static void update_pidfile (ctx_t *ctx);
static void rank0_shell (ctx_t *ctx);
static int rank0_shell_exit_handler (struct subprocess *p, void *arg);

static void boot_pmi (ctx_t *ctx);
static void boot_local (ctx_t *ctx);

static const struct flux_handle_ops broker_handle_ops;

#define OPTIONS "t:vqR:S:p:M:X:L:N:Pk:e:r:s:c:nH:O:x:T:g:D:"
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
    {"timeout",         required_argument,  0, 'T'},
    {"shutdown-grace",  required_argument,  0, 'g'},
    {"socket-directory",required_argument,  0, 'D'},
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
" -H,--heartrate SECS          Set heartrate in seconds (rank 0 only)\n"
" -T,--timeout SECS            Set wireup timeout in seconds (rank 0 only)\n"
" -g,--shutdown-grace SECS     Set shutdown grace period in seconds\n"
" -D,--socket-directory DIR    Create ipc sockets in DIR (local bootstrap)\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int c;
    ctx_t ctx;
    bool nopt = false;
    zlist_t *modules, *modopts;
    zhash_t *modexclude;
    char *modpath;
    const char *confdir;
    int security_clr = 0;
    int security_set = 0;
    const char *secdir = NULL;

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
    ctx.modhash = modhash_create ();
    ctx.services = svchash_create ();
    ctx.eventsvc = svchash_create ();
    ctx.overlay = overlay_create ();
    ctx.snoop = snoop_create ();
    ctx.hello = hello_create ();
    ctx.k_ary = 2; /* binary TBON is default */
    ctx.heartbeat = heartbeat_create ();
    ctx.shutdown = shutdown_create ();

    ctx.pid = getpid();
    if (!(modpath = getenv ("FLUX_MODULE_PATH")))
        modpath = MODULE_PATH;

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
                    security_clr = FLUX_SEC_TYPE_ALL;
                else if (!strcmp (optarg, "plain"))
                    security_set |= FLUX_SEC_TYPE_PLAIN;
                else if (!strcmp (optarg, "curve"))
                    security_set |= FLUX_SEC_TYPE_CURVE;
                else
                    msg_exit ("--security argument must be none|plain|curve");
                break;
            case 't':   /* --child-uri URI[,URI,...] */
                overlay_set_child (ctx.overlay, "%s", optarg);
                break;
            case 'p': { /* --parent-uri URI */
                overlay_push_parent (ctx.overlay, "%s", optarg);
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
                if (zlist_push (modules, xstrdup (optarg)) < 0 )
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
                modpath = optarg;
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
                overlay_set_event (ctx.overlay, "%s", optarg);
                break;
            case 'r':   /* --right-uri */
                overlay_set_right (ctx.overlay, "%s", optarg);
                break;
            case 'c':   /* --command CMD */
                if (ctx.shell_cmd)
                    free (ctx.shell_cmd);
                ctx.shell_cmd = xstrdup (optarg);
                break;
            case 'n':   /* --noshell */
                nopt = true;
                break;
            case 'H':   /* --heartrate SECS */
                if (heartbeat_set_ratestr (ctx.heartbeat, optarg) < 0)
                    err_exit ("heartrate `%s'", optarg);
                break;
            case 'T':   /* --timeout SECS */
                ctx.hello_timeout = strtod (optarg, NULL);
                break;
            case 'g':   /* --shutdown-grace SECS */
                ctx.shutdown_grace = strtod (optarg, NULL);
                break;
            case 'D': { /* --socket-directory DIR */
                struct stat sb;
                if (stat (optarg, &sb) < 0)
                    err_exit ("%s", optarg);
                if (!S_ISDIR (sb.st_mode))
                    msg_exit ("%s: not a directory", optarg);
                if ((sb.st_mode & S_IRWXU) != S_IRWXU)
                    msg_exit ("%s: invalid mode: 0%o", optarg, sb.st_mode);
                ctx.socket_dir = xstrdup (optarg);
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
    if (!(secdir = getenv ("FLUX_SEC_DIRECTORY")))
        msg_exit ("FLUX_SEC_DIRECTORY is not set"); 

    /* Process config from the KVS of enclosing instance (if any)
     * and not forced to use a config file by the command line.
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
    } else if (getenv ("FLUX_URI")) {
        flux_t h;
        if (ctx.verbose)
            msg ("Loading config from KVS");
        if (!(h = flux_open (NULL, 0)))
            err_exit ("flux_open");
        if (kvs_conf_load (h, ctx.cf) < 0)
            err_exit ("could not load config from KVS");
        flux_close (h);
        /* Stash FLUX_URI value for later use, but unset it in the environment
         * so a connection to the enclosing instance is not made inadvertantly.
         */
        ctx.parent_uri = xstrdup (getenv ("FLUX_URI"));
        unsetenv ("FLUX_URI");
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

    /* Initailize zeromq context
     */
    ctx.zctx = zctx_new ();
    if (!ctx.zctx)
        err_exit ("zctx_new");
    zctx_set_linger (ctx.zctx, 5);

    /* Set up flux handle.
     * The handle is used for simple purposes such as logging,
     * and also provides the main reactor loop for the broker.
     */
    if (!(ctx.h = flux_handle_create (&ctx, &broker_handle_ops, 0)))
        err_exit ("flux_handle_create");
    flux_aux_set (ctx.h, "flux::rank", &ctx.rank, NULL); /* prelim */
    flux_aux_set (ctx.h, "flux::size", &ctx.size, NULL); /* prelim */
    flux_log_set_facility (ctx.h, "broker");
    if (ctx.rank == 0)
        flux_log_set_redirect (ctx.h, broker_log, &ctx);

    subprocess_manager_set (ctx.sm, SM_FLUX, ctx.h);

    /* Prepare signal handling
     */
    broker_init_signalfd (&ctx);

    /* Initialize security context.
     */
    if (!(ctx.sec = flux_sec_create ()))
        err_exit ("flux_sec_create");
    flux_sec_set_directory (ctx.sec, secdir);
    if (security_clr && flux_sec_disable (ctx.sec, security_clr) < 0)
        err_exit ("flux_sec_disable");
    if (security_set && flux_sec_enable (ctx.sec, security_set) < 0)
        err_exit ("flux_sec_enable");
    if (flux_sec_zauth_init (ctx.sec, ctx.zctx, "flux") < 0)
        msg_exit ("flux_sec_zauth_init: %s", flux_sec_errstr (ctx.sec));
    if (flux_sec_munge_init (ctx.sec) < 0)
        msg_exit ("flux_sec_munge_init: %s", flux_sec_errstr (ctx.sec));

    overlay_set_heartbeat (ctx.overlay, ctx.heartbeat);
    overlay_set_zctx (ctx.overlay, ctx.zctx);
    overlay_set_sec (ctx.overlay, ctx.sec);
    overlay_set_flux (ctx.overlay, ctx.h);

    overlay_set_parent_cb (ctx.overlay, parent_cb, &ctx);
    overlay_set_child_cb (ctx.overlay, child_cb, &ctx);
    overlay_set_event_cb (ctx.overlay, event_cb, &ctx);

    /* Sets rank, size, parent URI.
     * Initialize child socket.
     */
    if (ctx.boot_pmi) {
        if (overlay_get_child (ctx.overlay))
            msg_exit ("--child-uri should not be specified with --pmi-boot");
        if (overlay_get_parent (ctx.overlay))
            msg_exit ("--parent-uri should not be specified with --pmi-boot");
        if (overlay_get_event (ctx.overlay))
            msg_exit ("--event-uri should not be specified with --pmi-boot");
        if (ctx.sid)
            msg_exit ("--session-id should not be specified with --pmi-boot");
        boot_pmi (&ctx);
        flux_aux_set (ctx.h, "flux::rank", &ctx.rank, NULL);
        flux_aux_set (ctx.h, "flux::size", &ctx.size, NULL);
    }
    if (!ctx.sid)
        ctx.sid = xstrdup ("0");

    /* Now that we know the rank (either from command line or PMI,
     * create a subdirectory of socket_dir for the sockets and pidfile
     * specific to this rank of the broker.
     */
    if (!ctx.socket_dir) {
        char *tmpdir = getenv ("TMPDIR");
        char *template = xasprintf ("%s/flux-%s-XXXXXX",
                                    tmpdir ? tmpdir : "/tmp", ctx.sid);

        if (!(ctx.socket_dir = mkdtemp (template)))
            err_exit ("mkdtemp %s", template);
        cleanup_push_string (cleanup_directory, ctx.socket_dir);
    }

    create_rankdir (&ctx);

    /* If we're missing the wiring, presume that the session is to be
     * started on a single node and compute appropriate ipc:/// sockets.
     */
    if (ctx.size > 1 && !overlay_get_event (ctx.overlay)
                     && !overlay_get_child (ctx.overlay)
                     && !overlay_get_parent (ctx.overlay)) {
        boot_local (&ctx);
    }
    if (ctx.rank == 0 && overlay_get_parent (ctx.overlay))
        msg_exit ("rank 0 must NOT have parent");
    if (ctx.rank > 0 && !overlay_get_parent (ctx.overlay))
        msg_exit ("rank > 0 must have parents");
    if (ctx.size > 1 && !overlay_get_event (ctx.overlay))
        msg_exit ("--event-uri is required for size > 1");

    overlay_set_rank (ctx.overlay, ctx.rank);

    /* If shutdown_grace was not provided on the command line,
     * make a guess.
     */
    if (ctx.shutdown_grace == 0) {
        if (ctx.size < 16)
            ctx.shutdown_grace = 0.5;
        else if (ctx.size < 128)
            ctx.shutdown_grace = 1;
        else if (ctx.size < 1024)
            ctx.shutdown_grace = 2;
        else
            ctx.shutdown_grace = 5;
    }

    if (ctx.verbose) {
        const char *parent = overlay_get_parent (ctx.overlay);
        const char *child = overlay_get_child (ctx.overlay);
        const char *event = overlay_get_event (ctx.overlay);
        const char *relay = overlay_get_relay (ctx.overlay);
        msg ("parent: %s", parent ? parent : "none");
        msg ("child: %s", child ? child : "none");
        msg ("event: %s", event ? event : "none");
        msg ("relay: %s", relay ? relay : "none");
    }

    update_proctitle (&ctx);
    update_pidfile (&ctx);

    if (!nopt && ctx.rank == 0 && (isatty (STDIN_FILENO) || ctx.shell_cmd)) {
        ctx.shell = subprocess_create (ctx.sm);
        subprocess_set_callback (ctx.shell, rank0_shell_exit_handler, &ctx);
    }

    /* Wire up the overlay.
     */
    if (ctx.verbose)
        msg ("initializing overlay sockets");
    if (overlay_bind (ctx.overlay) < 0) /* idempotent */
        err_exit ("overlay_bind");
    if (overlay_connect (ctx.overlay) < 0)
        err_exit ("overlay_connect");

    /* Set up snoop socket
     */
    snoop_set_zctx (ctx.snoop, ctx.zctx);
    snoop_set_sec (ctx.snoop, ctx.sec);
    snoop_set_uri (ctx.snoop, "ipc://*");

    shutdown_set_handle (ctx.shutdown, ctx.h);
    shutdown_set_callback (ctx.shutdown, shutdown_cb, &ctx);

    /* Register internal services
     */
    broker_add_services (&ctx);

    /* Load modules
     */
    if (ctx.verbose)
        msg ("loading modules");
    modhash_set_zctx (ctx.modhash, ctx.zctx);
    modhash_set_rank (ctx.modhash, ctx.rank);
    modhash_set_flux (ctx.modhash, ctx.h);
    modhash_set_heartbeat (ctx.modhash, ctx.heartbeat);
    load_modules (&ctx, modules, modopts, modexclude, modpath);

    /* install heartbeat (including timer on rank 0)
     */
    heartbeat_set_flux (ctx.heartbeat, ctx.h);
    heartbeat_set_callback (ctx.heartbeat, heartbeat_cb, &ctx);
    if (ctx.rank == 0) {
        if (heartbeat_start (ctx.heartbeat) < 0)
            err_exit ("heartbeat_start");
        if (ctx.verbose)
            msg ("installing session heartbeat: T=%0.1fs",
                  heartbeat_get_rate (ctx.heartbeat));
    }

    /* Send hello message to parent.
     * Report progress every second.
     * Start init once wireup is complete.
     */
    hello_set_flux (ctx.hello, ctx.h);
    hello_set_callback (ctx.hello, hello_update_cb, &ctx);
    hello_set_timeout (ctx.hello, 1.0);
    if (hello_start (ctx.hello) < 0)
        err_exit ("hello_start");

    /* Event loop
     */
    if (ctx.verbose)
        msg ("entering event loop");
    if (flux_reactor_start (ctx.h) < 0)
        err ("flux_reactor_start");
    if (ctx.verbose)
        msg ("exited event loop");

    /* remove heartbeat timer, if any
     */
    heartbeat_stop (ctx.heartbeat);

    /* Unload modules.
     * FIXME: this will hang in pthread_join unless modules have been stopped.
     */
    if (ctx.verbose)
        msg ("unloading modules");
    modhash_destroy (ctx.modhash);

    if (ctx.verbose)
        msg ("cleaning up");
    if (ctx.sec)
        flux_sec_destroy (ctx.sec);
    zctx_destroy (&ctx.zctx);
    overlay_destroy (ctx.overlay);
    heartbeat_destroy (ctx.heartbeat);
    snoop_destroy (ctx.snoop);
    svchash_destroy (ctx.services);
    svchash_destroy (ctx.eventsvc);
    hello_destroy (ctx.hello);
    flux_close (ctx.h);

    zlist_destroy (&modules);       /* autofree set */
    zlist_destroy (&modopts);       /* autofree set */
    zhash_destroy (&modexclude);    /* values const (no destructor) */

    if (ctx.parent_uri)
        free (ctx.parent_uri);
    if (ctx.local_uri)
        free (ctx.local_uri);

    return 0;
}

/* On rank 0 flux_log is directed here.
 */
static void broker_log (void *arg, const char *facility, int level,
                        int rank, struct timeval tv, const char *s)
{
    //ctx_t *ctx = arg;
    const char *levstr;

    if (!(levstr = log_leveltostr (level)))
        levstr = "unknown";
    msg ("[%-.6lu.%-.6lu] %s.%s[%d] %s",
         tv.tv_sec, tv.tv_usec, facility, levstr, rank, s);
}

static void hello_update_cb (hello_t *hello, void *arg)
{
    ctx_t *ctx = arg;

    if (hello_complete (hello)) {
        flux_log (ctx->h, LOG_INFO, "nodeset: %s (complete)",
                  hello_get_nodeset (hello));
        if (ctx->shell)
            rank0_shell (ctx);
    } else  {
        flux_log (ctx->h, LOG_ERR, "nodeset: %s (incomplete)",
                  hello_get_nodeset (hello));
    }
    if (ctx->hello_timeout != 0
                        && hello_get_time (hello) >= ctx->hello_timeout) {
        shutdown_arm (ctx->shutdown, ctx->shutdown_grace, 1,
                      "hello timeout after %.1fs", ctx->hello_timeout);
        hello_set_timeout (hello, 0);
    }
}

static void shutdown_cb (shutdown_t *s, void *arg)
{
    //ctx_t *ctx = arg;
    int rc = shutdown_get_rc (s);
    exit (rc);
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

/* The 'ranktmp' dir will contain the broker.pid file and local:// socket.
 * It will be created in ctx->socket_dir.
 */
static void create_rankdir (ctx_t *ctx)
{
    char *ranktmp = xasprintf ("%s/%d", ctx->socket_dir, ctx->rank);

    if (mkdir (ranktmp, 0700) < 0)
        err_exit ("mkdir %s", ranktmp);
    cleanup_push_string (cleanup_directory, ranktmp);
    ctx->local_uri = xasprintf ("local://%s", ranktmp);
    free (ranktmp);
}

static void update_pidfile (ctx_t *ctx)
{
    char *pidfile  = xasprintf ("%s/%d/broker.pid", ctx->socket_dir, ctx->rank);
    FILE *f;

    if (!(f = fopen (pidfile, "w+")))
        err_exit ("%s", pidfile);
    if (fprintf (f, "%u", getpid ()) < 0)
        err_exit ("%s", pidfile);
    if (fclose(f) < 0)
        err_exit ("%s", pidfile);
    cleanup_push_string (cleanup_file, pidfile);
    free (pidfile);
}

/* See POSIX 2008 Volume 3 Shell and Utilities, Issue 7
 * Section 2.8.2 Exit status for shell commands (page 2315)
 */
static int rank0_shell_exit_handler (struct subprocess *p, void *arg)
{
    int rc;
    ctx_t *ctx = (ctx_t *) arg;
    assert (ctx != NULL);

    if (subprocess_signaled (p))
        rc = 128 + subprocess_signaled (p);
    else
        rc = subprocess_exit_code (p);
    shutdown_arm (ctx->shutdown, ctx->shutdown_grace, rc,
                  "%s", subprocess_state_string (p));
    return 0;
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
    subprocess_setenv (ctx->shell, "FLUX_URI", ctx->local_uri, 1);

    if (!ctx->quiet)
        flux_log (ctx->h, LOG_INFO, "starting shell");

    subprocess_run (ctx->shell);
}

/* N.B. If there are multiple nodes and multiple brokers per node, the
 * lowest rank in each clique will subscribe to the epgm:// socket
 * and relay events to an ipc:// socket for the other ranks in the
 * clique.  This is required due to a limitation of epgm.
 */
static void boot_pmi (ctx_t *ctx)
{
    pmi_t *pmi = pmi_init (NULL);
    int relay_rank = pmi_relay_rank (pmi);
    int right_rank = pmi_right_rank (pmi);
    char ipaddr[HOST_NAME_MAX + 1];

    ctx->size = pmi_size (pmi);
    ctx->rank = pmi_rank (pmi);
    ctx->sid = xstrdup (pmi_sid (pmi));

    overlay_set_rank (ctx->overlay, ctx->rank);

    ipaddr_getprimary (ipaddr, sizeof (ipaddr));
    overlay_set_child (ctx->overlay, "tcp://%s:*", ipaddr);

    if (relay_rank >= 0 && ctx->rank == relay_rank)
        overlay_set_relay (ctx->overlay, "ipc://*");

    if (overlay_bind (ctx->overlay) < 0) /* expand URI wildcards */
        err_exit ("overlay_bind failed");   /* function is idempotent */

    const char *child_uri = overlay_get_child (ctx->overlay);
    const char *relay_uri = overlay_get_relay (ctx->overlay);

    if (child_uri)
        pmi_put_uri (pmi, ctx->rank, child_uri);
    if (relay_uri)
        pmi_put_relay (pmi, ctx->rank, relay_uri);

    /* Puts are complete, now we synchronize and begin our gets.
     */
    pmi_fence (pmi);

    if (ctx->rank > 0) {
        int prank = ctx->k_ary == 0 ? 0 : (ctx->rank - 1) / ctx->k_ary;
        overlay_push_parent (ctx->overlay, "%s", pmi_get_uri (pmi, prank));
    }
    overlay_set_right (ctx->overlay, "%s", pmi_get_uri (pmi, right_rank));

    if (relay_rank >= 0 && ctx->rank != relay_rank) {
        overlay_set_event (ctx->overlay, "%s", pmi_get_relay (pmi, relay_rank));
    } else {
        int p = 5000 + pmi_jobid (pmi) % 1024;
        overlay_set_event (ctx->overlay, "epgm://%s;239.192.1.1:%d", ipaddr, p);
    }

    pmi_fini (pmi);
}

static void boot_local (ctx_t *ctx)
{
    int rrank = ctx->rank == 0 ? ctx->size - 1 : ctx->rank - 1;
    char *reqfile = xasprintf ("%s/%d/req", ctx->socket_dir, ctx->rank);
    char *eventfile = xasprintf ("%s/event", ctx->socket_dir);
    overlay_set_child (ctx->overlay, "ipc://%s", reqfile);
    cleanup_push_string (cleanup_file, reqfile);
    free (reqfile);
    if (ctx->rank > 0) {
        int prank = ctx->k_ary == 0 ? 0 : (ctx->rank - 1) / ctx->k_ary;
        overlay_push_parent (ctx->overlay, "ipc://%s/%d/req",
                             ctx->socket_dir, prank);
    }
    overlay_set_event (ctx->overlay, "ipc://%s", eventfile);
    if (ctx->rank == 0) {
        cleanup_push_string (cleanup_file, eventfile);
    }
    free (eventfile);
    overlay_set_right (ctx->overlay, "ipc://%s/%d/req",
                       ctx->socket_dir, rrank);
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

static int mod_svc_cb (zmsg_t **zmsg, void *arg)
{
    module_t *p = arg;
    int rc = module_sendmsg (p, *zmsg);
    zmsg_destroy (zmsg);
    return rc;
}

/* Load command line/default comms modules.  If module name contains
 * one or more '/' characters, it refers to a .so path.
 */
static void load_modules (ctx_t *ctx, zlist_t *modules, zlist_t *modopts,
                          zhash_t *modexclude, const char *modpath)
{
    char *s;
    module_t *p;
    zhash_t *mods; /* hash by module name */

    /* Create modules, adding to 'mods' list
     */
    if (!(mods = zhash_new ()))
        oom ();
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
            if (!(path = flux_modfind (modpath, s)))
                msg_exit ("%s: not found in module search path", s);
            name = s;
        }
        if (modexclude && zhash_lookup (modexclude, name))
            goto next;
        if (!(p = module_add (ctx->modhash, path))) {
            err ("%s: module_add %s", name, path);
            goto next;
        }
        if (!svc_add (ctx->services, module_get_name (p), mod_svc_cb, p)) {
            msg ("could not register service %s", module_get_name (p));
            module_remove (ctx->modhash, p);
            goto next;
        }
        zhash_update (mods, module_get_name (p), p);
        module_set_poller_cb (p, module_cb, ctx);
        module_set_rmmod_cb (p, rmmod_cb, ctx);
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
        if (!(p = zhash_lookup (mods, s)))
            msg_exit ("module argument for unknown module: %s", s);
        module_add_arg (p, arg);
        s = zlist_next (modopts);
    }

    /* Now start all the modules we just created.
     */
    module_start_all (ctx->modhash);

    zhash_destroy (&mods);
}

static void broker_init_signalfd (ctx_t *ctx)
{
    sigset_t sigmask;
    int fd;

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

    if ((fd = signalfd(-1, &sigmask, SFD_NONBLOCK | SFD_CLOEXEC)) < 0)
        err_exit ("signalfd");
    if (!(ctx->signalfd_w = flux_fd_watcher_create (fd, FLUX_POLLIN,
                                                    signalfd_cb, ctx)))
        err_exit ("flux_fd_watcher_create");
    flux_fd_watcher_start (ctx->h, ctx->signalfd_w);
}

/**
 ** Built-in services
 **
 ** Requests received from modules/peers via their respective reactor
 ** callbacks are sent on via broker_request_sendmsg().  The broker handle
 ** then dispatches locally matched ones to their svc handlers.
 **
 ** If the request zmsg is not destroyed by the service handler, an
 ** a no-payload response is generated.  If the handler returned -1,
 ** the response errnum is set to errno, else 0.
 **/

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
    zmsg_t *zmsg = (zmsg_t *) subprocess_get_context (p, "zmsg");
    json_object *resp;

    assert (ctx != NULL);
    assert (zmsg != NULL);

    resp = subprocess_json_resp (ctx, p);
    util_json_object_add_int (resp, "status", subprocess_exit_status (p));
    util_json_object_add_int (resp, "code", subprocess_exit_code (p));
    if ((n = subprocess_signaled (p)))
        util_json_object_add_int (resp, "signal", n);
    if ((n = subprocess_exec_error (p)))
        util_json_object_add_int (resp, "exec_errno", n);

    flux_json_respond (ctx->h, resp, &zmsg);
    json_object_put (resp);

    subprocess_destroy (p);

    return (0);
}

static int subprocess_io_cb (struct subprocess *p, const char *json_str)
{
    ctx_t *ctx = subprocess_get_context (p, "ctx");
    flux_msg_t *orig = subprocess_get_context (p, "zmsg");
    json_object *o = NULL;
    int rc = -1;

    assert (ctx != NULL);
    assert (orig != NULL);

    if (!(o = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    /* Add this rank */
    Jadd_int (o, "rank", ctx->rank);
    rc = flux_respond (ctx->h, orig, 0, Jtostr (o));
done:
    Jput (o);
    return (rc);
}

static struct subprocess *
subprocess_get_pid (struct subprocess_manager *sm, int pid)
{
    struct subprocess *p = NULL;
    p = subprocess_manager_first (sm);
    while (p) {
        if (pid == subprocess_pid (p))
            return (p);
        p = subprocess_manager_next (sm);
    }
    return (NULL);
}

static int cmb_write_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *request = NULL;
    json_object *response = NULL;
    json_object *o = NULL;
    int pid;
    int errnum = EPROTO;

    if (flux_json_request_decode (*zmsg, &request) < 0)
        goto out;

    if (Jget_int (request, "pid", &pid) &&
        Jget_obj (request, "stdin", &o)) {
        int len;
        void *data = NULL;
        bool eof;
        struct subprocess *p;

        /* XXX: We use zio_json_decode() here for convenience. Probably
         *  this should be bubbled up as a subprocess IO json spec with
         *  encode/decode functions.
         */
        if ((len = zio_json_decode (Jtostr (o), &data, &eof)) < 0)
            goto out;
        if (!(p = subprocess_get_pid (ctx->sm, pid))) {
            errnum = ENOENT;
            free (data);
            goto out;
        }
        if (subprocess_write (p, data, len, eof) < 0) {
            errnum = errno;
            free (data);
            goto out;
        }
        free (data);
    }
out:
    response = util_json_object_new_object ();
    Jadd_int (response, "code", errnum);
    flux_json_respond (ctx->h, response, zmsg);
    if (response)
        json_object_put (response);
    if (request)
        json_object_put (request);
    return (0);
}

static int cmb_signal_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *request = NULL;
    json_object *response = NULL;
    int pid;
    int errnum = EPROTO;

    if (flux_json_request_decode (*zmsg, &request) < 0)
        goto out;
    if (Jget_int (request, "pid", &pid)) {
        int signum;
        struct subprocess *p;
        if (!Jget_int (request, "signum", &signum))
            signum = SIGTERM;
        p = subprocess_manager_first (ctx->sm);
        while (p) {
            if (pid == subprocess_pid (p)) {
                errnum = 0;
                if (subprocess_kill (p, signum) < 0)
                    errnum = errno;
            }
            p = subprocess_manager_next (ctx->sm);
        }
    }
out:
    response = util_json_object_new_object ();
    Jadd_int (response, "code", errnum);
    flux_json_respond (ctx->h, response, zmsg);
    if (response)
        json_object_put (response);
    if (request)
        json_object_put (request);
    return (0);
}

/*
 *  Create a subprocess described in the zmsg argument.
 */
static int cmb_exec_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *request = NULL;
    json_object *response = NULL;
    json_object *o;
    struct subprocess *p;
    zmsg_t *copy;
    int i, argc;

    if (flux_json_request_decode (*zmsg, &request) < 0)
        goto out_free;

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
    subprocess_set_context (p, "ctx", ctx);

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
    }
    else
        subprocess_set_environ (p, environ);
    /*
     *  Override key FLUX environment variables in env array
     */
    subprocess_setenv (p, "FLUX_URI", ctx->local_uri, 1);

    if (json_object_object_get_ex (request, "cwd", &o) && o != NULL) {
        const char *dir = json_object_get_string (o);
        if (dir != NULL)
            subprocess_set_cwd (p, dir);
    }

    /*
     * Save a copy of zmsg for future messages
     */
    copy = zmsg_dup (*zmsg);
    subprocess_set_context (p, "zmsg", (void *) copy);

    subprocess_set_io_callback (p, subprocess_io_cb);

    if (subprocess_fork (p) < 0) {
        /*
         *  Fork error, respond directly to exec client with error
         *   (There will be no subprocess to reap)
         */
        (void) flux_respond (ctx->h, *zmsg, errno, NULL);
        goto out_free;
    }

    if (subprocess_exec (p) >= 0) {
        /*
         *  Send response, destroys original zmsg.
         *   For "Exec Failure" allow that state to be transmitted
         *   to caller on completion handler (which will be called
         *   immediately)
         */
        response = subprocess_json_resp (ctx, p);
        flux_json_respond (ctx->h, response, zmsg);
    }
out_free:
    if (request)
        json_object_put (request);
    if (response)
        json_object_put (response);
    return (0);
}

static char *subprocess_sender (struct subprocess *p)
{
    char *sender = NULL;
    zmsg_t *zmsg = subprocess_get_context (p, "zmsg");
    if (zmsg)
        flux_msg_get_route_first (zmsg, &sender);
    return (sender);
}

static int terminate_subprocesses_by_uuid (ctx_t *ctx, char *id)
{
    struct subprocess *p = subprocess_manager_first (ctx->sm);
    while (p) {
        char *sender;
        if ((sender = subprocess_sender (p))) {
            if (strcmp (id, sender) == 0)
                subprocess_kill (p, SIGKILL);
            free (sender);
        }
        p = subprocess_manager_next (ctx->sm);
    }
    return (0);
}

static JSON subprocess_json_info (struct subprocess *p)
{
    int i;
    char buf [MAXPATHLEN];
    const char *cwd;
    char *sender = NULL;
    JSON o = Jnew ();
    JSON a = Jnew_ar ();

    Jadd_int (o, "pid", subprocess_pid (p));
    for (i = 0; i < subprocess_get_argc (p); i++) {
        Jadd_ar_str (a, subprocess_get_arg (p, i));
    }
    /*  Avoid shortjson here so we don't take
     *   unnecessary reference to 'a'
     */
    json_object_object_add (o, "cmdline", a);
    if ((cwd = subprocess_get_cwd (p)) == NULL)
        cwd = getcwd (buf, MAXPATHLEN-1);
    Jadd_str (o, "cwd", cwd);
    if ((sender = subprocess_sender (p))) {
        Jadd_str (o, "sender", sender); 
        free (sender);
    }
    return (o);
}

static int cmb_ps_cb (zmsg_t **zmsg, void *arg)
{
    struct subprocess *p;
    ctx_t *ctx = arg;
    JSON out = Jnew ();
    JSON procs = Jnew_ar ();
    int rc;

    Jadd_int (out, "rank", ctx->rank);

    p = subprocess_manager_first (ctx->sm);
    while (p) {
        JSON o = subprocess_json_info (p);
        /* Avoid shortjson here so we don't take an unnecessary
         *  reference to 'o'.
         */
        json_object_array_add (procs, o);
        p = subprocess_manager_next (ctx->sm);
    }
    json_object_object_add (out, "procs", procs);
    rc = flux_json_respond (ctx->h, out, zmsg);
    Jput (out);
    return (rc);
}

static int cmb_info_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON out = Jnew ();
    int rc;

    Jadd_int (out, "rank", ctx->rank);
    Jadd_int (out, "size", ctx->size);
    Jadd_int (out, "arity", ctx->k_ary);

    rc = flux_json_respond (ctx->h, out, zmsg);
    Jput (out);
    return rc;
}

static int cmb_getattr_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON out = Jnew ();
    JSON in = NULL;
    const char *name = NULL;
    const char *val = NULL;
    int rc = -1;

    if (flux_json_request_decode (*zmsg, &in) < 0)
        goto done;
    if (!Jget_str (in, "name", &name)) {
        errno = EPROTO;
        goto done;
    }
    if (!strcmp (name, "snoop-uri"))
        val = snoop_get_uri (ctx->snoop);
    else if (!strcmp (name, "tbon-parent-uri"))
        val = overlay_get_parent (ctx->overlay);
    else if (!strcmp (name, "tbon-request-uri"))
        val = overlay_get_child (ctx->overlay);
    else if (!strcmp (name, "local-uri"))
        val = ctx->local_uri;
    else if (!strcmp (name, "parent-uri"))
        val = ctx->parent_uri;
    else
        errno = ENOENT;
    if (!val)
        goto done;
    Jadd_str (out, (char *)name, val);
    rc = flux_json_respond (ctx->h, out, zmsg);
done:
    Jput (in);
    Jput (out);
    return rc;
}

static int cmb_rusage_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON out = NULL;
    struct rusage usage;
    int rc = -1;

    if (getrusage (RUSAGE_THREAD, &usage) < 0)
        goto done;
    if (!(out = rusage_to_json (&usage)))
        goto done;
    rc = flux_json_respond (ctx->h, out, zmsg);
done:
    Jput (out);
    return rc;
}

static int cmb_rmmod_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    char *name = NULL;
    int rc = -1;
    module_t *p;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (flux_rmmod_json_decode (json_str, &name) < 0)
        goto done;
    if (!(p = module_lookup_byname (ctx->modhash, name))) {
        errno = ENOENT;
        goto done;
    }
    /* N.B. can't remove 'service' entry here as distributed
     * module shutdown may require inter-rank module communication.
     */
    if (module_stop (p, *zmsg) < 0)
        goto done;
    zmsg_destroy (zmsg); /* zmsg will be replied to later */
    flux_log (ctx->h, LOG_INFO, "rmmod %s", name);
    rc = 0;
done:
    if (name)
        free (name);
    return rc;
}

static int cmb_insmod_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    char *name = NULL;
    char *path = NULL;
    char *argz = NULL;
    size_t argz_len = 0;
    module_t *p;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (flux_insmod_json_decode (json_str, &path, &argz, &argz_len) < 0)
        goto done;
    if (!(name = flux_modname (path))) {
        errno = ENOENT;
        goto done;
    }
    if (!(p = module_add (ctx->modhash, path)))
        goto done;
    if (!svc_add (ctx->services, module_get_name (p), mod_svc_cb, p)) {
        module_remove (ctx->modhash, p);
        errno = EEXIST;
        goto done;
    }
    arg = argz_next (argz, argz_len, NULL);
    while (arg) {
        module_add_arg (p, arg);
        arg = argz_next (argz, argz_len, arg);
    }
    module_set_poller_cb (p, module_cb, ctx);
    module_set_rmmod_cb (p, rmmod_cb, ctx);
    if (module_start (p) < 0)
        goto done;
    flux_log (ctx->h, LOG_INFO, "insmod %s", name);
    rc = 0;
done:
    if (name)
        free (name);
    if (path)
        free (path);
    if (argz)
        free (argz);
    return rc;
}

static int cmb_lsmod_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    flux_modlist_t mods = NULL;
    char *json_str = NULL;
    JSON out = NULL;
    int rc = -1;

    if (!(mods = module_get_modlist (ctx->modhash)))
        goto done;
    if (!(json_str = flux_lsmod_json_encode (mods)))
        goto done;
    out = Jfromstr (json_str);
    assert (out != NULL);
    if (flux_json_respond (ctx->h, out, zmsg) < 0)
        goto done;
    rc = 0;
done:
    Jput (out);
    if (json_str)
        free (json_str);
    if (mods)
        flux_modlist_destroy (mods);
    return rc;
}

static int cmb_lspeer_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON out = overlay_lspeer_encode (ctx->overlay);
    int rc = flux_json_respond (ctx->h, out, zmsg);
    Jput (out);
    return rc;
}

static int cmb_ping_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON inout = NULL;
    char *s = NULL;
    char *route = NULL;
    int rc = -1;

    if (flux_json_request_decode (*zmsg, &inout) < 0)
        goto done;
    if (!(s = flux_msg_get_route_string (*zmsg)))
        goto done;
    route = xasprintf ("%s!%u", s, ctx->rank);
    Jadd_str (inout, "route", route);
    rc = flux_json_respond (ctx->h, inout, zmsg);
done:
    if (s)
        free (s);
    if (route)
        free (route);
    return rc;
}

static int cmb_reparent_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON in = NULL;
    const char *uri;
    bool recycled = false;
    int rc = -1;

    if (flux_json_request_decode (*zmsg, &in) < 0)
        goto done;
    if (!Jget_str (in, "uri", &uri)) {
        errno = EPROTO;
        goto done;
    }
    if (overlay_reparent (ctx->overlay, uri, &recycled) < 0)
        goto done;
    flux_log (ctx->h, LOG_INFO, "reparent %s (%s)", uri, recycled ? "restored"
                                                                  : "new");
    rc = 0;
done:
    Jput (in);
    return rc;
}

static int cmb_panic_cb (zmsg_t **zmsg, void *arg)
{
    JSON in = NULL;
    const char *s = NULL;
    int rc = -1;

    if (flux_json_request_decode (*zmsg, &in) < 0)
        goto done;
    if (!Jget_str (in, "msg", &s))
        s = "no reason";
    msg ("PANIC: %s", s ? s : "no reason");
    exit (1);
done:
    Jput (in);
    return rc;
}

static int cmb_log_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0) {
        msg ("%s: decode error", __FUNCTION__);
        goto done;
    }
    if (flux_log_json (ctx->h, json_str) < 0) {
        msg ("%s: flux_log_json %s: %s", __FUNCTION__, json_str,
             strerror (errno));
        goto done;
    }
done:
    zmsg_destroy (zmsg); /* no reply */
    return 0;
}

static int cmb_event_mute_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    char *uuid = NULL;

    if (flux_msg_get_route_last (*zmsg, &uuid) == 0)
        overlay_mute_child (ctx->overlay, uuid);
    if (uuid)
        free (uuid);
    zmsg_destroy (zmsg); /* no reply */
    return 0;
}

static int cmb_disconnect_cb (zmsg_t **zmsg, void *arg)
{
    char *sender;
    if (flux_msg_get_route_first (*zmsg, &sender) == 0) {
        terminate_subprocesses_by_uuid ((ctx_t *) arg, sender);
        free (sender);
    }
    zmsg_destroy (zmsg); /* no reply */
    return 0;
}

static int cmb_hello_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    if (ctx->rank == 0)
        hello_recvmsg (ctx->hello, *zmsg);
    zmsg_destroy (zmsg); /* no reply */
    return 0;
}

static int cmb_sub_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    char *uuid = NULL;
    JSON in = NULL;
    const char *topic;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "topic", &topic)) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_route_first (*zmsg, &uuid) < 0)
        goto done;
    if (!uuid) {
        errno = EPROTO;
        goto done;
    }
    rc = module_subscribe (ctx->modhash, uuid, topic);
done:
    if (rc < 0)
        flux_log (ctx->h, LOG_ERR, "%s: %s", __FUNCTION__, strerror (errno));
    if (uuid)
        free (uuid);
    Jput (in);
    rc = flux_respond (ctx->h, *zmsg, rc < 0 ? errno : 0, NULL);
    zmsg_destroy (zmsg);
    return rc;
}

static int cmb_unsub_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    char *uuid = NULL;
    JSON in = NULL;
    const char *topic;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "topic", &topic)) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_route_first (*zmsg, &uuid) < 0)
        goto done;
    if (!uuid) {
        errno = EPROTO;
        goto done;
    }
    rc = module_unsubscribe (ctx->modhash, uuid, topic);
done:
    if (rc < 0)
        flux_log (ctx->h, LOG_ERR, "%s: %s", __FUNCTION__, strerror (errno));
    if (uuid)
        free (uuid);
    Jput (in);
    rc = flux_respond (ctx->h, *zmsg, rc < 0 ? errno : 0, NULL);
    zmsg_destroy (zmsg);
    return rc;
}

static int event_hb_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;

    if (heartbeat_recvmsg (ctx->heartbeat, *zmsg) < 0)
        flux_log (ctx->h, LOG_ERR, "%s: heartbeat_recvmsg: %s", __FUNCTION__,
                  strerror (errno));
    return 0;
}

/* Shutdown:
 * - start the shutdown timer
 * - send shutdown message to all modules
 */
static int event_shutdown_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    shutdown_recvmsg (ctx->shutdown, *zmsg);
    //if (module_stop_all (ctx->modhash) < 0)
    //    flux_log (ctx->h, LOG_ERR, "module_stop_all: %s", strerror (errno));
    return 0;
}

static void broker_add_services (ctx_t *ctx)
{
    if (!svc_add (ctx->services, "cmb.info", cmb_info_cb, ctx)
          || !svc_add (ctx->services, "cmb.getattr", cmb_getattr_cb, ctx)
          || !svc_add (ctx->services, "cmb.rusage", cmb_rusage_cb, ctx)
          || !svc_add (ctx->services, "cmb.rmmod", cmb_rmmod_cb, ctx)
          || !svc_add (ctx->services, "cmb.insmod", cmb_insmod_cb, ctx)
          || !svc_add (ctx->services, "cmb.lsmod", cmb_lsmod_cb, ctx)
          || !svc_add (ctx->services, "cmb.lspeer", cmb_lspeer_cb, ctx)
          || !svc_add (ctx->services, "cmb.ping", cmb_ping_cb, ctx)
          || !svc_add (ctx->services, "cmb.reparent", cmb_reparent_cb, ctx)
          || !svc_add (ctx->services, "cmb.panic", cmb_panic_cb, ctx)
          || !svc_add (ctx->services, "cmb.log", cmb_log_cb, ctx)
          || !svc_add (ctx->services, "cmb.event-mute", cmb_event_mute_cb, ctx)
          || !svc_add (ctx->services, "cmb.exec", cmb_exec_cb, ctx)
          || !svc_add (ctx->services, "cmb.exec.signal", cmb_signal_cb, ctx)
          || !svc_add (ctx->services, "cmb.exec.write", cmb_write_cb, ctx)
          || !svc_add (ctx->services, "cmb.processes", cmb_ps_cb, ctx)
          || !svc_add (ctx->services, "cmb.disconnect", cmb_disconnect_cb, ctx)
          || !svc_add (ctx->services, "cmb.hello", cmb_hello_cb, ctx)
          || !svc_add (ctx->services, "cmb.sub", cmb_sub_cb, ctx)
          || !svc_add (ctx->services, "cmb.unsub", cmb_unsub_cb, ctx)
          || !svc_add (ctx->eventsvc, "hb", event_hb_cb, ctx)
          || !svc_add (ctx->eventsvc, "shutdown", event_shutdown_cb, ctx))
        err_exit ("can't register internal services");
}

/**
 ** reactor callbacks
 **/


/* Handle requests from overlay peers.
 */
static void child_cb (overlay_t *ov, void *sock, void *arg)
{
    ctx_t *ctx = arg;
    int type;
    char *uuid = NULL;
    int rc;
    zmsg_t *zmsg = zmsg_recv (sock);

    if (!zmsg)
        goto done;
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    if (flux_msg_get_route_last (zmsg, &uuid) < 0)
        goto done;
    overlay_checkin_child (ctx->overlay, uuid);
    switch (type) {
        case FLUX_MSGTYPE_KEEPALIVE:
            (void)snoop_sendmsg (ctx->snoop, zmsg);
            break;
        case FLUX_MSGTYPE_REQUEST:
            rc = broker_request_sendmsg (ctx, &zmsg);
            if (zmsg)
                flux_err_respond (ctx->h, rc < 0 ? errno : 0, &zmsg);
            break;
        case FLUX_MSGTYPE_EVENT:
            rc = broker_event_sendmsg (ctx, &zmsg);
            break;
    }
done:
    if (uuid)
        free (uuid);
    zmsg_destroy (&zmsg);
}

/* helper for event_cb, parent_cb, broker_event_sendmsg (rank 0 only) */
static int handle_event (ctx_t *ctx, zmsg_t **zmsg)
{
    if (ctx->rank > 0) {
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
        if (ctx->event_seq > 0) { /* don't log initial missed events */
            for (i = ctx->event_seq + 1; i < seq; i++)
                flux_log (ctx->h, LOG_ERR, "lost event %d", i);
        }
        ctx->event_seq = seq;
    }

    (void)overlay_mcast_child (ctx->overlay, *zmsg);
    (void)overlay_sendmsg_relay (ctx->overlay, *zmsg);
    (void)svc_sendmsg (ctx->eventsvc, zmsg);

    return module_event_mcast (ctx->modhash, *zmsg);
}

/* helper for parent_cb */
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

/* Handle messages from one or more parents.
 */
static void parent_cb (overlay_t *ov, void *sock, void *arg)
{
    ctx_t *ctx = arg;
    zmsg_t *zmsg = zmsg_recv (sock);
    int type;

    if (!zmsg)
        goto done;
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            if (broker_response_sendmsg (ctx, zmsg) < 0)
                goto done;
            break;
        case FLUX_MSGTYPE_EVENT:
            if (ctx->event_active) {
                send_mute_request (ctx, sock);
                goto done;
            }
            if (flux_msg_clear_route (zmsg) < 0) {
                flux_log (ctx->h, LOG_ERR, "dropping malformed event");
                goto done;
            }
            if (handle_event (ctx, &zmsg) < 0)
                goto done;
            break;
        default:
            flux_log (ctx->h, LOG_ERR, "%s: unexpected %s", __FUNCTION__,
                      flux_msg_typestr (type));
            break;
    }
done:
    zmsg_destroy (&zmsg);
}

/* Handle messages on the service socket of a comms module.
 */
static void module_cb (module_t *p, void *arg)
{
    ctx_t *ctx = arg;
    zmsg_t *zmsg = module_recvmsg (p);
    int type, rc;

    if (!zmsg)
        goto done;
    if (zmsg_content_size (zmsg) == 0) { /* EOF - safe to pthread_join */
        svc_remove (ctx->services, module_get_name (p));
        module_remove (ctx->modhash, p);
        goto done;
    }
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            (void)broker_response_sendmsg (ctx, zmsg);
            break;
        case FLUX_MSGTYPE_REQUEST:
            rc = broker_request_sendmsg (ctx, &zmsg);
            if (zmsg)
                flux_err_respond (ctx->h, rc < 0 ? errno : 0, &zmsg);
            break;
        case FLUX_MSGTYPE_EVENT:
            if (broker_event_sendmsg (ctx, &zmsg) < 0) {
                flux_log (ctx->h, LOG_ERR, "%s(%s): broker_event_sendmsg %s: %s",
                          __FUNCTION__, module_get_name (p),
                          flux_msg_typestr (type), strerror (errno));
            }
            break;
        default:
            flux_log (ctx->h, LOG_ERR, "%s(%s): unexpected %s",
                      __FUNCTION__, module_get_name (p),
                      flux_msg_typestr (type));
            break;
    }
done:
    zmsg_destroy (&zmsg);
}

static void rmmod_cb (module_t *p, void *arg)
{
    ctx_t *ctx = arg;
    zmsg_t *zmsg;

    while ((zmsg = module_pop_rmmod (p))) {
        if (flux_err_respond (ctx->h, 0, &zmsg) < 0)
            err ("%s: flux_err_respond", __FUNCTION__);
        zmsg_destroy (&zmsg);
    }
}

static void event_cb (overlay_t *ov, void *sock, void *arg)
{
    ctx_t *ctx = arg;
    zmsg_t *zmsg = overlay_recvmsg_event (ov);
    int type;

    if (!zmsg)
        goto done;
    ctx->event_active = true;
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_EVENT:
            if (handle_event (ctx, &zmsg) < 0)
                goto done;
            break;
        default:
            flux_log (ctx->h, LOG_ERR, "%s: unexpected %s", __FUNCTION__,
                      flux_msg_typestr (type));
            break;
    }
done:
    zmsg_destroy (&zmsg);
}

/* This is called on each heartbeat (all ranks).
 */
static void heartbeat_cb (heartbeat_t *h, void *arg)
{
    ctx_t *ctx = arg;

    if (ctx->rank > 0)
        (void) overlay_keepalive_parent (ctx->overlay);
}

static void signalfd_cb (flux_t h, flux_fd_watcher_t *w,
                         int fd, int revents, void *arg)
{
    ctx_t *ctx = arg;
    struct signalfd_siginfo fdsi;
    ssize_t n;

    if ((n = read (fd, &fdsi, sizeof (fdsi))) < 0) {
        if (errno != EWOULDBLOCK)
            err_exit ("read");
    } else if (n == sizeof (fdsi)) {
        if (fdsi.ssi_signo == SIGCHLD)
            subprocess_manager_reap_all (ctx->sm);
        else {
            shutdown_arm (ctx->shutdown, ctx->shutdown_grace, 0,
                          "signal %d (%s) %d",
                          fdsi.ssi_signo, strsignal (fdsi.ssi_signo));
        }
    }
}

static int broker_request_sendmsg (ctx_t *ctx, zmsg_t **zmsg)
{
    uint32_t nodeid;
    int flags;
    int rc = -1;

    if (flux_msg_get_nodeid (*zmsg, &nodeid, &flags) < 0)
        goto done;
    if ((flags & FLUX_MSGFLAG_UPSTREAM) && nodeid == ctx->rank) {
        rc = overlay_sendmsg_parent (ctx->overlay, *zmsg);
        if (rc == 0)
            zmsg_destroy (zmsg);
    } else if ((flags & FLUX_MSGFLAG_UPSTREAM) && nodeid != ctx->rank) {
        rc = svc_sendmsg (ctx->services, zmsg);
        if (rc < 0 && errno == ENOSYS) {
            rc = overlay_sendmsg_parent (ctx->overlay, *zmsg);
            if (rc == 0)
                zmsg_destroy (zmsg);
        }
    } else if (nodeid == FLUX_NODEID_ANY) {
        rc = svc_sendmsg (ctx->services, zmsg);
        if (rc < 0 && errno == ENOSYS) {
            rc = overlay_sendmsg_parent (ctx->overlay, *zmsg);
            if (rc == 0)
                zmsg_destroy (zmsg);
        }
    } else if (nodeid == ctx->rank) {
        rc = svc_sendmsg (ctx->services, zmsg);
    } else if (nodeid == 0) {
        rc = overlay_sendmsg_parent (ctx->overlay, *zmsg);
        if (rc == 0)
            zmsg_destroy (zmsg);
    } else {
        rc = overlay_sendmsg_right (ctx->overlay, *zmsg);
        if (rc == 0)
            zmsg_destroy (zmsg);
    }
done:
    /* N.B. don't destroy zmsg on error as we use it to send errnum reply.
     */
    return rc;
}

static int broker_response_sendmsg (ctx_t *ctx, const flux_msg_t *msg)
{
    int rc = module_response_sendmsg (ctx->modhash, msg);
    if (rc < 0 && errno == ENOSYS)
        rc = overlay_sendmsg_child (ctx->overlay, msg);
    return rc;
}

/* Events are forwarded up the TBON to rank 0, then published from there.
 * Rank 0 doesn't receive the events it transmits so we have to "loop back"
 * here via handle_event().
 */
static int broker_event_sendmsg (ctx_t *ctx, zmsg_t **zmsg)
{
    int rc = -1;

    if (ctx->rank > 0) {
        if (flux_msg_enable_route (*zmsg) < 0)
            goto done;
        rc = overlay_sendmsg_parent (ctx->overlay, *zmsg);
    } else {
        if (flux_msg_clear_route (*zmsg) < 0)
            goto done;
        if (flux_msg_set_seq (*zmsg, ++ctx->event_seq) < 0)
            goto done;
        if (overlay_sendmsg_event (ctx->overlay, *zmsg) < 0)
            goto done;
        rc = handle_event (ctx, zmsg);
    }
done:
    zmsg_destroy (zmsg);
    return rc;
}

/**
 ** Broker's internal, minimal flux_t implementation.
 **/

static int broker_send (void *impl, const flux_msg_t *msg, int flags)
{
    ctx_t *ctx = impl;
    int type;
    flux_msg_t *cpy = NULL;
    int rc = -1;

    (void)snoop_sendmsg (ctx->snoop, (zmsg_t *)msg);

    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            if (!(cpy = flux_msg_copy (msg, true)))
                goto done;
            rc = broker_request_sendmsg (ctx, &cpy);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            rc = broker_response_sendmsg (ctx, msg);
            break;
        case FLUX_MSGTYPE_EVENT:
            if (!(cpy = flux_msg_copy (msg, true)))
                goto done;
            rc = broker_event_sendmsg (ctx, &cpy);
            break;
        default:
            errno = EINVAL;
            break;
    }
done:
    return rc;
}

static const struct flux_handle_ops broker_handle_ops = {
    .send = broker_send,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
