/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Flux subprocess-based exec implementation
 *
 * DESCRIPTION
 *
 * Launch configured job shell, one per rank.
 *
 * TEST CONFIGURATION
 *
 * Test and other configuration may be presented in the jobspec
 * attributes.system.exec.bulkexec object. Supported keys include
 *
 * {
 *    "mock_exception":s       - Generate a mock exception in phase:
 *                               "init", or "starting"
 *    "service":s              - Specify service to use for launching remote
 *                               subprocesses: "rexec" or "sdexec".
 *    "barrier-timeout":F      - Specify timeout for start barrier in floating
 *                               point seconds.
 * }
 *
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <unistd.h>
#include <string.h>

#include "src/common/libjob/idf58.h"
#include "ccan/str/str.h"
#include "src/common/libutil/basename.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libsubprocess/bulk-exec.h"

#include "job-exec.h"
#include "exec_config.h"
#include "rset.h"

/*  Numeric severity used for a non-fatal, critical job exception:
 *  (e.g. node failure)
 */
#define FLUX_JOB_EXCEPTION_CRIT 2

extern char **environ;

struct exec_ctx {
    struct jobinfo *job;

    const char * mock_exception;   /* fake exception */
    struct idset *barrier_pending_ranks;
    int barrier_enter_count;
    int barrier_completion_count;
    int exit_count;

    flux_watcher_t *shell_barrier_timer;
};

static void barrier_timer_cb (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    struct exec_ctx *ctx = arg;
    struct jobinfo *job = ctx->job;
    struct bulk_exec *exec = ctx->job->data;
    char *ranks;

    if (!(ranks = idset_encode (ctx->barrier_pending_ranks,
                                IDSET_FLAG_RANGE))) {
        flux_log_error (job->h,
                        "failed to encode barrier pending ranks for job %s",
                        idf58 (job->id));
        return;
    }

    (void) jobinfo_drain_ranks (job,
                                ranks,
                                "job %s start timeout: %s",
                                idf58 (job->id),
                                "possible node hang");

    jobinfo_fatal_error (job,
                         0,
                         "%s waiting for %zu/%d nodes (rank%s %s)",
                         "start barrier timeout",
                         idset_count (ctx->barrier_pending_ranks),
                         bulk_exec_total (exec),
                         idset_count (ctx->barrier_pending_ranks) > 1 ?"s":"",
                         ranks);
    free (ranks);
}


static void exec_ctx_destroy (struct exec_ctx *tc)
{
    if (tc) {
        int saved_errno = errno;
        idset_destroy (tc->barrier_pending_ranks);
        flux_watcher_destroy (tc->shell_barrier_timer);
        free (tc);
        errno = saved_errno;
    }
}

static struct exec_ctx *exec_ctx_create (struct jobinfo *job,
                                         const struct idset *ranks,
                                         flux_error_t *errp)
{
    json_error_t error;
    const char *service;
    struct exec_ctx *ctx = calloc (1, sizeof (*ctx));
    flux_reactor_t *r = flux_get_reactor (job->h);
    double barrier_timeout = config_get_default_barrier_timeout ();

    if (!r || !ctx || !(ctx->barrier_pending_ranks = idset_copy (ranks))) {
        errprintf (errp, "Out of memory");
        goto error;
    }

    ctx->job = job;

    /* Note: service unpacked below but unused to allow use of strict (!)
     * unpacking.
     */
    if (json_unpack_ex (job->jobspec,
                        &error,
                        0,
                        "{s?{s?{s?{s?{s?s s?s s?F !}}}}}",
                        "attributes",
                          "system",
                            "exec",
                              "bulkexec",
                                "service", &service,
                                "mock_exception", &ctx->mock_exception,
                                "barrier-timeout", &barrier_timeout) < 0) {
        errprintf (errp,
                   "failed to unpack system.exec.bulkexec for %s: %s",
                    idf58 (job->id),
                    error.text);
        goto error;
    }

    if (barrier_timeout > 0.) {
        ctx->shell_barrier_timer = flux_timer_watcher_create (r,
                                                              barrier_timeout,
                                                              0.,
                                                              barrier_timer_cb,
                                                              ctx);
        if (!ctx->shell_barrier_timer) {
            errprintf (errp,
                       "%s: failed to create barrier timer",
                       idf58 (ctx->job->id));
            goto error;
        }
    }

    return ctx;
error:
    exec_ctx_destroy (ctx);
    return NULL;
}

