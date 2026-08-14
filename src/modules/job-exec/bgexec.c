/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* bgexec - background/waitable exec implementation
 *
 * DESCRIPTION
 *
 * A peer of the bulk-exec implementation (exec.c) that launches the job
 * shells using the libsubprocess bgexec abstraction and supports reattach.
 * Selected by setting the exec.method config key to "bgexec" or loading
 * job-exec with the method=bgexec module option.
 *
 * TEST CONFIGURATION
 *
 * Like bulk-exec, this reads a per-implementation object from the jobspec,
 * attributes.system.exec.bgexec, for "mock_exception", "barrier-timeout",
 * and "sdexec-test-expected-cpus".
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
#include "src/common/libsubprocess/bgexec.h"

#include "job-exec.h"
#include "exec_config.h"
#include "exec_cmd.h"
#include "rset.h"
#include "barrier.h"

struct bgexec_ctx {
    struct jobinfo *job;

    const char *mock_exception;   /* fake exception */
    const char *sdexec_test_expected_cpus; /* override for post-start check */

    /*  Shells enter a sequence of two barriers during startup: the first
     *  after initialization, the second after tasks have started.  Only the
     *  first is timed (with first_barrier_timeout); on completion the current
     *  barrier is destroyed and a fresh untimed one is created for the next.
     *  first_barrier_done and second_barrier_done record their completion.
     *  The RFC 50 recoverable event is posted once the second barrier
     *  completes, since barrier state cannot be reconstructed on reattach
     *  and only then are all shells known to be past the startup sequence.
     */
    struct barrier *barrier;
    double first_barrier_timeout;
    bool first_barrier_done;
    bool second_barrier_done;

    /*  Set to true if one shell terminates before the first barrier *and*
     *  the first exception.  This allows other ranks to be drained when they
     *  exit too.
     */
    bool terminated_before_first_barrier;
};

static void bgexec_ctx_destroy (struct bgexec_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        barrier_destroy (ctx->barrier);
        free (ctx);
        errno = saved_errno;
    }
}

static struct bgexec_ctx *bgexec_ctx_create (struct jobinfo *job,
                                             const struct idset *ranks,
                                             flux_error_t *errp)
{
    json_error_t error;
    struct bgexec_ctx *ctx = NULL;

    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        errprintf (errp, "%s", strerror (errno));
        goto error;
    }

    ctx->job = job;
    ctx->first_barrier_timeout = config_get_default_barrier_timeout ();

    if (json_unpack_ex (job->jobspec,
                        &error,
                        0,
                        "{s?{s?{s?{s?{s?s s?s s?F !}}}}}",
                        "attributes",
                          "system",
                            "exec",
                              "bgexec",
                                "mock_exception", &ctx->mock_exception,
                                "sdexec-test-expected-cpus",
                                    &ctx->sdexec_test_expected_cpus,
                                "barrier-timeout",
                                    &ctx->first_barrier_timeout) < 0) {
        errprintf (errp,
                   "failed to unpack system.exec.bgexec for %s: %s",
                    idf58 (job->id),
                    error.text);
        goto error;
    }

    if (!(ctx->barrier = barrier_create (job,
                                         ranks,
                                         ctx->first_barrier_timeout,
                                         errp)))
        goto error;

    return ctx;
error:
    bgexec_ctx_destroy (ctx);
    return NULL;
}

static const char *bgexec_mock_exception (struct bgexec *bg)
{
    struct bgexec_ctx *ctx = bgexec_aux_get (bg, "ctx");
    if (!ctx || !ctx->mock_exception)
        return "none";
    return ctx->mock_exception;
}

static void start_cb (struct bgexec *bg, void *arg)
{
    struct jobinfo *job = arg;
    if (job->reattach)
        jobinfo_reattached (job);
    else {
        jobinfo_started (job);
        /*  The shells are launched WAITABLE and their status can be
         *  recovered by re-issuing the wait by label.  A single-shell job
         *  runs no barriers and has no barrier state to lose across a
         *  restart, so post the RFC 50 recoverable event now.  A multi-shell
         *  job defers this to second-barrier completion (see
         *  bgexec_barrier_enter_op) because its barrier state is not
         *  reconstructible on reattach.
         */
        if (bgexec_total (bg) == 1)
            jobinfo_emit_event_pack_nowait (job, "recoverable", NULL);
    }
}

