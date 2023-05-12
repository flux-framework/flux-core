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
#include <assert.h>
#include <libgen.h>
#include <signal.h>
#include <locale.h>
#include <inttypes.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/syscall.h>
#ifdef HAVE_ARGZ_ADD
#include <argz.h>
#else
#include "src/common/libmissing/argz.h"
#endif
#include <flux/core.h>
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
#include <flux/taskmap.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libidset/idset.h"
#include "src/common/libutil/ipaddr.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/intree.h"
#include "src/common/librouter/subhash.h"
#include "src/common/libfluxutil/method.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "ccan/ptrint/ptrint.h"
#ifndef HAVE_STRLCPY
#include "src/common/libmissing/strlcpy.h"
#endif
#ifndef HAVE_STRLCAT
#include "src/common/libmissing/strlcat.h"
#endif

#include "module.h"
#include "modhash.h"
#include "brokercfg.h"
#include "groups.h"
#include "overlay.h"
#include "service.h"
#include "attr.h"
#include "log.h"
#include "runat.h"
#include "heaptrace.h"
#include "exec.h"
#include "boot_flub.h"
#include "boot_config.h"
#include "boot_pmi.h"
#include "publisher.h"
#include "state_machine.h"
#include "shutdown.h"

#include "broker.h"


static int broker_request_sendmsg_new_internal (broker_ctx_t *ctx,
                                                flux_msg_t **msg);

static void h_internal_watcher (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg);

static int overlay_recv_cb (flux_msg_t **msg, overlay_where_t where, void *arg);

static void signal_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg);
static int broker_handle_signals (broker_ctx_t *ctx);

static flux_msg_handler_t **broker_add_services (broker_ctx_t *ctx);
static void broker_remove_services (flux_msg_handler_t *handlers[]);

static void set_proctitle (uint32_t rank);

static int create_rundir (attr_t *attrs);
static int check_statedir (attr_t *attrs);

static int create_runat_phases (broker_ctx_t *ctx);

static int handle_event (broker_ctx_t *ctx, const flux_msg_t *msg);

static void init_attrs (attr_t *attrs, pid_t pid, struct flux_msg_cred *cred);

static void init_attrs_starttime (attr_t *attrs, double starttime);

static int init_local_uri_attr (struct overlay *ov, attr_t *attrs);

static int init_critical_ranks_attr (struct overlay *ov, attr_t *attrs);

static int execute_parental_notifications (struct broker *ctx);