static const char * exec_mock_exception (struct bulk_exec *exec)
{
    struct exec_ctx *ctx = bulk_exec_aux_get (exec, "ctx");
    if (!ctx || !ctx->mock_exception)
        return "none";
    return ctx->mock_exception;
}

static void start_cb (struct bulk_exec *exec, void *arg)
{
    struct jobinfo *job = arg;
    jobinfo_started (job);
}

static void complete_cb (struct bulk_exec *exec, void *arg)
{
    struct jobinfo *job = arg;
    jobinfo_tasks_complete (job,
                            resource_set_ranks (job->R),
                            bulk_exec_rc (exec));
}

static void barrier_timer_stop (struct exec_ctx *ctx)
{
    flux_watcher_stop (ctx->shell_barrier_timer);
}

static void barrier_timer_start (struct exec_ctx *ctx)
{
    /* Only ever create one barrier timer (for the first shell barrier)
     */
    if (ctx->barrier_completion_count == 0)
        flux_watcher_start (ctx->shell_barrier_timer);
}


static int exec_barrier_enter (struct bulk_exec *exec, int rank)
{
    struct exec_ctx *ctx = bulk_exec_aux_get (exec, "ctx");

    if (!ctx)
        return -1;

    (void) idset_clear (ctx->barrier_pending_ranks, rank);
    if (++ctx->barrier_enter_count == bulk_exec_total (exec)) {
        if (bulk_exec_write (exec, "stdin", "exit=0\n", 7) < 0)
            return -1;
        ctx->barrier_enter_count = 0;
        ctx->barrier_completion_count++;
        barrier_timer_stop (ctx);
    }
    else if (ctx->barrier_enter_count == 1 && ctx->exit_count > 0) {
        /*
         *  Terminate barrier with error immediately when a barrier is
         *   started after one or more shells have already exited. The
         *   case where a shell exits while a barrier is already in progress
         *   is handled in exit_cb().
         */
        if (bulk_exec_write (exec, "stdin", "exit=1\n", 7) < 0)
            return -1;
    }

    /*  When the first shell enters the barrier, start a timer after
     *   which the job will be terminated if all shells have not reached
     *   the barrier.
     */
    if (ctx->barrier_enter_count == 1)
        barrier_timer_start (ctx);

    return 0;
}

static void output_cb (struct bulk_exec *exec,
                       flux_subprocess_t *p,
                       const char *stream,
                       const char *data,
                       int len,
                       void *arg)
{
    struct jobinfo *job = arg;
    const char *cmd = flux_cmd_arg (flux_subprocess_get_cmd (p), 0);
    int rank = flux_subprocess_rank (p);

    if (streq (stream, "stdout")
        && len == 6
        && strncmp (data, "enter\n", 6) == 0) {
        if (exec_barrier_enter (exec, rank) < 0)
            jobinfo_fatal_error (job,
                                 errno,
                                 "Failed to handle barrier");
        return;
    }
    jobinfo_log_output (job,
                        flux_subprocess_rank (p),
                        basename_simple (cmd),
                        stream,
                        data,
                        len);
}

static int lost_shell (struct jobinfo *job,
                       bool critical,
                       int shell_rank,
                       const char *fmt,
                       ...)
{
    flux_future_t *f;
    char msgbuf[160];
    int msglen = sizeof (msgbuf);
    char *msg = msgbuf;
    va_list ap;
    int severity = critical ? 0 : FLUX_JOB_EXCEPTION_CRIT;

    if (fmt) {
        va_start (ap, fmt);
        if (vsnprintf (msg, msglen, fmt, ap) >= msglen)
            (void) snprintf (msg, msglen, "%s", "lost contact with job shell");
        va_end (ap);
    }

    if (!critical) {
        /* Raise a non-fatal job exception if the lost shell was not critical.
         * The job exec service will raise a fatal exception later for
         * critical shells.
         */
        jobinfo_raise (job,
                       "node-failure",
                       FLUX_JOB_EXCEPTION_CRIT,
                       "%s",
                       msg);
        /* If an exception was raised, do not duplicate the message
         * to the shell exception service since the message will already
         * be displayed as part of the exception note:
         */
        msg = "";
    }

    /* Also notify job shell rank 0 of exception
     */
    if (!(f = jobinfo_shell_rpc_pack (job,
                                      "exception",
                                      "{s:s s:i s:i s:s}",
                                      "type", "lost-shell",
                                      "severity", severity,
                                      "shell_rank", shell_rank,
                                      "message", msg)))
            return -1;
    /*  Do not wait for response. If a shell is lost because the job
     *  is terminating, then the rank 0 shell may also have exited by the
     *  time this message is sent, so a response may never come. This
     *  could leak the future (and the job reference taken by
     *  jobinfo_shell_rpc_pack())
     */
    flux_future_destroy (f);
    return 0;
}

