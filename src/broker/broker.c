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
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <argz.h>
#include <flux/core.h>
#include <czmq.h>
#if HAVE_CALIPER
#include <caliper/cali.h>
#include <sys/syscall.h>
#endif

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/kary.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "src/common/libsubprocess/zio.h"
#include "src/common/libsubprocess/subprocess.h"

#include "heartbeat.h"
#include "module.h"
#include "overlay.h"
#include "service.h"
#include "hello.h"
#include "shutdown.h"
#include "attr.h"
#include "sequence.h"
#include "log.h"
#include "content-cache.h"
#include "runlevel.h"
#include "heaptrace.h"
#include "exec.h"
#include "ping.h"
#include "rusage.h"

/* Generally accepted max, although some go higher (IE is 2083) */
#define ENDPOINT_MAX 2048

typedef enum {
    ERROR_MODE_RESPOND,
    ERROR_MODE_RETURN,
} request_error_mode_t;

struct tbon_param {
    int k;
    int level;
    int maxlevel;
    int descendants;
};

typedef struct {
    /* 0MQ
     */
    flux_sec_t *sec;             /* security context (MT-safe) */

    /* Reactor
     */
    flux_t *h;
    flux_reactor_t *reactor;
    zlist_t *sigwatchers;

    /* Sockets.
     */
    overlay_t *overlay;

    /* Session parameters
     */
    uint32_t size;              /* session size */
    uint32_t rank;              /* our rank in session */
    attr_t *attrs;
    uint32_t userid;            /* instance owner */
    uint32_t rolemask;

    /* Modules
     */
    modhash_t *modhash;
    /* Misc
     */
    bool verbose;
    bool quiet;
    pid_t pid;
    int event_recv_seq;
    int event_send_seq;
    bool event_active;          /* primary event source is active */
    svchash_t *services;
    heartbeat_t *heartbeat;
    shutdown_t *shutdown;
    double shutdown_grace;
    zlist_t *subscriptions;     /* subscripts for internal services */
    content_cache_t *cache;
    struct tbon_param tbon;
    /* Bootstrap
     */
    hello_t *hello;
    flux_t *enclosing_h;
    runlevel_t *runlevel;

    /* Subprocess management
     */
    struct subprocess_manager *sm;

    char *init_shell_cmd;
    size_t init_shell_cmd_len;
    struct subprocess *init_shell;
} broker_ctx_t;

static int broker_event_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg);
static int broker_response_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg);
static int broker_request_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg,
                                   request_error_mode_t errmode);

static void event_cb (overlay_t *ov, void *sock, void *arg);
static void parent_cb (overlay_t *ov, void *sock, void *arg);
static void child_cb (overlay_t *ov, void *sock, void *arg);
static void module_cb (module_t *p, void *arg);
static void module_status_cb (module_t *p, int prev_state, void *arg);
static void hello_update_cb (hello_t *h, void *arg);
static void shutdown_cb (shutdown_t *s, bool expired, void *arg);
static void signal_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg);
static void broker_handle_signals (broker_ctx_t *ctx, zlist_t *sigwatchers);
static void broker_unhandle_signals (zlist_t *sigwatchers);

static void broker_add_services (broker_ctx_t *ctx);

static int load_module_byname (broker_ctx_t *ctx, const char *name,
                               const char *argz, size_t argz_len,
                               const flux_msg_t *request);
static int unload_module_byname (broker_ctx_t *ctx, const char *name,
                                 const flux_msg_t *request, bool async);

static void set_proctitle (uint32_t rank);
static void runlevel_cb (runlevel_t *r, int level, int rc, double elapsed,
                         const char *state, void *arg);
static void runlevel_io_cb (runlevel_t *r, const char *name,
                            const char *msg, void *arg);

static int create_persistdir (attr_t *attrs, uint32_t rank);
static int create_rundir (attr_t *attrs);
static int create_dummyattrs (flux_t *h, uint32_t rank, uint32_t size);

static char *calc_endpoint (broker_ctx_t *ctx, const char *endpoint);

static int boot_pmi (broker_ctx_t *ctx, double *elapsed_sec);

static int attr_get_overlay (const char *name, const char **val, void *arg);

static void init_attrs (broker_ctx_t *ctx);

static const struct flux_handle_ops broker_handle_ops;

static int exit_rc = 0;