static struct optparse_option opts[] = {
    { .name = "verbose",    .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Be annoyingly informative by degrees", },
    { .name = "setattr",    .key = 'S', .has_arg = 1, .arginfo = "ATTR=VAL",
      .usage = "Set broker attribute", },
    { .name = "config-path",.key = 'c', .has_arg = 1, .arginfo = "PATH",
      .usage = "Set broker config from PATH (default: none)", },
    OPTPARSE_TABLE_END,
};

void parse_command_line_arguments (int argc, char *argv[], broker_ctx_t *ctx)
{
    int optindex;
    const char *arg;

    if (!(ctx->opts = optparse_create ("flux-broker"))
        || optparse_add_option_table (ctx->opts, opts) != OPTPARSE_SUCCESS)
        log_msg_exit ("error setting up option parsing");
    if ((optindex = optparse_parse_args (ctx->opts, argc, argv)) < 0)
        exit (1);

    ctx->verbose = optparse_get_int (ctx->opts, "verbose", 0);

    optparse_get_str (ctx->opts, "config-path", NULL);

    while ((arg = optparse_getopt_next (ctx->opts, "setattr"))) {
        char *val, *attr;
        if (!(attr = strdup (arg)))
            log_err_exit ("out of memory duplicating optarg");
        if ((val = strchr (attr, '=')))
            *val++ = '\0';
        if (attr_add (ctx->attrs, attr, val, 0) < 0)
            if (attr_set (ctx->attrs, attr, val) < 0)
                log_err_exit ("setattr %s=%s", attr, val);
        free (attr);
    }

    if (optindex < argc) {
        int e;
        if ((e = argz_create (argv + optindex,
                              &ctx->init_shell_cmd,
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

static int increase_rlimits (void)
{
    struct rlimit rlim;

    /*  Increase number of open files to max to prevent potential failures
     *   due to file descriptor exhaustion (e.g. failure to open /dev/urandom)
     */
    if (getrlimit (RLIMIT_NOFILE, &rlim) < 0) {
        log_err ("getrlimit");
        return -1;
    }
    rlim.rlim_cur = rlim.rlim_max;
    if (setrlimit (RLIMIT_NOFILE, &rlim) < 0) {
        log_err ("Failed to increase nofile limit");
        return -1;
    }
    return 0;
}

int main (int argc, char *argv[])
{
    broker_ctx_t ctx;
    sigset_t old_sigmask;
    struct sigaction old_sigact_int;
    struct sigaction old_sigact_term;
    flux_msg_handler_t **handlers = NULL;
    const flux_conf_t *conf;
    flux_error_t error;

    setlocale (LC_ALL, "");

    memset (&ctx, 0, sizeof (ctx));
    log_init (argv[0]);

    ctx.exit_rc = 1;

    if (!(ctx.sigwatchers = zlist_new ())
        || !(ctx.services = service_switch_create ())
        || !(ctx.attrs = attr_create ())
        || !(ctx.sub = subhash_create ()))
        log_msg_exit ("Out of memory in early initialization");

    /* Record the instance owner: the effective uid of the broker. */
    ctx.cred.userid = getuid ();
    /* Set default rolemask for messages sent with flux_send()
     * on the broker's internal handle. */
    ctx.cred.rolemask = FLUX_ROLE_OWNER | FLUX_ROLE_LOCAL;

    init_attrs (ctx.attrs, getpid (), &ctx.cred);

    parse_command_line_arguments (argc, argv, &ctx);

    /* Block all signals but those that we want to generate core dumps.
     * Save old mask and actions for SIGINT, SIGTERM.
     */
    sigset_t sigmask;
    sigfillset (&sigmask);
    sigdelset (&sigmask, SIGSEGV);
    sigdelset (&sigmask, SIGFPE);
    sigdelset (&sigmask, SIGILL);
    sigdelset (&sigmask, SIGABRT);
    sigdelset (&sigmask, SIGFPE);
    sigdelset (&sigmask, SIGSYS);
    sigdelset (&sigmask, SIGTRAP);
    sigdelset (&sigmask, SIGXCPU);
    sigdelset (&sigmask, SIGXFSZ);
    if (sigprocmask (SIG_SETMASK, &sigmask, &old_sigmask) < 0
        || sigaction (SIGINT, NULL, &old_sigact_int) < 0
        || sigaction (SIGTERM, NULL, &old_sigact_term) < 0)
        log_err_exit ("error setting signal mask");

    /* Set up two interthread flux_t handles, connected back to back.
     * ctx.h is used conventionally within the broker for RPCs, message
     * handlers, etc.  ctx.h_internal belongs to the broker's routing logic
     * and is accessed using flux_send() and flux_recv() only.  Both handles
     * share a reactor, which is created with FLUX_REACTOR_SIGCHLD in order
     * to support libsubprocess.
     *
     * N.B. since both handles are in the same thread, synchronous RPCs on
     * ctx.h will deadlock.  The main broker reactor must run in order
     * to move messages from the interthread queue to the routing logic.
     * Careful with flux_attr_get(), which hides a synchronous RPC if the
     * requested value is not cached.
     */
    if (!(ctx.reactor = flux_reactor_create (FLUX_REACTOR_SIGCHLD))
        || !(ctx.h = flux_open ("interthread://broker", 0))
        || flux_set_reactor (ctx.h, ctx.reactor) < 0
        || !(ctx.h_internal = flux_open ("interthread://broker", 0))
        || flux_set_reactor (ctx.h_internal, ctx.reactor) < 0
        || !(ctx.w_internal = flux_handle_watcher_create (ctx.reactor,
                                                          ctx.h_internal,
                                                          FLUX_POLLIN,
                                                          h_internal_watcher,
                                                          &ctx))) {
        log_err ("error setting up broker reactor/flux_t handle");
        goto cleanup;
    }
    flux_watcher_start (ctx.w_internal);

    const char *val;
    if (attr_get (ctx.attrs, "broker.sd-notify", &val, NULL) == 0
        && !streq (val, "0")) {
#if !HAVE_LIBSYSTEMD
        log_err ("broker.sd_notify is set but Flux was not built"
                 " with systemd support.");
        goto cleanup;
#else
        ctx.sd_notify = true;
#endif
    }

    /* Initialize module infrastructure.
     */
    if (!(ctx.modhash = modhash_create (&ctx))) {
        log_err ("error creating broker module hash");
        goto cleanup;
    }

    /* Parse config.
     */
    if (!(ctx.config = brokercfg_create (ctx.h,
                                         optparse_get_str (ctx.opts,
                                                           "config-path",
                                                           NULL),
                                         ctx.attrs,
                                         ctx.modhash)))
        goto cleanup;
    conf = flux_get_conf (ctx.h);

    if (increase_rlimits () < 0)
        goto cleanup;

    /* Prepare signal handling
     */
    if (broker_handle_signals (&ctx) < 0) {
        log_err ("broker_handle_signals");
        goto cleanup;
    }

    if (!(ctx.overlay = overlay_create (ctx.h,
                                        ctx.attrs,
                                        NULL,
                                        overlay_recv_cb,
                                        &ctx))) {
        log_err ("overlay_create");
        goto cleanup;
    }

    /* Arrange for the publisher to route event messages.
     */
    if (!(ctx.publisher = publisher_create (&ctx,
                                            (publisher_send_f)handle_event,
                                            &ctx))) {
        log_err ("error setting up event publishing service");
        goto cleanup;
    }

    if (create_rundir (ctx.attrs) < 0)
        goto cleanup;
    if (check_statedir (ctx.attrs) < 0)
        goto cleanup;

    /* Record the broker start time.  This time will also be used to
     * capture how long network bootstrap takes.
     */
    flux_reactor_now_update (ctx.reactor);
    ctx.starttime = flux_reactor_now (ctx.reactor);
    init_attrs_starttime (ctx.attrs, ctx.starttime);

    /* Execute broker network bootstrap.
     */
    const char *boot_method;
    if (attr_get (ctx.attrs, "broker.boot-method", &boot_method, NULL) < 0) {
        if (flux_conf_unpack (conf, NULL, "{s:{}}", "bootstrap") == 0)
            boot_method = "config";
        else if (attr_get (ctx.attrs, "broker.boot-server", NULL, NULL) == 0)
            boot_method = "flub";
        else
            boot_method = NULL;
    }
    if (boot_method && streq (boot_method, "config")) {
        if (boot_config (ctx.h, ctx.overlay, ctx.attrs) < 0) {
            log_msg ("boot-config failed");
            goto cleanup;
        }
    }
    else if (boot_method && streq (boot_method, "flub")) {
        if (boot_flub (&ctx, &error) < 0) {
            log_msg ("boot-flub: %s", error.text);
            goto cleanup;
        }
    }
    else {
        if (boot_pmi (ctx.overlay, ctx.attrs) < 0) {
            log_msg ("boot-pmi failed");
            goto cleanup;
        }
    }

    ctx.rank = overlay_get_rank (ctx.overlay);
    ctx.size = overlay_get_size (ctx.overlay);

    assert (ctx.size > 0);

    /* Must be called after overlay setup */
    if (overlay_register_attrs (ctx.overlay) < 0) {
        log_err ("registering overlay attributes");
        goto cleanup;
    }

    if (ctx.verbose) {
        flux_reactor_now_update (ctx.reactor);
        log_msg ("boot: rank=%d size=%d time %.3fs",
                  ctx.rank,
                  ctx.size,
                  flux_reactor_now (ctx.reactor) - ctx.starttime);
    }

    // Setup profiling
    setup_profiling (argv[0], ctx.rank);

    /* Initialize logging.
     * OK to call flux_log*() after this.
     */
    logbuf_initialize (ctx.h, ctx.rank, ctx.attrs);

    /* Allow flux_get_rank(), flux_get_size(), flux_get_hostybyrank(), etc.
     * to work in the broker without causing a synchronous RPC to self that
     * would deadlock.
     */
    if (attr_cache_immutables (ctx.attrs, ctx.h) < 0) {
        log_err ("error priming broker attribute cache");
        goto cleanup;
    }
    int flags;
    assert (attr_get (ctx.attrs, "rank", NULL, &flags) == 0
            && (flags & ATTR_IMMUTABLE));
    assert (attr_get (ctx.attrs, "size", NULL, &flags) == 0
            && (flags & ATTR_IMMUTABLE));
    assert (attr_get (ctx.attrs, "hostlist", NULL, &flags) == 0
            && (flags & ATTR_IMMUTABLE));

    if (!(ctx.groups = groups_create (&ctx))) {
        log_err ("groups_create");
        goto cleanup;
    }

    if (ctx.verbose) {
        const char *parent = overlay_get_parent_uri (ctx.overlay);
        const char *child = overlay_get_bind_uri (ctx.overlay);
        log_msg ("parent: %s", parent ? parent : "none");
        log_msg ("child: %s", child ? child : "none");
    }

    set_proctitle (ctx.rank);

    if (init_local_uri_attr (ctx.overlay, ctx.attrs) < 0 // used by runat
        || init_critical_ranks_attr (ctx.overlay, ctx.attrs) < 0)
        goto cleanup;

    if (create_runat_phases (&ctx) < 0)
        goto cleanup;

    /* Wire up the overlay.
     */
    if (ctx.rank > 0) {
        if (ctx.verbose)
            log_msg ("initializing overlay connect");
        if (overlay_connect (ctx.overlay) < 0) {
            log_err ("overlay_connect");
            goto cleanup;
        }
    }

    /* Register internal services
     */
    if (attr_register_handlers (ctx.attrs, ctx.h) < 0) {
        log_err ("attr_register_handlers");
        goto cleanup;
    }
    if (heaptrace_initialize (ctx.h) < 0) {
        log_err ("heaptrace_initialize");
        goto cleanup;
    }
    if (exec_initialize (ctx.h, ctx.rank, ctx.attrs) < 0) {
        log_err ("exec_initialize");
        goto cleanup;
    }
    if (flux_aux_set (ctx.h,
                      "flux::uuid",
                      (char *)overlay_get_uuid (ctx.overlay),
                      NULL) < 0) {
        log_err ("error adding broker uuid to aux container");
        goto cleanup;
    }
    if (!(handlers = broker_add_services (&ctx))) {
        log_err ("broker_add_services");
        goto cleanup;
    }
    /* overlay_control_start() calls flux_sync_create(), thus
     * requires event.subscribe to have a handler before running.
     */
    if (overlay_control_start (ctx.overlay) < 0) {
        log_err ("error initializing overlay control messages");
        goto cleanup;
    }

    /* Configure broker state machine
     */
    if (!(ctx.state_machine = state_machine_create (&ctx))) {
        log_err ("error creating broker state machine");
        goto cleanup;
    }
    state_machine_post (ctx.state_machine, "start");

    /* Create shutdown mechanism
     */
    if (!(ctx.shutdown = shutdown_create (&ctx))) {
        log_err ("error creating shutdown mechanism");
        goto cleanup;
    }

    /* Load the local connector module.
     * Other modules will be loaded in rc1 using flux module,
     * which uses the local connector.
     * The shutdown protocol unloads it.
     */
    if (ctx.verbose > 1)
        log_msg ("loading connector-local");
    if (modhash_load (ctx.modhash,
                      NULL,
                      "connector-local",
                      NULL,
                      NULL,
                      &error) < 0) {
        log_err ("load_module connector-local: %s", error.text);
        goto cleanup;
    }

    if (ctx.rank == 0 && execute_parental_notifications (&ctx) < 0)
        goto cleanup;

    /* Event loop
     */
    if (ctx.verbose > 1)
        log_msg ("entering event loop");
    /* Once we enter the reactor, default exit_rc is now 0 */
    ctx.exit_rc = 0;
    if (flux_reactor_run (ctx.reactor, 0) < 0)
        log_err ("flux_reactor_run");
    if (ctx.verbose > 1)
        log_msg ("exited event loop");

cleanup:
    if (ctx.verbose > 1)
        log_msg ("cleaning up");

    /* Restore default sigmask and actions for SIGINT, SIGTERM
     */
    if (sigprocmask (SIG_SETMASK, &old_sigmask, NULL) < 0
        || sigaction (SIGINT, &old_sigact_int, NULL) < 0
        || sigaction (SIGTERM, &old_sigact_term, NULL) < 0)
        log_err ("error restoring signal mask");

    /* Unregister builtin services
     */
    attr_destroy (ctx.attrs);

    if (modhash_destroy (ctx.modhash) > 0) {
        if (ctx.exit_rc == 0)
            ctx.exit_rc = 1;
    }
    zlist_destroy (&ctx.sigwatchers);
    shutdown_destroy (ctx.shutdown);
    state_machine_destroy (ctx.state_machine);
    overlay_destroy (ctx.overlay);
    groups_destroy (ctx.groups);
    service_switch_destroy (ctx.services);
    broker_remove_services (handlers);
    publisher_destroy (ctx.publisher);
    brokercfg_destroy (ctx.config);
    runat_destroy (ctx.runat);
    flux_watcher_destroy (ctx.w_internal);
    flux_close (ctx.h_internal);
    flux_close (ctx.h);
    flux_reactor_destroy (ctx.reactor);
    subhash_destroy (ctx.sub);
    free (ctx.init_shell_cmd);
    optparse_destroy (ctx.opts);

    return ctx.exit_rc;
}

static void init_attrs_broker_pid (attr_t *attrs, pid_t pid)
{
    char *attrname = "broker.pid";
    char pidval[32];

    snprintf (pidval, sizeof (pidval), "%u", pid);
    if (attr_add (attrs,
                  attrname,
                  pidval,
                  ATTR_IMMUTABLE) < 0)
        log_err_exit ("attr_add %s", attrname);
}

static void init_attrs_rc_paths (attr_t *attrs)
{
    if (attr_add (attrs,
                  "broker.rc1_path",
                  flux_conf_builtin_get ("rc1_path", FLUX_CONF_AUTO),
                  0) < 0)
        log_err_exit ("attr_add rc1_path");

    if (attr_add (attrs,
                  "broker.rc3_path",
                  flux_conf_builtin_get ("rc3_path", FLUX_CONF_AUTO),
                  0) < 0)
        log_err_exit ("attr_add rc3_path");
}

static void init_attrs_shell_paths (attr_t *attrs)
{
    if (attr_add (attrs,
                  "conf.shell_pluginpath",
                  flux_conf_builtin_get ("shell_pluginpath", FLUX_CONF_AUTO),
                  0) < 0)
        log_err_exit ("attr_add conf.shell_pluginpath");
    if (attr_add (attrs,
                  "conf.shell_initrc",
                  flux_conf_builtin_get ("shell_initrc", FLUX_CONF_AUTO),
                  0) < 0)
        log_err_exit ("attr_add conf.shell_initrc");
}

static void init_attrs_starttime (attr_t *attrs, double starttime)
{
    char buf[32];

    snprintf (buf, sizeof (buf), "%.2f", starttime);
    if (attr_add (attrs, "broker.starttime", buf, ATTR_IMMUTABLE) < 0)
        log_err_exit ("error setting broker.starttime attribute");
}

static void init_attrs (attr_t *attrs, pid_t pid, struct flux_msg_cred *cred)
{
    const char *val;

    /* Set the parent-uri attribute IFF this instance was run as a job
     * in the enclosing instance.  "parent" in this context reflects
     * a hierarchy of resource allocation.
     */
    if (getenv ("FLUX_JOB_ID"))
        val = getenv ("FLUX_URI");
    else
        val = NULL;
    if (attr_add (attrs, "parent-uri", val, ATTR_IMMUTABLE) < 0)
        log_err_exit ("setattr parent-uri");
    unsetenv ("FLUX_URI");

    /* Unset FLUX_PROXY_REMOTE since once a new broker starts we're no
     * longer technically running under the influence of flux-proxy(1).
     */
    unsetenv ("FLUX_PROXY_REMOTE");

    val = getenv ("FLUX_KVS_NAMESPACE");
    if (attr_add (attrs, "parent-kvs-namespace", val, ATTR_IMMUTABLE) < 0)
        log_err_exit ("setattr parent-kvs-namespace");
    unsetenv ("FLUX_KVS_NAMESPACE");

    init_attrs_broker_pid (attrs, pid);
    init_attrs_rc_paths (attrs);
    init_attrs_shell_paths (attrs);

    /* Allow version to be changed by instance owner for testing
     */
    if (attr_add (attrs, "version", FLUX_CORE_VERSION_STRING, 0) < 0)
        log_err_exit ("attr_add version");

    char tmp[32];
    snprintf (tmp, sizeof (tmp), "%ju", (uintmax_t)cred->userid);
    if (attr_add (attrs, "security.owner", tmp, ATTR_IMMUTABLE) < 0)
        log_err_exit ("attr_add owner");
}

static void set_proctitle (uint32_t rank)
{
    static char proctitle[32];
    snprintf (proctitle, sizeof (proctitle), "flux-broker-%"PRIu32, rank);
    (void)prctl (PR_SET_NAME, proctitle, 0, 0, 0);
}

static bool is_interactive_shell (const char *argz, size_t argz_len)
{
    bool result = false;
    /*  If no command is specified, then an interactive shell will be run
     */
    if (argz == NULL)
        return true;

    /*  O/w, if command is plain "$SHELL", e.g. bash, zsh, csh, etc.
     *   then assume interactive shell.
     */
    if (argz_count (argz, argz_len) == 1) {
        char *shell;
        char *cmd = argz_next (argz, argz_len, NULL);
        while ((shell = getusershell ())) {
            if (streq (cmd, shell)
                || streq (cmd, basename (shell))) {
                result = true;
                break;
            }
        }
        endusershell ();
    }
    return result;
}

static int create_runat_rc2 (struct runat *r, const char *argz, size_t argz_len)
{
    if (is_interactive_shell (argz, argz_len)) { // run interactive shell
        /*  Check if stdin is a tty and error out if not to avoid
         *   confusing users with what appears to be a hang.
         */
        if (!isatty (STDIN_FILENO))
            log_msg_exit ("stdin is not a tty - can't run interactive shell");
        if (runat_push_shell (r, "rc2", argz, 0) < 0)
            return -1;
    }
    else if (argz_count (argz, argz_len) == 1) { // run shell -c "command"
        if (runat_push_shell_command (r, "rc2", argz, 0) < 0)
            return -1;
    }
    else { // direct exec
        if (runat_push_command (r, "rc2", argz, argz_len, 0) < 0)
            return -1;
    }
    return 0;
}

static int create_runat_phases (broker_ctx_t *ctx)
{
    const char *rc1, *rc3, *local_uri;
    bool rc2_none = false;

    if (attr_get (ctx->attrs, "local-uri", &local_uri, NULL) < 0) {
        log_err ("local-uri is not set");
        return -1;
    }
    if (attr_get (ctx->attrs, "broker.rc1_path", &rc1, NULL) < 0) {
        log_err ("broker.rc1_path is not set");
        return -1;
    }
    if (attr_get (ctx->attrs, "broker.rc3_path", &rc3, NULL) < 0) {
        log_err ("broker.rc3_path is not set");
        return -1;
    }
    if (attr_get (ctx->attrs, "broker.rc2_none", NULL, NULL) == 0)
        rc2_none = true;

    if (!(ctx->runat = runat_create (ctx->h, local_uri, ctx->sd_notify))) {
        log_err ("runat_create");
        return -1;
    }

    /* rc1 - initialization
     */
    if (rc1 && strlen (rc1) > 0) {
        if (runat_push_shell_command (ctx->runat,
                                      "rc1",
                                      rc1,
                                      RUNAT_FLAG_LOG_STDIO) < 0) {
            log_err ("runat_push_shell_command rc1");
            return -1;
        }
    }

    /* rc2 - initial program
     */
    if (ctx->rank == 0 && !rc2_none) {
        if (create_runat_rc2 (ctx->runat,
                              ctx->init_shell_cmd,
                              ctx->init_shell_cmd_len) < 0) {
            log_err ("create_runat_rc2");
            return -1;
        }
    }

    /* rc3 - finalization
     */
    if (rc3 && strlen (rc3) > 0) {
        if (runat_push_shell_command (ctx->runat,
                                      "rc3",
                                      rc3,
                                      RUNAT_FLAG_LOG_STDIO) < 0) {
            log_err ("runat_push_shell_command rc3");
            return -1;
        }
    }
    return 0;
}

static int checkdir (const char *name, const char *path)
{
    struct stat sb;

    if (stat (path, &sb) < 0) {
        log_err ("cannot stat %s %s", name, path);
        return -1;
    }
    if (sb.st_uid != getuid ()) {
        errno = EPERM;
        log_err ("%s %s is not owned by instance owner", name, path);
        return -1;
    }
    if (!S_ISDIR (sb.st_mode)) {
        errno = ENOTDIR;
        log_err ("%s %s", name, path);
        return -1;
    }
    if ((sb.st_mode & S_IRWXU) != S_IRWXU) {
        log_msg ("%s %s does not have owner=rwx permissions", name, path);
        errno = EPERM;
        return -1;
    }
    return 0;
}

/* Validate statedir, if set.
 * Ensure that the attribute cannot change from this point forward.
 */
static int check_statedir (attr_t *attrs)
{
    const char *statedir;

    if (attr_get (attrs, "statedir", &statedir, NULL) < 0) {
        if (attr_add (attrs, "statedir", NULL, ATTR_IMMUTABLE) < 0) {
            log_err ("error creating statedir broker attribute");
            return -1;
        }
    }
    else {
        if (checkdir ("statedir", statedir) < 0)
            return -1;
        if (attr_set_flags (attrs, "statedir", ATTR_IMMUTABLE) < 0) {
            log_err ("error setting statedir broker attribute flags");
            return -1;
        }
    }
    return 0;
}

static int create_rundir_symlinks (const char *run_dir, flux_error_t *error)
{
    char path[1024];
    size_t size = sizeof (path);
    const char *target;

    if (strlcpy (path, run_dir, size) >= size
        || strlcat (path, "/bin", size) >= size)
        goto overflow;
    if (mkdir (path, 0755) < 0) {
        errprintf (error, "mkdir %s: %s", path, strerror (errno));
        return -1;
    }
    cleanup_push_string (cleanup_directory_recursive, path);
    if (strlcat (path, "/flux", size) >= size)
        goto overflow;
    if (executable_is_intree () == 1)
        target = ABS_TOP_BUILDDIR "/src/cmd/flux";
    else
        target = X_BINDIR "/flux";
    if (symlink (target, path) < 0) {
        errprintf (error, "symlink %s: %s", path, strerror (errno));
        return -1;
    }
    return 0;
overflow:
    errprintf (error, "buffer overflow");
    errno = EOVERFLOW;
    return -1;
}

/*  Handle global rundir attribute.
 */
static int create_rundir (attr_t *attrs)
{
    const char *tmpdir;
    const char *run_dir = NULL;
    char path[1024];
    int len;
    bool do_cleanup = true;
    int rc = -1;

    /*  If rundir attribute isn't set, then create a temp directory
     *   and use that as rundir. If directory was set, try to create it if
     *   it doesn't exist. If directory was pre-existing, do not schedule
     *   the dir for auto-cleanup at broker exit.
     */
    if (attr_get (attrs, "rundir", &run_dir, NULL) < 0) {
        if (!(tmpdir = getenv ("TMPDIR")))
            tmpdir = "/tmp";
        len = snprintf (path, sizeof (path), "%s/flux-XXXXXX", tmpdir);
        if (len >= sizeof (path)) {
            log_msg ("rundir buffer overflow");
            goto done;
        }
        if (!(run_dir = mkdtemp (path))) {
            log_err ("cannot create directory in %s", tmpdir);
            goto done;
        }
        if (attr_add (attrs, "rundir", run_dir, 0) < 0) {
            log_err ("error setting rundir broker attribute");
            goto done;
        }
    }
    else if (mkdir (run_dir, 0700) < 0) {
        if (errno != EEXIST) {
            log_err ("error creating rundir %s ", run_dir);
            goto done;
        }
        /* Do not cleanup directory if we did not create it here
         */
        do_cleanup = false;
    }

    /*  Ensure created or existing directory is writeable:
     */
    if (checkdir ("rundir", run_dir) < 0)
        goto done;

    /*  Ensure that AF_UNIX sockets can be created in rundir - see #3925.
     */
    struct sockaddr_un sa;
    size_t path_limit = sizeof (sa.sun_path) - sizeof ("/local-9999");
    size_t path_length = strlen (run_dir);
    if (path_length > path_limit) {
        log_msg ("rundir length of %zu bytes exceeds max %zu"
                 " to allow for AF_UNIX socket creation.",
                 path_length,
                 path_limit);
        goto done;
    }

    /*  rundir is now fixed, so make the attribute immutable, and
     *   schedule the dir for cleanup at exit if we created it here.
     */
    if (attr_set_flags (attrs, "rundir", ATTR_IMMUTABLE) < 0) {
        log_err ("error setting rundir broker attribute flags");
        goto done;
    }

    /*  Create $rundir/bin/flux so flux-relay can be found - see #5583.
     */
    flux_error_t error;
    if (create_rundir_symlinks (run_dir, &error) < 0) {
        if (errno != EEXIST)
            log_err ("error creating rundir symlinks: %s", error.text);
        // if this fails, soldier on
    }

    rc = 0;
done:
    if (do_cleanup && run_dir != NULL)
        cleanup_push_string (cleanup_directory_recursive, run_dir);
    return rc;
}

static int init_local_uri_attr (struct overlay *ov, attr_t *attrs)
{
    const char *uri;

    if (attr_get (attrs, "local-uri", &uri, NULL) < 0) {
        uint32_t rank = overlay_get_rank (ov);
        const char *rundir;
        char buf[1024];

        if (attr_get (attrs, "rundir", &rundir, NULL) < 0) {
            log_msg ("rundir attribute is not set");
            return -1;
        }
        if (snprintf (buf, sizeof (buf), "local://%s/local-%d",
                      rundir, rank) >= sizeof (buf)) {
            log_msg ("buffer overflow while building local-uri");
            return -1;
        }
        if (attr_add (attrs, "local-uri", buf, ATTR_IMMUTABLE) < 0) {
            log_err ("setattr local-uri");
            return -1;
        }
    }
    else {
        char path[1024];

        if (!strstarts (uri, "local://")) {
            log_msg ("local-uri is malformed");
            return -1;
        }
        if (snprintf (path, sizeof (path), "%s", uri + 8) >= sizeof (path)) {
            log_msg ("buffer overflow while checking local-uri");
            return -1;
        }
        if (checkdir ("local-uri directory", dirname (path)) < 0)
            return -1;

        /* see #3925 */
        struct sockaddr_un sa;
        size_t path_limit = sizeof (sa.sun_path) - 1;
        size_t path_length = strlen (uri + 8);
        if (path_length > path_limit) {
            log_msg ("local-uri length of %zu bytes exceeds max %zu"
                     " AF_UNIX socket path length",
                     path_length,
                     path_limit);
            return -1;
        }
    }
    return 0;
}

static int init_critical_ranks_attr (struct overlay *ov, attr_t *attrs)
{
    int rc = -1;
    const char *val;
    char *ranks = NULL;
    struct idset *critical_ranks = NULL;

    if (attr_get (attrs, "broker.critical-ranks", &val, NULL) < 0) {
        if (!(critical_ranks = overlay_get_default_critical_ranks (ov))
            || !(ranks = idset_encode (critical_ranks, IDSET_FLAG_RANGE))) {
            log_err ("unable to calculate critical-ranks attribute");
            goto out;
        }
        if (attr_add (attrs,
                      "broker.critical-ranks",
                      ranks,
                      ATTR_IMMUTABLE) < 0) {
            log_err ("attr_add critical_ranks");
            goto out;
        }
    }
    else {
        if (!(critical_ranks = idset_decode (val))
            || idset_last (critical_ranks) >= overlay_get_size (ov)) {
            log_msg ("invalid value for broker.critical-ranks='%s'", val);
            goto out;
        }
        /*  Need to set immutable flag when attr set on command line
         */
        if (attr_set_flags (attrs,
                            "broker.critical-ranks",
                            ATTR_IMMUTABLE) < 0) {
            log_err ("failed to make broker.criitcal-ranks attr immutable");
            goto out;
        }
    }
    rc = 0;
out:
    idset_destroy (critical_ranks);
    free (ranks);
    return rc;
}

static flux_future_t *set_uri_job_memo (flux_t *h,
                                        flux_jobid_t id,
                                        attr_t *attrs)
{
    const char *local_uri = NULL;
    const char *path;
    char uri [1024];
    char hostname [MAXHOSTNAMELEN + 1];

    if (attr_get (attrs, "local-uri", &local_uri, NULL) < 0) {
        log_err ("Unexpectedly unable to fetch local-uri attribute");
        return NULL;
    }
    if (gethostname (hostname, sizeof (hostname)) < 0) {
        log_err ("gethostname failure");
        return NULL;
    }
    path = local_uri + 8; /* forward past "local://" */
    if (snprintf (uri,
                 sizeof (uri),
                 "ssh://%s%s",
                 hostname, path) >= sizeof (uri)) {
        log_msg ("buffer overflow while checking local-uri");
        return NULL;
    }
    return flux_rpc_pack (h,
                          "job-manager.memo",
                          FLUX_NODEID_ANY,
                          0,
                          "{s:I s:{s:s}}",
                          "id", id,
                          "memo",
                            "uri", uri);
}

/*  Encode idset of critical nodes/shell ranks, which is calculated
 *   from broker.mapping and broker.critical-ranks.
 */
static char *encode_critical_nodes (attr_t *attrs)
{
    struct idset *ranks = NULL;
    struct idset *nodeids = NULL;
    struct taskmap *map = NULL;
    char *s = NULL;
    int nodeid;
    const char *mapping;
    const char *ranks_attr;
    unsigned int i;


    if (attr_get (attrs, "broker.mapping", &mapping, NULL) < 0
        || mapping == NULL
        || !(map = taskmap_decode (mapping, NULL))
        || attr_get (attrs, "broker.critical-ranks", &ranks_attr, NULL) < 0
        || !(ranks = idset_decode (ranks_attr))
        || !(nodeids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        goto done;

    /*  Map the broker ranks from the broker.critical-ranks attr to
     *  shell ranks/nodeids using PMI_process_mapping (this handles the
     *  rare case where multiple brokers per node/shell were launched)
     */
    i = idset_first (ranks);
    while (i != IDSET_INVALID_ID) {
        if ((nodeid = taskmap_nodeid (map, i)) < 0
            || idset_set (nodeids, nodeid) < 0)
            goto done;
        i = idset_next (ranks, i);
    }
    s = idset_encode (nodeids, IDSET_FLAG_RANGE);
done:
    taskmap_destroy (map);
    idset_destroy (ranks);
    idset_destroy (nodeids);
    return s;
}

static flux_future_t *set_critical_ranks (flux_t *h,
                                          flux_jobid_t id,
                                          attr_t *attrs)
{
    int saved_errno;
    flux_future_t *f;
    char *nodeids;

    if (!(nodeids = encode_critical_nodes (attrs)))
        return NULL;
    f = flux_rpc_pack (h,
                       "job-exec.critical-ranks",
                       FLUX_NODEID_ANY, 0,
                       "{s:I s:s}",
                       "id", id,
                       "ranks", nodeids);
    saved_errno = errno;
    free (nodeids);
    errno = saved_errno;
    return f;
}

static int execute_parental_notifications (struct broker *ctx)
{
    const char *jobid = NULL;
    const char *parent_uri = NULL;
    flux_jobid_t id;
    flux_t *h = NULL;
    flux_future_t *f = NULL;
    flux_future_t *f2 = NULL;
    int rc = -1;

    /* Skip if "jobid" or "parent-uri" not set, this is probably
     *  not a child of any Flux instance.
     */
    if (attr_get (ctx->attrs, "parent-uri", &parent_uri, NULL) < 0
        || parent_uri == NULL
        || attr_get (ctx->attrs, "jobid", &jobid, NULL) < 0
        || jobid == NULL)
        return 0;

    if (flux_job_id_parse (jobid, &id) < 0) {
        log_err ("Unable to parse jobid attribute '%s'", jobid);
        return -1;
    }

    /*  Open connection to parent instance:
     */
    if (!(h = flux_open (parent_uri, 0))) {
        log_err ("flux_open to parent failed");
        return -1;
    }

    /*  Perform any RPCs to parent in parallel */
    if (!(f = set_uri_job_memo (h, id, ctx->attrs)))
        goto out;

    /*  Note: not an error if rpc to set critical ranks fails, but
     *  issue an error notifying user that no criitcal ranks are set.
     */
    if (!(f2 = set_critical_ranks (h, id, ctx->attrs)))
        log_msg ("Unable to get critical ranks, all ranks will be critical");

    /*  Wait for RPC results */
    if (flux_future_get (f, NULL) < 0) {
        log_err ("job-manager.memo uri");
        goto out;
    }
    if (f2 && flux_future_get (f2, NULL) < 0 && errno != ENOSYS) {
        log_err ("job-exec.critical-ranks");
        goto out;
    }
    rc = 0;
out:
    flux_close (h);
    flux_future_destroy (f);
    flux_future_destroy (f2);
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

static int mod_svc_cb (flux_msg_t **msg, void *arg)
{
    module_t *p = arg;
    return module_sendmsg_new (p, msg);
}

static void broker_destroy_sigwatcher (void *data)
{
    flux_watcher_t *w = data;
    flux_watcher_stop (w);
    flux_watcher_destroy (w);
}

static int broker_handle_signals (broker_ctx_t *ctx)
{
    int i, sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM,
                      SIGALRM, SIGUSR1, SIGUSR2 };
    int blocked[] = { SIGPIPE };
    flux_watcher_t *w;

    for (i = 0; i < ARRAY_SIZE (sigs); i++) {
        w = flux_signal_watcher_create (ctx->reactor, sigs[i], signal_cb, ctx);
        if (!w) {
            log_err ("flux_signal_watcher_create");
            return -1;
        }
        if (zlist_push (ctx->sigwatchers, w) < 0) {
            log_errn (ENOMEM, "zlist_push");
            return -1;
        }
        zlist_freefn (ctx->sigwatchers, w, broker_destroy_sigwatcher, false);
        flux_watcher_start (w);
    }

    /*  Block the list of signals in the blocked array */
    for (i = 0; i < ARRAY_SIZE (blocked); i++)
        signal(blocked[i], SIG_IGN);
    return 0;
}

/**
 ** Built-in services
 **/

#if CODE_COVERAGE_ENABLED
void __gcov_dump (void);
void __gcov_reset (void);
#endif

static void broker_disconnect_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
}

static int route_to_handle (flux_msg_t **msg, void *arg)
{
    broker_ctx_t *ctx = arg;
    if (flux_send_new (ctx->h_internal, msg, 0) < 0) {
        flux_log_error (ctx->h, "send failed on internal broker handle");
        return -1;
    }
    return 0;
}

/* Check whether requestor 'cred' is authorized to add/remove service 'name'.
 * Allow a guest control over a service IFF it is prefixed with "<userid>-".
 * Return 0 on success, -1 with errno set on failure.
 */
static int service_allow (struct flux_msg_cred cred, const char *name)
{
    char prefix[16];
    if ((cred.rolemask & FLUX_ROLE_OWNER))
        return 0;
    snprintf (prefix, sizeof (prefix), "%" PRIu32 "-", cred.userid);
    if (strstarts (name, prefix))
        return 0;
    errno = EPERM;
    return -1;
}

/* Dynamic service registration.
 * These handlers need to appear in broker.c so that they have
 *  access to broker internals like modhash
 */
static void service_add_cb (flux_t *h,
                            flux_msg_handler_t *w,
                            const flux_msg_t *msg,
                            void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *name = NULL;
    const char *sender;
    module_t *p;
    struct flux_msg_cred cred;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "service", &name) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (service_allow (cred, name) < 0)
        goto error;
    if (!(sender = flux_msg_route_first (msg))) {
        errno = EPROTO;
        goto error;
    }
    if (!(p = modhash_lookup (ctx->modhash, sender))) {
        errno = ENOENT;
        goto error;
    }
    if (service_add (ctx->services, name, sender, mod_svc_cb, p) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "service_add: flux_respond");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "service_add: flux_respond_error");
}

static void service_remove_cb (flux_t *h,
                               flux_msg_handler_t *w,
                               const flux_msg_t *msg,
                               void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *name;
    const char *uuid;
    const char *sender;
    struct flux_msg_cred cred;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "service", &name) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (service_allow (cred, name) < 0)
        goto error;
    if (!(sender = flux_msg_route_first (msg))) {
        errno = EPROTO;
        goto error;
    }
    if (!(uuid = service_get_uuid (ctx->services, name))) {
        errno = ENOENT;
        goto error;
    }
    if (!streq (uuid, sender)) {
        errno = EINVAL;
        goto error;
    }
    service_remove (ctx->services, name);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "service_remove: flux_respond");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "service_remove: flux_respond_error");
}


static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "broker.rusage",
        method_rusage_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "broker.ping",
        method_ping_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "broker.disconnect",
        broker_disconnect_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "service.add",
        service_add_cb,
        FLUX_ROLE_USER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "service.remove",
        service_remove_cb,
        FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct internal_service {
    const char *name;
    const char *nodeset;
};

static struct internal_service services[] = {
    { "broker",             NULL }, // kind of a catch-all, slowly deprecating
    { "log",                NULL },
    { "attr",               NULL },
    { "heaptrace",          NULL },
    { "event",              NULL },
    { "service",            NULL },
    { "overlay",            NULL },
    { "module",             NULL },
    { "config",             NULL },
    { "runat",              NULL },
    { "state-machine",      NULL },
    { "groups",             NULL },
    { "shutdown",           NULL },
    { "rexec",              NULL },
    { NULL, NULL, },
};

/* Register builtin services (sharing ctx->h and broker thread).
 * Register message handlers for some broker services.  Others are registered
 * in their own initialization functions.
 */
static flux_msg_handler_t **broker_add_services (broker_ctx_t *ctx)
{
    flux_msg_handler_t **handlers;
    struct internal_service *svc;
    for (svc = &services[0]; svc->name != NULL; svc++) {
        if (!nodeset_member (svc->nodeset, ctx->rank))
            continue;
        if (service_add (ctx->services,
                         svc->name, NULL,
                         route_to_handle,
                         ctx) < 0) {
            log_err ("error registering service for %s", svc->name);
            return NULL;
        }
    }

    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &handlers) < 0) {
        log_err ("error registering message handlers");
        return NULL;
    }
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

/* Handle messages received from overlay peers.
 */
static int overlay_recv_cb (flux_msg_t **msg, overlay_where_t where, void *arg)
{
    broker_ctx_t *ctx = arg;
    int type;

    if (flux_msg_get_type (*msg, &type) < 0)
        return -1;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            /* broker_request_sendmsg_new() generates a response on error.
             */
            broker_request_sendmsg_new (ctx, msg);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            if (broker_response_sendmsg_new (ctx, msg) < 0)
                goto drop;
            break;
        case FLUX_MSGTYPE_EVENT:
            /* If event originated from upstream peer, then it has already been
             * published and we are to continue its distribution.
             * Otherwise, take the next step to get the event published.
             */
            if (where == OVERLAY_UPSTREAM) {
                if (handle_event (ctx, *msg) < 0)
                    goto drop;
            }
            else {
                if (broker_event_sendmsg_new (ctx, msg) < 0)
                    goto drop;
            }
            break;
        default:
            break;
    }
    flux_msg_decref (*msg);
    *msg = NULL;
    return 0;
drop:
    /* Suppress logging if a response could not be sent due to ENOSYS,
     * which happens if sending module unloads before finishing all RPCs.
     */
    if (type != FLUX_MSGTYPE_RESPONSE || errno != ENOSYS) {
        const char *topic = "unknown";
        (void)flux_msg_get_topic (*msg, &topic);
        flux_log_error (ctx->h,
                        "DROP %s %s topic=%s",
                        where == OVERLAY_UPSTREAM ? "upstream" : "downstream",
                        flux_msg_typestr (type),
                        topic);
    }
    return -1;
}

