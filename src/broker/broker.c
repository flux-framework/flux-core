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
#include <libgen.h>
#include <signal.h>
#include <locale.h>
#include <inttypes.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif
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
#include "src/common/libutil/basename.h"
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
#include "overlay.h"
#include "service.h"
#include "attr.h"
#include "log.h"
#include "runat.h"
#include "heaptrace.h"
#include "boot_config.h"
#include "boot_pmi.h"
#include "state_machine.h"
#include "shutdown.h"
#include "rundir.h"

#include "broker.h"


static void h_internal_watcher (flux_reactor_t *r,
                                flux_watcher_t *w,
                                int revents,
                                void *arg);
static void overlay_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg);

static void signal_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg);
static int broker_handle_signals (broker_ctx_t *ctx);

static flux_msg_handler_t **broker_add_services (broker_ctx_t *ctx,
                                                 flux_error_t *error);

static void set_proctitle (uint32_t rank);

static int create_runat_phases (broker_ctx_t *ctx, flux_error_t *error);

static int init_attrs (attr_t *attrs,
                       pid_t pid,
                       struct flux_msg_cred *cred,
                       flux_error_t *error);

static int init_attrs_post_boot (attr_t *attrs, flux_error_t *error);

static int init_attrs_starttime (attr_t *attrs,
                                 double starttime,
                                 flux_error_t *error);

static int init_local_uri_attr (struct overlay *ov,
                                attr_t *attrs,
                                flux_error_t *error);

static int init_critical_ranks_attr (struct overlay *ov,
                                     attr_t *attrs,
                                     flux_error_t *error);

static int execute_parental_notifications (struct broker *ctx,
                                           flux_error_t *error);

static struct optparse_option opts[] = {
    { .name = "verbose",    .key = 'v', .has_arg = 2, .arginfo = "[LEVEL]",
      .usage = "Be annoyingly informative by degrees", },
    { .name = "setattr",    .key = 'S', .has_arg = 1, .arginfo = "ATTR=VAL",
      .usage = "Set broker attribute", },
    { .name = "config-path",.key = 'c', .has_arg = 1, .arginfo = "PATH",
      .usage = "Set broker config from PATH (default: none)", },
    OPTPARSE_TABLE_END,
};

int parse_command_line_arguments (broker_ctx_t *ctx,
                                  int argc,
                                  char *argv[],
                                  flux_error_t *errp)
{
    int optindex;
    const char *arg;

    if (!(ctx->opts = optparse_create ("flux-broker"))
        || optparse_add_option_table (ctx->opts, opts) != OPTPARSE_SUCCESS)
        return errprintf (errp, "error setting up option parsing");
    if ((optindex = optparse_parse_args (ctx->opts, argc, argv)) < 0)
        exit (1);

    ctx->verbose = optparse_get_int (ctx->opts, "verbose", 0);

    optparse_get_str (ctx->opts, "config-path", NULL);

    while ((arg = optparse_getopt_next (ctx->opts, "setattr"))) {
        char *val, *attr;
        if (!(attr = strdup (arg)))
            return errprintf (errp, "out of memory duplicating optarg");
        if ((val = strchr (attr, '=')))
            *val++ = '\0';
        if (attr_add (ctx->attrs, attr, val, 0) < 0) {
            if (attr_set (ctx->attrs, attr, val) < 0) {
                errprintf (errp,
                           "setattr %s=%s: %s",
                           attr,
                           val,
                           strerror (errno));
                free (attr);
                return -1;
            }
        }
        free (attr);
    }

    if (optindex < argc) {
        int e;
        if ((e = argz_create (argv + optindex,
                              &ctx->init_shell_cmd,
                              &ctx->init_shell_cmd_len)) != 0)
            return errprintf (errp, "argz_create: %s", strerror (e));
    }
    return 0;
}

static int increase_rlimits (void)
{
    struct rlimit rlim;

    /*  Increase number of open files to max to prevent potential failures
     *   due to file descriptor exhaustion (e.g. failure to open /dev/urandom)
     */
    if (getrlimit (RLIMIT_NOFILE, &rlim) < 0)
        return -1;
    rlim.rlim_cur = rlim.rlim_max;
    return setrlimit (RLIMIT_NOFILE, &rlim);
}

/* SIGHUP handler that resets SIGHUP handling to default after
 * the receipt of one signal. This is used just before exit so
 * the broker can signal its own process group.
 */
static void sighup_handler (int signum)
{
    signal (SIGHUP, SIG_DFL);
}

