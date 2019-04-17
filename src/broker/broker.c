/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

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
#include <jansson.h>
#if HAVE_CALIPER
#include <caliper/cali.h>
#include <sys/syscall.h>
#endif
#if HAVE_VALGRIND
# if HAVE_VALGRIND_H
#  include <valgrind.h>
# elif HAVE_VALGRIND_VALGRIND_H
#  include <valgrind/valgrind.h>
# endif
#endif

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libidset/idset.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/kary.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/zsecurity.h"
#include "src/common/libpmi/pmi.h"
#include "src/common/libpmi/pmi_strerror.h"
#include "src/common/libutil/fsd.h"

#include "heartbeat.h"
#include "module.h"
#include "overlay.h"
#include "service.h"
#include "hello.h"
#include "shutdown.h"
#include "attr.h"
#include "log.h"
#include "content-cache.h"
#include "runlevel.h"
#include "heaptrace.h"
#include "exec.h"
#include "ping.h"
#include "rusage.h"
#include "boot_config.h"
#include "boot_pmi.h"
#include "publisher.h"

/* Generally accepted max, although some go higher (IE is 2083) */
#define ENDPOINT_MAX 2048

typedef enum {
    ERROR_MODE_RESPOND,
    ERROR_MODE_RETURN,
} request_error_mode_t;

typedef struct {
    /* 0MQ
     */
    zsecurity_t *sec;             /* security context (MT-safe) */

    /* Reactor
     */
    flux_t *h;
    flux_reactor_t *reactor;

    /* Sockets.
     */
    overlay_t *overlay;

    /* Session parameters
     */
    attr_t *attrs;
    uint32_t userid;            /* instance owner */
    uint32_t rolemask;

    /* Modules
     */
    modhash_t *modhash;
    /* Misc
     */
    bool verbose;
    int event_recv_seq;
    struct service_switch *services;
    heartbeat_t *heartbeat;
    shutdown_t *shutdown;
    double shutdown_grace;
    zlist_t *subscriptions;     /* subscripts for internal services */
    content_cache_t *cache;
    struct publisher *publisher;
    int tbon_k;
    /* Bootstrap
     */
    hello_t *hello;
    runlevel_t *runlevel;

    char *init_shell_cmd;
    size_t init_shell_cmd_len;
} broker_ctx_t;

static int broker_event_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg);
static int broker_response_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg);
static int broker_request_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg,
                                   request_error_mode_t errmode);

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

static flux_msg_handler_t **broker_add_services (broker_ctx_t *ctx);
static void broker_remove_services (flux_msg_handler_t *handlers[]);

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
static void create_broker_rundir (overlay_t *ov, void *arg);
static int create_dummyattrs (flux_t *h, uint32_t rank, uint32_t size);

static int handle_event (broker_ctx_t *ctx, const flux_msg_t *msg);

static void init_attrs (attr_t *attrs, pid_t pid);

static const struct flux_handle_ops broker_handle_ops;

static int exit_rc = 0;

#define OPTIONS "+vM:X:k:s:g:EIS:"
static const struct option longopts[] = {
    {"verbose",         no_argument,        0, 'v'},
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
" -X,--module-path PATH        Set module search path (colon separated)\n"
" -s,--security=plain|curve|none    Select security mode (default: curve)\n"
" -k,--k-ary K                 Wire up in a k-ary tree\n"
" -H,--heartrate SECS          Set heartrate in seconds (rank 0 only)\n"
" -g,--shutdown-grace SECS     Set shutdown grace period in seconds\n"
" -S,--setattr ATTR=VAL        Set broker attribute\n"
);
    exit (1);
}