/* Distribute events downstream, and to module and broker-resident subscribers.
 * On rank 0, publisher is wired to send events here also.
 */
static int handle_event (broker_ctx_t *ctx, const flux_msg_t *msg)
{
    uint32_t seq;
    const char *topic;

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
    overlay_sendmsg (ctx->overlay, msg, OVERLAY_DOWNSTREAM);

    /* Internal services may install message handlers for events.
     */
    if (subhash_topic_match (ctx->sub, topic)) {
        if (flux_send (ctx->h_internal, msg, 0) < 0)
            flux_log_error (ctx->h, "send failed on internal broker handle");
    }
    /* Finally, route to local module subscribers.
     */
    return modhash_event_mcast (ctx->modhash, msg);
}

static bool signal_is_deadly (int signum)
{
    int deadly_sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGALRM };
    for (int i = 0; i < ARRAY_SIZE (deadly_sigs); i++) {
        if (signum == deadly_sigs[i])
            return true;
    }
    return false;
}

static void killall_cb (flux_future_t *f, void *arg)
{
    broker_ctx_t *ctx = arg;
    int count = 0;
    if (flux_rpc_get_unpack (f, "{s:i}", "count", &count) < 0) {
        flux_log_error (ctx->h,
                        "job-manager.killall: %s",
                        future_strerror (f, errno));
    }
    flux_future_destroy (f);
    if (count) {
        flux_log (ctx->h,
                  LOG_INFO,
                  "forwarded signal %d to %d jobs",
                  (int) ptr2int (flux_future_aux_get (f, "signal")),
                  count);
    }
}