int main (int argc, char *argv[])
{
    broker_ctx_t ctx;
    flux_error_t error;
    sigset_t old_sigmask;
    struct sigaction old_sigact_int;
    struct sigaction old_sigact_term;
    flux_msg_handler_t **handlers = NULL;
    const flux_conf_t *conf;
    const char *method;

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

    if (init_attrs (ctx.attrs, getpid (), &ctx.cred, &error) < 0)
        log_msg_exit ("%s", error.text);

    const char *hostname = getenv ("FLUX_FAKE_HOSTNAME");
    if (hostname)
        strlcpy (ctx.hostname, hostname, sizeof (ctx.hostname));
    else if (gethostname (ctx.hostname, sizeof (ctx.hostname)) < 0)
        log_err_exit ("gethostname");

    if (parse_command_line_arguments (&ctx, argc, argv, &error) < 0)
        log_msg_exit ("%s", error.text);

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
     * share a reactor.
     *
     * N.B. since both handles are in the same thread, synchronous RPCs on
     * ctx.h will deadlock.  The main broker reactor must run in order
     * to move messages from the interthread queue to the routing logic.
     * Careful with flux_attr_get(), which hides a synchronous RPC if the
     * requested value is not cached.
     */
    if (!(ctx.reactor = flux_reactor_create (0))
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

    /* Set early logger configuration so that flux_log(3) et al can
     * be used before the rank is known, reactor is running, etc..
     */
    flux_log_set_redirect (ctx.h, log_early, ctx.attrs);
    flux_log_set_hostname (ctx.h, ctx.hostname); // in lieu of rank
    flux_log_set_appname (ctx.h, "broker");

    /* Give log-stderr-mode and log-stderr-level some values informed
     * by --verbose if they weren't explicitly specified on the command line.
     * No fatal errors here - best effort.
     */
    if (attr_get (ctx.attrs, "log-stderr-mode", NULL, NULL) < 0) {
        if (ctx.verbose > 0)
            (void)attr_add (ctx.attrs, "log-stderr-mode", "local", 0);
    }
    if (attr_get (ctx.attrs, "log-stderr-level", NULL, NULL) < 0) {
        int level;
        if (ctx.verbose > 1)
            level = LOG_DEBUG;
        else if (ctx.verbose == 1)
            level = LOG_INFO;
        else
            level = LOG_ERR;
        (void)attr_add_int (ctx.attrs, "log-stderr-level", level, 0);
    }

    const char *val;
    if (attr_get (ctx.attrs, "broker.sd-notify", &val, NULL) == 0
        && !streq (val, "0")) {
#if !HAVE_LIBSYSTEMD
        flux_log (ctx.h,
                  LOG_CRIT,
                  "broker.sd_notify is set but Flux was not built"
                  " with systemd support.");
        goto cleanup;
#else
        ctx.sd_notify = true;
#endif
    }

    /* Initialize module infrastructure.
     */
    if (!(ctx.modhash = modhash_create (&ctx))) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "error creating broker module hash: %s",
                  strerror (errno));
        goto cleanup;
    }

    /* Parse config.
     */
    if (!(ctx.config = brokercfg_create (ctx.h,
                                         optparse_get_str (ctx.opts,
                                                           "config-path",
                                                           NULL),
                                         ctx.attrs,
                                         ctx.modhash,
                                         &error))) {
        flux_log (ctx.h, LOG_CRIT, "%s", error.text);
        goto cleanup;
    }
    conf = flux_get_conf (ctx.h);

    if (increase_rlimits () < 0) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "Failed to increase nofile limit: %s", strerror (errno));
        goto cleanup;
    }

    /* Prepare signal handling
     */
    if (broker_handle_signals (&ctx) < 0) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "Error installing signal handlers: %s",
                  strerror (errno));
        goto cleanup;
    }

    if (!(ctx.overlay = overlay_create (ctx.h,
                                        ctx.hostname,
                                        ctx.attrs,
                                        NULL,
                                        "interthread://overlay",
                                        &error))) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "Error initializing overlay: %s",
                  error.text);
        goto cleanup;
    }
    if (!(ctx.h_overlay = flux_open ("interthread://overlay", 0))
        || flux_set_reactor (ctx.h_overlay, ctx.reactor) < 0
        || !(ctx.w_overlay = flux_handle_watcher_create (ctx.reactor,
                                                         ctx.h_overlay,
                                                          FLUX_POLLIN,
                                                          overlay_cb,
                                                          &ctx))) {
        log_err ("error opening overlay message channel");
        goto cleanup;
    }
    flux_watcher_start (ctx.w_overlay);

    /* Create rundir now as it may be needed for overlay sockets during
     * bootstrap.  N.B. tmpdir is used later when statedir is created.
     */
    const char *tmpdir = getenv ("TMPDIR");
    if (rundir_create (ctx.attrs,
                       "rundir",
                       tmpdir ? tmpdir : "/tmp",
                       &error) < 0) {
        flux_log (ctx.h, LOG_CRIT, "rundir %s", error.text);
        goto cleanup;
    }

    /* Record the broker start time.  This time will also be used to
     * capture how long network bootstrap takes.
     */
    flux_reactor_now_update (ctx.reactor);
    ctx.starttime = flux_reactor_now (ctx.reactor);
    if (init_attrs_starttime (ctx.attrs, ctx.starttime, &error) < 0) {
        flux_log (ctx.h, LOG_CRIT, "%s", error.text);
        goto cleanup;
    }

    /* Execute broker network bootstrap.
     * Default method is pmi.
     * If [bootstrap] is defined in configuration, use static configuration.
     */
    if (attr_get (ctx.attrs, "broker.boot-method", &method, NULL) < 0) {
        if (flux_conf_unpack (conf, NULL, "{s:{}}", "bootstrap") == 0)
            method = "config";
        else
            method = NULL;
    }
    if (!method || !streq (method, "config")) {
        if (boot_pmi (ctx.hostname, ctx.overlay, ctx.attrs, &error) < 0) {
            flux_log (ctx.h, LOG_CRIT, "bootstrap: %s", error.text);
            goto cleanup;
        }
    }
    else {
        if (boot_config (ctx.h,
                         ctx.hostname,
                         ctx.overlay,
                         ctx.attrs,
                         &error) < 0) {
            flux_log (ctx.h, LOG_CRIT, "bootstrap: %s", error.text);
            goto cleanup;
        }
    }

    if (init_attrs_post_boot (ctx.attrs, &error) < 0) {
        flux_log (ctx.h, LOG_CRIT, "%s", error.text);
        goto cleanup;
    }

    ctx.rank = overlay_get_rank (ctx.overlay);
    ctx.size = overlay_get_size (ctx.overlay);

    if (ctx.size == 0) {
        flux_log (ctx.h, LOG_CRIT, "internal error: instance size is zero!");
        goto cleanup;
    }

    /* Now that rank is known, create or check the statedir.
     * The statedir is only used on the leader broker, so unset the
     * attribute elsewhere.
     *
     * N.B. the system instance sets statedir to /var/lib/flux and supports
     * restarts, while other instances default to a temporary directory that
     * is removed on broker exit.  If TMPDIR is not set, use /var/tmp for
     * the temporary directory as opposed to /tmp which is more likely to
     * be a ram-backed tmpfs.
     *
     * See also: file-hierarchy(7)
     */
    if (ctx.rank == 0) {
        if (rundir_create (ctx.attrs,
                           "statedir",
                           tmpdir ? tmpdir : "/var/tmp",
                           &error) < 0) {
            flux_log (ctx.h, LOG_CRIT, "statedir %s", error.text);
            goto cleanup;
        }
    }
    else {
        (void)attr_delete (ctx.attrs, "statedir", true);
    }

    /* Must be called after overlay setup */
    if (overlay_register_attrs (ctx.overlay) < 0) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "registering overlay attributes: %s",
                  strerror (errno));
        goto cleanup;
    }

    if (ctx.verbose) {
        flux_reactor_now_update (ctx.reactor);
        flux_log (ctx.h,
                  LOG_INFO,
                  "boot: rank=%d size=%d time %.3fs",
                  ctx.rank,
                  ctx.size,
                  flux_reactor_now (ctx.reactor) - ctx.starttime);
    }

    /* Allow flux_get_rank(), flux_get_size(), flux_get_hostybyrank(), etc.
     * to work in the broker without causing a synchronous RPC to self that
     * would deadlock.
     */
    if (attr_cache_immutables (ctx.attrs, ctx.h) < 0) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "error priming broker attribute cache: %s",
                  strerror (errno));
        goto cleanup;
    }

    /* Initialize the full log subsystem.
     */
    if (logbuf_initialize (ctx.h, ctx.rank, ctx.attrs) < 0) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "Error initializing logging: %s",
                  strerror (errno));
        goto cleanup;
    }

    if (ctx.verbose) {
        const char *parent = overlay_get_parent_uri (ctx.overlay);
        const char *child = overlay_get_bind_uri (ctx.overlay);
        flux_log (ctx.h, LOG_INFO, "parent: %s", parent ? parent : "none");
        flux_log (ctx.h, LOG_INFO, "child: %s", child ? child : "none");
    }

    set_proctitle (ctx.rank);

    // N.B. local-uri is used by runat
    if (init_local_uri_attr (ctx.overlay, ctx.attrs, &error) < 0
        || init_critical_ranks_attr (ctx.overlay, ctx.attrs, &error) < 0) {
        flux_log (ctx.h, LOG_CRIT, "%s", error.text);
        goto cleanup;
    }

    /* Wire up the overlay.
     */
    if (ctx.rank > 0) {
        if (ctx.verbose)
            flux_log (ctx.h, LOG_INFO, "initializing overlay connect");
        if (overlay_connect (ctx.overlay) < 0) {
            flux_log (ctx.h, LOG_CRIT, "overlay_connect: %s", strerror (errno));
            goto cleanup;
        }
    }

    /* Register internal services
     */
    if (attr_register_handlers (ctx.attrs, ctx.h) < 0) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "attr_register_handlers: %s",
                  strerror (errno));
        goto cleanup;
    }
    if (heaptrace_initialize (ctx.h) < 0) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "heaptrace_initialize: %s",
                  strerror (errno));
        goto cleanup;
    }
    if (flux_aux_set (ctx.h,
                      "flux::uuid",
                      (char *)overlay_get_uuid (ctx.overlay),
                      NULL) < 0) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "error adding broker uuid to aux container: %s",
                  strerror (errno));
        goto cleanup;
    }
    if (!(handlers = broker_add_services (&ctx, &error))) {
        flux_log (ctx.h, LOG_CRIT, "%s", error.text);
        goto cleanup;
    }
    /* overlay_control_start() calls flux_sync_create(), thus
     * requires event.subscribe to have a handler before running.
     */
    if (overlay_control_start (ctx.overlay) < 0) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "error initializing overlay control messages: %s",
                  strerror (errno));
        goto cleanup;
    }

    /* Configure broker state machine
     */
    if (!(ctx.state_machine = state_machine_create (&ctx, &error))) {
        flux_log (ctx.h, LOG_CRIT, "%s", error.text);
        goto cleanup;
    }
    /* This registers a state machine callback so call after
     * state_machine_create().
     */
    if (create_runat_phases (&ctx, &error) < 0) {
        flux_log (ctx.h, LOG_CRIT, "%s", error.text);
        goto cleanup;
    }

    state_machine_kickoff (ctx.state_machine);

    /* Create shutdown mechanism
     */
    if (!(ctx.shutdown = shutdown_create (&ctx))) {
        flux_log (ctx.h,
                  LOG_CRIT,
                  "error creating shutdown mechanism: %s",
                  strerror (errno));
        goto cleanup;
    }

    if (ctx.rank == 0 && execute_parental_notifications (&ctx, &error) < 0) {
        flux_log (ctx.h, LOG_CRIT, "%s", error.text);
        goto cleanup;
    }

    /* Event loop
     */
    if (ctx.verbose > 1)
        flux_log (ctx.h, LOG_INFO, "entering event loop");
    /* Once we enter the reactor, default exit_rc is now 0 */
    ctx.exit_rc = 0;
    if (flux_reactor_run (ctx.reactor, 0) < 0)
        flux_log_error (ctx.h, "flux_reactor_run");
    if (ctx.verbose > 1)
        flux_log (ctx.h, LOG_INFO, "exited event loop");