static void complete_cb (struct bgexec *bg, void *arg)
{
    struct jobinfo *job = arg;
    jobinfo_tasks_complete (job,
                            resource_set_ranks (job->R),
                            bgexec_rc (bg));
}

static void output_cb (struct bgexec *bg,
                       struct bgexec_task *task,
                       const char *stream,
                       const char *data,
                       int len,
                       void *arg)
{
    struct jobinfo *job = arg;
    const char *cmd = flux_cmd_arg (bgexec_task_cmd (task), 0);

    jobinfo_log_output (job,
                        bgexec_task_rank (task),
                        basename_simple (cmd),
                        stream,
                        data,
                        len);
}

static void error_cb (struct bgexec *bg, struct bgexec_task *task, void *arg)
{
    struct jobinfo *job = arg;
    flux_cmd_t *cmd;
    int errnum;
    int rank;
    int shell_rank;
    const char *hostname;
    const char *errmsg;

    /*  A NULL task signals a whole-object launch-pacing failure that is not
     *  tied to any one rank.  Raise a fatal error directly rather than
     *  deriving a bogus rank/host from the absent task.
     */
    if (!task) {
        jobinfo_fatal_error (job, 0, "job shell exec error");
        return;
    }

    cmd = bgexec_task_cmd (task);
    errnum = bgexec_task_errnum (task);
    rank = bgexec_task_rank (task);
    shell_rank = resource_set_rank_index (job->R, rank);
    hostname = flux_get_hostbyrank (job->h, rank);
    errmsg = bgexec_task_errmsg (task);

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
                                        errmsg,
                                        idf58 (job->id));
            bool critical = jobinfo_is_critical_rank (job, shell_rank);

            /*  Always notify rank 0 shell of a lost shell.
             */
            jobinfo_lost_shell (job,
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
            bool critical = jobinfo_is_critical_rank (job, shell_rank);

            /*  Always notify rank 0 shell of a lost shell.
             */
            jobinfo_lost_shell (job,
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
                                        errmsg);
            jobinfo_fatal_error (job,
                                 0,
                                 "sdexec constraint check failed "
                                 "on %s (rank %d): %s",
                                 hostname,
                                 rank,
                                 errmsg);
        }
        else if (errnum == ENOSYS) {
            jobinfo_fatal_error (job,
                                 0,
                                 "%s service is not loaded on %s (rank %d)",
                                 bgexec_service_name (bg),
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
                                 errmsg ? errmsg : strerror (errnum));
        }
    }
    else
        jobinfo_fatal_error (job,
                             errnum,
                             "job shell exec error on %s (rank %d)",
                             hostname,
                             rank);
}