static int killall_jobs (broker_ctx_t *ctx, int signum)
{
    flux_future_t *f = NULL;
    if (!(f = flux_rpc_pack (ctx->h,
                             "job-manager.killall",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:b s:i s:i}",
                             "dry_run", 0,
                             "userid", FLUX_USERID_UNKNOWN,
                             "signum", signum))
        || flux_future_then (f, -1., killall_cb, ctx) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    if (flux_future_aux_set (f, "signum", int2ptr (signum), NULL) < 0)
        flux_log_error (ctx->h, "killall: future_aux_set");
    return 0;
}

static void signal_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg)
{
    broker_ctx_t *ctx = arg;
    int signum = flux_signal_watcher_get_signum (w);

    flux_log (ctx->h, LOG_INFO, "signal %d", signum);

    if (ctx->rank == 0 && !signal_is_deadly (signum)) {
        /* Attempt to forward non-deadly signals to jobs. If that fails,
         * then fall through to state_machine_kill() so the signal is
         * delivered somewhere.
         */
        if (killall_jobs (ctx, signum) == 0)
            return;
        /*
         * Note: flux_rpc(3) in the rank 0 broker to the job manager module
         *  is expected to fail immediately if the job-manager module is not
         *  loaded due to the broker internal flux_t handle implementation.
         */
        flux_log (ctx->h,
                  LOG_INFO,
                  "killall failed, delivering signal %d locally instead",
                  signum);
    }
    state_machine_kill (ctx->state_machine, signum);
}