cleanup:
    if (ctx.verbose > 1)
        flux_log (ctx.h, LOG_INFO, "cleaning up");

    /* If the broker is the current process group leader, send SIGHUP
     * to the current process group to attempt cleanup of any rc2
     * background processes. The broker will be process group leader
     * only if it was invoked as a child of the job shell, not when
     * launched as a direct child of flux-start(1) in test mode.
     */
    if (getpgrp () == getpid ()) {
        if (signal (SIGHUP, sighup_handler) < 0
            || kill (0, SIGHUP) < 0)
            flux_log_error (ctx.h, "failed to raise SIGHUP on process group");
    }

    /* Restore default sigmask and actions for SIGINT, SIGTERM
     */
    if (sigprocmask (SIG_SETMASK, &old_sigmask, NULL) < 0
        || sigaction (SIGINT, &old_sigact_int, NULL) < 0
        || sigaction (SIGTERM, &old_sigact_term, NULL) < 0)
        flux_log_error (ctx.h, "error restoring signal mask");

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
    flux_watcher_destroy (ctx.w_overlay);
    flux_close (ctx.h_overlay);
    overlay_destroy (ctx.overlay);
    service_switch_destroy (ctx.services);
    flux_msg_handler_delvec (handlers);
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

static int init_attrs_broker_pid (attr_t *attrs, pid_t pid, flux_error_t *errp)
{
    char *attrname = "broker.pid";
    char pidval[32];

    snprintf (pidval, sizeof (pidval), "%u", pid);
    if (attr_add (attrs,
                  attrname,
                  pidval,
                  ATTR_IMMUTABLE) < 0)
        return errprintf (errp, "attr_add %s: %s", attrname, strerror (errno));
    return 0;
}