void parse_command_line_arguments(int argc, char *argv[],
                                  broker_ctx_t *ctx, int *sec_typemask)
{
    int c;
    int e;
    char *endptr;

    while ((c = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (c) {
        case 's':   /* --security=MODE */
            if (!strcmp (optarg, "none")) {
                *sec_typemask = 0;
            } else if (!strcmp (optarg, "plain")) {
                *sec_typemask |= ZSECURITY_TYPE_PLAIN;
                *sec_typemask &= ~ZSECURITY_TYPE_CURVE;
            } else if (!strcmp (optarg, "curve")) {
                *sec_typemask |= ZSECURITY_TYPE_CURVE;
                *sec_typemask &= ~ZSECURITY_TYPE_PLAIN;
            } else {
                log_msg_exit ("--security arg must be none|plain|curve");
            }
            break;
        case 'v':   /* --verbose */
            ctx->verbose = true;
            break;
        case 'X':   /* --module-path PATH */
            if (attr_set (ctx->attrs, "conf.module_path", optarg, true) < 0)
                log_err_exit ("setting conf.module_path attribute");
            break;
        case 'k':   /* --k-ary k */
            errno = 0;
            ctx->tbon_k = strtoul (optarg, &endptr, 10);
            if (errno || *endptr != '\0')
                log_err_exit ("k-ary '%s'", optarg);
            if (ctx->tbon_k < 1)
                usage ();
            break;
        case 'H':   /* --heartrate SECS */
            if (heartbeat_set_ratestr (ctx->heartbeat, optarg) < 0)
                log_err_exit ("heartrate `%s'", optarg);
            break;
        case 'g':   /* --shutdown-grace SECS */
            if (fsd_parse_duration (optarg, &ctx->shutdown_grace) < 0) {
                log_err_exit ("shutdown-grace '%s'", optarg);
                usage ();
            }
            break;
        case 'S': { /* --setattr ATTR=VAL */
            char *val, *attr = xstrdup (optarg);
            if ((val = strchr (attr, '=')))
                *val++ = '\0';
            if (attr_add (ctx->attrs, attr, val, 0) < 0)
                if (attr_set (ctx->attrs, attr, val, true) < 0)
                    log_err_exit ("setattr %s=%s", attr, val);
            free (attr);
            break;
        }
        default:
            usage ();
        }
    }
    if (optind < argc) {
        if ((e = argz_create (argv + optind, &ctx->init_shell_cmd,
                              &ctx->init_shell_cmd_len)) != 0)
            log_errn_exit (e, "argz_create");
    }
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
    broker_ctx_t ctx;
    zlist_t *sigwatchers;
    int sec_typemask = ZSECURITY_TYPE_CURVE;
    sigset_t old_sigmask;
    struct sigaction old_sigact_int;
    struct sigaction old_sigact_term;
    flux_msg_handler_t **handlers;
    const char *boot_method;

    memset (&ctx, 0, sizeof (ctx));
    log_init (argv[0]);

    if (!(sigwatchers = zlist_new ()))
        oom ();

    ctx.modhash = modhash_create ();
    if (!(ctx.services = service_switch_create ()))
        log_err_exit ("service_switch_create");
    ctx.overlay = overlay_create ();
    ctx.hello = hello_create ();
    ctx.tbon_k = 2; /* binary TBON is default */
    ctx.heartbeat = heartbeat_create ();
    ctx.shutdown = shutdown_create ();
    ctx.attrs = attr_create ();
    if (!(ctx.subscriptions = zlist_new ()))
        oom ();
    if (!(ctx.cache = content_cache_create ()))
        oom ();
    if (!(ctx.runlevel = runlevel_create ()))
        oom ();
    if (!(ctx.publisher = publisher_create ()))
        oom ();

    init_attrs (ctx.attrs, getpid());

    parse_command_line_arguments(argc, argv, &ctx, &sec_typemask);

    /* Record the instance owner: the effective uid of the broker.
     * Set default rolemask for messages sent with flux_send()
     * on the broker's internal handle.
     */
    ctx.userid = geteuid ();
    ctx.rolemask = FLUX_ROLE_OWNER;

    if (content_cache_register_attrs (ctx.cache, ctx.attrs) < 0)
        log_err_exit ("content cache attributes");

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

    /* Prepare signal handling
     */
    broker_handle_signals (&ctx, sigwatchers);

    /* Initialize security context.
     * Delay calling zsecurity_comms_init() so that we can defer creating
     * the libzmq work thread until we are ready to communicate.
     */
    const char *keydir;
    if (attr_get (ctx.attrs, "security.keydir", &keydir, NULL) < 0)
        log_err_exit ("getattr security.keydir");
    if (!(ctx.sec = zsecurity_create (sec_typemask, keydir)))
        log_err_exit ("zsecurity_create");

    /* The first call to overlay_bind() or overlay_connect() calls
     * zsecurity_comms_init().
     */
    overlay_set_sec (ctx.overlay, ctx.sec);
    overlay_set_flux (ctx.overlay, ctx.h);

    overlay_set_parent_cb (ctx.overlay, parent_cb, &ctx);
    overlay_set_child_cb (ctx.overlay, child_cb, &ctx);

    /* Arrange for the publisher to route event messages.
     * handle_event - local subscribers (ctx.h)
     */
    if (publisher_set_flux (ctx.publisher, ctx.h) < 0)
        log_err_exit ("publisher_set_flux");
    if (publisher_set_sender (ctx.publisher, "handle_event",
                              (publisher_send_f)handle_event, &ctx) < 0)
        log_err_exit ("publisher_set_sender");

    if (create_rundir (ctx.attrs) < 0)
        log_err_exit ("create_rundir");

    /* Set & create broker.rundir *after* overlay initialization,
     * when broker rank is determined.
     */
    overlay_set_init_callback (ctx.overlay, create_broker_rundir, ctx.attrs);

    /* Execute boot method selected by 'boot.method' attr.
     * Default is pmi.
     */
    if (attr_get (ctx.attrs, "boot.method", &boot_method, NULL) < 0) {
        boot_method = "pmi";
        if (attr_add (ctx.attrs, "boot.method", boot_method, 0))
            log_err_exit ("setattr boot.method");
    }
    if (attr_set_flags (ctx.attrs, "boot.method", FLUX_ATTRFLAG_IMMUTABLE) < 0)
        log_err_exit ("attr_set_flags boot.method");
    if (!strcmp (boot_method, "config")) {
        if (boot_config (ctx.overlay, ctx.attrs, ctx.tbon_k) < 0)
            log_msg_exit ("bootstrap failed");
    }
    else if (!strcmp (boot_method, "pmi")) {
        double elapsed_sec;
        struct timespec start_time;
        monotime (&start_time);
        if (boot_pmi (ctx.overlay, ctx.attrs, ctx.tbon_k) < 0)
            log_msg_exit ("bootstrap failed");
        elapsed_sec = monotime_since (start_time) / 1000;
        flux_log (ctx.h, LOG_INFO, "pmi: bootstrap time %.1fs", elapsed_sec);

    }
    else
        log_err_exit ("unknown boot method: %s", boot_method);
    uint32_t rank = overlay_get_rank(ctx.overlay);
    uint32_t size = overlay_get_size(ctx.overlay);

    assert (size > 0);
    assert (attr_get (ctx.attrs, "session-id", NULL, NULL) == 0);

    if (ctx.verbose) {
        const char *sid = "unknown";
        (void)attr_get (ctx.attrs, "session-id", &sid, NULL);
        log_msg ("boot: rank=%d size=%d session-id=%s", rank, size, sid);
    }

    if (attr_set_flags (ctx.attrs, "session-id", FLUX_ATTRFLAG_IMMUTABLE) < 0)
        log_err_exit ("attr_set_flags session-id");

    // Setup profiling
    setup_profiling (argv[0], rank);

    /* If persist-filesystem or persist-directory are set, initialize those,
     * but only on rank 0.
     */
    if (create_persistdir (ctx.attrs, rank) < 0)
        log_err_exit ("create_persistdir");

    /* Initialize logging.
     * OK to call flux_log*() after this.
     */
    logbuf_initialize (ctx.h, rank, ctx.attrs);

    /* Allow flux_get_rank() and flux_get_size() to work in the broker.
     */
    if (create_dummyattrs (ctx.h, rank, size) < 0)
        log_err_exit ("creating dummy attributes");

    /* Registers message handlers and obtains rank.
     */
    if (content_cache_set_flux (ctx.cache, ctx.h) < 0)
        log_err_exit ("content_cache_set_flux");

    /* Configure attributes.
     */
    if (overlay_register_attrs(ctx.overlay, ctx.attrs) < 0)
        log_err_exit ("registering overlay attributes");
    if (hello_register_attrs (ctx.hello, ctx.attrs) < 0)
        log_err_exit ("configuring attributes");

    if (rank == 0) {
        if (runlevel_register_attrs (ctx.runlevel, ctx.attrs) < 0)
            log_err_exit ("configuring runlevel attributes");
    }

    /* The previous value of FLUX_URI (refers to enclosing instance)
     * was stored above.  Clear it here so a connection to the enclosing
     * instance is not made inadvertantly.
     */
    unsetenv ("FLUX_URI");

    /* If shutdown_grace was not provided on the command line,
     * make a guess.
     */
    if (ctx.shutdown_grace == 0) {
        if (size < 16)
            ctx.shutdown_grace = 1;
        else if (size < 128)
            ctx.shutdown_grace = 2;
        else if (size < 1024)
            ctx.shutdown_grace = 4;
        else
            ctx.shutdown_grace = 10;
    }

    if (ctx.verbose) {
        const char *parent = overlay_get_parent (ctx.overlay);
        const char *child = overlay_get_child (ctx.overlay);
        log_msg ("parent: %s", parent ? parent : "none");
        log_msg ("child: %s", child ? child : "none");
    }

    set_proctitle (rank);

    if (rank == 0) {
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

        runlevel_set_size (ctx.runlevel, size);
        runlevel_set_callback (ctx.runlevel, runlevel_cb, &ctx);
        runlevel_set_io_callback (ctx.runlevel, runlevel_io_cb, &ctx);
        runlevel_set_flux (ctx.runlevel, ctx.h);

        if (runlevel_set_rc (ctx.runlevel, 1, rc1, rc1 ? strlen (rc1) + 1 : 0, uri) < 0)
            log_err_exit ("runlevel_set_rc 1");
        if (runlevel_set_rc (ctx.runlevel, 2, rc2, rc2_len, uri) < 0)
            log_err_exit ("runlevel_set_rc 2");
        if (runlevel_set_rc (ctx.runlevel, 3, rc3, rc3 ? strlen (rc3) + 1 : 0, uri) < 0)
            log_err_exit ("runlevel_set_rc 3");
    }

    /* If Flux was launched by Flux, now that PMI bootstrap and runlevel
     * initialization is complete, unset Flux job environment variables
     * so that they don't leak into the jobs other children of this instance.
     */
    unsetenv ("FLUX_JOB_ID");
    unsetenv ("FLUX_JOB_SIZE");
    unsetenv ("FLUX_JOB_NNODES");

    /* Wire up the overlay.
     */
    if (ctx.verbose)
        log_msg ("initializing overlay sockets");
    if (overlay_bind (ctx.overlay) < 0) /* idempotent */
        log_err_exit ("overlay_bind");
    if (overlay_connect (ctx.overlay) < 0)
        log_err_exit ("overlay_connect");

    shutdown_set_handle (ctx.shutdown, ctx.h);
    shutdown_set_callback (ctx.shutdown, shutdown_cb, &ctx);

    /* Register internal services
     */
    if (attr_register_handlers (ctx.attrs, ctx.h) < 0)
        log_err_exit ("attr_register_handlers");
    if (heaptrace_initialize (ctx.h) < 0)
        log_msg_exit ("heaptrace_initialize");
    if (exec_initialize (ctx.h, rank, ctx.attrs) < 0)
        log_err_exit ("exec2_initialize");
    if (ping_initialize (ctx.h, "cmb") < 0)
        log_err_exit ("ping_initialize");
    if (rusage_initialize (ctx.h, "cmb") < 0)
        log_err_exit ("rusage_initialize");

    handlers = broker_add_services (&ctx);

    /* Initialize comms module infrastructure.
     */
    if (ctx.verbose)
        log_msg ("initializing modules");
    modhash_set_rank (ctx.modhash, rank);
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
    if (rank == 0 && ctx.verbose)
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
    attr_unregister_handlers (ctx.attrs);
    content_cache_destroy (ctx.cache);

    broker_unhandle_signals (sigwatchers);
    zlist_destroy (&sigwatchers);

    if (ctx.verbose)
        log_msg ("cleaning up");
    if (ctx.sec)
        zsecurity_destroy (ctx.sec);
    overlay_destroy (ctx.overlay);
    heartbeat_destroy (ctx.heartbeat);
    service_switch_destroy (ctx.services);
    hello_destroy (ctx.hello);
    attr_destroy (ctx.attrs);
    shutdown_destroy (ctx.shutdown);
    broker_remove_services (handlers);
    publisher_destroy (ctx.publisher);
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
    { "FLUX_SEC_DIRECTORY",     "security.keydir",          1 },

    { "FLUX_URI",               "parent-uri",               0 },
    { NULL, NULL, 0 },
};

static void init_attrs_from_environment (attr_t *attrs)
{
    struct attrmap *m;
    const char *val;
    int flags = 0;  // XXX possibly these should be immutable?

    for (m = &attrmap[0]; m->env != NULL; m++) {
        val = getenv (m->env);
        if (!val && m->required)
            log_msg_exit ("required environment variable %s is not set", m->env);
        if (attr_add (attrs, m->attr, val, flags) < 0)
            log_err_exit ("attr_add %s", m->attr);
    }
}

static void init_attrs_broker_pid (attr_t *attrs, pid_t pid)
{
    char *attrname = "broker.pid";
    char *pidval;

    pidval = xasprintf ("%u", pid);
    if (attr_add (attrs,
                  attrname,
                  pidval,
                  FLUX_ATTRFLAG_IMMUTABLE) < 0)
        log_err_exit ("attr_add %s", attrname);
    free (pidval);
}

static void init_attrs (attr_t *attrs, pid_t pid)
{
    /* Initialize config attrs from environment set up by flux(1)
     */
    init_attrs_from_environment (attrs);

    /* Initialize other miscellaneous attrs
     */
    init_attrs_broker_pid (attrs, pid);
    if (attr_add (attrs, "version", FLUX_CORE_VERSION_STRING,
                                            FLUX_ATTRFLAG_IMMUTABLE) < 0)
        log_err_exit ("attr_add version");
}

static void hello_update_cb (hello_t *hello, void *arg)
{
    broker_ctx_t *ctx = arg;

    if (hello_complete (hello)) {
        flux_log (ctx->h, LOG_INFO, "wireup: %d/%d (complete) %.1fs",
                  hello_get_count (hello), overlay_get_size(ctx->overlay),
                  hello_get_time (hello));
        flux_log (ctx->h, LOG_INFO, "Run level %d starting", 1);
        overlay_set_idle_warning (ctx->overlay, 3);
        if (runlevel_set_level (ctx->runlevel, 1) < 0)
            log_err_exit ("runlevel_set_level 1");
        /* FIXME: shutdown hello protocol */
    } else  {
        flux_log (ctx->h, LOG_INFO, "wireup: %d/%d (incomplete) %.1fs",
                  hello_get_count (hello), overlay_get_size(ctx->overlay),
                  hello_get_time (hello));
    }
}

/* Currently 'expired' is always true.
 */
static void shutdown_cb (shutdown_t *s, bool expired, void *arg)
{
    broker_ctx_t *ctx = arg;
    if (expired) {
        if (overlay_get_rank(ctx->overlay) == 0)
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
    int loglevel = !strcmp (name, "STDERR") ? LOG_ERR : LOG_INFO;
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
    if (flux_attr_set_cacheonly (h, "rank", s) < 0)
        return -1;
    free (s);

    s = xasprintf ("%"PRIu32, size);
    if (flux_attr_set_cacheonly (h, "size", s) < 0)
        return -1;
    free (s);

    return 0;
}

/*  Handle global rundir attribute.
 *
 *  If not set, create a temporary directory and use it as the rundir.
 *  If set, attempt to create it if it doesn't exist. In either case,
 *  validate directory persmissions and set the rundir attribute
 *  immutable. If the rundir is created by this function it will be
 *  scheduled for later cleanup at broker exit. Pre-existing directories
 *  are left intact.
 */
static int create_rundir (attr_t *attrs)
{
    const char *run_dir;
    char *dir = NULL;
    char *uri = NULL;
    bool do_cleanup = true;
    struct stat sb;
    int rc = -1;

    /*  If rundir attribute isn't set, then create a temp directory
     *   and use that as rundir. If directory was set, try to create it if
     *   it doesn't exist. If directory was pre-existing, do not schedule
     *   the dir for auto-cleanup at broker exit.
     */
    if (attr_get (attrs, "rundir", &run_dir, NULL) < 0) {
        const char *tmpdir = getenv ("TMPDIR");
        if (asprintf (&dir, "%s/flux-XXXXXX", tmpdir ? tmpdir : "/tmp") < 0)
            goto done;
        if (!(run_dir = mkdtemp (dir)))
            goto done;
        if (attr_add (attrs, "rundir", run_dir, 0) < 0)
            goto done;
    }
    else if (mkdir (run_dir, 0700) < 0) {
        if (errno != EEXIST)
            goto done;
        /* Do not cleanup directory if we did not create it here
         */
        do_cleanup = false;
    }

    /*  Ensure created or existing directory is writeable:
     */
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

    /*  rundir is now fixed, so make the attribute immutable, and
     *   schedule the dir for cleanup at exit if we created it here.
     */
    if (attr_set_flags (attrs, "rundir", FLUX_ATTRFLAG_IMMUTABLE) < 0)
        goto done;
    if (do_cleanup)
        cleanup_push_string (cleanup_directory_recursive, run_dir);
    rc = 0;
done:
    free (dir);
    free (uri);
    return rc;
}

void create_broker_rundir (overlay_t *ov, void *arg)
{
    attr_t *attrs = arg;
    uint32_t rank;
    const char *rundir;
    const char *local_uri;
    char *broker_rundir = NULL;

    if (attr_get (attrs, "rundir", &rundir, NULL) < 0)
        log_msg_exit ("create_broker_rundir: rundir attribute not set");

    rank = overlay_get_rank (ov);
    if (asprintf (&broker_rundir, "%s/%u", rundir, rank) < 0)
        log_err_exit ("create_broker_rundir: asprintf");
    if (mkdir (broker_rundir, 0700) < 0)
        log_err_exit ("create_broker_rundir: mkdir (%s)", broker_rundir);
    if (attr_add (attrs, "broker.rundir", broker_rundir,
                         FLUX_ATTRFLAG_IMMUTABLE) < 0)
        log_err_exit ("create_broker_rundir: attr_add broker.rundir");

    if (attr_get (attrs, "local-uri", &local_uri, NULL) < 0) {
        char *uri;
        if (asprintf (&uri, "local://%s", broker_rundir) < 0)
            log_err_exit ("create_broker_rundir: asprintf (uri)");
        if (attr_add (attrs, "local-uri", uri, FLUX_ATTRFLAG_IMMUTABLE) < 0)
            log_err_exit ("create_broker_rundir: attr_add (local-uri)");
        free (uri);
    }
    free (broker_rundir);
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

static bool nodeset_member (const char *s, uint32_t rank)
{
    struct idset *ns = NULL;
    bool member = true;

    if (s) {
        if (!(ns = idset_decode (s)))
            log_msg_exit ("malformed nodeset: %s", s);
        member = idset_test (ns, rank);
        idset_destroy (ns);
    }
    return member;
}

static int mod_svc_cb (const flux_msg_t *msg, void *arg)
{
    module_t *p = arg;
    int rc = module_sendmsg (p, msg);
    return rc;
}

/* If a dlerror/dlsym error occurs during modfind/modname,
 * log it here.  Such messages can be helpful in diagnosing
 * dynamic binding problems for comms modules.
 */
static void module_dlerror (const char *errmsg, void *arg)
{
    flux_t *h = arg;
    flux_log (h, LOG_DEBUG, "flux_modname: %s", errmsg);
}


static int load_module_bypath (broker_ctx_t *ctx, const char *path,
                               const char *argz, size_t argz_len,
                               const flux_msg_t *request)
{
    module_t *p = NULL;
    char *name, *arg;

    if (!(name = flux_modname (path, module_dlerror, ctx->h))) {
        errno = ENOENT;
        goto error;
    }
    if (!(p = module_add (ctx->modhash, path)))
        goto error;
    if (service_add (ctx->services, module_get_name (p),
                                    module_get_uuid (p), mod_svc_cb, p) < 0)
        goto module_remove;
    arg = argz_next (argz, argz_len, NULL);
    while (arg) {
        module_add_arg (p, arg);
        arg = argz_next (argz, argz_len, arg);
    }
    module_set_poller_cb (p, module_cb, ctx);
    module_set_status_cb (p, module_status_cb, ctx);
    if (request && module_push_insmod (p, request) < 0) // response deferred
        goto service_remove;
    if (module_start (p) < 0)
        goto service_remove;
    flux_log (ctx->h, LOG_DEBUG, "insmod %s", name);
    free (name);
    return 0;
service_remove:
    service_remove_byuuid (ctx->services, module_get_uuid (p));
module_remove:
    module_remove (ctx->modhash, p);
error:
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
    if (!(path = flux_modfind (modpath, name, module_dlerror, ctx->h))) {
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
        service_remove_byuuid (ctx->services, module_get_uuid (p));
        module_remove (ctx->modhash, p);
    }
    flux_log (ctx->h, LOG_DEBUG, "rmmod %s", name);
    return 0;
}

static void broker_handle_signals (broker_ctx_t *ctx, zlist_t *sigwatchers)
{
    int i, sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGSEGV, SIGFPE,
                      SIGALRM };
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
 **/

/* Unload a comms module by name, asynchronously.
 * Message format is defined by RFC 5.
 * N.B. unload_module_byname() handles response, unless it fails early
 * and returns -1.
 */
static void cmb_rmmod_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *name;

    if (flux_request_unpack (msg, NULL, "{s:s}", "name", &name) < 0)
        goto error;
    if (unload_module_byname (ctx, name, msg, true) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* Load a comms module by name, asynchronously.
 * Message format is defined by RFC 5.
 * N.B. load_module_bypath() handles response, unless it returns -1.
 */
static void cmb_insmod_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *path;
    json_t *args;
    size_t index;
    json_t *value;
    char *argz = NULL;
    size_t argz_len = 0;
    error_t e;

    if (flux_request_unpack (msg, NULL, "{s:s s:o}", "path", &path,
                                                     "args", &args) < 0)
        goto error;
    if (!json_is_array (args))
        goto proto;
    json_array_foreach (args, index, value) {
        if (!json_is_string (value))
            goto proto;
        if ((e = argz_add (&argz, &argz_len, json_string_value (value)))) {
            errno = e;
            goto error;
        }
    }
    if (load_module_bypath (ctx, path, argz, argz_len, msg) < 0)
        goto error;
    free (argz);
    return;
proto:
    errno = EPROTO;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    free (argz);
}

/* Load a comms module by name.
 * Message format is defined by RFC 5.
 */
static void cmb_lsmod_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    json_t *mods = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(mods = module_get_modlist (ctx->modhash, ctx->services)))
        goto error;
    if (flux_respond_pack (h, msg, "{s:O}", "mods", mods) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    json_decref (mods);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void cmb_lspeer_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    char *out;

    if (!(out = overlay_lspeer_encode (ctx->overlay))) {
        if (flux_respond_error (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
        return;
    }
    if (flux_respond (h, msg, out) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (out);
}

#if CODE_COVERAGE_ENABLED
void __gcov_flush (void);
#endif

static void cmb_panic_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    const char *reason;
    int flags; // reserved

    if (flux_request_unpack (msg, NULL, "{s:s s:i}",
                             "reason", &reason,
                             "flags", &flags) < 0) {
        flux_log_error (h, "malformed cmb.panic request");
        return;
    }
    fprintf (stderr, "PANIC: %s\n", reason);
#if CODE_COVERAGE_ENABLED
    __gcov_flush ();
#endif
    _exit (1);
    /*NOTREACHED*/
}

static void cmb_disconnect_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    char *sender = NULL;

    if (flux_msg_get_route_first (msg, &sender) == 0) {
        exec_terminate_subprocesses_by_uuid (h, sender);
        free (sender);
    }
    /* no response */
}

static void cmb_sub_cb (flux_t *h, flux_msg_handler_t *mh,
                        const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    char *uuid = NULL;
    const char *topic;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        goto error;
    if (flux_msg_get_route_first (msg, &uuid) < 0)
        goto error;
    if (!uuid) {
        errno = EPROTO;
        goto error;
    }
    if (module_subscribe (ctx->modhash, uuid, topic) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (uuid);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    free (uuid);
}

static void cmb_unsub_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    char *uuid = NULL;
    const char *topic;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        goto error;
    if (flux_msg_get_route_first (msg, &uuid) < 0)
        goto error;
    if (!uuid) {
        errno = EPROTO;
        goto error;
    }
    if (module_unsubscribe (ctx->modhash, uuid, topic) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    free (uuid);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    free (uuid);
}

static int route_to_handle (const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    if (flux_requeue (ctx->h, msg, FLUX_RQ_TAIL) < 0)
        flux_log_error (ctx->h, "%s: flux_requeue\n", __FUNCTION__);
    return 0;
}

/* Dynamic service registration.
 * These handlers need to appear in broker.c so that they have
 *  access to broker internals like modhash
 */
static void service_add_cb (flux_t *h, flux_msg_handler_t *w,
                            const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *name = NULL;
    char *sender = NULL;
    module_t *p;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "service", &name) < 0)
        goto error;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto error;
    if (!(p = module_lookup (ctx->modhash, sender))) {
        errno = ENOENT;
        goto error;
    }
    if (service_add (ctx->services, name, sender, mod_svc_cb, p) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "service_add: flux_respond");
    free (sender);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "service_add: flux_respond_error");
    free (sender);
}

static void service_remove_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *name;
    const char *uuid;
    char *sender = NULL;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "service", &name) < 0)
        goto error;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto error;
    if (!(uuid = service_get_uuid (ctx->services, name))) {
        errno = ENOENT;
        goto error;
    }
    if (strcmp (uuid, sender) != 0) {
        errno = EINVAL;
        goto error;
    }
    service_remove (ctx->services, name);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "service_remove: flux_respond");
    free (sender);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "service_remove: flux_respond_error");
    free (sender);
}