/* Route request.
 * On success, return 0.  On failure, return -1 with errno set.
 */
static int broker_request_sendmsg_new_internal (broker_ctx_t *ctx,
                                                flux_msg_t **msg)
{
    bool upstream = flux_msg_has_flag (*msg, FLUX_MSGFLAG_UPSTREAM);
    uint32_t nodeid;

    if (flux_msg_get_nodeid (*msg, &nodeid) < 0)
        return -1;
    /* Route up TBON if destination if upstream of this broker.
     */
    if (upstream && nodeid == ctx->rank) {
        if (overlay_sendmsg_new (ctx->overlay, msg, OVERLAY_UPSTREAM) < 0)
            return -1;
    }
    /* Deliver to local service if destination *could* be this broker.
     * If there is no such service locally (ENOSYS), route up TBON.
     */
    else if ((upstream && nodeid != ctx->rank) || nodeid == FLUX_NODEID_ANY) {
        if (service_send_new (ctx->services, msg) < 0) {
            if (errno != ENOSYS)
                return -1;
            if (overlay_sendmsg_new (ctx->overlay, msg, OVERLAY_UPSTREAM) < 0) {
                if (errno == EHOSTUNREACH)
                    errno = ENOSYS;
                return -1;
            }
        }
    }
    /* Deliver to local service if this broker is the addressed rank.
     */
    else if (nodeid == ctx->rank) {
        if (service_send_new (ctx->services, msg) < 0)
            return -1;
    }
    /* Send the request up or down TBON as addressed.
     */
    else {
        if (overlay_sendmsg_new (ctx->overlay, msg, OVERLAY_ANY) < 0)
            return -1;
    }
    return 0;
}