static int init_attrs_rc_paths (attr_t *attrs, flux_error_t *errp)
{
    if (attr_add (attrs,
                  "broker.rc1_path",
                  flux_conf_builtin_get ("rc1_path", FLUX_CONF_AUTO),
                  0) < 0)
        return errprintf (errp, "attr_add rc1_path: %s", strerror (errno));
    if (attr_add (attrs,
                  "broker.rc3_path",
                  flux_conf_builtin_get ("rc3_path", FLUX_CONF_AUTO),
                  0) < 0)
        return errprintf (errp, "attr_add rc3_path: %s", strerror (errno));
    return 0;
}

static int init_attrs_shell_paths (attr_t *attrs, flux_error_t *errp)
{
    if (attr_add (attrs,
                  "conf.shell_pluginpath",
                  flux_conf_builtin_get ("shell_pluginpath", FLUX_CONF_AUTO),
                  0) < 0) {
        return errprintf (errp,
                          "attr_add conf.shell_pluginpath: %s",
                          strerror (errno));
    }
    if (attr_add (attrs,
                  "conf.shell_initrc",
                  flux_conf_builtin_get ("shell_initrc", FLUX_CONF_AUTO),
                  0) < 0) {
        return errprintf (errp,
                          "attr_add conf.shell_initrc: %s",
                          strerror (errno));
    }
    return 0;
}

static int init_attrs_starttime (attr_t *attrs,
                                 double starttime,
                                 flux_error_t *errp)
{
    char buf[32];

    snprintf (buf, sizeof (buf), "%.2f", starttime);
    if (attr_add (attrs, "broker.starttime", buf, ATTR_IMMUTABLE) < 0) {
        return errprintf (errp,
                          "error setting broker.starttime attribute: %s",
                          strerror (errno));
    }
    return 0;
}

/* Initialize attributes after bootstrap since these attributes may depend
 * on whether this instance is a job or not.
 */