static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "cmb.rmmod",      cmb_rmmod_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.insmod",     cmb_insmod_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.lsmod",      cmb_lsmod_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.lspeer",     cmb_lspeer_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.panic",      cmb_panic_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.disconnect", cmb_disconnect_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.sub",        cmb_sub_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "cmb.unsub",      cmb_unsub_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "service.add",    service_add_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "service.remove", service_remove_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

struct internal_service {
    const char *name;
    const char *nodeset;
};

static struct internal_service services[] = {
    { "cmb",                NULL }, // kind of a catch-all, slowly deprecating
    { "log",                NULL },
    { "seq",                "[0]" },
    { "content",            NULL },
    { "hello",              NULL },
    { "attr",               NULL },
    { "heaptrace",          NULL },
    { "event",              "[0]" },
    { "service",            NULL },
    { NULL, NULL, },
};

/* Register builtin services (sharing ctx->h and broker thread).
 * Register message handlers for some cmb services.  Others are registered
 * in their own initialization functions.
 */
static flux_msg_handler_t **broker_add_services (broker_ctx_t *ctx)
{
    flux_msg_handler_t **handlers;
    struct internal_service *svc;
    for (svc = &services[0]; svc->name != NULL; svc++) {
        if (!nodeset_member (svc->nodeset, overlay_get_rank(ctx->overlay)))
            continue;
        if (service_add (ctx->services, svc->name, NULL,
                          route_to_handle, ctx) < 0)
            log_err_exit ("error registering service for %s", svc->name);
    }

    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &handlers) < 0)
        log_err_exit ("error registering message handlers");
    return handlers;
}

