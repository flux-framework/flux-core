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
#include <czmq.h>
#if WITH_TCMALLOC
#if HAVE_GPERFTOOLS_HEAP_PROFILER_H
  #include <gperftools/heap-profiler.h>
#elif HAVE_GOOGLE_HEAP_PROFILER_H
  #include <google/heap-profiler.h>
#else
  #error gperftools headers not configured
#endif
#endif /* WITH_TCMALLOC */
#if HAVE_CALIPER
#include <caliper/cali.h>
#include <sys/syscall.h>
#endif

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/getrusage_json.h"
#include "src/common/libutil/kary.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "src/common/libsubprocess/zio.h"
#include "src/common/libsubprocess/subprocess.h"

#include "heartbeat.h"
#include "module.h"
#include "overlay.h"
#include "snoop.h"
#include "service.h"
#include "hello.h"
#include "shutdown.h"
#include "attr.h"
#include "sequence.h"
#include "log.h"
#include "content-cache.h"
#include "runlevel.h"

#ifndef ZMQ_IMMEDIATE
#define ZMQ_IMMEDIATE           ZMQ_DELAY_ATTACH_ON_CONNECT
#define zsocket_set_immediate   zsocket_set_delay_attach_on_connect
#endif

const char *default_modules = "connector-local";

struct tbon_param {
    int k;
    int level;
    int maxlevel;
    int descendants;
};

typedef struct {
    /* 0MQ
     */
    zctx_t *zctx;               /* zeromq context (MT-safe) */
    flux_sec_t *sec;             /* security context (MT-safe) */

    /* Reactor
     */
    flux_t *h;
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
    int event_recv_seq;
    int event_send_seq;
    bool event_active;          /* primary event source is active */
    svchash_t *services;
    seqhash_t *seq;
    heartbeat_t *heartbeat;
    shutdown_t *shutdown;
    double shutdown_grace;
    logbuf_t *logbuf;
    zlist_t *subscriptions;     /* subscripts for internal services */
    content_cache_t *cache;
    struct tbon_param tbon;
    /* Bootstrap
     */
    bool enable_epgm;
    bool shared_ipc_namespace;
    hello_t *hello;
    flux_t *enclosing_h;
    runlevel_t *runlevel;

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
static void module_cb (module_t *p, void *arg);
static void module_status_cb (module_t *p, int prev_state, void *arg);
static void hello_update_cb (hello_t *h, void *arg);
static void shutdown_cb (shutdown_t *s, bool expired, void *arg);
static void signal_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg);
static void broker_block_signals (void);
static void broker_handle_signals (ctx_t *ctx, zlist_t *sigwatchers);
static void broker_unhandle_signals (zlist_t *sigwatchers);

static void broker_add_services (ctx_t *ctx);

static void load_modules (ctx_t *ctx, const char *default_modules);

static void update_proctitle (ctx_t *ctx);
static void update_pidfile (ctx_t *ctx);
static void runlevel_cb (runlevel_t *r, int level, int rc, double elapsed,
                         const char *state, void *arg);
static void runlevel_io_cb (runlevel_t *r, const char *name,
                            const char *msg, void *arg);

static int create_persistdir (ctx_t *ctx);
static int create_scratchdir (ctx_t *ctx);
static int create_rankdir (ctx_t *ctx);
static int create_dummyattrs (ctx_t *ctx);

static int boot_pmi (ctx_t *ctx, double *elapsed_sec);

static int attr_get_snoop (const char *name, const char **val, void *arg);
static int attr_get_overlay (const char *name, const char **val, void *arg);

static void init_attrs_from_environment (attr_t *attrs);

static const struct flux_handle_ops broker_handle_ops;

#define OPTIONS "+vqM:X:k:s:g:EIS:"
static const struct option longopts[] = {
    {"verbose",         no_argument,        0, 'v'},
    {"quiet",           no_argument,        0, 'q'},
    {"security",        required_argument,  0, 's'},
    {"module-path",     required_argument,  0, 'X'},
    {"k-ary",           required_argument,  0, 'k'},
    {"heartrate",       required_argument,  0, 'H'},
    {"shutdown-grace",  required_argument,  0, 'g'},
    {"enable-epgm",     no_argument,        0, 'E'},
    {"shared-ipc-namespace", no_argument,   0, 'I'},
    {"setattr",         required_argument,  0, 'S'},
    {0, 0, 0, 0},
};

static void usage (void)
{
    fprintf (stderr,
"Usage: flux-broker OPTIONS [initial-command ...]\n"
" -v,--verbose                 Be annoyingly verbose\n"
" -q,--quiet                   Be mysteriously taciturn\n"
" -X,--module-path PATH        Set module search path (colon separated)\n"
" -s,--security=plain|curve|none    Select security mode (default: curve)\n"
" -k,--k-ary K                 Wire up in a k-ary tree\n"
" -H,--heartrate SECS          Set heartrate in seconds (rank 0 only)\n"
" -g,--shutdown-grace SECS     Set shutdown grace period in seconds\n"
" -E,--enable-epgm             Enable EPGM for events (PMI bootstrap)\n"
" -I,--shared-ipc-namespace    Wire up session TBON over ipc sockets\n"
" -S,--setattr ATTR=VAL        Set broker attribute\n"
);
    exit (1);
}

static int setup_profiling (const char *program, int rank)
{
#if HAVE_CALIPER
    cali_begin_string_byname ("flux.type", "main");
    cali_begin_int_byname ("flux.tid", syscall (SYS_gettid));
    cali_begin_string_byname ("binary", program);
    cali_begin_int_byname ("flux.rank", rank);
    // TODO: this is a stopgap until we have better control over
    // instrumemtation in child processes. If we want to see what children
    // that load libflux are up to, this should be disabled
    unsetenv ("CALI_SERVICES_ENABLE");
    unsetenv ("CALI_CONFIG_PROFILE");
#endif
    return (0);
}