static int init_attrs_post_boot (attr_t *attrs, flux_error_t *errp)
{
    const char *val;
    bool instance_is_job;

    /* Use the jobid attribute instead of FLUX_JOB_ID in the current
     * environment to determine if this instance was run as a job. This
     * is because the jobid attribute is only set by PMI, whereas
     * FLUX_JOB_ID could leak from the calling environment, e.g.
     * `flux run flux start --test-size=2`.
     */
    instance_is_job = attr_get (attrs, "jobid", NULL, NULL) == 0;

    /* Set the parent-uri attribute IFF this instance was run as a job
     * in the enclosing instance.  "parent" in this context reflects
     * a hierarchy of resource allocation.
     */
    if (instance_is_job)
        val = getenv ("FLUX_URI");
    else
        val = NULL;
    if (attr_add (attrs, "parent-uri", val, ATTR_IMMUTABLE) < 0)
        return errprintf (errp, "setattr parent-uri: %s", strerror (errno));
    unsetenv ("FLUX_URI");

    /* Unset FLUX_PROXY_REMOTE since once a new broker starts we're no
     * longer technically running under the influence of flux-proxy(1).
     */
    unsetenv ("FLUX_PROXY_REMOTE");

    if (instance_is_job) {
        val = getenv ("FLUX_KVS_NAMESPACE");
        if (attr_add (attrs, "parent-kvs-namespace", val, ATTR_IMMUTABLE) < 0) {
            return errprintf (errp,
                              "setattr parent-kvs-namespace: %s",
                              strerror (errno));
        }
    }
    unsetenv ("FLUX_KVS_NAMESPACE");

    val = getenv ("FLUX_JOB_ID_PATH");
    if (!val || !instance_is_job)
        val = "/";
    if (attr_add (attrs, "jobid-path", val, ATTR_IMMUTABLE) < 0)
        return errprintf (errp, "setattr jobid-path: %s", strerror (errno));
    unsetenv ("FLUX_JOB_ID_PATH");

    return 0;
}

static int init_attrs (attr_t *attrs,
                       pid_t pid,
                       struct flux_msg_cred *cred,
                       flux_error_t *errp)
{
    if (init_attrs_broker_pid (attrs, pid, errp) < 0
        || init_attrs_rc_paths (attrs, errp) < 0
        || init_attrs_shell_paths (attrs, errp) < 0)
        return -1;

    /* Allow version to be changed by instance owner for testing
     */
    if (attr_add (attrs, "version", FLUX_CORE_VERSION_STRING, 0) < 0)
        return errprintf (errp, "attr_add version: %s", strerror (errno));

    char tmp[32];
    snprintf (tmp, sizeof (tmp), "%ju", (uintmax_t)cred->userid);
    if (attr_add (attrs, "security.owner", tmp, ATTR_IMMUTABLE) < 0)
        return errprintf (errp, "attr_add owner: %s", strerror (errno));

    return 0;
}

static void set_proctitle (uint32_t rank)
{
#ifdef PR_SET_NAME
    static char proctitle[32];
    snprintf (proctitle, sizeof (proctitle), "flux-broker-%"PRIu32, rank);
    (void)prctl (PR_SET_NAME, proctitle, 0, 0, 0);
#endif
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
            const char *basename = basename_simple (shell);
            if (streq (basename, "true"))
                continue;
            if (streq (basename, "false"))
                continue;
            if (streq (cmd, shell) || streq (cmd, basename)) {
                result = true;
                break;
            }
        }
        endusershell ();
    }
    return result;
}

static int create_runat_rc2 (struct runat *r,
                             int flags,
                             const char *argz,
                             size_t argz_len,
                             flux_error_t *errp)
{
    if (is_interactive_shell (argz, argz_len)) { // run interactive shell
        /*  Check if stdin is a tty and error out if not to avoid
         *   confusing users with what appears to be a hang.
         */
        if (!isatty (STDIN_FILENO)) {
            return errprintf (errp,
                              "stdin is not a tty -"
                              " can't run interactive shell");
        }
        if (runat_push_shell (r, "rc2", argz, flags) < 0)
            goto error;
    }
    else if (argz_count (argz, argz_len) == 1) { // run shell -c "command"
        if (runat_push_shell_command (r, "rc2", argz, flags) < 0)
            goto error;
    }
    else { // direct exec
        if (runat_push_command (r, "rc2", argz, argz_len, flags) < 0)
            goto error;
    }
    return 0;
error:
    return errprintf (errp,
                      "error creating rc2 execution context: %s",
                      strerror (errno));
}