static bool is_critical_rank (struct jobinfo *job, int shell_rank)
{
    return idset_test (job->critical_ranks, shell_rank);
}

static void error_cb (struct bulk_exec *exec, flux_subprocess_t *p, void *arg)
{
    struct jobinfo *job = arg;
    flux_cmd_t *cmd = flux_subprocess_get_cmd (p);
    int errnum = flux_subprocess_fail_errno (p);
    int rank = flux_subprocess_rank (p);
    int shell_rank = resource_set_rank_index (job->R, rank);
    const char *hostname = flux_get_hostbyrank (job->h, rank);

    /*  cmd may be NULL here if exec implementation failed to
     *   create flux_cmd_t
     */
    if (cmd) {
        if (errnum == EDEADLK) {
            /*  EDEADLK from sdexec means that unkillable processes were left
             *   on the node and it must be drained.  A "finished" response
             *   will not have been received, so after draining, treat this
             *   like EHOSTUNREACH.
             */
            char ranks[16];
            snprintf (ranks, sizeof (ranks), "%d", rank);
            (void) jobinfo_drain_ranks (job,
                                        ranks,
                                        "unkillable processes from job %s",
                                        idf58 (job->id));
            bool critical = is_critical_rank (job, shell_rank);

            /*  Always notify rank 0 shell of a lost shell.
             */
            lost_shell (job,
                        critical,
                        shell_rank,
                        "shell exited with unkillable processes"
                        " on %s (shell rank %d)",
                        hostname,
                        shell_rank);

            /*  Raise a fatal error and terminate job immediately if
             *  the lost shell was critical.
             */
            if (critical)
                jobinfo_fatal_error (job,
                                     0,
                                     "shell exited with unkillable processes"
                                     " on %s (rank %d)",
                                     hostname,
                                     rank);
        }
        else if (errnum == EHOSTUNREACH) {
            bool critical = is_critical_rank (job, shell_rank);

            /*  Always notify rank 0 shell of a lost shell.
             */
            lost_shell (job,
                        critical,
                        shell_rank,
                        "node failure on %s (shell rank %d)",
                        hostname,
                        shell_rank);

            /*  Raise a fatal error and terminate job immediately if
             *  the lost shell was critical.
             */
            if (critical)
                jobinfo_fatal_error (job,
                                     0,
                                     "node failure on %s (rank %d)",
                                     hostname,
                                     rank);
        }
        else if (errnum == ENOSYS) {
            jobinfo_fatal_error (job,
                                 0,
                                 "%s service is not loaded on %s (rank %d)",
                                 bulk_exec_service_name (exec),
                                 hostname,
                                 rank);
        }
        else {
            jobinfo_fatal_error (job,
                                 0,
                                 "%s on broker %s (rank %d): %s",
                                 "job shell exec error",
                                 hostname,
                                 rank,
                                 flux_subprocess_fail_error (p));
        }
    }
    else
        jobinfo_fatal_error (job,
                             flux_subprocess_fail_errno (p),
                             "job shell exec error on %s (rank %d)",
                             hostname,
                             rank);
}