/* Route request.  If there is an error routing the request,
 * generate an error response.  Make an extra effort to return a useful
 * error message if ENOSYS indicates an unmatched service name.
 */
void broker_request_sendmsg_new (broker_ctx_t *ctx, flux_msg_t **msg)
{
    if (broker_request_sendmsg_new_internal (ctx, msg) < 0) {
        const char *topic;
        char errbuf[64];
        const char *errstr = NULL;

        if (errno == ENOSYS && flux_msg_get_topic (*msg, &topic) == 0) {
            snprintf (errbuf,
                      sizeof (errbuf),
                      "No service matching %s is registered", topic);
            errstr = errbuf;
        }
        if (flux_respond_error (ctx->h, *msg, errno, errstr) < 0)
            flux_log_error (ctx->h, "flux_respond");
        flux_msg_decref (*msg);
        *msg = NULL;
    }
}

/* Route a response message, determining next hop from route stack.
 * If there is no next hop, routing is complete to broker-resident service.
 * If the next hop is an overlay peer, route up or down the TBON.
 * If not a peer, look up a module by uuid.
 */
int broker_response_sendmsg_new (broker_ctx_t *ctx, flux_msg_t **msg)
{
    const char *uuid;

    if (!(uuid = flux_msg_route_last (*msg))) {
        if (flux_send_new (ctx->h_internal, msg, 0) < 0)
            return -1;
    }
    else if (overlay_uuid_is_parent (ctx->overlay, uuid)) {
        if (overlay_sendmsg_new (ctx->overlay, msg, OVERLAY_UPSTREAM) < 0)
            return -1;
    }
    else if (overlay_uuid_is_child (ctx->overlay, uuid)) {
        if (overlay_sendmsg_new (ctx->overlay, msg, OVERLAY_DOWNSTREAM) < 0)
            return -1;
    }
    else {
        if (modhash_response_sendmsg_new (ctx->modhash, msg) < 0)
            return -1;
    }
    return 0;
}