static int create_runat_phases (broker_ctx_t *ctx, flux_error_t *errp)
{
    const char *jobid = NULL;
    const char *rc1, *rc3, *local_uri;
    bool rc2_none = false;
    bool rc2_nopgrp = false;

    /* jobid may be NULL */
    (void) attr_get (ctx->attrs, "jobid", &jobid, NULL);

    if (attr_get (ctx->attrs, "local-uri", &local_uri, NULL) < 0)
        return errprintf (errp, "local-uri is not set");
    if (attr_get (ctx->attrs, "broker.rc1_path", &rc1, NULL) < 0)
        return errprintf (errp, "broker.rc1_path is not set");
    if (attr_get (ctx->attrs, "broker.rc3_path", &rc3, NULL) < 0)
        return errprintf (errp, "broker.rc3_path is not set");
    if (attr_get (ctx->attrs, "broker.rc2_none", NULL, NULL) == 0)
        rc2_none = true;

   /* If the broker is a process group leader and broker.rc2_pgrp was
    * not set, then do _not_ run rc2 in a separate process group by
    * default. This is done to enable cleanup when the broker exits.
    * It may then safely send a signal to its own process group at exit
    * to terminate any background processes which may otherwise hold up
    * job completion. This is not possible for interactive shells, which
    * call setpgrp(2) themselves to enable job control.
    */
   if (getpgrp() == getpid ()
       && attr_get (ctx->attrs, "broker.rc2_pgrp", NULL, NULL) != 0)
       rc2_nopgrp = true;

    if (!(ctx->runat = runat_create (ctx->h,
                                     local_uri,
                                     jobid,
                                     (runat_notify_f)state_machine_sd_notify,
                                     ctx->state_machine)))
        return errprintf (errp, "runat_create: %s", strerror (errno));

    /* rc1 - initialization
     */
    if (rc1 && strlen (rc1) > 0) {
        if (runat_push_shell_command (ctx->runat,
                                      "rc1",
                                      rc1,
                                      RUNAT_FLAG_LOG_STDIO) < 0) {
            return errprintf (errp,
                              "runat_push_shell_command rc1: %s",
                              strerror (errno));
        }
    }

    /* rc2 - initial program
     */
    if (ctx->rank == 0 && !rc2_none) {
        if (create_runat_rc2 (ctx->runat,
                              rc2_nopgrp ? RUNAT_FLAG_NO_SETPGRP: 0,
                              ctx->init_shell_cmd,
                              ctx->init_shell_cmd_len,
                              errp) < 0)
            return -1;
    }

    /* rc3 - finalization
     */
    if (rc3 && strlen (rc3) > 0) {
        if (runat_push_shell_command (ctx->runat,
                                      "rc3",
                                      rc3,
                                      RUNAT_FLAG_LOG_STDIO) < 0) {
            return errprintf (errp,
                              "runat_push_shell_command rc3: %s",
                              strerror (errno));
        }
    }
    return 0;
}

static int init_local_uri_attr (struct overlay *ov,
                                attr_t *attrs,
                                flux_error_t *errp)
{
    const char *uri;

    if (attr_get (attrs, "local-uri", &uri, NULL) < 0) {
        uint32_t rank = overlay_get_rank (ov);
        const char *rundir;
        char buf[1024];

        if (attr_get (attrs, "rundir", &rundir, NULL) < 0)
            return errprintf (errp, "rundir attribute is not set");
        if (snprintf (buf,
                      sizeof (buf),
                      "local://%s/local-%d",
                      rundir, rank) >= sizeof (buf))
            return errprintf (errp, "buffer overflow while building local-uri");
        if (attr_add (attrs, "local-uri", buf, ATTR_IMMUTABLE) < 0)
            return errprintf (errp, "setattr local-uri: %s", strerror (errno));
    }
    else {
        char path[1024];
        flux_error_t error;

        if (!strstarts (uri, "local://"))
            return errprintf (errp, "local-uri is malformed");
        if (snprintf (path, sizeof (path), "%s", uri + 8) >= sizeof (path))
            return errprintf (errp, "buffer overflow while checking local-uri");
        if (rundir_checkdir (dirname (path), &error) < 0)
            return errprintf (errp, "local-uri directory %s", error.text);

        /* see #3925 */
        struct sockaddr_un sa;
        size_t path_limit = sizeof (sa.sun_path) - 1;
        size_t path_length = strlen (uri + 8);
        if (path_length > path_limit) {
            return errprintf (errp,
                              "local-uri length of %zu bytes exceeds max %zu"
                              " AF_UNIX socket path length",
                              path_length,
                              path_limit);
        }
    }
    return 0;
}