static void exit_cb (struct bulk_exec *exec,
                     void *arg,
                     const struct idset *ranks)
{
    struct jobinfo *job = arg;
    struct exec_ctx *ctx = bulk_exec_aux_get (exec, "ctx");

    /*  Nothing to do here if the job consists of only one shell.
     *  (or, if we fail to to get ctx object (highly unlikely))
     */
    if (bulk_exec_total (exec) == 1
        || !(ctx = bulk_exec_aux_get (exec, "ctx")))
        return;

    ctx->exit_count++;

    /*  Check if a shell is exiting before the first barrier, in which
     *   case we raise a job exception because the shell or IMP may not
     *   have had a chance to do so.
     */
    if (ctx->barrier_completion_count == 0) {
        char *ids = idset_encode (ranks, IDSET_FLAG_RANGE);
        char *hosts = flux_hostmap_lookup (job->h, ids, NULL);
        jobinfo_fatal_error (job, 0,
                             "%s (rank%s %s) terminated before first barrier",
                              hosts ? hosts : "(unknown)",
                              idset_count (ranks) ? "s" : "",
                              ids ? ids : "(unknown)");
        free (ids);
        free (hosts);
    }

    /*  If a shell exited before the first barrier or there is a
     *   barrier in progress (enter_count > 0), then terminate the
     *   current/next barrier immediately with error. This will allow
     *   shells currently waiting or entering the barrier in the future
     *   to exit immediately, rather than being killed by exec system.
     */
    if (ctx->barrier_completion_count == 0
        || ctx->barrier_enter_count > 0) {
        if (bulk_exec_write (exec, "stdin", "exit=1\n", 7) < 0)
            jobinfo_fatal_error (job,
                                 0,
                                 "failed to terminate barrier: %s",
                                 strerror (errno));
    }

    /*  If a shell exits due to signal report the shell as lost to
     *  the leader shell. This avoids potential hangs in the leader
     *  shell if it is waiting for data from job shells that did not
     *  exit cleanly.
     */
    unsigned int rank = idset_first (ranks);
    while (rank != IDSET_INVALID_ID) {
        flux_subprocess_t *p = bulk_exec_get_subprocess (exec, rank);
        int signo = flux_subprocess_signaled (p);
        int shell_rank = resource_set_rank_index (job->R, rank);
        if (p && signo > 0) {
            if (shell_rank != 0)
                lost_shell (job,
                            is_critical_rank (job, shell_rank),
                            shell_rank,
                            "shell rank %d (on %s): %s",
                            shell_rank,
                            flux_get_hostbyrank (job->h, rank),
                            strsignal (signo));
            else {
                /*  Job can't continue without the leader shell, which has
                 *  terminated unexpectedly. Cancel the job now to avoid
                 *  a potential hang.
                 */
                jobinfo_fatal_error (job,
                                     0,
                                     "shell rank 0 (on %s): %s",
                                     flux_get_hostbyrank (job->h, rank),
                                     strsignal (signo));
            }
        }
        rank = idset_next (ranks, rank);
    }
}

static int parse_service_option (json_t *jobspec,
                                 const char **service,
                                 flux_error_t *error)
{
    const char *s = config_get_exec_service (); // default
    bool override = config_get_exec_service_override ();
    json_error_t e;

    if (jobspec) {
        const char *s2 = NULL;
        if (json_unpack_ex (jobspec,
                            &e,
                            0,
                            "{s:{s?{s?{s?{s?s}}}}}",
                            "attributes",   // key is required per RFC 14
                              "system",     // key is optional per RFC 14
                                "exec",
                                  "bulkexec",
                                    "service", &s2) < 0) {
            errprintf (error, "error parsing bulkexec.service: %s", e.text);
            errno = EINVAL;
            return -1;
        }
        if (s2) {
            if (!override && !streq (s, s2)) {
                errprintf (error, "exec service override is not permitted");
                errno = EINVAL;
                return -1;
            }
            s = s2;
        }
    }
    if (!streq (s, "rexec") && !streq (s, "sdexec")) {
        errprintf (error, "unknown bulkexec.service value: %s", s);
        errno = EINVAL;
        return -1;
    }
    *service = s;
    return 0;
}


static struct bulk_exec_ops exec_ops = {
    .on_start =     start_cb,
    .on_exit =      exit_cb,
    .on_complete =  complete_cb,
    .on_output =    output_cb,
    .on_error =     error_cb
};