int main (int argc, char *argv[])
{
    int c;
    ctx_t ctx;
    zlist_t *sigwatchers;
    int security_clr = 0;
    int security_set = 0;
    int e;


    memset (&ctx, 0, sizeof (ctx));
    log_init (argv[0]);

    if (!(sigwatchers = zlist_new ()))
        oom ();

    ctx.rank = FLUX_NODEID_ANY;
    ctx.modhash = modhash_create ();
    ctx.services = svchash_create ();
    if (!(ctx.seq = sequence_hash_create ()))
        oom ();
    ctx.overlay = overlay_create ();
    ctx.snoop = snoop_create ();
    ctx.hello = hello_create ();
    ctx.tbon.k = 2; /* binary TBON is default */
    ctx.heartbeat = heartbeat_create ();
    ctx.shutdown = shutdown_create ();
    ctx.attrs = attr_create ();
    ctx.logbuf = logbuf_create ();
    if (!(ctx.subscriptions = zlist_new ()))
        oom ();
    if (!(ctx.cache = content_cache_create ()))
        oom ();
    if (!(ctx.runlevel = runlevel_create ()))
        oom ();

    ctx.pid = getpid();

    /* Initialize config attrs from environment set up by flux(1)
     */
    init_attrs_from_environment (ctx.attrs);

    if (!(ctx.sm = subprocess_manager_create ()))
        oom ();
    subprocess_manager_set (ctx.sm, SM_WAIT_FLAGS, WNOHANG);

    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 's':   /* --security=MODE */
                if (!strcmp (optarg, "none"))
                    security_clr = FLUX_SEC_TYPE_ALL;
                else if (!strcmp (optarg, "plain"))
                    security_set |= FLUX_SEC_TYPE_PLAIN;
                else if (!strcmp (optarg, "curve"))
                    security_set |= FLUX_SEC_TYPE_CURVE;
                else
                    log_msg_exit ("--security argument must be none|plain|curve");
                break;
            case 'v':   /* --verbose */
                ctx.verbose = true;
                break;
            case 'q':   /* --quiet */
                ctx.quiet = true;
                break;
            case 'X':   /* --module-path PATH */
                if (attr_set (ctx.attrs, "conf.module_path", optarg, true) < 0)
                    log_err_exit ("setting conf.module_path attribute");
                break;
            case 'k':   /* --k-ary k */
                ctx.tbon.k = strtoul (optarg, NULL, 10);
                if (ctx.tbon.k < 1)
                    usage ();
                break;
            case 'H':   /* --heartrate SECS */
                if (heartbeat_set_ratestr (ctx.heartbeat, optarg) < 0)
                    log_err_exit ("heartrate `%s'", optarg);
                break;
            case 'g':   /* --shutdown-grace SECS */
                ctx.shutdown_grace = strtod (optarg, NULL);
                break;
            case 'E': /* --enable-epgm */
                ctx.enable_epgm = true;
                break;
            case 'I': /* --shared-ipc-namespace */
                ctx.shared_ipc_namespace = true;
                break;
            case 'S': { /* --setattr ATTR=VAL */
                char *val, *attr = xstrdup (optarg);
                if ((val = strchr (attr, '=')))
                    *val++ = '\0';
                if (attr_add (ctx.attrs, attr, val, 0) < 0)
                    if (attr_set (ctx.attrs, attr, val, true) < 0)
                        log_err_exit ("setattr %s=%s", attr, val);
                free (attr);
                break;
            }
            default:
                usage ();
        }
    }
    if (optind < argc) {
        size_t len = 0;
        if ((e = argz_create (argv + optind, &ctx.init_shell_cmd, &len)) != 0)
            log_errn_exit (e, "argz_create");
        argz_stringify (ctx.init_shell_cmd, len, ' ');
    }


    /* Connect to enclosing instance, if any.
     */
    if (getenv ("FLUX_URI")) {
        if (!(ctx.enclosing_h = flux_open (NULL, 0)))
            log_err_exit ("flux_open enclosing instance");
    }

    broker_block_signals ();

    /* Initailize zeromq context
     */
    zsys_handler_set (NULL);
    ctx.zctx = zctx_new ();
    if (!ctx.zctx)
        log_err_exit ("zctx_new");
    zctx_set_linger (ctx.zctx, 5);

    /* Set up the flux reactor.
     */
    if (!(ctx.reactor = flux_reactor_create (SIGCHLD)))
        log_err_exit ("flux_reactor_create");

    /* Set up flux handle.
     * The handle is used for simple purposes such as logging.
     */
    if (!(ctx.h = flux_handle_create (&ctx, &broker_handle_ops, 0)))
        log_err_exit ("flux_handle_create");
    if (flux_set_reactor (ctx.h, ctx.reactor) < 0)
        log_err_exit ("flux_set_reactor");

    subprocess_manager_set (ctx.sm, SM_REACTOR, ctx.reactor);

    /* Prepare signal handling
     */
    broker_handle_signals (&ctx, sigwatchers);

    /* Initialize security context.
     */
    if (!(ctx.sec = flux_sec_create ()))
        log_err_exit ("flux_sec_create");
    const char *keydir;
    if (attr_get (ctx.attrs, "security.keydir", &keydir, NULL) < 0)
        log_err_exit ("getattr security.keydir");
    flux_sec_set_directory (ctx.sec, keydir);
    if (security_clr && flux_sec_disable (ctx.sec, security_clr) < 0)
        log_err_exit ("flux_sec_disable");
    if (security_set && flux_sec_enable (ctx.sec, security_set) < 0)
        log_err_exit ("flux_sec_enable");
    if (flux_sec_zauth_init (ctx.sec, ctx.zctx, "flux") < 0)
        log_msg_exit ("flux_sec_zauth_init: %s", flux_sec_errstr (ctx.sec));
    if (flux_sec_munge_init (ctx.sec) < 0)
        log_msg_exit ("flux_sec_munge_init: %s", flux_sec_errstr (ctx.sec));

    overlay_set_zctx (ctx.overlay, ctx.zctx);
    overlay_set_sec (ctx.overlay, ctx.sec);
    overlay_set_flux (ctx.overlay, ctx.h);

    overlay_set_parent_cb (ctx.overlay, parent_cb, &ctx);
    overlay_set_child_cb (ctx.overlay, child_cb, &ctx);
    overlay_set_event_cb (ctx.overlay, event_cb, &ctx);

    /* Boot with PMI.
     */
    double pmi_elapsed_sec;
    if (boot_pmi (&ctx, &pmi_elapsed_sec) < 0)
        log_msg_exit ("bootstrap failed");

    assert (ctx.rank != FLUX_NODEID_ANY);
    assert (ctx.size > 0);
    assert (attr_get (ctx.attrs, "session-id", NULL, NULL) == 0);

    ctx.tbon.level = kary_levelof (ctx.tbon.k, ctx.rank);
    ctx.tbon.maxlevel = kary_levelof (ctx.tbon.k, ctx.size - 1);
    ctx.tbon.descendants = kary_sum_descendants (ctx.tbon.k, ctx.size, ctx.rank);

    if (ctx.verbose) {
        const char *sid = "unknown";
        (void)attr_get (ctx.attrs, "session-id", &sid, NULL);
        log_msg ("boot: rank=%d size=%d session-id=%s", ctx.rank, ctx.size, sid);
    }

    if (attr_set_flags (ctx.attrs, "session-id", FLUX_ATTRFLAG_IMMUTABLE) < 0)
        log_err_exit ("attr_set_flags session-id");

    // Setup profiling
    setup_profiling (argv[0], ctx.rank);

    /* Create directory for sockets, and a subdirectory specific
     * to this rank that will contain the pidfile and local connector socket.
     * (These may have already been called by boot method)
     * If persist-filesystem or persist-directory are set, initialize those,
     * but only on rank 0.
     */
    if (create_scratchdir (&ctx) < 0)
        log_err_exit ("create_scratchdir");
    if (create_rankdir (&ctx) < 0)
        log_err_exit ("create_rankdir");
    if (create_persistdir (&ctx) < 0)
        log_err_exit ("create_persistdir");

    /* Initialize logging.
     * First establish a redirect callback that forces all logs
     * to ctx.h to go to our local log class rather than be sent
     * as a "cmb.log" request message.  Next, provide the handle
     * and rank to the log class.
     */
    flux_log_set_appname (ctx.h, "broker");
    flux_log_set_redirect (ctx.h, logbuf_append_redirect, ctx.logbuf);
    logbuf_set_flux (ctx.logbuf, ctx.h);
    logbuf_set_rank (ctx.logbuf, ctx.rank);

    /* Dummy up cached attributes on the broker's handle so logging's
     * use of flux_get_rank() etc will work despite limitations.
     */
    if (create_dummyattrs (&ctx) < 0)
        log_err_exit ("creating dummy attributes");

    overlay_set_rank (ctx.overlay, ctx.rank);

    /* Registers message handlers and obtains rank.
     */
    if (content_cache_set_flux (ctx.cache, ctx.h) < 0)
        log_err_exit ("content_cache_set_flux");

    content_cache_set_enclosing_flux (ctx.cache, ctx.enclosing_h);

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
            || attr_add_active_uint32 (ctx.attrs, "rank", &ctx.rank,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0
            || attr_add_active_uint32 (ctx.attrs, "size", &ctx.size,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0
            || attr_add_active_int (ctx.attrs, "tbon.arity", &ctx.tbon.k,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0
            || attr_add_active_int (ctx.attrs, "tbon.level", &ctx.tbon.level,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0
            || attr_add_active_int (ctx.attrs, "tbon.maxlevel",
                                &ctx.tbon.maxlevel,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0
            || attr_add_active_int (ctx.attrs, "tbon.descendants",
                                &ctx.tbon.descendants,
                                FLUX_ATTRFLAG_IMMUTABLE) < 0
            || hello_register_attrs (ctx.hello, ctx.attrs) < 0
            || logbuf_register_attrs (ctx.logbuf, ctx.attrs) < 0
            || content_cache_register_attrs (ctx.cache, ctx.attrs) < 0) {
        log_err_exit ("configuring attributes");
    }

    if (ctx.rank == 0) {
        if (runlevel_register_attrs (ctx.runlevel, ctx.attrs) < 0)
            log_err_exit ("configuring runlevel attributes");
    }

    flux_log (ctx.h, LOG_INFO, "pmi: bootstrap time %.1fs", pmi_elapsed_sec);

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
        log_msg ("parent: %s", parent ? parent : "none");
        log_msg ("child: %s", child ? child : "none");
        log_msg ("event: %s", event ? event : "none");
        log_msg ("relay: %s", relay ? relay : "none");
    }

    update_proctitle (&ctx);
    update_pidfile (&ctx);

    if (ctx.rank == 0) {
        const char *rc1, *rc3, *pmi, *uri;
        const char *rc2 = ctx.init_shell_cmd;

        if (attr_get (ctx.attrs, "local-uri", &uri, NULL) < 0)
            log_err_exit ("local-uri is not set");
        if (attr_get (ctx.attrs, "broker.rc1_path", &rc1, NULL) < 0)
            log_err_exit ("conf.rc1_path is not set");
        if (attr_get (ctx.attrs, "broker.rc3_path", &rc3, NULL) < 0)
            log_err_exit ("conf.rc3_path is not set");
        if (attr_get (ctx.attrs, "conf.pmi_library_path", &pmi, NULL) < 0)
            log_err_exit ("conf.pmi_library_path is not set");

        runlevel_set_size (ctx.runlevel, ctx.size);
        runlevel_set_subprocess_manager (ctx.runlevel, ctx.sm);
        runlevel_set_callback (ctx.runlevel, runlevel_cb, &ctx);
        runlevel_set_io_callback (ctx.runlevel, runlevel_io_cb, &ctx);

        if (runlevel_set_rc (ctx.runlevel, 1, rc1, uri) < 0)
            log_err_exit ("runlevel_set_rc 1");
        if (runlevel_set_rc (ctx.runlevel, 2, rc2, uri) < 0)
            log_err_exit ("runlevel_set_rc 2");
        if (runlevel_set_rc (ctx.runlevel, 3, rc3, uri) < 0)
            log_err_exit ("runlevel_set_rc 3");
    }

    /* Wire up the overlay.
     */
    if (ctx.verbose)
        log_msg ("initializing overlay sockets");
    if (overlay_bind (ctx.overlay) < 0) /* idempotent */
        log_err_exit ("overlay_bind");
    if (overlay_connect (ctx.overlay) < 0)
        log_err_exit ("overlay_connect");

    /* Set up snoop socket
     */
    snoop_set_zctx (ctx.snoop, ctx.zctx);
    snoop_set_sec (ctx.snoop, ctx.sec);
    {
        const char *scratch_dir;
        if (attr_get (ctx.attrs, "scratch-directory", &scratch_dir, NULL) < 0) {
            log_msg_exit ("scratch-directory attribute is not set");
        }
        snoop_set_uri (ctx.snoop, "ipc://%s/%d/snoop", scratch_dir, ctx.rank);
    }

    shutdown_set_handle (ctx.shutdown, ctx.h);
    shutdown_set_callback (ctx.shutdown, shutdown_cb, &ctx);

    /* Register internal services
     */
    broker_add_services (&ctx);

    /* Load default modules
     */
    if (ctx.verbose)
        log_msg ("loading default modules");
    modhash_set_zctx (ctx.modhash, ctx.zctx);
    modhash_set_rank (ctx.modhash, ctx.rank);
    modhash_set_flux (ctx.modhash, ctx.h);
    modhash_set_heartbeat (ctx.modhash, ctx.heartbeat);
    load_modules (&ctx, default_modules);

    /* install heartbeat (including timer on rank 0)
     */
    heartbeat_set_flux (ctx.heartbeat, ctx.h);
    if (heartbeat_set_attrs (ctx.heartbeat, ctx.attrs) < 0)
        log_err_exit ("initializing heartbeat attributes");
    if (heartbeat_start (ctx.heartbeat) < 0)
        log_err_exit ("heartbeat_start");
    if (ctx.rank == 0 && ctx.verbose)
        log_msg ("installing session heartbeat: T=%0.1fs",
                  heartbeat_get_rate (ctx.heartbeat));

    /* Send hello message to parent.
     * N.B. uses tbon topology attributes set above.
     * Start init once wireup is complete.
     */
    hello_set_flux (ctx.hello, ctx.h);
    hello_set_callback (ctx.hello, hello_update_cb, &ctx);
    if (hello_start (ctx.hello) < 0)
        log_err_exit ("hello_start");

    /* Event loop
     */
    if (ctx.verbose)
        log_msg ("entering event loop");
    if (flux_reactor_run (ctx.reactor, 0) < 0)
        log_err ("flux_reactor_run");
    if (ctx.verbose)
        log_msg ("exited event loop");

    /* remove heartbeat timer, if any
     */
    heartbeat_stop (ctx.heartbeat);

    /* Unload modules.
     * FIXME: this will hang in pthread_join unless modules have been stopped.
     */
    if (ctx.verbose)
        log_msg ("unloading modules");
    modhash_destroy (ctx.modhash);

    broker_unhandle_signals (sigwatchers);
    zlist_destroy (&sigwatchers);

    if (ctx.verbose)
        log_msg ("cleaning up");
    if (ctx.enclosing_h)
        flux_close (ctx.enclosing_h);
    if (ctx.sec)
        flux_sec_destroy (ctx.sec);
    zctx_destroy (&ctx.zctx);
    overlay_destroy (ctx.overlay);
    heartbeat_destroy (ctx.heartbeat);
    snoop_destroy (ctx.snoop);
    svchash_destroy (ctx.services);
    sequence_hash_destroy (ctx.seq);
    hello_destroy (ctx.hello);
    attr_destroy (ctx.attrs);
    flux_close (ctx.h);
    flux_reactor_destroy (ctx.reactor);
    logbuf_destroy (ctx.logbuf);
    zlist_destroy (&ctx.subscriptions);
    content_cache_destroy (ctx.cache);
    runlevel_destroy (ctx.runlevel);

    return 0;
}

struct attrmap {
    const char *env;
    const char *attr;
    uint8_t required:1;
};

static struct attrmap attrmap[] = {
    { "FLUX_EXEC_PATH",         "conf.exec_path",           1 },
    { "FLUX_CONNECTOR_PATH",    "conf.connector_path",      1 },
    { "FLUX_MODULE_PATH",       "conf.module_path",         1 },
    { "FLUX_PMI_LIBRARY_PATH",  "conf.pmi_library_path",    1 },
    { "FLUX_RC1_PATH",          "broker.rc1_path",          1 },
    { "FLUX_RC3_PATH",          "broker.rc3_path",          1 },
    { "FLUX_WRECK_LUA_PATTERN", "wrexec.lua_pattern",       1 },
    { "FLUX_WREXECD_PATH",      "wrexec.wrexecd_path",      1 },
    { "FLUX_SEC_DIRECTORY",     "security.keydir",          1 },

    { "FLUX_URI",               "parent-uri",               0 },
    { NULL, NULL },
};

static void init_attrs_from_environment (attr_t *attrs)
{
    struct attrmap *m;
    const char *val;
    int flags = 0;  // XXX possibly these should be immutable?
                    //   however they weren't before and wreck test depends
                    //   on changing wrexec.lua_pattern

    for (m = &attrmap[0]; m->env != NULL; m++) {
        val = getenv (m->env);
        if (!val && m->required)
            log_msg_exit ("required environment variable %s is not set", m->env);
        if (attr_add (attrs, m->attr, val, flags) < 0)
            log_err_exit ("attr_add %s", m->attr);
    }
}

static void hello_update_cb (hello_t *hello, void *arg)
{
    ctx_t *ctx = arg;

    if (hello_complete (hello)) {
        flux_log (ctx->h, LOG_INFO, "wireup: %d/%d (complete) %.1fs",
                  hello_get_count (hello), ctx->size, hello_get_time (hello));
        flux_log (ctx->h, LOG_INFO, "Run level %d starting", 1);
        overlay_set_idle_warning (ctx->overlay, 3);
        if (runlevel_set_level (ctx->runlevel, 1) < 0)
            log_err_exit ("runlevel_set_level 1");
        /* FIXME: shutdown hello protocol */
    } else  {
        flux_log (ctx->h, LOG_INFO, "wireup: %d/%d (incomplete) %.1fs",
                  hello_get_count (hello), ctx->size, hello_get_time (hello));
    }
}

/* Currently 'expired' is always true.
 */
static void shutdown_cb (shutdown_t *s, bool expired, void *arg)
{
    ctx_t *ctx = arg;
    if (expired)
        exit (ctx->rank == 0 ? shutdown_get_rc (s) : 0);
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
    const char *scratch_dir;
    char *pidfile;
    FILE *f;

    if (attr_get (ctx->attrs, "scratch-directory", &scratch_dir, NULL) < 0)
        log_msg_exit ("scratch-directory attribute is not set");
    pidfile = xasprintf ("%s/%d/broker.pid", scratch_dir, ctx->rank);
    if (!(f = fopen (pidfile, "w+")))
        log_err_exit ("%s", pidfile);
    if (fprintf (f, "%u", getpid ()) < 0)
        log_err_exit ("%s", pidfile);
    if (fclose(f) < 0)
        log_err_exit ("%s", pidfile);
    cleanup_push_string (cleanup_file, pidfile);
    free (pidfile);
}

/* Handle line by line output on stdout, stderr of runlevel subprocess.
 */
static void runlevel_io_cb (runlevel_t *r, const char *name,
                            const char *msg, void *arg)
{
    ctx_t *ctx = arg;
    int loglevel = !strcmp (name, "stderr") ? LOG_ERR : LOG_INFO;
    int runlevel = runlevel_get_level (r);

    flux_log (ctx->h, loglevel, "rc%d: %s", runlevel, msg);
}

/* Handle completion of runlevel subprocess.
 */
static void runlevel_cb (runlevel_t *r, int level, int rc, double elapsed,
                         const char *exit_string, void *arg)
{
    ctx_t *ctx = arg;
    int new_level = -1;

    flux_log (ctx->h, rc == 0 ? LOG_INFO : LOG_ERR,
              "Run level %d %s (rc=%d) %.1fs", level, exit_string, rc, elapsed);

    switch (level) {
        case 1: /* init completed */
            if (rc != 0) {
                new_level = 3;
                shutdown_arm (ctx->shutdown, ctx->shutdown_grace,
                              rc, "run level 1 %s", exit_string);
            } else
                new_level = 2;
            break;
        case 2: /* initial program completed */
            new_level = 3;
            shutdown_arm (ctx->shutdown, ctx->shutdown_grace,
                          rc, "run level 2 %s", exit_string);
            break;
        case 3: /* finalization completed */
            break;
    }
    if (new_level != -1) {
        flux_log (ctx->h, LOG_INFO, "Run level %d starting", new_level);
        if (runlevel_set_level (r, new_level) < 0)
            log_err_exit ("runlevel_set_level %d", new_level);
    }
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

/* If user set the 'scratch-directory-rank' attribute on the command line,
 * validate the directory and its permissions, and set the immutable flag
 * on the attribute.  If unset, create it within 'scratch-directory'.
 * If we created the directory, arrange to remove it on exit.
 * This function is idempotent.
 */
static int create_rankdir (ctx_t *ctx)
{
    const char *attr = "scratch-directory-rank";
    const char *rank_dir, *scratch_dir, *local_uri;
    char *dir = NULL;
    char *uri = NULL;
    int rc = -1;

    if (attr_get (ctx->attrs, attr, &rank_dir, NULL) == 0) {
        struct stat sb;
        if (stat (rank_dir, &sb) < 0)
            goto done;
        if (!S_ISDIR (sb.st_mode)) {
            errno = ENOTDIR;
            goto done;
        }
        if ((sb.st_mode & S_IRWXU) != S_IRWXU) {
            errno = EPERM;
            goto done;
        }
        if (attr_set_flags (ctx->attrs, attr, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    } else {
        if (attr_get (ctx->attrs, "scratch-directory",
                                                &scratch_dir, NULL) < 0) {
            errno = EINVAL;
            goto done;
        }
        if (ctx->rank == FLUX_NODEID_ANY) {
            errno = EINVAL;
            goto done;
        }
        dir = xasprintf ("%s/%d", scratch_dir, ctx->rank);
        if (mkdir (dir, 0700) < 0)
            goto done;
        if (attr_add (ctx->attrs, attr, dir, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
        cleanup_push_string (cleanup_directory, dir);
        rank_dir = dir;
    }
    if (attr_get (ctx->attrs, "local-uri", &local_uri, NULL) < 0) {
        uri = xasprintf ("local://%s", rank_dir);
        if (attr_add (ctx->attrs, "local-uri", uri,
                                            FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }
    rc = 0;
done:
    if (dir)
        free (dir);
    if (uri)
        free (uri);
    return rc;
}

/* If user set the 'scratch-directory' attribute on the command line,
 * validate the directory and its permissions, and set the immutable flag
 * on the attribute.  If unset, create it in TMPDIR such that it will
 * be unique in the enclosing instance, and in a hierarchy of instances.
 * If we created the directory, arrange to remove it on exit.
 * This function is idempotent.
 */
static int create_scratchdir (ctx_t *ctx)
{
    const char *attr = "scratch-directory";
    const char *sid, *scratch_dir;
    const char *tmpdir = getenv ("TMPDIR");
    char *dir, *tmpl = NULL;
    int rc = -1;

    if (attr_get (ctx->attrs, attr, &scratch_dir, NULL) == 0) {
        struct stat sb;
        if (stat (scratch_dir, &sb) < 0)
            goto done;
        if (!S_ISDIR (sb.st_mode)) {
            errno = ENOTDIR;
            goto done;
        }
        if ((sb.st_mode & S_IRWXU) != S_IRWXU) {
            errno = EPERM;
            goto done;
        }
        if (attr_set_flags (ctx->attrs, attr, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    } else {
        if (attr_get (ctx->attrs, "session-id", &sid, NULL) < 0) {
            errno = EINVAL;
            goto done;
        }
        tmpl = xasprintf ("%s/flux-%s-XXXXXX", tmpdir ? tmpdir : "/tmp", sid);
        if (!(dir = mkdtemp (tmpl)))
            goto done;
        if (attr_add (ctx->attrs, attr, dir, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
        cleanup_push_string (cleanup_directory, dir);
    }
    rc = 0;
done:
    if (tmpl)
        free (tmpl);
    return rc;
}

/* If 'persist-directory' set, validate it, make it immutable, done.
 * If 'persist-filesystem' set, validate it, make it immutable, then:
 * Create 'persist-directory' beneath it such that it is both unique and
 * a different basename than 'scratch-directory' (in case persist-filesystem
 * is set to TMPDIR).
 */
static int create_persistdir (ctx_t *ctx)
{
    struct stat sb;
    const char *attr = "persist-directory";
    const char *sid, *persist_dir, *persist_fs;
    char *dir, *tmpl = NULL;
    int rc = -1;

    if (ctx->rank > 0) {
        (void) attr_delete (ctx->attrs, "persist-filesystem", true);
        (void) attr_delete (ctx->attrs, "persist-directory", true);
        goto done_success;
    }
    if (attr_get (ctx->attrs, attr, &persist_dir, NULL) == 0) {
        if (stat (persist_dir, &sb) < 0)
            goto done;
        if (!S_ISDIR (sb.st_mode)) {
            errno = ENOTDIR;
            goto done;
        }
        if ((sb.st_mode & S_IRWXU) != S_IRWXU) {
            errno = EPERM;
            goto done;
        }
        if (attr_set_flags (ctx->attrs, attr, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    } else {
        if (attr_get (ctx->attrs, "session-id", &sid, NULL) < 0) {
            errno = EINVAL;
            goto done;
        }
        if (attr_get (ctx->attrs, "persist-filesystem", &persist_fs, NULL)< 0) {
            goto done_success;
        }
        if (stat (persist_fs, &sb) < 0)
            goto done;
        if (!S_ISDIR (sb.st_mode)) {
            errno = ENOTDIR;
            goto done;
        }
        if ((sb.st_mode & S_IRWXU) != S_IRWXU) {
            errno = EPERM;
            goto done;
        }
        if (attr_set_flags (ctx->attrs, "persist-filesystem",
                                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
        tmpl = xasprintf ("%s/fluxP-%s-XXXXXX", persist_fs, sid);
        if (!(dir = mkdtemp (tmpl)))
            goto done;
        if (attr_add (ctx->attrs, attr, dir, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }
done_success:
    if (attr_get (ctx->attrs, "persist-filesystem", NULL, NULL) < 0) {
        if (attr_add (ctx->attrs, "persist-filesystem", NULL,
                                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }
    if (attr_get (ctx->attrs, "persist-directory", NULL, NULL) < 0) {
        if (attr_add (ctx->attrs, "persist-directory", NULL,
                                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }
    rc = 0;
done:
    if (tmpl)
        free (tmpl);
    return rc;
}

static int boot_pmi (ctx_t *ctx, double *elapsed_sec)
{
    const char *scratch_dir;
    int spawned, size, rank, appnum;
    int relay_rank = -1, parent_rank;
    int clique_size;
    int *clique_ranks = NULL;
    char ipaddr[HOST_NAME_MAX + 1];
    const char *child_uri, *relay_uri;
    int kvsname_len, key_len, val_len;
    char *id = NULL;
    char *kvsname = NULL;
    char *key = NULL;
    char *val = NULL;
    int e, rc = -1;
    struct timespec start_time;

    monotime (&start_time);

    if ((e = PMI_Init (&spawned)) != PMI_SUCCESS) {
        log_msg ("PMI_Init: %s", pmi_strerror (e));
        goto done;
    }

    if (ctx->enable_epgm || !ctx->shared_ipc_namespace)
        ipaddr_getprimary (ipaddr, sizeof (ipaddr));


    /* Get rank, size, appnum
     */
    if ((e = PMI_Get_size (&size)) != PMI_SUCCESS) {
        log_msg ("PMI_Get_size: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = PMI_Get_rank (&rank)) != PMI_SUCCESS) {
        log_msg ("PMI_Get_rank: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = PMI_Get_appnum (&appnum)) != PMI_SUCCESS) {
        log_msg ("PMI_Get_appnum: %s", pmi_strerror (e));
        goto done;
    }
    ctx->rank = rank;
    ctx->size = size;
    overlay_set_rank (ctx->overlay, ctx->rank);

    /* Get id string.
     */
    if (attr_get (ctx->attrs, "session-id", NULL, NULL) < 0) {
        id = xasprintf ("%d", appnum);
        if (attr_add (ctx->attrs, "session-id", id, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }

    /* Initialize scratch-directory/rankdir
     */
    if (create_scratchdir (ctx) < 0) {
        log_err ("pmi: could not initialize scratch-directory");
        goto done;
    }
    if (attr_get (ctx->attrs, "scratch-directory", &scratch_dir, NULL) < 0) {
        log_msg ("scratch-directory attribute is not set");
        goto done;
    }
    if (create_rankdir (ctx) < 0) {
        log_err ("could not initialize ankdir");
        goto done;
    }

    /* Set TBON request addr.  We will need any wildcards expanded below.
     */
    if (ctx->shared_ipc_namespace) {
        char *reqfile = xasprintf ("%s/%d/req", scratch_dir, ctx->rank);
        overlay_set_child (ctx->overlay, "ipc://%s", reqfile);
        cleanup_push_string (cleanup_file, reqfile);
        free (reqfile);
    } else {
        overlay_set_child (ctx->overlay, "tcp://%s:*", ipaddr);
    }

    /* Set up epgm relay if multiple ranks are being spawned per node,
     * as indicated by "clique ranks".  FIXME: if epgm is used but
     * pmi_get_clique_ranks() is not implemented, this fails.  Find an
     * alternate method to determine if ranks are co-located on a node.
     */
    if (ctx->enable_epgm) {
        if ((e = PMI_Get_clique_size (&clique_size)) != PMI_SUCCESS) {
            log_msg ("PMI_get_clique_size: %s", pmi_strerror (e));
            goto done;
        }
        clique_ranks = xzmalloc (sizeof (int) * clique_size);
        if ((e = PMI_Get_clique_ranks (clique_ranks, clique_size))
                                                          != PMI_SUCCESS) {
            log_msg ("PMI_Get_clique_ranks: %s", pmi_strerror (e));
            goto done;
        }
        if (clique_size > 1) {
            int i;
            for (i = 0; i < clique_size; i++)
                if (relay_rank == -1 || clique_ranks[i] < relay_rank)
                    relay_rank = clique_ranks[i];
            if (relay_rank >= 0 && ctx->rank == relay_rank) {
                char *relayfile = xasprintf ("%s/%d/relay", scratch_dir, rank);
                overlay_set_relay (ctx->overlay, "ipc://%s", relayfile);
                cleanup_push_string (cleanup_file, relayfile);
                free (relayfile);
            }
        }
    }

    /* Prepare for PMI KVS operations by grabbing the kvsname,
     * and buffers for keys and values.
     */
    if ((e = PMI_KVS_Get_name_length_max (&kvsname_len)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Get_name_length_max: %s", pmi_strerror (e));
        goto done;
    }
    kvsname = xzmalloc (kvsname_len);
    if ((e = PMI_KVS_Get_my_name (kvsname, kvsname_len)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Get_my_name: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = PMI_KVS_Get_key_length_max (&key_len)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Get_key_length_max: %s", pmi_strerror (e));
        goto done;
    }
    key = xzmalloc (key_len);
    if ((e = PMI_KVS_Get_value_length_max (&val_len)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Get_value_length_max: %s", pmi_strerror (e));
        goto done;
    }
    val = xzmalloc (val_len);

    /* Bind to addresses to expand URI wildcards, so we can exchange
     * the real addresses.
     */
    if (overlay_bind (ctx->overlay) < 0) {
        log_err ("overlay_bind failed");   /* function is idempotent */
        goto done;
    }

    /* Write the URI of downstream facing socket under the rank (if any).
     */
    if ((child_uri = overlay_get_child (ctx->overlay))) {
        if (snprintf (key, key_len, "cmbd.%d.uri", rank) >= key_len) {
            log_msg ("pmi key string overflow");
            goto done;
        }
        if (snprintf (val, val_len, "%s", child_uri) >= val_len) {
            log_msg ("pmi val string overflow");
            goto done;
        }
        if ((e = PMI_KVS_Put (kvsname, key, val)) != PMI_SUCCESS) {
            log_msg ("PMI_KVS_Put: %s", pmi_strerror (e));
            goto done;
        }
    }

    /* Write the uri of the epgm relay under the rank (if any).
     */
    if (ctx->enable_epgm && (relay_uri = overlay_get_relay (ctx->overlay))) {
        if (snprintf (key, key_len, "cmbd.%d.relay", rank) >= key_len) {
            log_msg ("pmi key string overflow");
            goto done;
        }
        if (snprintf (val, val_len, "%s", relay_uri) >= val_len) {
            log_msg ("pmi val string overflow");
            goto done;
        }
        if ((e = PMI_KVS_Put (kvsname, key, val)) != PMI_SUCCESS) {
            log_msg ("PMI_KVS_Put: %s", pmi_strerror (e));
            goto done;
        }
    }

    /* Puts are complete, now we synchronize and begin our gets.
     */
    if ((e = PMI_KVS_Commit (kvsname)) != PMI_SUCCESS) {
        log_msg ("PMI_KVS_Commit: %s", pmi_strerror (e));
        goto done;
    }
    if ((e = PMI_Barrier ()) != PMI_SUCCESS) {
        log_msg ("PMI_Barrier: %s", pmi_strerror (e));
        goto done;
    }

    /* Read the uri of our parent, after computing its rank
     */
    if (ctx->rank > 0) {
        parent_rank = kary_parentof (ctx->tbon.k, ctx->rank);
        if (snprintf (key, key_len, "cmbd.%d.uri", parent_rank) >= key_len) {
            log_msg ("pmi key string overflow");
            goto done;
        }
        if ((e = PMI_KVS_Get (kvsname, key, val, val_len)) != PMI_SUCCESS) {
            log_msg ("pmi_kvs_get: %s", pmi_strerror (e));
            goto done;
        }
        overlay_push_parent (ctx->overlay, "%s", val);
    }

    /* Event distribution (four configurations):
     * 1) epgm enabled, one broker per node
     *    All brokers subscribe to the same epgm address.
     * 2) epgm enabled, mutiple brokers per node
     *    The lowest rank in each clique will subscribe to the epgm:// socket
     *    and relay events to an ipc:// socket for the other ranks in the
     *    clique.  This is necessary due to limitation of epgm.
     * 3) epgm disabled, all brokers concentrated on one node
     *    Rank 0 publishes to a ipc:// socket, other ranks subscribe
     * 4) epgm disabled brokers distributed across nodes
     *    No dedicated event overlay,.  Events are distributed over the TBON.
     */
    if (ctx->enable_epgm) {
        if (relay_rank >= 0 && rank != relay_rank) {
            if (snprintf (key, key_len, "cmbd.%d.relay", relay_rank)
                                                                >= key_len) {
                log_msg ("pmi key string overflow");
                goto done;
            }
            if ((e = PMI_KVS_Get (kvsname, key, val, val_len))
                                                            != PMI_SUCCESS) {
                log_msg ("PMI_KVS_Get: %s", pmi_strerror (e));
                goto done;
            }
            overlay_set_event (ctx->overlay, "%s", val);
        } else {
            int port = 5000 + appnum % 1024;
            overlay_set_event (ctx->overlay, "epgm://%s;239.192.1.1:%d",
                               ipaddr, port);
        }
    } else if (ctx->shared_ipc_namespace) {
        char *eventfile = xasprintf ("%s/event", scratch_dir);
        overlay_set_event (ctx->overlay, "ipc://%s", eventfile);
        if (ctx->rank == 0)
            cleanup_push_string (cleanup_file, eventfile);
        free (eventfile);
    }
    if ((e = PMI_Barrier ()) != PMI_SUCCESS) {
        log_msg ("PMI_Barrier: %s", pmi_strerror (e));
        goto done;
    }
    PMI_Finalize ();
    rc = 0;
done:
    *elapsed_sec = monotime_since (start_time) / 1000;
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
    if (rc != 0)
        errno = EPROTO;
    return rc;
}

static bool nodeset_member (const char *s, uint32_t rank)
{
    nodeset_t *ns = NULL;
    bool member = true;

    if (s) {
        if (!(ns = nodeset_create_string (s)))
            log_msg_exit ("malformed nodeset: %s", s);
        member = nodeset_test_rank (ns, rank);
    }
    if (ns)
        nodeset_destroy (ns);
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
static void load_modules (ctx_t *ctx, const char *default_modules)
{
    char *cpy = xstrdup (default_modules);
    char *s, *saveptr = NULL, *a1 = cpy;
    const char *modpath;
    module_t *p;

    if (attr_get (ctx->attrs, "conf.module_path", &modpath, NULL) < 0)
        log_err_exit ("conf.module_path is not set");

    while ((s = strtok_r (a1, ",", &saveptr))) {
        char *name = NULL;
        char *path = NULL;
        char *sp;
        if ((sp = strchr (s, '['))) {
            if (!nodeset_member (sp, ctx->rank))
                goto next;
            *sp = '\0';
        }
        if (strchr (s, '/')) {
            if (!(name = flux_modname (s)))
                log_msg_exit ("%s", dlerror ());
            path = s;
        } else {
            if (!(path = flux_modfind (modpath, s)))
                log_msg_exit ("%s: not found in module search path", s);
            name = s;
        }
        if (!(p = module_add (ctx->modhash, path)))
            log_err_exit ("%s: module_add %s", name, path);
        if (!svc_add (ctx->services, module_get_name (p),
                                     module_get_service (p), mod_svc_cb, p))
            log_msg_exit ("could not register service %s", module_get_name (p));
        module_set_poller_cb (p, module_cb, ctx);
        module_set_status_cb (p, module_status_cb, ctx);
next:
        if (name != s)
            free (name);
        if (path != s)
            free (path);
        a1 = NULL;
    }
    module_start_all (ctx->modhash);
    free (cpy);
}

static void broker_block_signals (void)
{
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigfillset(&sigmask);
    if (sigprocmask (SIG_SETMASK, &sigmask, NULL) < 0)
        log_err_exit ("sigprocmask");
}

static void broker_handle_signals (ctx_t *ctx, zlist_t *sigwatchers)
{
    int i, sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGSEGV, SIGFPE };
    flux_watcher_t *w;

    for (i = 0; i < sizeof (sigs) / sizeof (sigs[0]); i++) {
        w = flux_signal_watcher_create (ctx->reactor, sigs[i], signal_cb, ctx);
        if (!w)
            log_err_exit ("flux_signal_watcher_create");
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
    json_object *resp = Jnew ();

    assert (ctx != NULL);
    assert (resp != NULL);

    Jadd_int (resp, "rank", ctx->rank);
    Jadd_int (resp, "pid", subprocess_pid (p));
    Jadd_str (resp, "state", subprocess_state_string (p));
    return (resp);
}

static int child_exit_handler (struct subprocess *p)
{
    int n;
    ctx_t *ctx = (ctx_t *) subprocess_get_context (p, "ctx");
    zmsg_t *zmsg = (zmsg_t *) subprocess_get_context (p, "zmsg");
    json_object *resp;

    assert (ctx != NULL);
    assert (zmsg != NULL);

    resp = subprocess_json_resp (ctx, p);
    Jadd_int (resp, "status", subprocess_exit_status (p));
    Jadd_int (resp, "code", subprocess_exit_code (p));
    if ((n = subprocess_signaled (p)))
        Jadd_int (resp, "signal", n);
    if ((n = subprocess_exec_error (p)))
        Jadd_int (resp, "exec_errno", n);

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
    response = Jnew ();
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
                /* Send signal to entire process group */
                if (kill (-pid, signum) < 0)
                    errnum = errno;
            }
            p = subprocess_manager_next (ctx->sm);
        }
    }
out:
    response = Jnew ();
    Jadd_int (response, "code", errnum);
    flux_respond (ctx->h, *zmsg, 0, Jtostr (response));
    zmsg_destroy (zmsg);
    if (response)
        json_object_put (response);
    if (request)
        json_object_put (request);
    return (0);
}

static int do_setpgrp (struct subprocess *p)
{
    if (setpgrp () < 0)
        fprintf (stderr, "setpgrp: %s", strerror (errno));
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
        log_err_exit ("%s: local_uri is not set", __FUNCTION__);

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
    subprocess_set_context (p, "ctx", ctx);
    subprocess_add_hook (p, SUBPROCESS_COMPLETE, child_exit_handler);
    subprocess_add_hook (p, SUBPROCESS_PRE_EXEC, do_setpgrp);

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
    char *sender;
    zmsg_t *zmsg = subprocess_get_context (p, "zmsg");
    if (!zmsg || flux_msg_get_route_first (zmsg, &sender) < 0)
        return NULL;
    return (sender);
}

static int terminate_subprocesses_by_uuid (ctx_t *ctx, char *id)
{
    struct subprocess *p = subprocess_manager_first (ctx->sm);
    while (p) {
        char *sender;
        if ((sender = subprocess_sender (p))) {
            pid_t pid;
            if ((strcmp (id, sender) == 0)
               && ((pid = subprocess_pid (p)) > (pid_t) 0)) {
                /* Kill process group for subprocess p */
                flux_log (ctx->h, LOG_INFO,
                          "Terminating PGRP %ld", (unsigned long) pid);
                if (kill (-pid, SIGKILL) < 0)
                    flux_log_error (ctx->h, "killpg");
            }
            free (sender);
        }
        p = subprocess_manager_next (ctx->sm);
    }
    return (0);
}

static json_object *subprocess_json_info (struct subprocess *p)
{
    int i;
    char buf [MAXPATHLEN];
    const char *cwd;
    char *sender = NULL;
    json_object *o = Jnew ();
    json_object *a = Jnew_ar ();

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
    json_object *out = Jnew ();
    json_object *procs = Jnew_ar ();
    int rc;

    Jadd_int (out, "rank", ctx->rank);

    p = subprocess_manager_first (ctx->sm);
    while (p) {
        json_object *o = subprocess_json_info (p);
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
    json_object *in = NULL;
    json_object *out = Jnew ();
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
    json_object *in = NULL;
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
    json_object *out = Jnew ();
    json_object *array = Jnew_ar ();
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
    json_object *out = NULL;
    int rc = -1;

    if (getrusage_json (RUSAGE_THREAD, &out) < 0)
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
    if (module_push_rmmod (p, *zmsg) < 0)
        goto done;
    zmsg_destroy (zmsg); /* zmsg will be replied to later */
    if (module_stop (p) < 0)
        goto done;
    flux_log (ctx->h, LOG_DEBUG, "rmmod %s", name);
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
    if (!svc_add (ctx->services, module_get_name (p),
                                 module_get_service (p), mod_svc_cb, p)) {
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
    module_set_status_cb (p, module_status_cb, ctx);
    if (module_push_insmod (p, *zmsg) < 0) {
        module_remove (ctx->modhash, p);
        goto done;
    }
    if (module_start (p) < 0) {
        module_remove (ctx->modhash, p);
        goto done;
    }
    zmsg_destroy (zmsg); /* zmsg will be replied to later */
    flux_log (ctx->h, LOG_DEBUG, "insmod %s", name);
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
    flux_modlist_t *mods = NULL;
    char *json_str = NULL;
    int rc = -1;

    if (!(mods = module_get_modlist (ctx->modhash)))
        goto done;
    if (!(json_str = flux_lsmod_json_encode (mods)))
        goto done;
    if (flux_respond (ctx->h, *zmsg, 0, json_str) < 0)
        goto done;
    rc = 0;
done:
    zmsg_destroy (zmsg);
    if (json_str)
        free (json_str);
    if (mods)
        flux_modlist_destroy (mods);
    return rc;
}

static int cmb_lspeer_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *out = overlay_lspeer_encode (ctx->overlay);
    int rc = flux_respond (ctx->h, *zmsg, 0, Jtostr (out));
    zmsg_destroy (zmsg);
    Jput (out);
    return rc;
}

static int cmb_ping_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *inout = NULL;
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
    json_object *in = NULL;
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
    flux_log (ctx->h, LOG_CRIT, "reparent %s (%s)", uri, recycled ? "restored"
                                                                  : "new");
    rc = 0;
done:
    Jput (in);
    return rc;
}

static int cmb_panic_cb (zmsg_t **zmsg, void *arg)
{
    json_object *in = NULL;
    const char *s = NULL;
    const char *json_str;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "msg", &s))
        s = "no reason";
    log_msg_exit ("PANIC: %s", s ? s : "no reason");
done:
    Jput (in);
    return rc;
}

static int cmb_log_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *buf;
    int len;

    if (flux_request_decode_raw (*zmsg, NULL, &buf, &len) < 0) {
        log_msg ("%s: decode error", __FUNCTION__);
        goto done;
    }
    if (logbuf_append (ctx->logbuf, buf, len) < 0)
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
    json_object *in = NULL;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_int (in, "seq", &seq)) {
        errno = EPROTO;
        goto done;
    }
    logbuf_clear (ctx->logbuf, seq);
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
    const char *buf;
    int len;
    int seq;
    json_object *in = NULL;
    json_object *out = NULL;
    bool follow;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_int (in, "seq", &seq)
                                    || !Jget_bool (in, "follow", &follow)) {
        errno = EPROTO;
        goto done;
    }
    if (logbuf_get (ctx->logbuf, seq, &seq, &buf, &len) < 0) {
        if (follow && errno == ENOENT) {
            flux_msg_t *cpy = flux_msg_copy (msg, true);
            if (!cpy)
                goto done;
            if (logbuf_sleepon (ctx->logbuf, cmb_dmesg, cpy, arg) < 0) {
                free (cpy);
                goto done;
            }
            goto done_noreply;
        }
        goto done;
    }
    out = Jnew ();
    Jadd_int (out, "seq", seq);
    Jadd_str_len (out, "buf", buf, len);
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
    logbuf_disconnect (ctx->logbuf, sender);
done:
    if (sender)
        free (sender);
    zmsg_destroy (zmsg); /* no reply */
    return 0;
}

static int cmb_sub_cb (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    char *uuid = NULL;
    json_object *in = NULL;
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
        FLUX_LOG_ERROR (ctx->h);
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
    json_object *in = NULL;
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
        FLUX_LOG_ERROR (ctx->h);
    if (uuid)
        free (uuid);
    Jput (in);
    rc = flux_respond (ctx->h, *zmsg, rc < 0 ? errno : 0, NULL);
    zmsg_destroy (zmsg);
    return rc;
}

static int cmb_seq (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *out = NULL;
    int rc = sequence_request_handler (ctx->seq, (flux_msg_t *) *zmsg, &out);

    if (flux_respond (ctx->h, *zmsg, rc < 0 ? errno : 0, Jtostr (out)) < 0)
        flux_log_error (ctx->h, "cmb.seq: flux_respond");
    if (out)
        Jput (out);
    zmsg_destroy (zmsg);
    return (rc);
}

static int cmb_heaptrace_start_cb (zmsg_t **zmsg, void *arg)
{
    const char *json_str, *filename;
    json_object *in = NULL;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "filename", &filename)) {
        errno = EPROTO;
        goto done;
    }
#if WITH_TCMALLOC
    if (IsHeapProfilerRunning ()) {
        errno = EINVAL;
        goto done;
    }
    HeapProfilerStart (filename);
    rc = 0;
#else
    errno = ENOSYS;
#endif
done:
    Jput (in);
    return rc;
}

static int cmb_heaptrace_dump_cb (zmsg_t **zmsg, void *arg)
{
    const char *json_str, *reason;
    json_object *in = NULL;
    int rc = -1;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "reason", &reason)) {
        errno = EPROTO;
        goto done;
    }
#if WITH_TCMALLOC
    if (!IsHeapProfilerRunning ()) {
        errno = EINVAL;
        goto done;
    }
    HeapProfilerDump (reason);
    rc = 0;
#else
    errno = ENOSYS;
#endif
done:
    Jput (in);
    return rc;
}

static int cmb_heaptrace_stop_cb (zmsg_t **zmsg, void *arg)
{
    int rc = -1;
    if (flux_request_decode (*zmsg, NULL, NULL) < 0)
        goto done;
#if WITH_TCMALLOC
    if (!IsHeapProfilerRunning ()) {
        errno = EINVAL;
        goto done;
    }
    HeapProfilerStop();
    rc = 0;
#else
    errno = ENOSYS;
#endif /* WITH_TCMALLOC */
done:
    return rc;
}

static int requeue_for_service (zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    if (flux_requeue (ctx->h, *zmsg, FLUX_RQ_TAIL) < 0)
        flux_log_error (ctx->h, "%s: flux_requeue\n", __FUNCTION__);
    zmsg_destroy (zmsg);
    return 0;
}

struct internal_service {
    const char *topic;
    const char *nodeset;
    int (*fun)(zmsg_t **zmsg, void *arg);
};

static struct internal_service services[] = {
    { "cmb.attrget",    NULL,   cmb_attrget_cb      },
    { "cmb.attrset",    NULL,   cmb_attrset_cb      },
    { "cmb.attrlist",   NULL,   cmb_attrlist_cb     },
    { "cmb.rusage",     NULL,   cmb_rusage_cb,      },
    { "cmb.rmmod",      NULL,   cmb_rmmod_cb,       },
    { "cmb.insmod",     NULL,   cmb_insmod_cb,      },
    { "cmb.lsmod",      NULL,   cmb_lsmod_cb,       },
    { "cmb.lspeer",     NULL,   cmb_lspeer_cb,      },
    { "cmb.ping",       NULL,   cmb_ping_cb,        },
    { "cmb.reparent",   NULL,   cmb_reparent_cb,    },
    { "cmb.panic",      NULL,   cmb_panic_cb,       },
    { "cmb.log",        NULL,   cmb_log_cb,         },
    { "cmb.dmesg.clear",NULL,   cmb_dmesg_clear_cb, },
    { "cmb.dmesg",      NULL,   cmb_dmesg_cb,       },
    { "cmb.event-mute", NULL,   cmb_event_mute_cb,  },
    { "cmb.exec",       NULL,   cmb_exec_cb,        },
    { "cmb.exec.signal",NULL,   cmb_signal_cb,      },
    { "cmb.exec.write", NULL,   cmb_write_cb,       },
    { "cmb.processes",  NULL,   cmb_ps_cb,          },
    { "cmb.disconnect", NULL,   cmb_disconnect_cb,  },
    { "cmb.sub",        NULL,   cmb_sub_cb,         },
    { "cmb.unsub",      NULL,   cmb_unsub_cb,       },
    { "cmb.seq.fetch",  "[0]",  cmb_seq             },
    { "cmb.seq.set",    "[0]",  cmb_seq             },
    { "cmb.seq.destroy","[0]",  cmb_seq             },
    { "cmb.heaptrace.start",NULL, cmb_heaptrace_start_cb },
    { "cmb.heaptrace.dump", NULL, cmb_heaptrace_dump_cb  },
    { "cmb.heaptrace.stop", NULL, cmb_heaptrace_stop_cb  },
    { "content",        NULL,   requeue_for_service },
    { "hello",          NULL,   requeue_for_service },
    { NULL, NULL, },
};

static void broker_add_services (ctx_t *ctx)
{
    struct internal_service *svc;

    for (svc = &services[0]; svc->topic != NULL; svc++) {
        if (!nodeset_member (svc->nodeset, ctx->rank))
            continue;
        if (!svc_add (ctx->services, svc->topic, NULL, svc->fun, ctx))
            log_err_exit ("error adding handler for %s", svc->topic);
    }
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
        case FLUX_MSGTYPE_RESPONSE:
            /* TRICKY:  Fix up ROUTER socket used in reverse direction.
             * Request/response is designed for requests to travel
             * ROUTER->DEALER (up) and responses DEALER-ROUTER (down).
             * When used conventionally, the route stack is accumulated
             * automatically as a request is routed up, and unwound
             * automatically as a response is routed down.  When responses
             * are routed up, ROUTER socket behavior must be subverted on
             * the receiving end by popping two frames off of the stack and
             * discarding.
             */
            (void)flux_msg_pop_route (zmsg, NULL);
            (void)flux_msg_pop_route (zmsg, NULL);
            if (broker_response_sendmsg (ctx, zmsg) < 0)
                goto done;
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

/* helper for event_cb, parent_cb, and (on rank 0) broker_event_sendmsg */
static int handle_event (ctx_t *ctx, zmsg_t **zmsg)
{
    uint32_t seq;
    const char *topic, *s;

    if (flux_msg_get_seq (*zmsg, &seq) < 0
            || flux_msg_get_topic (*zmsg, &topic) < 0) {
        flux_log (ctx->h, LOG_ERR, "dropping malformed event");
        return -1;
    }
    if (seq <= ctx->event_recv_seq) {
        //flux_log (ctx->h, LOG_DEBUG, "dropping duplicate event %d", seq);
        return -1;
    }
    if (ctx->event_recv_seq > 0) { /* don't log initial missed events */
        int first = ctx->event_recv_seq + 1;
        int count = seq - first;
        if (count > 1)
            flux_log (ctx->h, LOG_ERR, "lost events %d-%d", first, seq - 1);
        else if (count == 1)
            flux_log (ctx->h, LOG_ERR, "lost event %d", first);
    }
    ctx->event_recv_seq = seq;

    (void)overlay_mcast_child (ctx->overlay, *zmsg);
    (void)overlay_sendmsg_relay (ctx->overlay, *zmsg);

    /* Internal services may install message handlers for events.
     */
    s = zlist_first (ctx->subscriptions);
    while (s) {
        if (!strncmp (s, topic, strlen (s))) {
            if (flux_requeue (ctx->h, *zmsg, FLUX_RQ_TAIL) < 0)
                flux_log_error (ctx->h, "%s: flux_requeue\n", __FUNCTION__);
            break;
        }
        s = zlist_next (ctx->subscriptions);
    }
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
        flux_log_error (ctx->h, "failed to send mute request");
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
    int type, rc;

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
        case FLUX_MSGTYPE_REQUEST:
            rc = broker_request_sendmsg (ctx, &zmsg);
            if (zmsg)
                flux_respond (ctx->h, zmsg, rc < 0 ? errno : 0, NULL);
            if (rc < 0)
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
    flux_msg_t *msg = module_recvmsg (p);
    int type, rc;
    int ka_errnum, ka_status;

    if (!msg)
        goto done;
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            (void)broker_response_sendmsg (ctx, msg);
            break;
        case FLUX_MSGTYPE_REQUEST:
            rc = broker_request_sendmsg (ctx, &msg);
            if (msg)
                flux_respond (ctx->h, msg, rc < 0 ? errno : 0, NULL);
            break;
        case FLUX_MSGTYPE_EVENT:
            if (broker_event_sendmsg (ctx, &msg) < 0) {
                flux_log_error (ctx->h, "%s(%s): broker_event_sendmsg %s",
                                __FUNCTION__, module_get_name (p),
                                flux_msg_typestr (type));
            }
            break;
        case FLUX_MSGTYPE_KEEPALIVE:
            if (flux_keepalive_decode (msg, &ka_errnum, &ka_status) < 0) {
                flux_log_error (ctx->h, "%s: flux_keepalive_decode",
                                module_get_name (p));
                break;
            }
            if (ka_status == FLUX_MODSTATE_EXITED)
                module_set_errnum (p, ka_errnum);
            module_set_status (p, ka_status);
            break;
        default:
            flux_log (ctx->h, LOG_ERR, "%s(%s): unexpected %s",
                      __FUNCTION__, module_get_name (p),
                      flux_msg_typestr (type));
            break;
    }
done:
    flux_msg_destroy (msg);
}

static void module_status_cb (module_t *p, int prev_status, void *arg)
{
    ctx_t *ctx = arg;
    flux_msg_t *msg;
    int status = module_get_status (p);
    const char *name = module_get_name (p);

    /* Transition from INIT
     * Respond to insmod request, if any.
     * If transitioning to EXITED, return error to insmod if mod_main() = -1
     */
    if (prev_status == FLUX_MODSTATE_INIT) {
        if ((msg = module_pop_insmod (p))) {
            int errnum = 0;
            if (status == FLUX_MODSTATE_EXITED)
                errnum = module_get_errnum (p);
            if (flux_respond (ctx->h, msg, errnum, NULL) < 0)
                flux_log_error (ctx->h, "flux_respond to insmod %s", name);
            flux_msg_destroy (msg);
        }
    }

    /* Transition to EXITED
     * Remove service routes, respond to rmmod request(s), if any,
     * and remove the module (which calls pthread_join).
     */
    if (status == FLUX_MODSTATE_EXITED) {
        flux_log (ctx->h, LOG_DEBUG, "module %s exited", name);
        svc_remove (ctx->services, module_get_name (p));
        while ((msg = module_pop_rmmod (p))) {
            if (flux_respond (ctx->h, msg, 0, NULL) < 0)
                flux_log_error (ctx->h, "flux_respond to rmmod %s", name);
            flux_msg_destroy (msg);
        }
        module_remove (ctx->modhash, p);
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

static void signal_cb (flux_reactor_t *r, flux_watcher_t *w,
                         int revents, void *arg)
{
    ctx_t *ctx = arg;
    int signum = flux_signal_watcher_get_signum (w);

    shutdown_arm (ctx->shutdown, ctx->shutdown_grace, 0,
                  "signal %d (%s) %d", signum, strsignal (signum));
}

/* TRICKY:  Fix up ROUTER socket used in reverse direction.
 * Request/response is designed for requests to travel
 * ROUTER->DEALER (up) and responses DEALER-ROUTER (down).
 * When used conventionally, the route stack is accumulated
 * automatically as a reqest is routed up, and unwound
 * automatically as a response is routed down.  When requests
 * are routed down, ROUTER socket behavior must be subverted on the
 * sending end by pushing the identity of the sender onto the stack,
 * followed by the identity of the peer we want to route the message to.
 */
static int subvert_sendmsg_child (ctx_t *ctx, const flux_msg_t *msg,
                                  uint32_t nodeid)
{
    flux_msg_t *cpy = flux_msg_copy (msg, true);
    int saved_errno;
    char uuid[16];
    int rc = -1;

    snprintf (uuid, sizeof (uuid), "%u", ctx->rank);
    if (flux_msg_push_route (cpy, uuid) < 0)
        goto done;
    snprintf (uuid, sizeof (uuid), "%u", nodeid);
    if (flux_msg_push_route (cpy, uuid) < 0)
        goto done;
    if (overlay_sendmsg_child (ctx->overlay, cpy) < 0)
        goto done;
    rc = 0;
done:
    saved_errno = errno;
    flux_msg_destroy (cpy);
    errno = saved_errno;
    return rc;
}

static int broker_request_sendmsg (ctx_t *ctx, zmsg_t **zmsg)
{
    uint32_t nodeid, gw;
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
    } else if ((gw = kary_child_route (ctx->tbon.k, ctx->size,
                                       ctx->rank, nodeid)) != KARY_NONE) {
        rc = subvert_sendmsg_child (ctx, *zmsg, gw);
        if (rc == 0)
            zmsg_destroy (zmsg);
    } else if (ctx->rank > 0) {
        rc = overlay_sendmsg_parent (ctx->overlay, *zmsg);
        if (rc == 0)
            zmsg_destroy (zmsg);
    } else {
        errno = EHOSTUNREACH;
    }
done:
    /* N.B. don't destroy zmsg on error as we use it to send errnum reply.
     */
    return rc;
}

static int broker_response_sendmsg (ctx_t *ctx, const flux_msg_t *msg)
{
    int rc = -1;
    char *uuid = NULL;
    uint32_t parent;
    char puuid[16];

    if (flux_msg_get_route_last (msg, &uuid) < 0)
        goto done;

    /* If no next hop, this is for broker-resident service.
     */
    if (uuid == NULL) {
        rc = flux_requeue (ctx->h, msg, FLUX_RQ_TAIL);
        goto done;
    }

    parent = kary_parentof (ctx->tbon.k, ctx->rank);
    snprintf (puuid, sizeof (puuid), "%u", parent);

    /* See if it should go to the parent (backwards!)
     * (receiving end will compensate for reverse ROUTER behavior)
     */
    if (parent != KARY_NONE && !strcmp (puuid, uuid)) {
        rc = overlay_sendmsg_parent (ctx->overlay, msg);
        goto done;
    }

    /* Try to deliver to a module.
     * If modhash didn't match next hop, route to child.
     */
    rc = module_response_sendmsg (ctx->modhash, msg);
    if (rc < 0 && errno == ENOSYS)
        rc = overlay_sendmsg_child (ctx->overlay, msg);
done:
    if (uuid)
        free (uuid);
    return rc;
}

/* Events are forwarded up the TBON to rank 0, then published from there.
 * Rank 0 doesn't (generally) receive the events it transmits so we have
 * to "loop back" here via handle_event().
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
        if (flux_msg_set_seq (*zmsg, ++ctx->event_send_seq) < 0)
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

static int broker_subscribe (void *impl, const char *topic)
{
    ctx_t *ctx = impl;
    if (zlist_append (ctx->subscriptions, xstrdup (topic)))
        oom();
    return 0;
}

static int broker_unsubscribe (void *impl, const char *topic)
{
    ctx_t *ctx = impl;
    char *s = zlist_first (ctx->subscriptions);
    while (s) {
        if (!strcmp (s, topic)) {
            zlist_remove (ctx->subscriptions, s);
            break;
        }
        s = zlist_next (ctx->subscriptions);
    }
    return 0;
}

static const struct flux_handle_ops broker_handle_ops = {
    .send = broker_send,
    .event_subscribe = broker_subscribe,
    .event_unsubscribe = broker_unsubscribe,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