/* Events are forwarded up the TBON to rank 0, then published per RFC 3.
 * An alternate publishing mechanism that allows the event sequence number
 * to be obtained is to send an RPC to event.pub.
 */
int broker_event_sendmsg_new (broker_ctx_t *ctx, flux_msg_t **msg)
{
    if (ctx->rank > 0) {
        if (overlay_sendmsg_new (ctx->overlay, msg, OVERLAY_UPSTREAM) < 0)
            return -1;
    }
    else {
        if (publisher_send (ctx->publisher, *msg) < 0)
            return -1;
        flux_msg_decref (*msg);
        *msg = NULL;
    }
    return 0;
}

/* Handle messages received from the "router end" of the back to back
 * interthread handles.  Hand the message off to routing logic.
 */
static void h_internal_watcher (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg)
{
    flux_t *h = flux_handle_watcher_get_flux (w);
    broker_ctx_t *ctx = arg;
    flux_msg_t *msg;
    int type;

    if (!(msg = flux_recv (h, FLUX_MATCH_ANY, 0))
        || flux_msg_get_type (msg, &type) < 0)
        goto error;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            if (broker_request_sendmsg_new_internal (ctx, &msg) < 0)
                goto error;
            break;
        case FLUX_MSGTYPE_RESPONSE:
            if (broker_response_sendmsg_new (ctx, &msg) < 0)
                goto error;
            break;
        case FLUX_MSGTYPE_EVENT:
            if (broker_event_sendmsg_new (ctx, &msg) < 0)
                goto error;
            break;
        default:
            goto error;
    }
    return;
error:
    flux_msg_destroy (msg);
}

#if HAVE_VALGRIND
/* Disable dlclose() during valgrind operation
 */
void I_WRAP_SONAME_FNNAME_ZZ(Za,dlclose)(void *dso) {}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