static int exec_init (struct jobinfo *job)
{
    flux_cmd_t *cmd = NULL;
    struct exec_ctx *ctx = NULL;
    struct bulk_exec *exec = NULL;
    const struct idset *ranks = NULL;
    const char *imp_path = NULL;
    const char *service;
    flux_error_t error;

    if (job->multiuser && !(imp_path = config_get_imp_path ())) {
        flux_log (job->h,
                  LOG_ERR,
                  "unable run multiuser job with no IMP configured!");
        goto err;
    }

    if (!(ranks = resource_set_ranks (job->R))) {
        flux_log_error (job->h, "exec_init: resource_set_ranks");
        goto err;
    }
    if (parse_service_option (job->jobspec, &service, &error) < 0) {
        flux_log (job->h, LOG_ERR, "exec_init: %s" , error.text);
        goto err;
    }
    if (!(exec = bulk_exec_create (&exec_ops,
                                   service,
                                   job->id,
                                   job->multiuser ? "imp-shell" : "shell",
                                   job))) {
        flux_log_error (job->h, "exec_init: bulk_exec_create");
        goto err;
    }
    if (!(ctx = exec_ctx_create (job, ranks, &error))) {
        flux_log (job->h, LOG_ERR, "exec_ctx_create: %s", error.text);
        goto err;
    }
    if (bulk_exec_aux_set (exec, "ctx", ctx,
                          (flux_free_f) exec_ctx_destroy) < 0) {
        exec_ctx_destroy (ctx);
        flux_log_error (job->h, "exec_init: bulk_exec_aux_set");
        goto err;
    }
    if (!(cmd = flux_cmd_create (0, NULL, environ))) {
        flux_log_error (job->h, "exec_init: flux_cmd_create");
        goto err;
    }
    /* Set any configured exec.sdexec-properties.
     */
    json_t *props;
    if (streq (service, "sdexec")
        && (props = config_get_sdexec_properties ())) {
        const char *k;
        json_t *v;
        json_object_foreach (props, k, v) {
            char name[128];
            snprintf (name, sizeof (name), "SDEXEC_PROP_%s", k);
            if (flux_cmd_setopt (cmd, name, json_string_value (v)) < 0) {
                flux_log_error (job->h, "Unable to set sdexec options");
                return -1;
            }
        }
    }
    if (flux_cmd_setenvf (cmd, 1, "FLUX_KVS_NAMESPACE", "%s", job->ns) < 0) {
        flux_log_error (job->h, "exec_init: flux_cmd_setenvf");
        goto err;
    }
    if (job->multiuser) {
        if (flux_cmd_setenvf (cmd,
                              1,
                              "FLUX_IMP_EXEC_HELPER",
                              "flux imp-exec-helper %ju",
                              (uintmax_t) job->id) < 0) {
            flux_log_error (job->h, "exec_init: flux_cmd_setenvf");
            goto err;
        }
        /* The systemd user instance running as user flux is not privileged
         * to signal guest processes, therefore:
         * - Set the KillMode=process so only the IMP is signaled
         * - Use Type=notify in conjunction with IMP calling sd_notify(3) so
         *   the unit transitions to deactivating when the shell exits.
         * - Set TimeoutStopUsec=infinity to disable systemd's stop timeout.
         * - Enable sdexec's stop timeout which is armed at deactivating,
         *   delivers SIGUSR1 (proxy for SIGKILL) after 30s, then abandons
         *   the unit and terminates the exec RPC after another 30s.
         */
        if (streq (service, "sdexec")) {
            if (flux_cmd_setopt (cmd, "SDEXEC_PROP_KillMode", "process") < 0
                || flux_cmd_setopt (cmd, "SDEXEC_PROP_Type", "notify") < 0
                || flux_cmd_setopt (cmd,
                                    "SDEXEC_PROP_TimeoutStopUSec",
                                    "infinity") < 0
                || flux_cmd_setopt (cmd,
                                    "SDEXEC_STOP_TIMER_SIGNAL",
                                    config_get_sdexec_stop_timer_signal ()) < 0
                || flux_cmd_setopt (cmd,
                                    "SDEXEC_STOP_TIMER_SEC",
                                    config_get_sdexec_stop_timer_sec ()) < 0) {
                flux_log_error (job->h,
                                "Unable to set multiuser sdexec options");
                return -1;
            }
        }
        if (flux_cmd_argv_append (cmd, config_get_imp_path ()) < 0
            || flux_cmd_argv_append (cmd, "exec") < 0) {
            flux_log_error (job->h, "exec_init: flux_cmd_argv_append");
            goto err;
        }
    }
    if (flux_cmd_argv_append (cmd, config_get_job_shell (job)) < 0
        || flux_cmd_argv_appendf (cmd, "%ju", (uintmax_t) job->id) < 0) {
        flux_log_error (job->h, "exec_init: flux_cmd_argv_append");
        goto err;
    }
    if (bulk_exec_push_cmd (exec, ranks, cmd, 0) < 0) {
        flux_log_error (job->h, "exec_init: bulk_exec_push_cmd");
        goto err;
    }
    flux_cmd_destroy (cmd);
    job->data = exec;
    return 1;
err:
    flux_cmd_destroy (cmd);
    bulk_exec_destroy (exec);
    return -1;
}

static void exec_check_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct jobinfo *job = arg;
    struct bulk_exec *exec = job->data;
    if (bulk_exec_started_count (exec) >= 1) {
        jobinfo_fatal_error (job, 0, "mock starting exception generated");
        flux_log (job->h,
                  LOG_DEBUG,
                  "mock exception for starting job total=%d, current=%d",
                  bulk_exec_total (exec),
                  bulk_exec_started_count (exec));
        flux_watcher_destroy (w);
    }
}