static int init_critical_ranks_attr (struct overlay *ov,
                                     attr_t *attrs,
                                     flux_error_t *errp)
{
    int rc = -1;
    const char *val;
    char *ranks = NULL;
    struct idset *critical_ranks = NULL;

    if (attr_get (attrs, "broker.critical-ranks", &val, NULL) < 0) {
        if (!(critical_ranks = overlay_get_default_critical_ranks (ov))
            || !(ranks = idset_encode (critical_ranks, IDSET_FLAG_RANGE))) {
            errprintf (errp, "unable to calculate critical-ranks attribute");
            goto out;
        }
        if (attr_add (attrs,
                      "broker.critical-ranks",
                      ranks,
                      ATTR_IMMUTABLE) < 0) {
            errprintf (errp, "attr_add critical_ranks: %s", strerror (errno));
            goto out;
        }
    }
    else {
        if (!(critical_ranks = idset_decode (val))
            || idset_last (critical_ranks) >= overlay_get_size (ov)) {
            errprintf (errp,
                       "invalid value for broker.critical-ranks='%s'",
                       val);
            goto out;
        }
        /*  Need to set immutable flag when attr set on command line
         */
        if (attr_set_flags (attrs,
                            "broker.critical-ranks",
                            ATTR_IMMUTABLE) < 0) {
            errprintf (errp,
                       "failed to make broker.critical-ranks"
                       " attr immutable: %s",
                       strerror (errno));
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
                                        const char *hostname,
                                        flux_jobid_t id,
                                        attr_t *attrs,
                                        flux_error_t *errp)
{
    const char *local_uri = NULL;
    const char *path;
    char uri [1024];
    flux_future_t *f;

    if (attr_get (attrs, "local-uri", &local_uri, NULL) < 0) {
        errprintf (errp, "Unexpectedly unable to fetch local-uri attribute");
        return NULL;
    }
    path = local_uri + 8; /* forward past "local://" */
    if (snprintf (uri,
                 sizeof (uri),
                 "ssh://%s%s",
                 hostname, path) >= sizeof (uri)) {
        errprintf (errp, "buffer overflow while checking local-uri");
        return NULL;
    }
    if (!(f = flux_rpc_pack (h,
                             "job-manager.memo",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:I s:{s:s}}",
                             "id", id,
                             "memo",
                             "uri", uri))) {
        errprintf (errp,
                   "error sending job-manager.memo request: %s",
                   strerror (errno));
        return NULL;
    }
    return f;
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

static int execute_parental_notifications (struct broker *ctx,
                                           flux_error_t *errp)
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

    if (flux_job_id_parse (jobid, &id) < 0)
        return errprintf (errp, "Unable to parse jobid attribute '%s'", jobid);

    /*  Open connection to parent instance:
     */
    if (!(h = flux_open (parent_uri, 0))) {
        return errprintf (errp,
                          "flux_open to parent failed %s",
                          strerror (errno));
    }

    /*  Perform any RPCs to parent in parallel */
    if (!(f = set_uri_job_memo (h, ctx->hostname, id, ctx->attrs, errp)))
        goto out;

    /*  Note: not an error if rpc to set critical ranks fails, but
     *  issue an error notifying user that no critical ranks are set.
     */
    if (!(f2 = set_critical_ranks (h, id, ctx->attrs))) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "Unable to get critical ranks, all ranks will be critical");
    }

    /*  Wait for RPC results */
    if (flux_future_get (f, NULL) < 0) {
        errprintf (errp,
                   "job-manager.memo uri: %s",
                   future_strerror (f, errno));
        goto out;
    }
    if (f2 && flux_future_get (f2, NULL) < 0 && errno != ENOSYS) {
        errprintf (errp,
                   "job-exec.critical-ranks: %s",
                   future_strerror (f2, errno));
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
            return false;
        member = idset_test (ns, rank);
        idset_destroy (ns);
    }
    return member;
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
        if (!(w = flux_signal_watcher_create (ctx->reactor,
                                              sigs[i],
                                              signal_cb,
                                              ctx)))
            return -1;
        if (zlist_push (ctx->sigwatchers, w) < 0) {
            flux_watcher_destroy (w);
            errno = ENOMEM;
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

static void broker_getenv_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    json_t *names;
    json_t *env = NULL;
    size_t index;
    json_t *entry;

    if (flux_request_unpack (msg, NULL, "{s:o}", "names", &names) < 0)
        goto error;
    if (!(env = json_object ())) {
        errno = ENOMEM;
        goto error;
    }
    json_array_foreach (names, index, entry) {
        const char *name = json_string_value (entry);
        const char *val;

        if (name && (val = getenv (name))) {
            json_t *o;
            if (!(o = json_string (val))
                || json_object_set_new (env, name, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
                goto error;
            }
        }
    }
    if (flux_respond_pack (h, msg, "{s:O}", "env", env) < 0)
        flux_log_error (h, "error responding to broker.getenv");
    json_decref (env);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to broker.getenv");
    json_decref (env);
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
 * The 'sender' below always refers to a broker module, even if the original
 * requestor request is a client of connector-local, since connector-local
 * intercepts client service add/remove requests from its clients and
 * sends add/remove requests to the broker on their behalf.
 * N.B. the requestor must be local.
 */
static void service_add_cb (flux_t *h,
                            flux_msg_handler_t *w,
                            const flux_msg_t *msg,
                            void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *name;
    const char *sender;
    struct flux_msg_cred cred;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "service", &name) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (service_allow (cred, name) < 0)
        goto error;
    if (!(sender = flux_msg_route_first (msg))) {
        errno = EPROTO;
        goto error;
    }
    if (modhash_service_add (ctx->modhash, sender, name, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "service_add: flux_respond");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "service_add: flux_respond_error");
}

static void service_remove_cb (flux_t *h,
                               flux_msg_handler_t *w,
                               const flux_msg_t *msg,
                               void *arg)
{
    broker_ctx_t *ctx = arg;
    struct flux_msg_cred cred;
    const char *name;
    const char *sender;
    flux_error_t error;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "service", &name) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (service_allow (cred, name) < 0)
        goto error;
    if (!(sender = flux_msg_route_first (msg))) {
        errno = EPROTO;
        goto error;
    }
    if (modhash_service_remove (ctx->modhash, sender, name, &error) < 0) {
        errmsg = error.text;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "service_remove: flux_respond");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "service_remove: flux_respond_error");
}

