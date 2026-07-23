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
#include "barrier.h"

/*  Numeric severity used for a non-fatal, critical job exception:
 *  (e.g. node failure)
 */
#define FLUX_JOB_EXCEPTION_CRIT 2

extern char **environ;

struct exec_ctx {
    struct jobinfo *job;

    const char * mock_exception;   /* fake exception */
    const char *sdexec_test_expected_cpus; /* override for post-start check */

    /*  Shells enter a sequence of barriers during startup.  Only the first
     *  is timed; on completion the current barrier is destroyed and a fresh
     *  untimed one is created for the next.  first_barrier_done records that
     *  the first barrier has completed.
     */
    struct barrier *barrier;
    double barrier_timeout;
    bool first_barrier_done;

    /*  terminated_before_barrier will be set to true if one shell terminates
     *  before the first barrier *and* the first exception. This allows other
     *  ranks to be drained when they exit too.
     */
    bool terminated_before_barrier;
};

static void exec_ctx_destroy (struct exec_ctx *tc)
{
    if (tc) {
        int saved_errno = errno;
        barrier_destroy (tc->barrier);
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
    struct exec_ctx *ctx = NULL;

    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        errprintf (errp, "%s", strerror (errno));
        goto error;
    }

    ctx->job = job;
    ctx->barrier_timeout = config_get_default_barrier_timeout ();

    /* Note: service unpacked below but unused to allow use of strict (!)
     * unpacking.
     */
    if (json_unpack_ex (job->jobspec,
                        &error,
                        0,
                        "{s?{s?{s?{s?{s?s s?s s?s s?F !}}}}}",
                        "attributes",
                          "system",
                            "exec",
                              "bulkexec",
                                "service", &service,
                                "mock_exception", &ctx->mock_exception,
                                "sdexec-test-expected-cpus",
                                    &ctx->sdexec_test_expected_cpus,
                                "barrier-timeout", &ctx->barrier_timeout) < 0) {
        errprintf (errp,
                   "failed to unpack system.exec.bulkexec for %s: %s",
                    idf58 (job->id),
                    error.text);
        goto error;
    }

    if (!(ctx->barrier = barrier_create (job,
                                         ranks,
                                         ctx->barrier_timeout,
                                         errp)))
        goto error;

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

static void output_cb (struct bulk_exec *exec,
                       flux_subprocess_t *p,
                       const char *stream,
                       const char *data,
                       int len,
                       void *arg)
{
    struct jobinfo *job = arg;
    const char *cmd = flux_cmd_arg (flux_subprocess_get_cmd (p), 0);

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
                                        "sdexec reports %s for job %s",
                                        flux_subprocess_fail_error (p),
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
        else if (errnum == EIO) {
            /*  EIO from sdexec indicates a post-start constraint check failed
             *  (e.g. AllowedCPUs not enforced by the kernel).  Drain the rank
             *  since the node is likely misconfigured and all subsequent jobs
             *  would also run unconstrained.
             */
            char ranks[16];
            snprintf (ranks, sizeof (ranks), "%d", rank);
            (void) jobinfo_drain_ranks (job,
                                        ranks,
                                        "sdexec constraint check failed "
                                        "on %s for job %s: %s",
                                        hostname,
                                        idf58 (job->id),
                                        flux_subprocess_fail_error (p));
            jobinfo_fatal_error (job,
                                 0,
                                 "sdexec constraint check failed "
                                 "on %s (rank %d): %s",
                                 hostname,
                                 rank,
                                 flux_subprocess_fail_error (p));
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
    struct exec_ctx *ctx;

    /*  Post shell-exit event if the leader shell (shell rank 0) is in the
     *  set of exiting ranks.  This must be done before the single-shell
     *  early-return so the event is posted for single-shell jobs too.
     *  jobinfo_post_shell_exit() is a no-op after the first call.
     */
    {
        unsigned int leader_rank = resource_set_nth_rank (job->R, 0);
        if (idset_test (ranks, leader_rank)) {
            flux_subprocess_t *p = bulk_exec_get_subprocess (exec, leader_rank);
            int wait_status = p ? flux_subprocess_status (p) : 0;
            jobinfo_post_shell_exit (job, leader_rank, wait_status);
        }
    }

    /*  Nothing more to do here if the job consists of only one shell.
     *  (or, if we fail to get ctx object (highly unlikely))
     */
    if (bulk_exec_total (exec) == 1
        || !(ctx = bulk_exec_aux_get (exec, "ctx")))
        return;

    /*  Check if a shell is exiting before the first barrier, in which
     *   case we raise a job exception because the shell or IMP may not
     *   have had a chance to do so.
     */
    if (!ctx->first_barrier_done
        && (!job->exception_in_progress || ctx->terminated_before_barrier)) {
        char *ids = idset_encode (ranks, IDSET_FLAG_RANGE);
        char *hosts = flux_hostmap_lookup (job->h, ids, NULL);
        jobinfo_fatal_error (job, 0,
                             "%s (rank%s %s) terminated before first barrier",
                              hosts ? hosts : "(unknown)",
                              idset_count (ranks) ? "s" : "",
                              ids ? ids : "(unknown)");

        /* Set the terminated-before-first-barrier flag. This will allow
         * other terminating tasks to also raise their own exception and
         * possibly drain affected ranks, even though exception_in_progress
         * is now true.
         */
        ctx->terminated_before_barrier = true;

        /* If this job was run under the IMP, drain the affected ranks since
         * this could indicate an unrecoverable node issue (like missing
         * or incorrect MUNGE key)
         */
        if (job->multiuser
            && barrier_drain_pending (ctx->barrier, ranks) < 0)
            flux_log_error (job->h,
                            "failed to drain %s (rank%s %s) for job %s",
                            hosts ? hosts : "(unknown)",
                            idset_count (ranks) ? "s" : "",
                            ids ? ids : "(unknown)",
                            idf58 (job->id));
        free (ids);
        free (hosts);
    }

    /*  Notify the barrier that shells have exited: terminates any barrier in
     *   progress with error (releasing waiting shells so they exit rather
     *   than being killed) and causes later barrier-enter requests to be
     *   rejected.
     */
    barrier_notify_shell_exit (ctx->barrier);

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

/* Set per-rank sdexec options on `cmd` for rank `r`.
 * Returns 0 on success, -1 on error.
 */
static int sdexec_cmd_set_rank_opts (struct exec_ctx *ctx,
                                     flux_cmd_t *cmd,
                                     unsigned int r)
{
    if (config_get_sdexec_constrain_resources ()) {
        char *R_str = resource_set_R_local (ctx->job->R, r);
        if (!R_str)
            return -1;
        int rc = flux_cmd_setopt (cmd, "SDEXEC_R_LOCAL", R_str);
        free (R_str);
        if (rc < 0)
            return -1;
    }
    if (ctx->sdexec_test_expected_cpus) {
        if (flux_cmd_setopt (cmd,
                             "SDEXEC_TEST_EXPECTED_CPUS",
                             ctx->sdexec_test_expected_cpus) < 0)
            return -1;
    }
    return 0;
}

/* Return true if per-rank sdexec commands are needed.
 * When true, exec_init() pushes one cmd per rank instead of one for all.
 */
static bool sdexec_needs_per_rank_cmds (struct exec_ctx *ctx)
{
    return config_get_sdexec_constrain_resources ()
        || ctx->sdexec_test_expected_cpus != NULL;
}

/* Push one bulk_exec cmd per rank, each with rank-specific sdexec options.
 * Returns 0 on success, -1 on error.
 */
static int sdexec_push_per_rank_cmds (struct bulk_exec *exec,
                                      const struct idset *ranks,
                                      flux_cmd_t *cmd)
{
    struct exec_ctx *ctx = bulk_exec_aux_get (exec, "ctx");
    unsigned int r = idset_first (ranks);
    while (r != IDSET_INVALID_ID) {
        flux_cmd_t *rcmd = NULL;
        struct idset *rset = NULL;
        int rc;

        if (!(rcmd = flux_cmd_copy (cmd))
            || !(rset = idset_create (0, IDSET_FLAG_AUTOGROW))
            || idset_set (rset, r) < 0
            || sdexec_cmd_set_rank_opts (ctx, rcmd, r) < 0) {
            flux_cmd_destroy (rcmd);
            idset_destroy (rset);
            return -1;
        }
        rc = bulk_exec_push_cmd (exec, rset, rcmd, 0);
        flux_cmd_destroy (rcmd);
        idset_destroy (rset);
        if (rc < 0)
            return -1;
        r = idset_next (ranks, r);
    }
    return 0;
}

static int exec_init (struct jobinfo *job)
{
    flux_cmd_t *cmd = NULL;
    struct exec_ctx *ctx = NULL;
    struct bulk_exec *exec = NULL;
    const struct idset *ranks = NULL;
    const char *service;
    flux_error_t error;

    if (job->multiuser && !config_get_imp_path ()) {
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
                              "flux imp_exec_helper %ju",
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
    /* When per-rank sdexec options are needed, push one cmd per rank so
     * each transient unit can be configured for its own allocation.
     * Otherwise push a single command covering all ranks (the common case).
     */
    if (streq (service, "sdexec") && sdexec_needs_per_rank_cmds (ctx)) {
        if (sdexec_push_per_rank_cmds (exec, ranks, cmd) < 0) {
            flux_log_error (job->h, "exec_init: sdexec per-rank cmd setup");
            goto err;
        }
    }
    else {
        if (bulk_exec_push_cmd (exec, ranks, cmd, 0) < 0) {
            flux_log_error (job->h, "exec_init: bulk_exec_push_cmd");
            goto err;
        }
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

static int exec_barrier_enter_op (struct jobinfo *job, const flux_msg_t *msg)
{
    struct bulk_exec *exec = job->data;
    struct exec_ctx *ctx = bulk_exec_aux_get (exec, "ctx");
    flux_error_t error;
    int rc;

    if (!ctx || !ctx->barrier)
        return -1;
    if ((rc = barrier_enter (ctx->barrier, msg)) != BARRIER_COMPLETE)
        return rc == BARRIER_ERROR ? -1 : 0;

    /*  This barrier is done and its shells have been released.  Replace it
     *  with a fresh untimed barrier for the next one in the sequence: only
     *  the first barrier is timed (a hung node at initialization), and this
     *  keeps barrier.c ignorant of the barrier sequence.
     */
    barrier_destroy (ctx->barrier);
    ctx->first_barrier_done = true;
    if (!(ctx->barrier = barrier_create (job,
                                         resource_set_ranks (job->R),
                                         0.,
                                         &error)))
        jobinfo_fatal_error (job, errno, "barrier: %s", error.text);
    return 0;
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
    .barrier_enter = exec_barrier_enter_op,
};

/* vi: ts=4 sw=4 expandtab
 */