/* Unregister message handlers
 */
static void broker_remove_services (flux_msg_handler_t *handlers[])
{
    flux_msg_handler_delvec (handlers);
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

/* Handle events received by parent_cb.
 * On rank 0, publisher is wired to send events here also.
 */
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

    /* Forward to this rank's children.
     */
    if (overlay_mcast_child (ctx->overlay, msg) < 0)
        flux_log_error (ctx->h, "%s: overlay_mcast_child", __FUNCTION__);

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
    /* Finally, route to local module subscribers.
     */
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
            int rc;
            if (status == FLUX_MODSTATE_EXITED)
                errnum = module_get_errnum (p);
            if (errnum == 0)
                rc = flux_respond (ctx->h, msg, NULL);
            else
                rc = flux_respond_error (ctx->h, msg, errnum, NULL);
            if (rc < 0)
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
        service_remove_byuuid (ctx->services, module_get_uuid (p));
        while ((msg = module_pop_rmmod (p))) {
            if (flux_respond (ctx->h, msg, NULL) < 0)
                flux_log_error (ctx->h, "flux_respond to rmmod %s", name);
            flux_msg_destroy (msg);
        }
        module_remove (ctx->modhash, p);
    }
}

static void signal_cb (flux_reactor_t *r, flux_watcher_t *w,
                         int revents, void *arg)
{
    broker_ctx_t *ctx = arg;
    int signum = flux_signal_watcher_get_signum (w);

    shutdown_arm (ctx->shutdown, ctx->shutdown_grace, 0,
                  "signal %d (%s)", signum, strsignal (signum));
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

    snprintf (uuid, sizeof (uuid), "%"PRIu32, overlay_get_rank(ctx->overlay));
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
    uint32_t rank = overlay_get_rank(ctx->overlay);
    uint32_t size = overlay_get_size(ctx->overlay);
    const char *topic;
    char errbuf[64];
    const char *errstr = NULL;

    if (flux_msg_get_nodeid (msg, &nodeid, &flags) < 0)
        goto error;
    if (flux_msg_get_topic (msg, &topic) < 0)
        goto error;
    if ((flags & FLUX_MSGFLAG_UPSTREAM) && nodeid == rank) {
        rc = overlay_sendmsg_parent (ctx->overlay, msg);
        if (rc < 0)
            goto error;
    } else if ((flags & FLUX_MSGFLAG_UPSTREAM) && nodeid != rank) {
        rc = service_send (ctx->services, msg);
        if (rc < 0 && errno == ENOSYS) {
            rc = overlay_sendmsg_parent (ctx->overlay, msg);
            if (rc < 0 && errno == EHOSTUNREACH)
                goto nosys;
        }
        if (rc < 0)
            goto error;
    } else if (nodeid == FLUX_NODEID_ANY) {
        rc = service_send (ctx->services, msg);
        if (rc < 0 && errno == ENOSYS) {
            rc = overlay_sendmsg_parent (ctx->overlay, msg);
            if (rc < 0 && errno == EHOSTUNREACH)
                goto nosys;
        }
        if (rc < 0)
            goto error;
    } else if (nodeid == rank) {
        rc = service_send (ctx->services, msg);
        if (rc < 0)
            goto error;
    } else if ((gw = kary_child_route (ctx->tbon_k, size, rank, nodeid))
               != KARY_NONE) {
        rc = subvert_sendmsg_child (ctx, msg, gw);
        if (rc < 0)
            goto error;
    } else {
        rc = overlay_sendmsg_parent (ctx->overlay, msg);
        if (rc < 0)
            goto error;
    }
    return 0;
nosys:
    errno = ENOSYS;
    (void)snprintf (errbuf, sizeof (errbuf),
                    "No service matching %s is registered", topic);
    errstr = errbuf;
error:
    if (errmode == ERROR_MODE_RETURN)
        return -1;
    /* ERROR_MODE_RESPOND */
    (void)flux_respond_error (ctx->h, msg, errno, errstr);
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

    parent = kary_parentof (ctx->tbon_k, overlay_get_rank(ctx->overlay));
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
 * (This mechanism predates and is separate from the "event.pub" service).
 */
static int broker_event_sendmsg (broker_ctx_t *ctx, const flux_msg_t *msg)
{

    if (overlay_get_rank(ctx->overlay) > 0) {
        flux_msg_t *cpy;
        if (!(cpy = flux_msg_copy (msg, true)))
            return -1;
        if (flux_msg_enable_route (cpy) < 0) {
            flux_msg_destroy (cpy);
            return -1;
        }
        if (overlay_sendmsg_parent (ctx->overlay, cpy) < 0) {
            flux_msg_destroy (cpy);
            return -1;
        }
        flux_msg_destroy (cpy);
    } else {
        if (publisher_send (ctx->publisher, msg) < 0)
            return -1;
    }
    return 0;
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


#if HAVE_VALGRIND
/* Disable dlclose() during valgrind operation
 */
void I_WRAP_SONAME_FNNAME_ZZ(Za,dlclose)(void *dso) {}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