static void event_subscribe_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *uuid;
    const char *topic;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        goto error;
    if ((uuid = flux_msg_route_first (msg))) {
        module_t *p;
        if (!(p = modhash_lookup (ctx->modhash, uuid))
            || module_subscribe (p, topic) < 0)
            goto error;
    }
    else {
        if (subhash_subscribe (ctx->sub, topic) < 0)
            goto error;
    }
    if (!flux_msg_is_noresponse (msg)
        && flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to subscribe request");
    return;
error:
    if (!flux_msg_is_noresponse (msg)
        && flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to subscribe request");
}

static void event_unsubscribe_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    broker_ctx_t *ctx = arg;
    const char *uuid;
    const char *topic;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        goto error;
    if ((uuid = flux_msg_route_first (msg))) {
        module_t *p;
        if (!(p = modhash_lookup (ctx->modhash, uuid))
            || module_unsubscribe (p, topic) < 0)
            goto error;
    }
    else {
        if (subhash_unsubscribe (ctx->sub, topic) < 0)
            goto error;
    }
    if (!flux_msg_is_noresponse (msg)
        && flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to unsubscribe request");
    return;
error:
    if (!flux_msg_is_noresponse (msg)
        && flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to unsubscribe request");
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
        "broker.getenv",
        broker_getenv_cb,
        0
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
    {
        FLUX_MSGTYPE_REQUEST,
        "event.subscribe",
        event_subscribe_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "event.unsubscribe",
        event_unsubscribe_cb,
        0
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
    { "shutdown",           NULL },
    { NULL, NULL, },
};

/* Register builtin services (sharing ctx->h and broker thread).
 * Register message handlers for some broker services.  Others are registered
 * in their own initialization functions.
 */
static flux_msg_handler_t **broker_add_services (broker_ctx_t *ctx,
                                                 flux_error_t *errp)
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
            errprintf (errp,
                       "error registering service for %s: %s",
                       svc->name,
                       strerror (errno));
            return NULL;
        }
    }

    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &handlers) < 0) {
        errprintf (errp,
                   "error registering message handlers: %s",
                   strerror (errno));
        return NULL;
    }
    return handlers;
}

/**
 ** reactor callbacks
 **/

/* Handle messages on interthread://overlay
 * N.B. even when there is only one node, event messages are processed
 * by the overlay.
 */
static void overlay_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    flux_t *h = flux_handle_watcher_get_flux (w);
    broker_ctx_t *ctx = arg;
    flux_msg_t *msg;
    int type;
    const char *topic;

    if (!(msg = flux_recv (h, FLUX_MATCH_ANY, 0))
        || flux_msg_get_type (msg, &type) < 0
        || flux_msg_get_topic (msg, &topic) < 0) {
        flux_msg_decref (msg);
        return;
    }
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            /* broker_request_sendmsg_new() generates a response on error.
             */
            broker_request_sendmsg_new (ctx, &msg);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            if (broker_response_sendmsg_new (ctx, &msg) < 0)
                goto drop;
            break;
        case FLUX_MSGTYPE_EVENT:
            if (modhash_event_mcast (ctx->modhash, msg) < 0)
                flux_log_error (ctx->h,
                                "mcast failed to broker modules");
            if (subhash_topic_match (ctx->sub, topic)
                && flux_send_new (ctx->h_internal, &msg, 0) < 0) {
                flux_log_error (ctx->h,
                                "send failed on internal broker handle");
            }
        default:
            break;
    }
    flux_msg_decref (msg);
    msg = NULL;
    return;
drop:
    /* Suppress logging if a response could not be sent due to ENOSYS,
     * which happens if sending module unloads before finishing all RPCs.
     */
    if (type != FLUX_MSGTYPE_RESPONSE || errno != ENOSYS) {
        flux_log_error (ctx->h,
                        "DROP overlay %s topic=%s",
                        flux_msg_typestr (type),
                        topic);
    }
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
        if (flux_send_new (ctx->h_overlay, msg, 0) < 0)
            return -1;
    }
    /* Deliver to local service if destination *could* be this broker.
     * If there is no such service locally (ENOSYS), route up TBON.
     */
    else if ((upstream && nodeid != ctx->rank) || nodeid == FLUX_NODEID_ANY) {
        if (service_send_new (ctx->services, msg) < 0) {
            if (errno != ENOSYS)
                return -1;
            if (flux_send_new (ctx->h_overlay, msg, 0) < 0)
                return -1;
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
        if (flux_send_new (ctx->h_overlay, msg, 0) < 0)
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
        if (flux_send_new (ctx->h_overlay, msg, 0) < 0)
            return -1;
    }
    else if (overlay_uuid_is_child (ctx->overlay, uuid)) {
        if (flux_send_new (ctx->h_overlay, msg, 0) < 0)
            return -1;
    }
    else {
        if (modhash_response_sendmsg_new (ctx->modhash, msg) < 0)
            return -1;
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
            broker_request_sendmsg_new (ctx, &msg);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            if (broker_response_sendmsg_new (ctx, &msg) < 0)
                goto error;
            break;
        case FLUX_MSGTYPE_EVENT:
            if (flux_send_new (ctx->h_overlay, &msg, 0) < 0)
                goto error;
            break;
        default:
            goto error;
    }
    return;
error:
    flux_msg_destroy (msg);
}

void broker_panic (broker_ctx_t *ctx, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    flux_vlog (ctx->h, LOG_CRIT, fmt, ap);
    va_end (ap);
    if (ctx->state_machine)
        state_machine_panic (ctx->state_machine);
    else
        exit (1);
}

#if HAVE_VALGRIND
/* Disable dlclose() during valgrind operation
 */
void I_WRAP_SONAME_FNNAME_ZZ(Za,dlclose)(void *dso) {}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