#define OPTIONS "+vqM:X:k:s:g:EIS:"
static const struct option longopts[] = {
    {"verbose",         no_argument,        0, 'v'},
    {"quiet",           no_argument,        0, 'q'},
    {"security",        required_argument,  0, 's'},
    {"module-path",     required_argument,  0, 'X'},
    {"k-ary",           required_argument,  0, 'k'},
    {"heartrate",       required_argument,  0, 'H'},
    {"shutdown-grace",  required_argument,  0, 'g'},
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
    broker_ctx_t ctx;
    zlist_t *sigwatchers;
    int sec_typemask = FLUX_SEC_TYPE_CURVE | FLUX_SEC_TYPE_MUNGE;
    int e;
    char *endptr;
    sigset_t old_sigmask;
    struct sigaction old_sigact_int;
    struct sigaction old_sigact_term;

    memset (&ctx, 0, sizeof (ctx));
    log_init (argv[0]);

    if (!(sigwatchers = zlist_new ()))
        oom ();

    ctx.rank = FLUX_NODEID_ANY;
    ctx.modhash = modhash_create ();
    ctx.services = svchash_create ();
    ctx.overlay = overlay_create ();
    ctx.hello = hello_create ();
    ctx.tbon.k = 2; /* binary TBON is default */
    ctx.heartbeat = heartbeat_create ();
    ctx.shutdown = shutdown_create ();
    ctx.attrs = attr_create ();
    if (!(ctx.subscriptions = zlist_new ()))
        oom ();
    if (!(ctx.cache = content_cache_create ()))
        oom ();
    if (!(ctx.runlevel = runlevel_create ()))
        oom ();

    ctx.pid = getpid();

    init_attrs (&ctx);

    if (!(ctx.sm = subprocess_manager_create ()))
        oom ();
    subprocess_manager_set (ctx.sm, SM_WAIT_FLAGS, WNOHANG);

    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
            case 's':   /* --security=MODE */
                if (!strcmp (optarg, "none")) {
                    sec_typemask = 0;
                } else if (!strcmp (optarg, "plain")) {
                    sec_typemask |= FLUX_SEC_TYPE_PLAIN;
                    sec_typemask &= ~FLUX_SEC_TYPE_CURVE;
                } else if (!strcmp (optarg, "curve")) {
                    sec_typemask |= FLUX_SEC_TYPE_CURVE;
                    sec_typemask &= ~FLUX_SEC_TYPE_PLAIN;
                } else {
                    log_msg_exit ("--security arg must be none|plain|curve");
                }
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
                errno = 0;
                ctx.tbon.k = strtoul (optarg, &endptr, 10);
                if (errno || *endptr != '\0')
                    log_err_exit ("k-ary '%s'", optarg);
                if (ctx.tbon.k < 1)
                    usage ();
                break;
            case 'H':   /* --heartrate SECS */
                if (heartbeat_set_ratestr (ctx.heartbeat, optarg) < 0)
                    log_err_exit ("heartrate `%s'", optarg);
                break;
            case 'g':   /* --shutdown-grace SECS */
                errno = 0;
                ctx.shutdown_grace = strtod (optarg, &endptr);
                if (errno || *endptr != '\0')
                    log_err_exit ("shutdown-grace '%s'", optarg);
                if (ctx.shutdown_grace < 0)
                    usage ();
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
        if ((e = argz_create (argv + optind, &ctx.init_shell_cmd,
                                             &ctx.init_shell_cmd_len)) != 0)
            log_errn_exit (e, "argz_create");
    }

    /* Record the instance owner: the effective uid of the broker.
     * Set default rolemask for messages sent with flux_send()
     * on the broker's internal handle.
     */
    ctx.userid = geteuid ();
    ctx.rolemask = FLUX_ROLE_OWNER;

    /* Connect to enclosing instance, if any.
     */
    if (getenv ("FLUX_URI")) {
        if (!(ctx.enclosing_h = flux_open (NULL, 0)))
            log_err_exit ("flux_open enclosing instance");
    }

    /* Block all signals, saving old mask and actions for SIGINT, SIGTERM.
     */
    sigset_t sigmask;
    sigfillset (&sigmask);
    if (sigprocmask (SIG_SETMASK, &sigmask, &old_sigmask) < 0)
        log_err_exit ("sigprocmask");
    if (sigaction (SIGINT, NULL, &old_sigact_int) < 0)
        log_err_exit ("sigaction");
    if (sigaction (SIGTERM, NULL, &old_sigact_term) < 0)
        log_err_exit ("sigaction");

    /* Initailize zeromq context
     */
    if (!zsys_init ())
        log_err_exit ("zsys_init");
    zsys_set_logstream (stderr);
    zsys_set_logident ("flux-broker");
    zsys_handler_set (NULL);
    zsys_set_linger (5);
    zsys_set_rcvhwm (0);
    zsys_set_sndhwm (0);

    /* Set up the flux reactor.
     */
    if (!(ctx.reactor = flux_reactor_create (FLUX_REACTOR_SIGCHLD)))
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
    const char *keydir;
    if (attr_get (ctx.attrs, "security.keydir", &keydir, NULL) < 0)
        log_err_exit ("getattr security.keydir");
    if (!(ctx.sec = flux_sec_create (sec_typemask, keydir)))
        log_err_exit ("flux_sec_create");
    if (flux_sec_comms_init (ctx.sec) < 0)
        log_msg_exit ("flux_sec_comms_init: %s", flux_sec_errstr (ctx.sec));

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

    /* Create/validate runtime directory (this function is idempotent)
     */
    if (create_rundir (ctx.attrs) < 0)
        log_err_exit ("create_rundir");
    /* If persist-filesystem or persist-directory are set, initialize those,
     * but only on rank 0.
     */
    if (create_persistdir (ctx.attrs, ctx.rank) < 0)
        log_err_exit ("create_persistdir");

    /* Initialize logging.
     * OK to call flux_log*() after this.
     */
    logbuf_initialize (ctx.h, ctx.rank, ctx.attrs);

    /* Allow flux_get_rank() and flux_get_size() to work in the broker.
     */
    if (create_dummyattrs (ctx.h, ctx.rank, ctx.size) < 0)
        log_err_exit ("creating dummy attributes");

    overlay_set_rank (ctx.overlay, ctx.rank);

    /* Registers message handlers and obtains rank.
     */
    if (content_cache_set_flux (ctx.cache, ctx.h) < 0)
        log_err_exit ("content_cache_set_flux");

    content_cache_set_enclosing_flux (ctx.cache, ctx.enclosing_h);

    /* Configure attributes.
     */
    if (attr_add_active (ctx.attrs, "tbon.parent-endpoint", 0,
                                attr_get_overlay, NULL, ctx.overlay) < 0
            || attr_add_active (ctx.attrs, "mcast.relay-endpoint",
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

    /* If Flux was launched by Flux, now that PMI bootstrap is complete,
     * unset Flux job environment variables since they don't leak into
     * the jobs other children of this instance.
     */
    unsetenv ("FLUX_JOB_ID");
    unsetenv ("FLUX_JOB_SIZE");
    unsetenv ("FLUX_JOB_NNODES");

    /* If shutdown_grace was not provided on the command line,
     * make a guess.
     */
    if (ctx.shutdown_grace == 0) {
        if (ctx.size < 16)
            ctx.shutdown_grace = 1;
        else if (ctx.size < 128)
            ctx.shutdown_grace = 2;
        else if (ctx.size < 1024)
            ctx.shutdown_grace = 4;
        else
            ctx.shutdown_grace = 10;
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

    set_proctitle (ctx.rank);

    if (ctx.rank == 0) {
        const char *rc1, *rc3, *pmi, *uri;
        const char *rc2 = ctx.init_shell_cmd;
        size_t rc2_len = ctx.init_shell_cmd_len;

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

        if (runlevel_set_rc (ctx.runlevel, 1, rc1, rc1 ? strlen (rc1) + 1 : 0, uri) < 0)
            log_err_exit ("runlevel_set_rc 1");
        if (runlevel_set_rc (ctx.runlevel, 2, rc2, rc2_len, uri) < 0)
            log_err_exit ("runlevel_set_rc 2");
        if (runlevel_set_rc (ctx.runlevel, 3, rc3, rc3 ? strlen (rc3) + 1 : 0, uri) < 0)
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

    {
        const char *rundir;
        if (attr_get (ctx.attrs, "broker.rundir", &rundir, NULL) < 0) {
            log_msg_exit ("broker.rundir attribute is not set");
        }
    }

    shutdown_set_handle (ctx.shutdown, ctx.h);
    shutdown_set_callback (ctx.shutdown, shutdown_cb, &ctx);

    /* Register internal services
     */
    if (attr_register_handlers (ctx.attrs, ctx.h) < 0)
        log_err_exit ("attr_register_handlers");
    if (heaptrace_initialize (ctx.h) < 0)
        log_msg_exit ("heaptrace_initialize");
    if (sequence_hash_initialize (ctx.h) < 0)
        log_err_exit ("sequence_hash_initialize");
    if (exec_initialize (ctx.h, ctx.sm, ctx.rank, ctx.attrs) < 0)
        log_err_exit ("exec_initialize");
    if (ping_initialize (ctx.h, "cmb") < 0)
        log_err_exit ("ping_initialize");
    if (rusage_initialize (ctx.h, "cmb") < 0)
        log_err_exit ("rusage_initialize");

    broker_add_services (&ctx);

    /* Initialize comms module infrastructure.
     */
    if (ctx.verbose)
        log_msg ("initializing modules");
    modhash_set_rank (ctx.modhash, ctx.rank);
    modhash_set_flux (ctx.modhash, ctx.h);
    modhash_set_heartbeat (ctx.modhash, ctx.heartbeat);
    /* Load the local connector module.
     * Other modules will be loaded in rc1 using flux module,
     * which uses the local connector.
     */
    if (ctx.verbose)
        log_msg ("loading connector-local");
    if (load_module_byname (&ctx, "connector-local", NULL, 0, NULL) < 0)
        log_err_exit ("load_module connector-local");

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

    /* Restore default sigmask and actions for SIGINT, SIGTERM
     */
    if (sigprocmask (SIG_SETMASK, &old_sigmask, NULL) < 0)
        log_err_exit ("sigprocmask");
    if (sigaction (SIGINT, &old_sigact_int, NULL) < 0)
        log_err_exit ("sigaction");
    if (sigaction (SIGTERM, &old_sigact_term, NULL) < 0)
        log_err_exit ("sigaction");

    /* remove heartbeat timer, if any
     */
    heartbeat_stop (ctx.heartbeat);

    /* Unload modules.
     */
    if (ctx.verbose)
        log_msg ("unloading connector-local");
    if (unload_module_byname (&ctx, "connector-local", NULL, false) < 0)
        log_err ("unload connector-local");
    if (ctx.verbose)
        log_msg ("finalizing modules");
    modhash_destroy (ctx.modhash);

    /* Unregister builtin services
     */
    attr_unregister_handlers ();
    content_cache_destroy (ctx.cache);

    broker_unhandle_signals (sigwatchers);
    zlist_destroy (&sigwatchers);

    if (ctx.verbose)
        log_msg ("cleaning up");
    if (ctx.enclosing_h)
        flux_close (ctx.enclosing_h);
    if (ctx.sec)
        flux_sec_destroy (ctx.sec);
    overlay_destroy (ctx.overlay);
    heartbeat_destroy (ctx.heartbeat);
    svchash_destroy (ctx.services);
    hello_destroy (ctx.hello);
    attr_destroy (ctx.attrs);
    flux_close (ctx.h);
    flux_reactor_destroy (ctx.reactor);
    if (ctx.subscriptions) {
        char *s;
        while ((s = zlist_pop (ctx.subscriptions)))
            free (s);
        zlist_destroy (&ctx.subscriptions);
    }
    runlevel_destroy (ctx.runlevel);
    free (ctx.init_shell_cmd);
    subprocess_manager_destroy (ctx.sm);

    return exit_rc;
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
    { NULL, NULL, 0 },
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

static void init_attrs_overlay (broker_ctx_t *ctx)
{
    char *tbonendpoint = "tbon.endpoint";
    char *mcastendpoint = "mcast.endpoint";

    if (attr_add (ctx->attrs,
                  tbonendpoint,
                  "tcp://%h:*",
                  0) < 0)
        log_err_exit ("attr_add %s", tbonendpoint);

    if (attr_add (ctx->attrs,
                  mcastendpoint,
                  "tbon",
                  0) < 0)
        log_err_exit ("attr_add %s", mcastendpoint);
}

static void init_attrs_broker_pid (broker_ctx_t *ctx)
{
    char *attrname = "broker.pid";
    char *pidval;

    pidval = xasprintf ("%u", ctx->pid);
    if (attr_add (ctx->attrs,
                  attrname,
                  pidval,
                  FLUX_ATTRFLAG_IMMUTABLE) < 0)
        log_err_exit ("attr_add %s", attrname);
    free (pidval);
}

static void init_attrs (broker_ctx_t *ctx)
{
    /* Initialize config attrs from environment set up by flux(1)
     */
    init_attrs_from_environment (ctx->attrs);

    /* Initialize other miscellaneous attrs
     */
    init_attrs_overlay (ctx);
    init_attrs_broker_pid (ctx);
}

static void hello_update_cb (hello_t *hello, void *arg)
{
    broker_ctx_t *ctx = arg;

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
    broker_ctx_t *ctx = arg;
    if (expired) {
        if (ctx->rank == 0)
            exit_rc = shutdown_get_rc (s);
        flux_reactor_stop (flux_get_reactor (ctx->h));
    }
}

static void set_proctitle (uint32_t rank)
{
    static char proctitle[32];
    snprintf (proctitle, sizeof (proctitle), "flux-broker-%"PRIu32, rank);
    (void)prctl (PR_SET_NAME, proctitle, 0, 0, 0);
}

/* Handle line by line output on stdout, stderr of runlevel subprocess.
 */
static void runlevel_io_cb (runlevel_t *r, const char *name,
                            const char *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    int loglevel = !strcmp (name, "stderr") ? LOG_ERR : LOG_INFO;
    int runlevel = runlevel_get_level (r);

    flux_log (ctx->h, loglevel, "rc%d: %s", runlevel, msg);
}

/* Handle completion of runlevel subprocess.
 */
static void runlevel_cb (runlevel_t *r, int level, int rc, double elapsed,
                         const char *exit_string, void *arg)
{
    broker_ctx_t *ctx = arg;
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

static int create_dummyattrs (flux_t *h, uint32_t rank, uint32_t size)
{
    char *s;
    s = xasprintf ("%"PRIu32, rank);
    if (flux_attr_fake (h, "rank", s, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    free (s);

    s = xasprintf ("%"PRIu32, size);
    if (flux_attr_fake (h, "size", s, FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    free (s);

    return 0;
}

/* If user set the 'broker.rundir' attribute on the command line,
 * validate the directory and its permissions, and set the immutable flag
 * on the attribute.  If unset, a unique directory and arrange to remove
 * it on exit.  This function is idempotent.
 */
static int create_rundir (attr_t *attrs)
{
    const char *run_dir, *local_uri;
    char *dir = NULL;
    char *uri = NULL;
    int rc = -1;

    if (attr_get (attrs, "broker.rundir", &run_dir, NULL) == 0) {
        struct stat sb;
        if (stat (run_dir, &sb) < 0)
            goto done;
        if (!S_ISDIR (sb.st_mode)) {
            errno = ENOTDIR;
            goto done;
        }
        if ((sb.st_mode & S_IRWXU) != S_IRWXU) {
            errno = EPERM;
            goto done;
        }
        if (attr_set_flags (attrs, "broker.rundir", FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    } else {
        const char *tmpdir = getenv ("TMPDIR");
        dir = xasprintf ("%s/flux-XXXXXX", tmpdir ? tmpdir : "/tmp");
        if (!mkdtemp (dir))
            goto done;
        cleanup_push_string (cleanup_directory, dir);
        if (attr_add (attrs, "broker.rundir", dir, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
        run_dir = dir;
    }
    if (attr_get (attrs, "local-uri", &local_uri, NULL) < 0) {
        uri = xasprintf ("local://%s", run_dir);
        if (attr_add (attrs, "local-uri", uri,
                                            FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }
    rc = 0;
done:
    free (dir);
    free (uri);
    return rc;
}

/* If 'persist-directory' set, validate it, make it immutable, done.
 * If 'persist-filesystem' set, validate it, make it immutable, then:
 * Avoid name collisions with other flux tmpdirs used in testing
 * e.g. "flux-<sid>-XXXXXX"
 */
static int create_persistdir (attr_t *attrs, uint32_t rank)
{
    struct stat sb;
    const char *attr = "persist-directory";
    const char *sid, *persist_dir, *persist_fs;
    char *dir, *tmpl = NULL;
    int rc = -1;

    if (rank > 0) {
        (void) attr_delete (attrs, "persist-filesystem", true);
        (void) attr_delete (attrs, "persist-directory", true);
        goto done_success;
    }
    if (attr_get (attrs, attr, &persist_dir, NULL) == 0) {
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
        if (attr_set_flags (attrs, attr, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    } else {
        if (attr_get (attrs, "session-id", &sid, NULL) < 0) {
            errno = EINVAL;
            goto done;
        }
        if (attr_get (attrs, "persist-filesystem", &persist_fs, NULL)< 0) {
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
        if (attr_set_flags (attrs, "persist-filesystem",
                                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
        tmpl = xasprintf ("%s/fluxP-%s-XXXXXX", persist_fs, sid);
        if (!(dir = mkdtemp (tmpl)))
            goto done;
        if (attr_add (attrs, attr, dir, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }
done_success:
    if (attr_get (attrs, "persist-filesystem", NULL, NULL) < 0) {
        if (attr_add (attrs, "persist-filesystem", NULL,
                                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }
    if (attr_get (attrs, "persist-directory", NULL, NULL) < 0) {
        if (attr_add (attrs, "persist-directory", NULL,
                                                FLUX_ATTRFLAG_IMMUTABLE) < 0)
            goto done;
    }
    rc = 0;
done:
    if (tmpl)
        free (tmpl);
    return rc;
}

/* Given a string with possible format specifiers, return string that is
 * fully expanded.
 *
 * Possible format specifiers:
 * - %h - IP address of current hostname
 * - %B - value of attribute broker.rundir
 *
 * Caller is responsible for freeing memory of returned value.
 */
static char * calc_endpoint (broker_ctx_t *ctx, const char *endpoint)
{
    char ipaddr[HOST_NAME_MAX + 1];
    char *ptr, *buf, *rv = NULL;
    bool percent_flag = false;
    unsigned int len = 0;
    const char *rundir;

    buf = xzmalloc (ENDPOINT_MAX + 1);

    ptr = (char *)endpoint;
    while (*ptr) {
        if (percent_flag) {
            if (*ptr == 'h') {
                ipaddr_getprimary (ipaddr, sizeof (ipaddr));
                if ((len + strlen (ipaddr)) > ENDPOINT_MAX) {
                    log_msg ("ipaddr overflow max endpoint length");
                    goto done;
                }
                strcat (buf, ipaddr);
                len += strlen (ipaddr);
            }
            else if (*ptr == 'B') {
                if (attr_get (ctx->attrs, "broker.rundir", &rundir, NULL) < 0) {
                    log_msg ("broker.rundir attribute is not set");
                    goto done;
                }
                if ((len + strlen (rundir)) > ENDPOINT_MAX) {
                    log_msg ("broker.rundir overflow max endpoint length");
                    goto done;
                }
                strcat (buf, rundir);
                len += strlen (rundir);
            }
            else if (*ptr == '%')
                buf[len++] = '%';
            else {
                buf[len++] = '%';
                buf[len++] = *ptr;
            }
            percent_flag = false;
        }
        else {
            if (*ptr == '%')
                percent_flag = true;
            else
                buf[len++] = *ptr;
        }

        if (len >= ENDPOINT_MAX) {
            log_msg ("overflow max endpoint length");
            goto done;
        }

        ptr++;
    }

    rv = buf;
done:
    if (!rv)
        free (buf);
    return (rv);
}

static int boot_pmi (broker_ctx_t *ctx, double *elapsed_sec)
{
    int spawned, size, rank, appnum;
    int relay_rank = -1, parent_rank;
    int clique_size;
    int *clique_ranks = NULL;
    const char *child_uri, *relay_uri;
    int kvsname_len, key_len, val_len;
    char *id = NULL;
    char *kvsname = NULL;
    char *key = NULL;
    char *val = NULL;
    int e, rc = -1;
    struct timespec start_time;
    const char *attrtbonendpoint;
    char *tbonendpoint = NULL;
    const char *attrmcastendpoint;
    char *mcastendpoint = NULL;

    monotime (&start_time);

    if ((e = PMI_Init (&spawned)) != PMI_SUCCESS) {
        log_msg ("PMI_Init: %s", pmi_strerror (e));
        goto done;
    }

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

    /* Initialize rundir
     */
    if (create_rundir (ctx->attrs) < 0) {
        log_err ("could not initialize rundir");
        goto done;
    }

    /* Set TBON endpoint and mcast endpoint based on user settings
     */

    if (attr_get (ctx->attrs, "tbon.endpoint", &attrtbonendpoint, NULL) < 0) {
        log_err ("tbon.endpoint is not set");
        goto done;
    }

    if (!(tbonendpoint = calc_endpoint (ctx, attrtbonendpoint))) {
        log_msg ("calc_endpoint error");
        goto done;
    }

    if (attr_set (ctx->attrs, "tbon.endpoint", tbonendpoint, true) < 0) {
        log_err ("tbon.endpoint could not be set");
        goto done;
    }

    overlay_set_child (ctx->overlay, tbonendpoint);

    if (attr_get (ctx->attrs, "mcast.endpoint", &attrmcastendpoint, NULL) < 0) {
        log_err ("mcast.endpoint is not set");
        goto done;
    }

    if (!(mcastendpoint = calc_endpoint (ctx, attrmcastendpoint))) {
        log_msg ("calc_endpoint error");
        goto done;
    }

    if (attr_set (ctx->attrs, "mcast.endpoint", mcastendpoint, true) < 0) {
        log_err ("mcast.endpoint could not be set");
        goto done;
    }

    /* Set up multicast (e.g. epgm) relay if multiple ranks are being
     * spawned per node, as indicated by "clique ranks".  FIXME: if
     * pmi_get_clique_ranks() is not implemented, this fails.  Find an
     * alternate method to determine if ranks are co-located on a
     * node.
     */
    if (strcasecmp (mcastendpoint, "tbon")) {
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
                const char *rundir;
                char *relayfile = NULL;

                if (attr_get (ctx->attrs, "broker.rundir", &rundir, NULL) < 0) {
                    log_msg ("broker.rundir attribute is not set");
                    goto done;
                }

                relayfile = xasprintf ("%s/relay", rundir);
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

    /* Write the uri of the multicast (e.g. epgm) relay under the rank
     * (if any).
     */
    if (strcasecmp (mcastendpoint, "tbon")
        && (relay_uri = overlay_get_relay (ctx->overlay))) {
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
        overlay_set_parent (ctx->overlay, "%s", val);
    }

    /* Event distribution (four configurations):
     * 1) multicast enabled, one broker per node
     *    All brokers subscribe to the same epgm address.
     * 2) multicast enabled, mutiple brokers per node The lowest rank
     *    in each clique will subscribe to the multicast
     *    (e.g. epgm://) socket and relay events to an ipc:// socket
     *    for the other ranks in the clique.  This is necessary due to
     *    limitation of epgm.
     * 3) multicast disabled, all brokers concentrated on one node
     *    Rank 0 publishes to a ipc:// socket, other ranks subscribe (set earlier via mcast.endpoint)
     * 4) multicast disabled brokers distributed across nodes
     *    No dedicated event overlay,.  Events are distributed over the TBON.
     */
    if (strcasecmp (mcastendpoint, "tbon")) {
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
        } else
            overlay_set_event (ctx->overlay, mcastendpoint);
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
    if (tbonendpoint)
        free (tbonendpoint);
    if (mcastendpoint)
        free (mcastendpoint);
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

static int mod_svc_cb (const flux_msg_t *msg, void *arg)
{
    module_t *p = arg;
    int rc = module_sendmsg (p, msg);
    return rc;
}

static int load_module_bypath (broker_ctx_t *ctx, const char *path,
                               const char *argz, size_t argz_len,
                               const flux_msg_t *request)
{
    module_t *p = NULL;
    char *name, *arg;

    if (!(name = flux_modname (path))) {
        errno = ENOENT;
        goto error;
    }
    if (!(p = module_add (ctx->modhash, path)))
        goto error;
    if (!svc_add (ctx->services, module_get_name (p),
                                 module_get_service (p), mod_svc_cb, p)) {
        errno = EEXIST;
        goto error;
    }
    arg = argz_next (argz, argz_len, NULL);
    while (arg) {
        module_add_arg (p, arg);
        arg = argz_next (argz, argz_len, arg);
    }
    module_set_poller_cb (p, module_cb, ctx);
    module_set_status_cb (p, module_status_cb, ctx);
    if (request && module_push_insmod (p, request) < 0) // response deferred
        goto error;
    if (module_start (p) < 0)
        goto error;
    flux_log (ctx->h, LOG_DEBUG, "insmod %s", name);
    free (name);
    return 0;
error:
    if (p)
        module_remove (ctx->modhash, p);
    free (name);
    return -1;
}

static int load_module_byname (broker_ctx_t *ctx, const char *name,
                               const char *argz, size_t argz_len,
                               const flux_msg_t *request)
{
    const char *modpath;
    char *path;

    if (attr_get (ctx->attrs, "conf.module_path", &modpath, NULL) < 0) {
        log_msg ("conf.module_path is not set");
        return -1;
    }
    if (!(path = flux_modfind (modpath, name))) {
        log_msg ("%s: not found in module search path", name);
        return -1;
    }
    if (load_module_bypath (ctx, path, argz, argz_len, request) < 0) {
        free (path);
        return -1;
    }
    free (path);
    return 0;
}

/* If 'async' is true, service de-registration and module
 * destruction (including join) are deferred until module keepalive
 * status indicates module main() has exited (via module_status_cb).
 * This allows modules with distributed shutdown to talk to each
 * other while they shut down, and also does not block the reactor
 * from handling other events.  If 'async' is false, do all that
 * teardown synchronously here.
 */
static int unload_module_byname (broker_ctx_t *ctx, const char *name,
                                 const flux_msg_t *request, bool async)
{
    module_t *p;

    if (!(p = module_lookup_byname (ctx->modhash, name))) {
        errno = ENOENT;
        return -1;
    }
    if (module_stop (p) < 0)
        return -1;
    if (async) {
        if (request && module_push_rmmod (p, request) < 0)
            return -1;
    } else {
        assert (request == NULL);
        svc_remove (ctx->services, module_get_name (p));
        module_remove (ctx->modhash, p);
    }
    flux_log (ctx->h, LOG_DEBUG, "rmmod %s", name);
    return 0;
}

static void broker_handle_signals (broker_ctx_t *ctx, zlist_t *sigwatchers)
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

static int attr_get_overlay (const char *name, const char **val, void *arg)
{
    overlay_t *overlay = arg;
    int rc = -1;

    if (!strcmp (name, "tbon.parent-endpoint"))
        *val = overlay_get_parent (overlay);
    else if (!strcmp (name, "mcast.relay-endpoint"))
        *val = overlay_get_relay (overlay);
    else {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

/**
 ** Built-in services
 **/

static void cmb_rmmod_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *json_str;
    char *name = NULL;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (!json_str) {
        errno = EPROTO;
        goto error;
    }
    if (flux_rmmod_json_decode (json_str, &name) < 0)
        goto error;
    if (unload_module_byname (ctx, name, msg, true) < 0)
        goto error;
    free (name);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (name);
}

static void cmb_insmod_cb (flux_t *h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *json_str;
    char *path = NULL;
    char *argz = NULL;
    size_t argz_len = 0;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto error;
    if (!json_str) {
        errno = EPROTO;
        goto error;
    }
    if (flux_insmod_json_decode (json_str, &path, &argz, &argz_len) < 0)
        goto error;
    if (load_module_bypath (ctx, path, argz, argz_len, msg) < 0)
        goto error;
    free (path);
    free (argz);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (path);
    free (argz);
}

static void cmb_lsmod_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    flux_modlist_t *mods = NULL;
    char *json_str = NULL;

    if (!(mods = module_get_modlist (ctx->modhash)))
        goto error;
    if (!(json_str = flux_lsmod_json_encode (mods)))
        goto error;
    if (flux_respond (h, msg, 0, json_str) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (json_str);
    if (mods)
        flux_modlist_destroy (mods);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (json_str);
    if (mods)
        flux_modlist_destroy (mods);
}

static void cmb_lspeer_cb (flux_t *h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    char *out;

    if (!(out = overlay_lspeer_encode (ctx->overlay))) {
        if (flux_respond (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
        return;
    }
    if (flux_respond (h, msg, 0, out) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (out);
}

static void cmb_panic_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    const char *s = NULL;

    if (flux_request_decodef (msg, NULL, "{}") < 0)
        goto error;
    if (flux_request_decodef (msg, NULL, "{ s:s }", "msg", &s) < 0)
        s = "no reason";
    log_msg_exit ("PANIC: %s", s ? s : "no reason");
    /*NOTREACHED*/
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void cmb_event_mute_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    char *uuid = NULL;

    if (flux_msg_get_route_last (msg, &uuid) == 0)
        overlay_mute_child (ctx->overlay, uuid);
    free (uuid);
    /* no response */
}

static void cmb_disconnect_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    char *sender = NULL;;

    if (flux_msg_get_route_first (msg, &sender) == 0) {
        exec_terminate_subprocesses_by_uuid (h, sender);
        free (sender);
    }
    /* no response */
}

static void cmb_sub_cb (flux_t *h, flux_msg_handler_t *w,
                        const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    char *uuid = NULL;
    const char *topic;

    if (flux_request_decodef (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        goto error;
    if (flux_msg_get_route_first (msg, &uuid) < 0)
        goto error;
    if (!uuid) {
        errno = EPROTO;
        goto error;
    }
    if (module_subscribe (ctx->modhash, uuid, topic) < 0)
        goto error;
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (uuid);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (uuid);
}

static void cmb_unsub_cb (flux_t *h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    char *uuid = NULL;
    const char *topic;

    if (flux_request_decodef (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        goto error;
    if (flux_msg_get_route_first (msg, &uuid) < 0)
        goto error;
    if (!uuid) {
        errno = EPROTO;
        goto error;
    }
    if (module_unsubscribe (ctx->modhash, uuid, topic) < 0)
        goto error;
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (uuid);
    return;
error:
    if (flux_respond (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (uuid);
}

static int route_to_handle (const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    if (flux_requeue (ctx->h, msg, FLUX_RQ_TAIL) < 0)
        flux_log_error (ctx->h, "%s: flux_requeue\n", __FUNCTION__);
    return 0;
}

static struct flux_msg_handler_spec handlers[] = {
    { FLUX_MSGTYPE_REQUEST, "cmb.rmmod",      cmb_rmmod_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.insmod",     cmb_insmod_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.lsmod",      cmb_lsmod_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.lspeer",     cmb_lspeer_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.panic",      cmb_panic_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.event-mute", cmb_event_mute_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.disconnect", cmb_disconnect_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.sub",        cmb_sub_cb, 0, NULL },
    { FLUX_MSGTYPE_REQUEST, "cmb.unsub",      cmb_unsub_cb, 0, NULL },
    FLUX_MSGHANDLER_TABLE_END,
};

struct internal_service {
    const char *topic;
    const char *nodeset;
};

static struct internal_service services[] = {
    { "cmb.rusage",         NULL },
    { "cmb.ping",           NULL },
    { "cmb.exec",           NULL },
    { "cmb.exec.signal",    NULL },
    { "cmb.exec.write",     NULL },
    { "cmb.processes",      NULL },
    { "log",                NULL },
    { "seq",                "[0]" },
    { "content",            NULL },
    { "hello",              NULL },
    { "attr",               NULL },
    { "heaptrace",          NULL },
    { NULL, NULL, },
};

/* Register builtin services (sharing ctx->h and broker thread).
 * First loop is for services that are registered in other files.
 * Second loop is for services registered here.
 */
static void broker_add_services (broker_ctx_t *ctx)
{
    struct internal_service *svc;
    for (svc = &services[0]; svc->topic != NULL; svc++) {
        if (!nodeset_member (svc->nodeset, ctx->rank))
            continue;
        if (!svc_add (ctx->services, svc->topic, NULL, route_to_handle, ctx))
            log_err_exit ("error registering service for %s", svc->topic);
    }

    struct flux_msg_handler_spec *spec;
    for (spec = &handlers[0]; spec->topic_glob != NULL; spec++) {
        if (!svc_add (ctx->services, spec->topic_glob, NULL,
                      route_to_handle, ctx))
            log_err_exit ("error registering service for %s", spec->topic_glob);
    }
    if (flux_msg_handler_addvec (ctx->h, handlers, ctx) < 0)
        log_err_exit ("error registering message handlers");
}

/**
 ** reactor callbacks
 **/


/* Handle requests from overlay peers.
 */
static void child_cb (overlay_t *ov, void *sock, void *arg)
{
    broker_ctx_t *ctx = arg;
    int type;
    char *uuid = NULL;
    flux_msg_t *msg = flux_msg_recvzsock (sock);

    if (!msg)
        goto done;
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (flux_msg_get_route_last (msg, &uuid) < 0)
        goto done;
    overlay_checkin_child (ctx->overlay, uuid);
    switch (type) {
        case FLUX_MSGTYPE_KEEPALIVE:
            break;
        case FLUX_MSGTYPE_REQUEST:
            (void)broker_request_sendmsg (ctx, msg, ERROR_MODE_RESPOND);
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
            (void)flux_msg_pop_route (msg, NULL);
            (void)flux_msg_pop_route (msg, NULL);
            if (broker_response_sendmsg (ctx, msg) < 0)
                goto done;
            break;
        case FLUX_MSGTYPE_EVENT:
            (void)broker_event_sendmsg (ctx, msg);
            break;
    }
done:
    if (uuid)
        free (uuid);
    flux_msg_destroy (msg);
}

/* helper for event_cb, parent_cb, and (on rank 0) broker_event_sendmsg */
static int handle_event (broker_ctx_t *ctx, const flux_msg_t *msg)
{
    uint32_t seq;
    const char *topic, *s;

    if (flux_msg_get_seq (msg, &seq) < 0
            || flux_msg_get_topic (msg, &topic) < 0) {
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

    (void)overlay_mcast_child (ctx->overlay, msg);
    (void)overlay_sendmsg_relay (ctx->overlay, msg);

    /* Internal services may install message handlers for events.
     */
    s = zlist_first (ctx->subscriptions);
    while (s) {
        if (!strncmp (s, topic, strlen (s))) {
            if (flux_requeue (ctx->h, msg, FLUX_RQ_TAIL) < 0)
                flux_log_error (ctx->h, "%s: flux_requeue\n", __FUNCTION__);
            break;
        }
        s = zlist_next (ctx->subscriptions);
    }
    return module_event_mcast (ctx->modhash, msg);
}

/* Handle messages from one or more parents.
 */
static void parent_cb (overlay_t *ov, void *sock, void *arg)
{
    broker_ctx_t *ctx = arg;
    flux_msg_t *msg = flux_msg_recvzsock (sock);
    int type;

    if (!msg)
        goto done;
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            if (broker_response_sendmsg (ctx, msg) < 0)
                goto done;
            break;
        case FLUX_MSGTYPE_EVENT:
            if (ctx->event_active) {
                flux_rpc_t *rpc;
                if (!(rpc = flux_rpc (ctx->h, "cmb.event-mute", NULL,
                              FLUX_NODEID_UPSTREAM, FLUX_RPC_NORESPONSE)))
                    flux_log_error (ctx->h, "cmb.event-mute RPC");
                goto done;
                flux_rpc_destroy (rpc);
            }
            if (flux_msg_clear_route (msg) < 0) {
                flux_log (ctx->h, LOG_ERR, "dropping malformed event");
                goto done;
            }
            if (handle_event (ctx, msg) < 0)
                goto done;
            break;
        case FLUX_MSGTYPE_REQUEST:
            (void)broker_request_sendmsg (ctx, msg, ERROR_MODE_RESPOND);
            break;
        default:
            flux_log (ctx->h, LOG_ERR, "%s: unexpected %s", __FUNCTION__,
                      flux_msg_typestr (type));
            break;
    }
done:
    flux_msg_destroy (msg);
}

/* Handle messages on the service socket of a comms module.
 */
static void module_cb (module_t *p, void *arg)
{
    broker_ctx_t *ctx = arg;
    flux_msg_t *msg = module_recvmsg (p);
    int type;
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
            (void)broker_request_sendmsg (ctx, msg, ERROR_MODE_RESPOND);
            break;
        case FLUX_MSGTYPE_EVENT:
            if (broker_event_sendmsg (ctx, msg) < 0) {
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
    broker_ctx_t *ctx = arg;
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
    broker_ctx_t *ctx = arg;
    flux_msg_t *msg = overlay_recvmsg_event (ov);
    int type;

    if (!msg)
        goto done;
    ctx->event_active = true;
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_EVENT:
            if (handle_event (ctx, msg) < 0)
                goto done;
            break;
        default:
            flux_log (ctx->h, LOG_ERR, "%s: unexpected %s", __FUNCTION__,
                      flux_msg_typestr (type));
            break;
    }
done:
    flux_msg_destroy (msg);
}

static void signal_cb (flux_reactor_t *r, flux_watcher_t *w,
                         int revents, void *arg)
{
    broker_ctx_t *ctx = arg;
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
static int subvert_sendmsg_child (broker_ctx_t *ctx, const flux_msg_t *msg,
                                  uint32_t nodeid)
{
    flux_msg_t *cpy = flux_msg_copy (msg, true);
    int saved_errno;
    char uuid[16];
    int rc = -1;

    snprintf (uuid, sizeof (uuid), "%"PRIu32, ctx->rank);
    if (flux_msg_push_route (cpy, uuid) < 0)
        goto done;
    snprintf (uuid, sizeof (uuid), "%"PRIu32, nodeid);
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

/* Select error mode for local errors (routing, bad msg, etc) with 'errmode'.
 *
 * ERROR_MODE_RESPOND:
 *    any local errors such as message decoding or routing failure
 *    trigger a response message, and function returns 0.
 * ERROR_MODE_RETURN:
 *    any local errors do not trigger a response, and function
 *    returns -1 with errno set.
 */
static int broker_request_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg,
                                   request_error_mode_t errmode)
{
    uint32_t nodeid, gw;
    int flags;
    int rc = -1;

    if (flux_msg_get_nodeid (msg, &nodeid, &flags) < 0)
        goto error;
    if ((flags & FLUX_MSGFLAG_UPSTREAM) && nodeid == ctx->rank) {
        rc = overlay_sendmsg_parent (ctx->overlay, msg);
        if (rc < 0)
            goto error;
    } else if ((flags & FLUX_MSGFLAG_UPSTREAM) && nodeid != ctx->rank) {
        rc = svc_sendmsg (ctx->services, msg);
        if (rc < 0 && errno == ENOSYS) {
            rc = overlay_sendmsg_parent (ctx->overlay, msg);
            if (rc < 0 && errno == EHOSTUNREACH)
                errno = ENOSYS;
        }
        if (rc < 0)
            goto error;
    } else if (nodeid == FLUX_NODEID_ANY) {
        rc = svc_sendmsg (ctx->services, msg);
        if (rc < 0 && errno == ENOSYS) {
            rc = overlay_sendmsg_parent (ctx->overlay, msg);
            if (rc < 0 && errno == EHOSTUNREACH)
                errno = ENOSYS;
        }
        if (rc < 0)
            goto error;
    } else if (nodeid == ctx->rank) {
        rc = svc_sendmsg (ctx->services, msg);
        if (rc < 0)
            goto error;
    } else if ((gw = kary_child_route (ctx->tbon.k, ctx->size,
                                       ctx->rank, nodeid)) != KARY_NONE) {
        rc = subvert_sendmsg_child (ctx, msg, gw);
        if (rc < 0)
            goto error;
    } else {
        rc = overlay_sendmsg_parent (ctx->overlay, msg);
        if (rc < 0)
            goto error;
    }
    return 0;
error:
    if (errmode == ERROR_MODE_RETURN)
        return -1;
    /* ERROR_MODE_RESPOND */
    (void)flux_respond (ctx->h, msg, errno, NULL);
    return 0;
}

static int broker_response_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg)
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
    snprintf (puuid, sizeof (puuid), "%"PRIu32, parent);

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
static int broker_event_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg)
{
    flux_msg_t *cpy = NULL;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (ctx->rank > 0) {
        if (flux_msg_enable_route (cpy) < 0)
            goto done;
        rc = overlay_sendmsg_parent (ctx->overlay, cpy);
    } else {
        if (flux_msg_clear_route (cpy) < 0)
            goto done;
        if (flux_msg_set_seq (cpy, ++ctx->event_send_seq) < 0)
            goto done;
        if (overlay_sendmsg_event (ctx->overlay, cpy) < 0)
            goto done;
        rc = handle_event (ctx, cpy);
    }
done:
    flux_msg_destroy (cpy);
    return rc;
}

/**
 ** Broker's internal flux_t implementation
 ** N.B. recv() method is missing because messages are "received"
 ** when routing logic calls flux_requeue().
 **/

static int broker_send (void *impl, const flux_msg_t *msg, int flags)
{
    broker_ctx_t *ctx = impl;
    int type;
    uint32_t userid, rolemask;
    flux_msg_t *cpy = NULL;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (flux_msg_get_type (cpy, &type) < 0)
        goto done;
    if (flux_msg_get_userid (cpy, &userid) < 0)
        goto done;
    if (flux_msg_get_rolemask (cpy, &rolemask) < 0)
        goto done;
    if (userid == FLUX_USERID_UNKNOWN)
        userid = ctx->userid;
    if (rolemask == FLUX_ROLE_NONE)
        rolemask = ctx->rolemask;
    if (flux_msg_set_userid (cpy, userid) < 0)
        goto done;
    if (flux_msg_set_rolemask (cpy, rolemask) < 0)
        goto done;

    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            rc = broker_request_sendmsg (ctx, cpy, ERROR_MODE_RETURN);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            rc = broker_response_sendmsg (ctx, cpy);
            break;
        case FLUX_MSGTYPE_EVENT:
            rc = broker_event_sendmsg (ctx, cpy);
            break;
        default:
            errno = EINVAL;
            break;
    }
done:
    flux_msg_destroy (cpy);
    return rc;
}

static int broker_subscribe (void *impl, const char *topic)
{
    broker_ctx_t *ctx = impl;
    char *cpy = NULL;

    if (!(cpy = strdup (topic)))
        goto nomem;
    if (zlist_append (ctx->subscriptions, cpy) < 0)
        goto nomem;
    return 0;
nomem:
    free (cpy);
    errno = ENOMEM;
    return -1;
}

static int broker_unsubscribe (void *impl, const char *topic)
{
    broker_ctx_t *ctx = impl;
    char *s = zlist_first (ctx->subscriptions);
    while (s) {
        if (!strcmp (s, topic)) {
            zlist_remove (ctx->subscriptions, s);
            free (s);
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