static void exit_cb (struct bgexec *bg,
                     void *arg,
                     const struct idset *ranks)
{
    struct jobinfo *job = arg;
    struct bgexec_ctx *ctx;

    /*  Post shell-exit event if the leader shell (shell rank 0) is in the
     *  set of exiting ranks.  This must be done before the single-shell
     *  early-return so the event is posted for single-shell jobs too.
     *  jobinfo_post_shell_exit() is a no-op after the first call.
     */
    {
        unsigned int leader_rank = resource_set_nth_rank (job->R, 0);
        if (idset_test (ranks, leader_rank)) {
            struct bgexec_task *task = bgexec_get_task (bg, leader_rank);
            int wait_status = task ? bgexec_task_status (task) : 0;
            jobinfo_post_shell_exit (job, leader_rank, wait_status);
        }
    }

    /*  Nothing more to do here if the job consists of only one shell.
     *  (or, if we fail to get ctx object (highly unlikely))
     */
    if (bgexec_total (bg) == 1
        || !(ctx = bgexec_aux_get (bg, "ctx")))
        return;

    /*  Check if a shell is exiting before the first barrier, in which
     *   case we raise a job exception because the shell or IMP may not
     *   have had a chance to do so.
     */
    if (!ctx->first_barrier_done
        && (!job->exception_in_progress
            || ctx->terminated_before_first_barrier)) {
        char *ids = idset_encode (ranks, IDSET_FLAG_RANGE);
        char *hosts = flux_hostmap_lookup (job->h, ids, NULL);
        jobinfo_fatal_error (job, 0,
                             "%s (rank%s %s) terminated before first barrier",
                              hosts ? hosts : "(unknown)",
                              idset_count (ranks) > 1 ? "s" : "",
                              ids ? ids : "(unknown)");

        /* Set the terminated-before-first-barrier flag. This will allow
         * other terminating tasks to also raise their own exception and
         * possibly drain affected ranks, even though exception_in_progress
         * is now true.
         */
        ctx->terminated_before_first_barrier = true;

        /* If this job was run under the IMP, drain the affected ranks since
         * this could indicate an unrecoverable node issue (like missing
         * or incorrect MUNGE key)
         */
        if (job->multiuser
            && barrier_drain_pending (ctx->barrier, ranks) < 0)
            flux_log_error (job->h,
                            "failed to drain %s (rank%s %s) for job %s",
                            hosts ? hosts : "(unknown)",
                            idset_count (ranks) > 1 ? "s" : "",
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
        struct bgexec_task *task = bgexec_get_task (bg, rank);
        int signo = task ? bgexec_task_signaled (task) : 0;
        int shell_rank = resource_set_rank_index (job->R, rank);
        if (task && signo > 0) {
            if (shell_rank != 0)
                jobinfo_lost_shell (job,
                                    jobinfo_is_critical_rank (job, shell_rank),
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

static int get_exec_service (const char **service, flux_error_t *error)
{
    const char *s = config_get_exec_service ();

    if (!streq (s, "rexec") && !streq (s, "sdexec")) {
        errprintf (error, "unsupported exec.service value: %s", s);
        errno = EINVAL;
        return -1;
    }
    *service = s;
    return 0;
}

static struct bgexec_ops bgexec_ops = {
    .on_start =     start_cb,
    .on_exit =      exit_cb,
    .on_complete =  complete_cb,
    .on_output =    output_cb,
    .on_error =     error_cb
};

/* Push one bgexec cmd per rank, each with rank-specific sdexec options.
 * Returns 0 on success, -1 on error.
 */
static int sdexec_push_per_rank_cmds (struct bgexec *bg,
                                      const struct idset *ranks,
                                      flux_cmd_t *cmd)
{
    struct bgexec_ctx *ctx = bgexec_aux_get (bg, "ctx");
    unsigned int r = idset_first (ranks);

    if (!ctx)
        return -1;
    while (r != IDSET_INVALID_ID) {
        flux_cmd_t *rcmd = NULL;
        struct idset *rset = NULL;
        int rc;

        if (!(rcmd = flux_cmd_copy (cmd))
            || !(rset = idset_create (0, IDSET_FLAG_AUTOGROW))
            || idset_set (rset, r) < 0
            || job_shell_cmd_set_rank_opts (ctx->job,
                                            rcmd,
                                            r,
                                            ctx->sdexec_test_expected_cpus)
               < 0) {
            flux_cmd_destroy (rcmd);
            idset_destroy (rset);
            return -1;
        }
        rc = bgexec_push_cmd (bg, rset, rcmd);
        flux_cmd_destroy (rcmd);
        idset_destroy (rset);
        if (rc < 0)
            return -1;
        r = idset_next (ranks, r);
    }
    return 0;
}

static int bgexec_impl_init (struct jobinfo *job)
{
    flux_cmd_t *cmd = NULL;
    struct bgexec_ctx *ctx = NULL;
    struct bgexec *bg = NULL;
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
        flux_log_error (job->h, "bgexec_init: resource_set_ranks");
        goto err;
    }
    if (get_exec_service (&service, &error) < 0) {
        flux_log (job->h, LOG_ERR, "bgexec_init: %s" , error.text);
        goto err;
    }
    if (!(bg = bgexec_create (&bgexec_ops,
                              service,
                              job->id,
                              job->multiuser ? "imp-shell" : "shell",
                              job))) {
        flux_log_error (job->h, "bgexec_init: bgexec_create");
        goto err;
    }
    if (!(ctx = bgexec_ctx_create (job, ranks, &error))) {
        flux_log (job->h, LOG_ERR, "bgexec_ctx_create: %s", error.text);
        goto err;
    }
    if (bgexec_aux_set (bg, "ctx", ctx,
                        (flux_free_f) bgexec_ctx_destroy) < 0) {
        bgexec_ctx_destroy (ctx);
        flux_log_error (job->h, "bgexec_init: bgexec_aux_set");
        goto err;
    }
    /* ctx is now owned by bg (freed via bgexec_destroy in the err: path and
     * on normal teardown); the local pointer is borrowed from here on.
     */
    if (!(cmd = job_shell_cmd_create (job, service)))
        goto err;
    /* When per-rank sdexec options are needed, push one cmd per rank so
     * each transient unit can be configured for its own allocation.
     * Otherwise push a single command covering all ranks (the common case).
     */
    if (streq (service, "sdexec")
        && job_shell_needs_per_rank_cmds (ctx->sdexec_test_expected_cpus)) {
        if (sdexec_push_per_rank_cmds (bg, ranks, cmd) < 0) {
            flux_log_error (job->h, "bgexec_init: sdexec per-rank cmd setup");
            goto err;
        }
    }
    else {
        if (bgexec_push_cmd (bg, ranks, cmd) < 0) {
            flux_log_error (job->h, "bgexec_init: bgexec_push_cmd");
            goto err;
        }
    }
    flux_cmd_destroy (cmd);
    job->data = bg;
    return 1;
err:
    flux_cmd_destroy (cmd);
    bgexec_destroy (bg);
    return -1;
}

static void bgexec_check_cb (flux_reactor_t *r,
                             flux_watcher_t *w,
                             int revents,
                             void *arg)
{
    struct jobinfo *job = arg;
    struct bgexec *bg = job->data;
    if (bgexec_started_count (bg) >= 1) {
        jobinfo_fatal_error (job, 0, "mock starting exception generated");
        flux_log (job->h,
                  LOG_DEBUG,
                  "mock exception for starting job total=%d, current=%d",
                  bgexec_total (bg),
                  bgexec_started_count (bg));
        flux_watcher_destroy (w);
    }
}

/*  Reattach to shells launched before a restart: the per-rank commands were
 *  reconstructed by bgexec_impl_init() from job->R (which yields the same
 *  deterministic per-rank labels as the original start), so recover status by
 *  re-issuing the wait for every rank by label rather than launching a fresh
 *  set of shells.  The processes survive because the rexec server retains
 *  waitable exited shells across a client disconnect.  The exec eventlog is
 *  not consulted: the label is reconstructible from the jobid and R alone.
 */
static int bgexec_impl_reattach (struct jobinfo *job, json_t *eventlog)
{
    struct bgexec *bg = job->data;
    struct bgexec_ctx *ctx;

    if (!bg || !(ctx = bgexec_aux_get (bg, "ctx"))) {
        jobinfo_fatal_error (job, errno, "failed to get bgexec ctx");
        return -1;
    }

    /*  Reattach is gated on the RFC 50 recoverable event, which is only
     *  posted once all shells are past the startup barrier sequence (the
     *  second barrier has completed).  The reattached shells will not
     *  re-enter that sequence, so mark both barriers done: otherwise
     *  exit_cb() would misread a normally-exiting shell as having terminated
     *  before the first barrier and raise a spurious exception (draining
     *  ranks under the IMP).
     */
    ctx->first_barrier_done = true;
    ctx->second_barrier_done = true;
    return bgexec_wait (job->h, bg);
}

static int bgexec_impl_start (struct jobinfo *job)
{
    struct bgexec *bg = job->data;

    if (!bg || !bgexec_aux_get (bg, "ctx")) {
        jobinfo_fatal_error (job, errno, "failed to get bgexec ctx");
        return -1;
    }

    if (streq (bgexec_mock_exception (bg), "init")) {
        /* If creating an "init" mock exception, generate it and
         *  then return to simulate an exception that came in before
         *  we could actually start the job
         */
        jobinfo_fatal_error (job, 0, "mock init exception generated");
        return 0;
    }
    else if (streq (bgexec_mock_exception (bg), "starting")) {
        /*  If we're going to mock an exception in "starting" phase, then
         *   set up a check watcher to cancel the job when some shells have
         *   started but (potentially) not all.
         */
        flux_reactor_t *r = flux_get_reactor (job->h);
        flux_watcher_t *w = flux_check_watcher_create (r, bgexec_check_cb, job);
        if (w)
            flux_watcher_start (w);
    }

    return bgexec_start (job->h, bg);
}

static void bgexec_kill_cb (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    if (flux_future_get (f, NULL) < 0 && errno != ENOENT)
        bgexec_kill_log_error (f, job->id);
    jobinfo_decref (job);
    flux_future_destroy (f);
}

static int bgexec_impl_kill (struct jobinfo *job, int signum)
{
    struct bgexec *bg = job->data;
    flux_future_t *f;

    if (!bg)
        return 0;
    if (!(f = bgexec_kill (bg, NULL, signum))) {
        if (errno != ENOENT)
            flux_log_error (job->h, "%s: bgexec_kill", idf58 (job->id));
        return 0;
    }

    jobinfo_incref (job);
    if (flux_future_then (f, 3., bgexec_kill_cb, job) < 0) {
        flux_log_error (job->h,
                        "%s: bgexec_kill: flux_future_then",
                        idf58 (job->id));
        flux_future_destroy (f);
        return -1;
    }
    return 0;
}

static int bgexec_impl_cancel (struct jobinfo *job)
{
    struct bgexec *bg = job->data;
    if (!bg)
        return 0;
    return bgexec_cancel (bg);
}

static void bgexec_impl_exit (struct jobinfo *job)
{
    struct bgexec *bg = job->data;
    bgexec_destroy (bg);
    job->data = NULL;
}

static json_t *bgexec_impl_stats (struct jobinfo *job)
{
    struct bgexec *bg;
    struct idset *active_ranks;
    char *s = NULL;
    json_t *o;
    int total, active;

    /*  Config stats are reported once, by the bulk-exec implementation
     *  (both implementations share exec_config).  Report per-job stats only.
     */
    if (!job)
        return NULL;

    bg = job->data;
    total = bgexec_total (bg);
    active = bgexec_active_count (bg);

    if ((active_ranks = bgexec_active_ranks (bg)))
        s = idset_encode (active_ranks, IDSET_FLAG_RANGE);

    o = json_pack ("{s:i s:i s:s}",
                   "total_shells", total,
                   "active_shells", active,
                   "active_ranks", s ? s : "");
    free (s);
    idset_destroy (active_ranks);
    return o;
}

static struct idset *bgexec_impl_active_ranks (struct jobinfo *job)
{
    if (job)
        return bgexec_active_ranks ((struct bgexec *) job->data);
    return NULL;
}

static int bgexec_barrier_enter_op (struct jobinfo *job, const flux_msg_t *msg)
{
    struct bgexec *bg = job->data;
    struct bgexec_ctx *ctx = bgexec_aux_get (bg, "ctx");
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
    if (!ctx->first_barrier_done)
        ctx->first_barrier_done = true;
    else if (!ctx->second_barrier_done) {
        ctx->second_barrier_done = true;
        /*  All shells are past the startup barrier sequence, so their state
         *  is now recoverable by re-issuing the wait by label: post the
         *  RFC 50 recoverable event to permit a reattach after a restart.
         */
        jobinfo_emit_event_pack_nowait (job, "recoverable", NULL);
    }
    if (!(ctx->barrier = barrier_create (job,
                                         resource_set_ranks (job->R),
                                         0.,
                                         &error)))
        jobinfo_fatal_error (job, errno, "barrier: %s", error.text);
    return 0;
}

struct exec_implementation bgexec = {
    .name =     "bgexec",
    .select =   EXEC_SELECT_CONFIG,
    .init =     bgexec_impl_init,
    .exit =     bgexec_impl_exit,
    .start =    bgexec_impl_start,
    .reattach = bgexec_impl_reattach,
    .kill =     bgexec_impl_kill,
    .cancel =   bgexec_impl_cancel,
    .stats =    bgexec_impl_stats,
    .active_ranks = bgexec_impl_active_ranks,
    .barrier_enter = bgexec_barrier_enter_op,
};

/* vi: ts=4 sw=4 expandtab
 */