static int exec_start (struct jobinfo *job)
{
    struct bulk_exec *exec = job->data;
    struct exec_ctx *ctx = bulk_exec_aux_get (exec, "ctx");

    if (!exec || !(ctx = bulk_exec_aux_get (exec, "ctx"))) {
        jobinfo_fatal_error (job, errno, "failed to get bulk-exec ctx");
        return -1;
    }

    if (streq (exec_mock_exception (exec), "init")) {
        /* If creating an "init" mock exception, generate it and
         *  then return to simulate an exception that came in before
         *  we could actually start the job
         */
        jobinfo_fatal_error (job, 0, "mock init exception generated");
        return 0;
    }
    else if (streq (exec_mock_exception (exec), "starting")) {
        /*  If we're going to mock an exception in "starting" phase, then
         *   set up a check watcher to cancel the job when some shells have
         *   started but (potentially) not all.
         */
        flux_reactor_t *r = flux_get_reactor (job->h);
        flux_watcher_t *w = flux_check_watcher_create (r, exec_check_cb, job);
        if (w)
            flux_watcher_start (w);
    }

    return bulk_exec_start (job->h, exec);
}

static void exec_kill_cb (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    if (flux_future_get (f, NULL) < 0 && errno != ENOENT)
        bulk_exec_kill_log_error (f, job->id);
    jobinfo_decref (job);
    flux_future_destroy (f);
}

static int exec_kill (struct jobinfo *job, int signum)
{
    struct  bulk_exec *exec = job->data;
    flux_future_t *f;

    if (!(f = bulk_exec_kill (exec, NULL, signum))) {
        if (errno != ENOENT)
            flux_log_error (job->h, "%s: bulk_exec_kill", idf58 (job->id));
        return 0;
    }

    jobinfo_incref (job);
    if (flux_future_then (f, 3., exec_kill_cb, job) < 0) {
        flux_log_error (job->h,
                        "%s: exec_kill: flux_future_then",
                        idf58 (job->id));
        flux_future_destroy (f);
        return -1;
    }
    return 0;
}

static int exec_cancel (struct jobinfo *job)
{
    struct bulk_exec *exec = job->data;
    return bulk_exec_cancel (exec);
}

static void exec_exit (struct jobinfo *job)
{
    struct bulk_exec *exec = job->data;
    bulk_exec_destroy (exec);
    job->data = NULL;
}

static int exec_config (flux_t *h,
                        const flux_conf_t *conf,
                        int argc,
                        char **argv,
                        flux_error_t *errp)
{
    return config_setup (h, conf, argc, argv, errp);
}

static json_t *exec_config_stats (void)
{
    json_t *o = NULL;
    json_t *conf = NULL;

    if (!(o = json_object ())) {
        errno = ENOMEM;
        goto error;
    }

    if (config_get_stats (&conf) < 0)
        goto error;

    if (json_object_set_new (o, "config", conf) < 0)
        goto error;

    return o;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    ERRNO_SAFE_WRAP (json_decref, conf);
    return NULL;
}

static json_t *exec_job_stats (struct jobinfo *job)
{
    struct bulk_exec *exec = job->data;
    struct idset *active_ranks;
    char *s = NULL;
    json_t *o;
    int total = bulk_exec_total (exec);
    int active = bulk_exec_active_count (exec);

    if ((active_ranks = bulk_exec_active_ranks (exec)))
        s = idset_encode (active_ranks, IDSET_FLAG_RANGE);

    o = json_pack ("{s:i s:i s:s}",
                   "total_shells", total,
                   "active_shells", active,
                   "active_ranks", s ? s : "");
    free (s);
    idset_destroy (active_ranks);
    return o;
}

static json_t *exec_stats (struct jobinfo *job)
{
    if (job)
        return exec_job_stats (job);
    else
        return exec_config_stats ();
}

static struct idset *active_ranks (struct jobinfo *job)
{
    if (job)
        return bulk_exec_active_ranks ((struct bulk_exec *) job->data);
    return NULL;
}

struct exec_implementation bulkexec = {
    .name =     "bulk-exec",
    .config =   exec_config,
    .init =     exec_init,
    .exit =     exec_exit,
    .start =    exec_start,
    .kill =     exec_kill,
    .cancel =   exec_cancel,
    .stats =    exec_stats,
    .active_ranks = active_ranks,
};

/* vi: ts=4 sw=4 expandtab
 */
