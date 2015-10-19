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
#include "src/common/libpmi-client/pmi-client.h"
#include "src/modules/libsubprocess/subprocess.h"
#include "src/modules/libzio/zio.h"

#include "heartbeat.h"
#include "module.h"
#include "overlay.h"
#include "snoop.h"
#include "service.h"
#include "hello.h"
#include "shutdown.h"
#include "attr.h"
#include "log.h"

#ifndef ZMQ_IMMEDIATE
#define ZMQ_IMMEDIATE           ZMQ_DELAY_ATTACH_ON_CONNECT
#define zsocket_set_immediate   zsocket_set_delay_attach_on_connect
#endif

const char *default_modules =
    "connector-local,modctl,kvs,live,mecho,job[0],wrexec,barrier,resource-hwloc";

const char *default_boot_method = "pmi";

typedef struct {
    /* 0MQ
     */
    zctx_t *zctx;               /* zeromq context (MT-safe) */
    flux_sec_t sec;             /* security context (MT-safe) */

    /* Reactor
     */
    flux_t h;
    flux_reactor_t *reactor;
    zlist_t *sigwatchers;

    /* Sockets.
     */
    overlay_t *overlay;
    snoop_t *snoop;

    /* Session parameters
     */
    uint32_t size;              /* session size */
    uint32_t rank;              /* our rank in session */
    char *socket_dir;
    attr_t *attrs;

    /* Modules
     */
    modhash_t *modhash;
    /* Misc
     */
    bool verbose;
    bool quiet;
    pid_t pid;
    char *proctitle;
    flux_conf_t cf;
    int event_seq;
    bool event_active;          /* primary event source is active */
    svchash_t *services;
    svchash_t *eventsvc;
    heartbeat_t *heartbeat;
    shutdown_t *shutdown;
    double shutdown_grace;
    log_t *log;
    /* Bootstrap
     */
    struct boot_method *boot_method;
    bool enable_epgm;
    int k_ary;
    hello_t *hello;
    double hello_timeout;

    /* Subprocess management
     */
    struct subprocess_manager *sm;

    char *init_shell_cmd;
    struct subprocess *init_shell;
} ctx_t;

struct boot_method {
    const char *name;
    int (*fun)(ctx_t *ctx);
};

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
static void signal_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg);
static void broker_block_signals (void);
static void broker_handle_signals (ctx_t *ctx, zlist_t *sigwatchers);
static void broker_unhandle_signals (zlist_t *sigwatchers);

static void broker_add_services (ctx_t *ctx);

static void load_modules (ctx_t *ctx, zlist_t *modules, zlist_t *modopts,
                          zhash_t *modexclude, const char *modpath);

static void update_proctitle (ctx_t *ctx);
static void update_pidfile (ctx_t *ctx);
static void init_shell (ctx_t *ctx);
static int init_shell_exit_handler (struct subprocess *p, void *arg);

static int create_socketdir (ctx_t *ctx);
static int create_rankdir (ctx_t *ctx);
static int create_dummyattrs (ctx_t *ctx);

static int boot_pmi (ctx_t *ctx);
static int boot_single (ctx_t *ctx);
static int boot_local (ctx_t *ctx);

static int attr_get_snoop (const char *name, const char **val, void *arg);
static int attr_get_overlay (const char *name, const char **val, void *arg);

static int attr_get_log (const char *name, const char **val, void *arg);
static int attr_set_log (const char *name, const char *val, void *arg);

static const struct flux_handle_ops broker_handle_ops;

static struct boot_method boot_table[] = {
    { "pmi", boot_pmi },
    { "single", boot_single },
    { "local", boot_local },
    { NULL, NULL },
};

#define OPTIONS "+vqR:S:M:X:L:N:k:s:H:O:x:T:g:D:Em:l:"
static const struct option longopts[] = {
    {"sid",             required_argument,  0, 'N'},
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
    {"k-ary",           required_argument,  0, 'k'},
    {"heartrate",       required_argument,  0, 'H'},
    {"timeout",         required_argument,  0, 'T'},
    {"shutdown-grace",  required_argument,  0, 'g'},
    {"socket-directory",required_argument,  0, 'D'},
    {"enable-epgm",     no_argument,        0, 'E'},
    {"boot-method",     required_argument,  0, 'm'},
    {"log-level",       required_argument,  0, 'l'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: flux-broker OPTIONS [module:key=val ...]\n"
" -v,--verbose                 Be annoyingly verbose\n"
" -q,--quiet                   Be mysteriously taciturn\n"
" -R,--rank N                  Set broker rank (0...size-1)\n"
" -S,--size N                  Set number of ranks in session\n"
" -N,--sid NAME                Set session id\n"
" -M,--module NAME             Load module NAME (may be repeated)\n"
" -x,--exclude NAME            Exclude module NAME\n"
" -O,--modopt NAME:key=val     Set option for module NAME (may be repeated)\n"
" -X,--module-path PATH        Set module search path (colon separated)\n"
" -L,--logdest FILE            Redirect log output to specified file\n"
" -l,--log-level LEVEL         Set log level (default: 6/info)\n"
"          [0=emerg 1=alert 2=crit 3=err 4=warning 5=notice 6=info 7=debug]\n"
" -s,--security=plain|curve|none    Select security mode (default: curve)\n"
" -k,--k-ary K                 Wire up in a k-ary tree\n"
" -H,--heartrate SECS          Set heartrate in seconds (rank 0 only)\n"
" -T,--timeout SECS            Set wireup timeout in seconds (rank 0 only)\n"
" -g,--shutdown-grace SECS     Set shutdown grace period in seconds\n"
" -D,--socket-directory DIR    Create ipc sockets in DIR (local bootstrap)\n"
" -E,--enable-epgm             Enable EPGM for events (PMI bootstrap)\n"
" -m,--boot-method             Select bootstrap: pmi, single, local\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    int c;
    ctx_t ctx;
    zlist_t *modules, *modopts, *sigwatchers;
    zhash_t *modexclude;
    char *modpath;
    const char *confdir;
    int security_clr = 0;
    int security_set = 0;
    const char *secdir = NULL;
    char *boot_method = "pmi";
    char *log_filename = NULL;
    FILE *log_f = NULL;
    int log_level = LOG_INFO;
    int e;

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
    if (!(sigwatchers = zlist_new ()))
        oom ();

    ctx.rank = FLUX_NODEID_ANY;
    ctx.modhash = modhash_create ();
    ctx.services = svchash_create ();
    ctx.eventsvc = svchash_create ();
    ctx.overlay = overlay_create ();
    ctx.snoop = snoop_create ();
    ctx.hello = hello_create ();
    ctx.k_ary = 2; /* binary TBON is default */
    ctx.heartbeat = heartbeat_create ();
    ctx.shutdown = shutdown_create ();
    ctx.attrs = attr_create ();
    ctx.log = log_create ();

    ctx.pid = getpid();
    if (!(modpath = getenv ("FLUX_MODULE_PATH")))
        modpath = MODULE_PATH;

    if (!(ctx.sm = subprocess_manager_create ()))
        oom ();
    subprocess_manager_set (ctx.sm, SM_WAIT_FLAGS, WNOHANG);

    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 'N':   /* --sid NAME */
                if (attr_add (ctx.attrs, "session-id", optarg,
                                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
                    err_exit ("attr_add session-id");
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
                log_filename = optarg;
                break;
            case 'l':   /* --log-level 0-7 */
                if ((log_level = log_strtolevel (optarg)) < 0)
                    log_level = strtol (optarg, NULL, 10);
                break;
            case 'k':   /* --k-ary k */
                ctx.k_ary = strtoul (optarg, NULL, 10);
                if (ctx.k_ary < 0)
                    usage ();
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
            case 'E': /* --enable-epgm */
                ctx.enable_epgm = true;
                break;
            case 'm': { /* --boot-method */
                boot_method = optarg;
                break;
            }
            default:
                usage ();
        }
    }
    if (optind < argc) {
        size_t len = 0;
        if ((e = argz_create (argv + optind, &ctx.init_shell_cmd, &len)) != 0)
            errn_exit (e, "argz_creawte");
        argz_stringify (ctx.init_shell_cmd, len, ' ');
    }
    if (boot_method) {
        struct boot_method *m;
        for (m = &boot_table[0]; m->name != NULL; m++)
            if (!strcasecmp (m->name, boot_method))
                break;
        ctx.boot_method = m;
    }
    if (!ctx.boot_method)
        msg_exit ("invalid boot method: %s", boot_method ? boot_method
                                                         : "none");

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

    broker_block_signals ();

    /* Initailize zeromq context
     */
    zsys_handler_set (NULL);
    ctx.zctx = zctx_new ();
    if (!ctx.zctx)
        err_exit ("zctx_new");
    zctx_set_linger (ctx.zctx, 5);

    /* Set up the flux reactor.
     */
    if (!(ctx.reactor = flux_reactor_create (SIGCHLD)))
        err_exit ("flux_reactor_create");

    /* Set up flux handle.
     * The handle is used for simple purposes such as logging.
     */
    if (!(ctx.h = flux_handle_create (&ctx, &broker_handle_ops, 0)))
        err_exit ("flux_handle_create");
    flux_set_reactor (ctx.h, ctx.reactor);

    subprocess_manager_set (ctx.sm, SM_REACTOR, ctx.reactor);

    /* Prepare signal handling
     */
    broker_handle_signals (&ctx, sigwatchers);

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

    /* Execute the selected boot method.
     * At minimum, this must ensure that rank, size, and sid are set.
     */
    if (ctx.verbose)
        msg ("boot: %s", ctx.boot_method->name);
    if (ctx.boot_method->fun (&ctx) < 0)
        err_exit ("boot %s", ctx.boot_method->name);
    assert (ctx.rank != FLUX_NODEID_ANY);
    assert (ctx.size > 0);
    assert (attr_get (ctx.attrs, "session-id", NULL, NULL) == 0);

    /* Create directory for sockets, and a subdirectory specific
     * to this rank that will contain the pidfile and local connector socket.
     * (These may have already been called by boot method)
     */
    if (create_socketdir (&ctx) < 0)
        err_exit ("create_socketdir");
    if (create_rankdir (&ctx) < 0)
        err_exit ("create_rankdir");

    /* Initialize logging.
     */
    flux_log_set_facility (ctx.h, "broker");
    flux_log_set_redirect (ctx.h, log_append_redirect, ctx.log);
    log_set_flux (ctx.log, ctx.h);
    log_set_rank (ctx.log, ctx.rank);
    log_set_buflimit (ctx.log, 1024); /* adjust by attribute */
    if (log_set_level (ctx.log, log_level) < 0)
        err_exit ("invalid log level: %d", log_level);
    if (ctx.rank == 0 && log_filename) {
        if (!strcmp (log_filename, "stderr"))
            log_f = stderr;
        else if (!(log_f = fopen (log_filename, "a")))
            err_exit ("%s", log_filename);
        log_set_file (ctx.log, log_f);
    }

    /* Dummy up cached attributes on the broker's handle so logging's
     * use of flux_get_rank() etc will work despite limitations.
     */
    if (create_dummyattrs (&ctx) < 0)
        err_exit ("creating dummy attributes");

    overlay_set_rank (ctx.overlay, ctx.rank);

    /* Configure attributes.
     */
    if (attr_add_active (ctx.attrs, "snoop-uri",
                                FLUX_ATTRFLAG_IMMUTABLE,
                                attr_get_snoop, NULL, ctx.snoop) < 0
            || attr_add_active (ctx.attrs, "tbon-parent-uri", 0,
                                attr_get_overlay, NULL, ctx.overlay) < 0
            || attr_add_active (ctx.attrs, "tbon-request-uri",
                                FLUX_ATTRFLAG_IMMUTABLE,
                                attr_get_overlay, NULL, ctx.overlay) < 0
            || attr_add_active (ctx.attrs, "event-uri",
                                FLUX_ATTRFLAG_IMMUTABLE,
                                attr_get_overlay, NULL, ctx.overlay) < 0
            || attr_add_active (ctx.attrs, "event-relay-uri",
                                FLUX_ATTRFLAG_IMMUTABLE,
                                attr_get_overlay, NULL, ctx.overlay) < 0
            || attr_add_active (ctx.attrs, "log-level", 0,
                                attr_get_log, attr_set_log, ctx.log) < 0
            || attr_add_active (ctx.attrs, "log-buflimit", 0,
                                attr_get_log, attr_set_log, ctx.log) < 0
            || attr_add_active (ctx.attrs, "log-bufcount", 0,
                                attr_get_log, attr_set_log, ctx.log) < 0
            || attr_add_active (ctx.attrs, "log-count", 0,
                                attr_get_log, attr_set_log, ctx.log) < 0
            || attr_add_active_uint32 (ctx.attrs, "rank", &ctx.rank,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0
            || attr_add_active_uint32 (ctx.attrs, "size", &ctx.size,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0
            || attr_add_active_int (ctx.attrs, "tbon-arity", &ctx.k_ary,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0
            || attr_add (ctx.attrs, "parent-uri", getenv ("FLUX_URI"),
                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
        err_exit ("attr_add");

    /* The previous value of FLUX_URI (refers to enclosing instance)
     * was stored above.  Clear it here so a connection to the enclosing
     * instance is not made inadvertantly.
     */
    unsetenv ("FLUX_URI");

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

    if (ctx.rank == 0) {
        ctx.init_shell = subprocess_create (ctx.sm);
        subprocess_set_callback (ctx.init_shell, init_shell_exit_handler, &ctx);
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
    if (flux_reactor_run (ctx.reactor, 0) < 0)
        err ("flux_reactor_run");
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

    broker_unhandle_signals (sigwatchers);
    zlist_destroy (&sigwatchers);

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
    attr_destroy (ctx.attrs);
    flux_close (ctx.h);
    flux_reactor_destroy (ctx.reactor);
    log_destroy (ctx.log);
    if (log_f)
        fclose (log_f);

    zlist_destroy (&modules);       /* autofree set */
    zlist_destroy (&modopts);       /* autofree set */
    zhash_destroy (&modexclude);    /* values const (no destructor) */

    return 0;
}

static void hello_update_cb (hello_t *hello, void *arg)
{
    ctx_t *ctx = arg;

    if (hello_complete (hello)) {
        flux_log (ctx->h, LOG_INFO, "nodeset: %s (complete)",
                  hello_get_nodeset (hello));
        if (ctx->init_shell)
            init_shell (ctx);
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
static int init_shell_exit_handler (struct subprocess *p, void *arg)
{
    int rc = subprocess_exit_code (p);
    ctx_t *ctx = (ctx_t *) arg;
    assert (ctx != NULL);

    shutdown_arm (ctx->shutdown, ctx->shutdown_grace, rc,
                  "%s", subprocess_state_string (p));
    subprocess_destroy (p);
    return 0;
}

static void path_prepend (char **s1, const char *s2)
{
    char *p;

    if (!s2)
        ;
    else if (!*s1)
        *s1 = xstrdup (s2);
    else if ((p = strstr (*s1, s2))) {
        int s2_len = strlen (s2);
        memmove (p, p + s2_len, strlen (p + s2_len) + 1);
        if (*p == ':')
            memmove (p, p + 1, strlen (p + 1) + 1);
        path_prepend (s1, s2);
    } else {
        p = xasprintf ("%s:%s", s2, *s1);
        free (*s1);
        *s1 = p;
    }
}

static void init_shell (ctx_t *ctx)
{
    const char *shell = getenv ("SHELL");
    char *ldpath = NULL;
    const char *local_uri;

    if (attr_get (ctx->attrs, "local-uri", &local_uri, NULL) < 0)
        err_exit ("%s: local_uri is not set", __FUNCTION__);
    if (!shell)
        shell = "/bin/bash";

    subprocess_argv_append (ctx->init_shell, shell);
    if (ctx->init_shell_cmd) {
        subprocess_argv_append (ctx->init_shell, "-c");
        subprocess_argv_append (ctx->init_shell, ctx->init_shell_cmd);
    }
    subprocess_set_environ (ctx->init_shell, environ);
    subprocess_setenv (ctx->init_shell, "FLUX_URI", local_uri, 1);
    path_prepend (&ldpath, subprocess_getenv (ctx->init_shell,
                                              "LD_LIBRARY_PATH"));
    path_prepend (&ldpath, PROGRAM_LIBRARY_PATH);
    path_prepend (&ldpath, flux_conf_get (ctx->cf,
                                            "general.program_library_path"));
    if (ldpath) {
        subprocess_setenv (ctx->init_shell, "LD_LIBRARY_PATH", ldpath, 1);
        free (ldpath);
    }

    if (!ctx->quiet)
        flux_log (ctx->h, LOG_INFO, "starting initial program");

    subprocess_run (ctx->init_shell);
}

static int create_dummyattrs (ctx_t *ctx)
{
    char *s;
    s = xasprintf ("%u", ctx->rank);
    if (flux_attr_fake (ctx->h, "rank", s, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    free (s);

    s = xasprintf ("%u", ctx->size);
    if (flux_attr_fake (ctx->h, "size", s, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    free (s);

    return 0;
}

/* The 'ranktmp' dir will contain the broker.pid file and local:// socket.
 * It will be created in ctx->socket_dir.
 */
static int create_rankdir (ctx_t *ctx)
{
    char *ranktmp = NULL;
    char *uri = NULL;
    int rc = -1;

    if (ctx->rank == FLUX_NODEID_ANY || ctx->socket_dir == NULL) {
        errno = EINVAL;
        goto done;
    }
    if (attr_get (ctx->attrs, "local-uri", NULL, NULL) < 0) {
        ranktmp = xasprintf ("%s/%d", ctx->socket_dir, ctx->rank);
        if (mkdir (ranktmp, 0700) < 0)
            goto done;
        cleanup_push_string (cleanup_directory, ranktmp);

        uri = xasprintf ("local://%s", ranktmp);
        if (attr_add (ctx->attrs, "local-uri", uri,
                                            FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }
    rc = 0;
done:
    if (ranktmp)
        free (ranktmp);
    if (uri)
        free (uri);
    return rc;
}

static int create_socketdir (ctx_t *ctx)
{
    const char *sid;

    if (attr_get (ctx->attrs, "session-id", &sid, NULL) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (!ctx->socket_dir) {
        char *tmpdir = getenv ("TMPDIR");
        char *template = xasprintf ("%s/flux-%s-XXXXXX",
                                    tmpdir ? tmpdir : "/tmp", sid);

        if (!(ctx->socket_dir = mkdtemp (template)))
            return -1;
        cleanup_push_string (cleanup_directory, ctx->socket_dir);
    }
    return 0;
}

/* N.B. If using epgm with multiple brokers per node, the
 * lowest rank in each clique will subscribe to the epgm:// socket
 * and relay events to an ipc:// socket for the other ranks in the
 * clique.  This is required due to a limitation of epgm.
 */
static int boot_pmi (ctx_t *ctx)
{
    pmi_t *pmi = NULL;
    int spawned, size, rank, appnum;
    int relay_rank = -1, right_rank, parent_rank;
    int clique_size;
    int *clique_ranks = NULL;
    char ipaddr[HOST_NAME_MAX + 1];
    const char *child_uri, *relay_uri;
    int id_len, kvsname_len, key_len, val_len;
    char *id = NULL;
    char *kvsname = NULL;
    char *key = NULL;
    char *val = NULL;

    int e, rc = -1;

    if (!(pmi = pmi_create_guess ())) {
        err ("pmi_create");
        goto done;
    }
    if ((e = pmi_init (pmi, &spawned)) != PMI_SUCCESS) {
        msg ("pmi_init: %s", pmi_strerror (e));
        goto done;
    }

    /* Get rank, size, appnum
     */
    if ((e = pmi_get_size (pmi, &size)) != PMI_SUCCESS) {
        msg ("pmi_get_size: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = pmi_get_rank (pmi, &rank)) != PMI_SUCCESS) {
        msg ("pmi_get_rank: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = pmi_get_appnum (pmi, &appnum)) != PMI_SUCCESS) {
        msg ("pmi_get_appnum: %s", pmi_strerror (e));
        goto done;
    }
    ctx->rank = rank;
    ctx->size = size;
    overlay_set_rank (ctx->overlay, ctx->rank);

    /* Get id string.
     */
    e = pmi_get_id_length_max (pmi, &id_len);
    if (e == PMI_SUCCESS) {
        id = xzmalloc (id_len);
        if ((e = pmi_get_id (pmi, id, id_len)) != PMI_SUCCESS) {
            msg ("pmi_get_rank: %s", pmi_strerror (e));
            goto done;
        }
    } else { /* No pmi_get_id() available? */
        id = xasprintf ("simple-%d", appnum);
    }
    if (attr_add (ctx->attrs, "session-id", id, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        goto done;


    /* Set ip addr.  We will need wildcards expanded below.
     */
    ipaddr_getprimary (ipaddr, sizeof (ipaddr));
    overlay_set_child (ctx->overlay, "tcp://%s:*", ipaddr);

    /* Set up epgm relay if multiple ranks are being spawned per node,
     * as indicated by "clique ranks".  FIXME: if epgm is used but
     * pmi_get_clique_ranks() is not implemented, this fails.  Find an
     * alternate method to determine if ranks are co-located on a node.
     */
    if (ctx->enable_epgm) {
        if ((e = pmi_get_clique_size (pmi, &clique_size)) != PMI_SUCCESS) {
            msg ("pmi_get_clique_size: %s", pmi_strerror (e));
            goto done;
        }
        clique_ranks = xzmalloc (sizeof (int) * clique_size);
        if ((e = pmi_get_clique_ranks (pmi, clique_ranks, clique_size))
                                                          != PMI_SUCCESS) {
            msg ("pmi_get_clique_ranks: %s", pmi_strerror (e));
            goto done;
        }
        if (clique_size > 1) {
            int i;
            for (i = 0; i < clique_size; i++)
                if (relay_rank == -1 || clique_ranks[i] < relay_rank)
                    relay_rank = clique_ranks[i];
            if (relay_rank >= 0 && ctx->rank == relay_rank)
                overlay_set_relay (ctx->overlay, "ipc://*");
        }
    }

    /* Prepare for PMI KVS operations by grabbing the kvsname,
     * and buffers for keys and values.
     */
    if ((e = pmi_kvs_get_name_length_max (pmi, &kvsname_len)) != PMI_SUCCESS) {
        msg ("pmi_kvs_get_name_length_max: %s", pmi_strerror (e));
        goto done;
    }
    kvsname = xzmalloc (kvsname_len);
    if ((e = pmi_kvs_get_my_name (pmi, kvsname, kvsname_len)) != PMI_SUCCESS) {
        msg ("pmi_kvs_get_my_name: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = pmi_kvs_get_key_length_max (pmi, &key_len)) != PMI_SUCCESS) {
        msg ("pmi_kvs_get_key_length_max: %s", pmi_strerror (e));
        goto done;
    }
    key = xzmalloc (key_len);
    if ((e = pmi_kvs_get_value_length_max (pmi, &val_len)) != PMI_SUCCESS) {
        msg ("pmi_kvs_get_value_length_max: %s", pmi_strerror (e));
        goto done;
    }
    val = xzmalloc (val_len);

    /* Bind to addresses to expand URI wildcards, so we can exchange
     * the real addresses.
     */
    if (overlay_bind (ctx->overlay) < 0) {
        err ("overlay_bind failed");   /* function is idempotent */
        goto done;
    }

    /* Write the URI of downstream facing socket under the rank (if any).
     */
    if ((child_uri = overlay_get_child (ctx->overlay))) {
        if (snprintf (key, key_len, "cmbd.%d.uri", rank) >= key_len) {
            msg ("pmi key string overflow");
            goto done;
        }
        if (snprintf (val, val_len, "%s", child_uri) >= val_len) {
            msg ("pmi val string overflow");
            goto done;
        }
        if ((e = pmi_kvs_put (pmi, kvsname, key, val)) != PMI_SUCCESS) {
            msg ("pmi_kvs_put: %s", pmi_strerror (e));
            goto done;
        }
    }

    /* Write the uri of the epgm relay under the rank (if any).
     */
    if (ctx->enable_epgm && (relay_uri = overlay_get_relay (ctx->overlay))) {
        if (snprintf (key, key_len, "cmbd.%d.relay", rank) >= key_len) {
            msg ("pmi key string overflow");
            goto done;
        }
        if (snprintf (val, val_len, "%s", relay_uri) >= val_len) {
            msg ("pmi val string overflow");
            goto done;
        }
        if ((e = pmi_kvs_put (pmi, kvsname, key, val)) != PMI_SUCCESS) {
            msg ("pmi_kvs_put: %s", pmi_strerror (e));
            goto done;
        }
    }

    /* Puts are complete, now we synchronize and begin our gets.
     */
    if ((e = pmi_kvs_commit (pmi, kvsname)) != PMI_SUCCESS) {
        msg ("pmi_kvs_commit: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = pmi_barrier (pmi)) != PMI_SUCCESS) {
        msg ("pmi_barrier: %s", pmi_strerror (e));
        goto done;
    }

    /* Read the uri of our parent, after computing its rank
     */
    if (ctx->rank > 0) {
        parent_rank = ctx->k_ary == 0 ? 0 : (ctx->rank - 1) / ctx->k_ary;
        if (snprintf (key, key_len, "cmbd.%d.uri", parent_rank) >= key_len) {
            msg ("pmi key string overflow");
            goto done;
        }
        if ((e = pmi_kvs_get (pmi, kvsname, key, val, val_len))
                                                            != PMI_SUCCESS) {
            msg ("pmi_kvs_get: %s", pmi_strerror (e));
            goto done;
        }
        overlay_push_parent (ctx->overlay, "%s", val);
    }

    /* Read the uri of our neigbor, after computing its rank.
     */
    right_rank = rank == 0 ? size - 1 : rank - 1;
    if (snprintf (key, key_len, "cmbd.%d.uri", right_rank) >= key_len) {
        msg ("pmi key string overflow");
        goto done;
    }
    if ((e = pmi_kvs_get (pmi, kvsname, key, val, val_len)) != PMI_SUCCESS) {
        msg ("pmi_kvs_get: %s", pmi_strerror (e));
        goto done;
    }
    overlay_set_right (ctx->overlay, "%s", val);

    /* Read the URI fo epgm relay rank, or connect directly.
     */
    if (ctx->enable_epgm) {
        if (relay_rank >= 0 && rank != relay_rank) {
            if (snprintf (key, key_len, "cmbd.%d.relay", relay_rank)
                                                                >= key_len) {
                msg ("pmi key string overflow");
                goto done;
            }
            if ((e = pmi_kvs_get (pmi, kvsname, key, val, val_len))
                                                            != PMI_SUCCESS) {
                msg ("pmi_kvs_get: %s", pmi_strerror (e));
                goto done;
            }
            overlay_set_event (ctx->overlay, "%s", val);
        } else {
            int port = 5000 + appnum % 1024;
            overlay_set_event (ctx->overlay, "epgm://%s;239.192.1.1:%d",
                               ipaddr, port);
        }
    }
    pmi_finalize (pmi);
    rc = 0;
done:
    if (id)
        free (id);
    if (clique_ranks)
        free (clique_ranks);
    if (kvsname)
        free (kvsname);
    if (key)
        free (key);
    if (val)
        free (val);
    if (pmi)
        pmi_destroy (pmi);
    if (rc != 0)
        errno = EPROTO;
    return rc;
}

/* This is the boot method selected by flux-start.
 * We should have been called with --rank, --size, and --sid.
 */
static int boot_local (ctx_t *ctx)
{
    if (ctx->rank == FLUX_NODEID_ANY || ctx->size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (create_socketdir (ctx) < 0 || create_rankdir (ctx) < 0)
        return -1;

    char *reqfile = xasprintf ("%s/%d/req", ctx->socket_dir, ctx->rank);
    overlay_set_child (ctx->overlay, "ipc://%s", reqfile);
    cleanup_push_string (cleanup_file, reqfile);
    free (reqfile);

    if (ctx->rank > 0) {
        int parent_rank = ctx->k_ary == 0 ? 0 : (ctx->rank - 1) / ctx->k_ary;
        overlay_push_parent (ctx->overlay, "ipc://%s/%d/req",
                             ctx->socket_dir, parent_rank);
    }

    char *eventfile = xasprintf ("%s/event", ctx->socket_dir);
    overlay_set_event (ctx->overlay, "ipc://%s", eventfile);
    if (ctx->rank == 0)
        cleanup_push_string (cleanup_file, eventfile);
    free (eventfile);

    int right_rank = ctx->rank == 0 ? ctx->size - 1 : ctx->rank - 1;
    overlay_set_right (ctx->overlay, "ipc://%s/%d/req",
                       ctx->socket_dir, right_rank);

    return 0;
}

static int boot_single (ctx_t *ctx)
{
    int rc = -1;
    char *sid = xasprintf ("%d", getpid ());

    ctx->rank = 0;
    ctx->size = 1;

    if (attr_add (ctx->attrs, "session-id", sid,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
        goto done;
    rc = 0;
done:
    free (sid);
    return rc;
}

static bool nodeset_suffix_member (char *name, uint32_t rank)
{
    char *s;
    nodeset_t *ns;
    bool member = true;

    if ((s = strchr (name, '['))) {
        if (!(ns = nodeset_create_string (s)))
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
        if (!(p = module_add (ctx->modhash, path)))
            err_exit ("%s: module_add %s", name, path);
        if (!svc_add (ctx->services, module_get_name (p), mod_svc_cb, p))
            msg_exit ("could not register service %s", module_get_name (p));
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

static void broker_block_signals (void)
{
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigfillset(&sigmask);
    if (sigprocmask (SIG_SETMASK, &sigmask, NULL) < 0)
        err_exit ("sigprocmask");
}

static void broker_handle_signals (ctx_t *ctx, zlist_t *sigwatchers)
{
    int i, sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGSEGV, SIGFPE };
    flux_watcher_t *w;

    for (i = 0; i < sizeof (sigs) / sizeof (sigs[0]); i++) {
        w = flux_signal_watcher_create (ctx->reactor, sigs[i], signal_cb, ctx);
        if (!w)
            err_exit ("flux_signal_watcher_create");
        if (zlist_push (sigwatchers, w) < 0)
            oom ();
        flux_watcher_start (w);
    }
}

static void broker_unhandle_signals (zlist_t *sigwatchers)
{
    flux_watcher_t *w;

    while ((w = zlist_pop (sigwatchers))) {
        flux_watcher_stop (w);
        flux_watcher_destroy (w);
    }
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

    flux_respond (ctx->h, zmsg, 0, Jtostr (resp));
    zmsg_destroy (&zmsg);
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
    const char *json_str;
    int pid;
    int errnum = EPROTO;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto out;

    if ((request = Jfromstr (json_str)) && Jget_int (request, "pid", &pid) &&
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
    flux_respond (ctx->h, *zmsg, 0, Jtostr (response));
    zmsg_destroy (zmsg);
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
    const char *json_str;
    int pid;
    int errnum = EPROTO;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto out;
    if ((request = Jfromstr (json_str)) && Jget_int (request, "pid", &pid)) {
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
    flux_respond (ctx->h, *zmsg, 0, Jtostr (response));
    zmsg_destroy (zmsg);
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
    const char *json_str;
    struct subprocess *p;
    zmsg_t *copy;
    int i, argc;
    const char *local_uri;

    if (attr_get (ctx->attrs, "local-uri", &local_uri, NULL) < 0)
        err_exit ("%s: local_uri is not set", __FUNCTION__);

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto out_free;

    if (!(request = Jfromstr (json_str))
        || !json_object_object_get_ex (request, "cmdline", &o)
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
    subprocess_setenv (p, "FLUX_URI", local_uri, 1);

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
        flux_respond (ctx->h, *zmsg, 0, Jtostr (response));
        zmsg_destroy (zmsg);
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
    rc = flux_respond (ctx->h, *zmsg, 0, Jtostr (out));
    zmsg_destroy (zmsg);
    Jput (out);
    return (rc);
}

static int attr_get_snoop (const char *name, const char **val, void *arg)
{
    snoop_t *snoop = arg;
    *val = snoop_get_uri (snoop);
    return 0;
}

static int attr_get_log (const char *name, const char **val, void *arg)
{
    log_t *log = arg;
    static char s[32];
    int n, rc = -1;

    if (!strcmp (name, "log-level")) {
        n = snprintf (s, sizeof (s), "%d", log_get_level (log));
        assert (n < sizeof (s));
    } else if (!strcmp (name, "log-buflimit")) {
        n = snprintf (s, sizeof (s), "%d", log_get_buflimit (log));
        assert (n < sizeof (s));
    } else if (!strcmp (name, "log-bufcount")) {
        n = snprintf (s, sizeof (s), "%d", log_get_bufcount (log));
        assert (n < sizeof (s));
    } else if (!strcmp (name, "log-count")) {
        n = snprintf (s, sizeof (s), "%d", log_get_count (log));
        assert (n < sizeof (s));
    } else {
        errno = ENOENT;
        goto done;
    }
    *val = s;
    rc = 0;
done:
    return rc;
}

static int attr_set_log (const char *name, const char *val, void *arg)
{
    log_t *log = arg;
    int rc = -1;

    if (!strcmp (name, "log-level")) {
        int level = strtol (val, NULL, 10);
        if (log_set_level (log, level) < 0)
            goto done;
    } else if (!strcmp (name, "log-buflimit")) {
        int limit = strtol (val, NULL, 10);
        if (log_set_buflimit (log, limit) < 0)
            goto done;
    } else {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static int attr_get_overlay (const char *name, const char **val, void *arg)
{
    overlay_t *overlay = arg;
    int rc = -1;

    if (!strcmp (name, "tbon-parent-uri"))
        *val = overlay_get_parent (overlay);
    else if (!strcmp (name, "tbon-request-uri"))
        *val = overlay_get_child (overlay);
    else if (!strcmp (name, "event-uri"))
        *val = overlay_get_event (overlay);
    else if (!strcmp (name, "event-relay-uri"))
        *val = overlay_get_relay (overlay);
    else {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static int cmb_attrget_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str, *name, *val;
    int flags;
    JSON in = NULL;
    JSON out = Jnew ();
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "name", &name)) {
        errno = EPROTO;
        goto done;
    }
    if (attr_get (ctx->attrs, name, &val, &flags) < 0)
        goto done;
    if (!val) {
        errno = ENOENT;
        goto done;
    }
    Jadd_str (out, "value", val);
    Jadd_int (out, "flags", flags);
    rc = 0;
done:
    rc = flux_respond (ctx->h, *zmsg, rc < 0 ? errno : 0,
                                      rc < 0 ? NULL : Jtostr (out));
    zmsg_destroy (zmsg);
    Jput (out);
    Jput (in);
    return rc;
}

static int cmb_attrset_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str, *name, *val = NULL;
    JSON in = NULL;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "name", &name)) {
        errno = EPROTO;
        goto done;
    }
    (void)Jget_str (in, "value", &val); /* may be NULL for unset */
    if (val) {
        if (attr_set (ctx->attrs, name, val, false) < 0) {
            if (errno != ENOENT)
                goto done;
            if (attr_add (ctx->attrs, name, val, 0) < 0)
                goto done;
        }
    } else {
        if (attr_delete (ctx->attrs, name, false) < 0)
            goto done;
    }
    rc = 0;
done:
    rc = flux_respond (ctx->h, *zmsg, rc < 0 ? errno : 0, NULL);
    zmsg_destroy (zmsg);
    Jput (in);
    return rc;
}

static int cmb_attrlist_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *name;
    JSON out = Jnew ();
    JSON array = Jnew_ar ();
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, NULL) < 0)
        goto done;
    name = attr_first (ctx->attrs);
    while (name) {
        Jadd_ar_str (array, name);
        name = attr_next (ctx->attrs);
    }
    Jadd_obj (out, "names", array);
    rc = 0;
done:
    rc = flux_respond (ctx->h, *zmsg, rc < 0 ? errno : 0,
                                      rc < 0 ? NULL : Jtostr (out));
    zmsg_destroy (zmsg);
    Jput (out);
    Jput (array);
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
    rc = flux_respond (ctx->h, *zmsg, 0, Jtostr (out));
    zmsg_destroy (zmsg);
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
    if (flux_respond (ctx->h, *zmsg, 0, Jtostr (out)) < 0)
        goto done;
    rc = 0;
done:
    zmsg_destroy (zmsg);
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
    int rc = flux_respond (ctx->h, *zmsg, 0, Jtostr (out));
    zmsg_destroy (zmsg);
    Jput (out);
    return rc;
}

static int cmb_ping_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON inout = NULL;
    const char *json_str;
    char *s = NULL;
    char *route = NULL;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(inout = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (!(s = flux_msg_get_route_string (*zmsg)))
        goto done;
    route = xasprintf ("%s!%u", s, ctx->rank);
    Jadd_str (inout, "route", route);
    rc = flux_respond (ctx->h, *zmsg, 0, Jtostr (inout));
    zmsg_destroy (zmsg);
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
    const char *json_str;
    bool recycled = false;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "uri", &uri)) {
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
    const char *json_str;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "msg", &s))
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
    if (log_append_json (ctx->log, json_str) < 0)
        goto done;
done:
    zmsg_destroy (zmsg); /* no reply */
    return 0;
}

static int cmb_dmesg_clear_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    int seq;
    JSON in = NULL;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_int (in, "seq", &seq)) {
        errno = EPROTO;
        goto done;
    }
    log_buf_clear (ctx->log, seq);
    rc = 0;
done:
    flux_respond (ctx->h, *zmsg, rc < 0 ? errno : 0, NULL);
    zmsg_destroy (zmsg);
    Jput (in);
    return 0;
}

static void cmb_dmesg (const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    int seq;
    JSON in = NULL;
    JSON out = NULL;
    bool follow;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_int (in, "seq", &seq)
                                    || !Jget_bool (in, "follow", &follow)) {
        errno = EPROTO;
        goto done;
    }
    if (!(json_str = log_buf_get (ctx->log, seq, &seq))) {
        if (follow && errno == ENOENT) {
            flux_msg_t *cpy = flux_msg_copy (msg, true);
            if (!cpy)
                goto done;
            if (log_buf_sleepon (ctx->log, cmb_dmesg, cpy, arg) < 0) {
                free (cpy);
                goto done;
            }
            goto done_noreply;
        }
        goto done;
    }
    if (!(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    Jadd_int (out, "seq", seq);
    rc = 0;
done:
    flux_respond (ctx->h, msg, rc < 0 ? errno : 0,
                               rc < 0 ? NULL : Jtostr (out));
done_noreply:
    Jput (in);
    Jput (out);
}

static int cmb_dmesg_cb (zmsg_t **zmsg, void *arg)
{
    cmb_dmesg (*zmsg, arg);
    zmsg_destroy (zmsg);
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
    ctx_t *ctx = arg;
    char *sender = NULL;;

    if (flux_msg_get_route_first (*zmsg, &sender) < 0)
        goto done;

    terminate_subprocesses_by_uuid (ctx, sender);
    log_buf_disconnect (ctx->log, sender);
done:
    if (sender)
        free (sender);
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
    if (!svc_add (ctx->services, "cmb.attrget", cmb_attrget_cb, ctx)
          || !svc_add (ctx->services, "cmb.attrset", cmb_attrset_cb, ctx)
          || !svc_add (ctx->services, "cmb.attrlist", cmb_attrlist_cb, ctx)
          || !svc_add (ctx->services, "cmb.rusage", cmb_rusage_cb, ctx)
          || !svc_add (ctx->services, "cmb.rmmod", cmb_rmmod_cb, ctx)
          || !svc_add (ctx->services, "cmb.insmod", cmb_insmod_cb, ctx)
          || !svc_add (ctx->services, "cmb.lsmod", cmb_lsmod_cb, ctx)
          || !svc_add (ctx->services, "cmb.lspeer", cmb_lspeer_cb, ctx)
          || !svc_add (ctx->services, "cmb.ping", cmb_ping_cb, ctx)
          || !svc_add (ctx->services, "cmb.reparent", cmb_reparent_cb, ctx)
          || !svc_add (ctx->services, "cmb.panic", cmb_panic_cb, ctx)
          || !svc_add (ctx->services, "cmb.log", cmb_log_cb, ctx)
          || !svc_add (ctx->services, "cmb.dmesg.clear", cmb_dmesg_clear_cb,ctx)
          || !svc_add (ctx->services, "cmb.dmesg", cmb_dmesg_cb, ctx)
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
                flux_respond (ctx->h, zmsg, rc < 0 ? errno : 0, NULL);
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
                flux_respond (ctx->h, zmsg, rc < 0 ? errno : 0, NULL);
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
        if (flux_respond (ctx->h, zmsg, 0, NULL) < 0)
            err ("%s: flux_respond", __FUNCTION__);
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

static void signal_cb (flux_reactor_t *r, flux_watcher_t *w,
                         int revents, void *arg)
{
    ctx_t *ctx = arg;
    int signum = flux_signal_watcher_get_signum (w);

    shutdown_arm (ctx->shutdown, ctx->shutdown_grace, 0,
                  "signal %d (%s) %d", signum, strsignal (signum));
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
