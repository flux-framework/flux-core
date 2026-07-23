/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Unit tests for bgexec, run against the in-process rcmdsrv mock server.
 *
 * The mock server implements the RFC 42 subprocess protocol (like rexec),
 * so it exercises the real client-side start/wait/kill code paths in
 * bgexec without a live broker.  It does not simulate a broker restart
 * (server survival across restart is an sdexec concern, a follow-on); the
 * recovery-wait test proves the wait-only-by-label protocol path by having
 * a "previous incarnation" start waitable background processes and then
 * reconstructing their labels in a fresh bgexec object.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <sys/wait.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/idset.h>

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libsubprocess/server.h"
#include "src/common/libsubprocess/subprocess.h"
#include "src/common/libsubprocess/bgexec.h"

#include "rcmdsrv.h"

#define SERVER_NAME "test-bgexec"

extern char **environ;

/* Aggregate observations collected by the callbacks for a single run.
 */
struct scorecard {
    int cb_on_start;
    int cb_on_complete;
    int exit_batches;
    int exited_ranks;   /* total ranks reported across all cb_on_exit batches */
    char exit_seen[64]; /* ranks already reported through cb_on_exit          */
    int exit_dups;      /* ranks reported through cb_on_exit more than once */
    int errors;
    int last_error_rank;
    int last_error_errno;
    char output[4096];  /* concatenated cb_on_output data */
};

static void cb_on_start (struct bgexec *bg, void *arg)
{
    struct scorecard *sc = arg;
    sc->cb_on_start++;
}

static void cb_on_exit (struct bgexec *bg, void *arg, const struct idset *ranks)
{
    struct scorecard *sc = arg;
    unsigned int rank;

    sc->exit_batches++;
    sc->exited_ranks += idset_count (ranks);
    /*  A rank must be reported through on_exit at most once.  Track which
     *  ranks have been seen so a stale exit_batch entry reappearing in a
     *  later batch is caught as a duplicate.
     */
    rank = idset_first (ranks);
    while (rank != IDSET_INVALID_ID) {
        if (rank < sizeof (sc->exit_seen)) {
            if (sc->exit_seen[rank])
                sc->exit_dups++;
            sc->exit_seen[rank] = 1;
        }
        rank = idset_next (ranks, rank);
    }
}

static void cb_on_complete (struct bgexec *bg, void *arg)
{
    struct scorecard *sc = arg;
    sc->cb_on_complete++;
}

static void cb_on_error (struct bgexec *bg, struct bgexec_task *task, void *arg)
{
    struct scorecard *sc = arg;
    sc->errors++;
    if (task) {
        sc->last_error_rank = bgexec_task_rank (task);
        sc->last_error_errno = bgexec_task_errnum (task);
    }
}

static void cb_on_output (struct bgexec *bg,
                       struct bgexec_task *task,
                       const char *stream,
                       const char *data,
                       int len,
                       void *arg)
{
    struct scorecard *sc = arg;
    size_t used = strlen (sc->output);
    if (used + len < sizeof (sc->output)) {
        memcpy (sc->output + used, data, len);
        sc->output[used + len] = '\0';
    }
}

static struct bgexec_ops test_ops = {
    .on_start = cb_on_start,
    .on_exit = cb_on_exit,
    .on_complete = cb_on_complete,
    .on_error = cb_on_error,
    .on_output = cb_on_output,
};

static const flux_jobid_t TEST_JOBID = 1234;

/* Push cmd (argv) on ranks [0, nranks) into a new bgexec and return it. */
static struct bgexec *setup (flux_t *h,
                             struct scorecard *sc,
                             int nranks,
                             char **argv,
                             int argc)
{
    struct bgexec *bg;
    struct idset *ranks;
    flux_cmd_t *cmd;
    int i;

    memset (sc, 0, sizeof (*sc));
    if (!(bg = bgexec_create (&test_ops,
                              SERVER_NAME,
                              TEST_JOBID,
                              "shell",
                              sc)))
        BAIL_OUT ("bgexec_create failed");
    if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW)))
        BAIL_OUT ("idset_create failed");
    for (i = 0; i < nranks; i++) {
        if (idset_set (ranks, i) < 0)
            BAIL_OUT ("idset_set failed");
    }
    if (!(cmd = flux_cmd_create (argc, argv, environ)))
        BAIL_OUT ("flux_cmd_create failed");
    if (bgexec_push_cmd (bg, ranks, cmd) < 0)
        BAIL_OUT ("bgexec_push_cmd failed");
    flux_cmd_destroy (cmd);
    idset_destroy (ranks);
    return bg;
}

/* Basic start: all ranks launch, cb_on_start fires once, all exit 0, and
 * cb_on_complete fires once.
 */
static void test_basic (flux_t *h)
{
    char *av[] = { "true", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 3, av, ARRAY_SIZE (av) - 1);

    ok (bgexec_total (bg) == 3,
        "basic: bgexec_total is 3");
    ok (bgexec_start (h, bg) == 0,
        "basic: bgexec_start works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "basic: reactor ran to completion");
    ok (sc.cb_on_start == 1,
        "basic: cb_on_start fired once (got %d)", sc.cb_on_start);
    ok (sc.cb_on_complete == 1,
        "basic: cb_on_complete fired once (got %d)", sc.cb_on_complete);
    ok (sc.exited_ranks == 3,
        "basic: all 3 ranks reported exited (got %d)", sc.exited_ranks);
    ok (sc.errors == 0,
        "basic: no errors (got %d)", sc.errors);
    ok (bgexec_rc (bg) == 0,
        "basic: exit status is 0 (got 0x%04x)", bgexec_rc (bg));
    ok (bgexec_complete (bg) == 3,
        "basic: complete count is 3 (got %d)", bgexec_complete (bg));
    bgexec_destroy (bg);
}

/* A rank exits nonzero: exit status reflects the max wait status. */
static void test_nonzero (flux_t *h)
{
    char *av[] = { "false", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 2, av, ARRAY_SIZE (av) - 1);
    int status;

    ok (bgexec_start (h, bg) == 0,
        "nonzero: bgexec_start works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "nonzero: reactor ran to completion");
    ok (sc.cb_on_complete == 1,
        "nonzero: cb_on_complete fired once");
    status = bgexec_rc (bg);
    ok (WIFEXITED (status) && WEXITSTATUS (status) == 1,
        "nonzero: exit status decodes to exit code 1 (got 0x%04x)",
        status);
    bgexec_destroy (bg);
}

/* Pre-RUNNING launch failure (exec of nonexistent file) is surfaced on the
 * start response as cb_on_error with errno ENOENT, and still counts toward
 * completion.  Because a rank fails to start, cb_on_start does not fire.
 */
static void test_launch_failure (flux_t *h)
{
    char *av[] = { "/noexist", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 1, av, ARRAY_SIZE (av) - 1);

    ok (bgexec_start (h, bg) == 0,
        "launch_failure: bgexec_start works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "launch_failure: reactor ran to completion");
    ok (sc.errors == 1,
        "launch_failure: cb_on_error fired once (got %d)", sc.errors);
    ok (sc.last_error_errno == ENOENT,
        "launch_failure: error errno is ENOENT (got %d)",
        sc.last_error_errno);
    ok (sc.cb_on_start == 0,
        "launch_failure: cb_on_start did not fire");
    ok (sc.cb_on_complete == 1,
        "launch_failure: cb_on_complete still fired");
    ok (bgexec_complete (bg) == 1,
        "launch_failure: task counted as complete");
    bgexec_destroy (bg);
}

/* Mixed launch failure + success: the good rank runs and exits, the bad
 * rank errors; both count toward completion; cb_on_start does not fire since
 * not all ranks reached STARTED.
 */
static void test_mixed (flux_t *h)
{
    struct bgexec *bg;
    struct scorecard sc;
    struct idset *ranks;
    char *good[] = { "true", NULL };
    char *bad[] = { "/noexist", NULL };
    flux_cmd_t *cmd_good = NULL, *cmd_bad = NULL;

    memset (&sc, 0, sizeof (sc));
    if (!(bg = bgexec_create (&test_ops,
                              SERVER_NAME,
                              TEST_JOBID,
                              "shell",
                              &sc)))
        BAIL_OUT ("bgexec_create failed");
    if (!(cmd_good = flux_cmd_create (ARRAY_SIZE (good) - 1, good, environ))
        || !(cmd_bad = flux_cmd_create (ARRAY_SIZE (bad) - 1, bad, environ)))
        BAIL_OUT ("flux_cmd_create failed");
    if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW)))
        BAIL_OUT ("idset_create failed");

    idset_set (ranks, 0);
    if (bgexec_push_cmd (bg, ranks, cmd_good) < 0)
        BAIL_OUT ("bgexec_push_cmd (good) failed");
    idset_clear (ranks, 0);
    idset_set (ranks, 1);
    if (bgexec_push_cmd (bg, ranks, cmd_bad) < 0)
        BAIL_OUT ("bgexec_push_cmd (bad) failed");

    ok (bgexec_start (h, bg) == 0,
        "mixed: bgexec_start works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "mixed: reactor ran to completion");
    ok (sc.errors == 1,
        "mixed: exactly one cb_on_error (got %d)", sc.errors);
    ok (sc.last_error_rank == 1,
        "mixed: error was for rank 1 (got %d)", sc.last_error_rank);
    ok (sc.cb_on_start == 0,
        "mixed: cb_on_start did not fire (a rank failed to start)");
    ok (sc.cb_on_complete == 1,
        "mixed: cb_on_complete fired");
    ok (bgexec_complete (bg) == 2,
        "mixed: both tasks complete (got %d)", bgexec_complete (bg));

    flux_cmd_destroy (cmd_good);
    flux_cmd_destroy (cmd_bad);
    idset_destroy (ranks);
    bgexec_destroy (bg);
}

/* Early telemetry: a process that emits stderr before exiting has that
 * output retained by the server and delivered via cb_on_output from the wait
 * response.
 */
static void test_output (flux_t *h)
{
    char *av[] = { TEST_SUBPROCESS_DIR "test_echo", "-E", "foo", "bar", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 1, av, ARRAY_SIZE (av) - 1);

    ok (bgexec_start (h, bg) == 0,
        "output: bgexec_start works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "output: reactor ran to completion");
    ok (sc.cb_on_complete == 1,
        "output: cb_on_complete fired");
    ok (streq (sc.output, "foo\nbar\n"),
        "output: retained stderr delivered via cb_on_output (got '%s')",
        sc.output);
    bgexec_destroy (bg);
}

/* Kill: start long-running processes, signal them by label, and verify
 * they are reported as terminated by signal.
 */
static void test_kill (flux_t *h)
{
    char *av[] = { "sleep", "30", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 2, av, ARRAY_SIZE (av) - 1);
    flux_reactor_t *r = flux_get_reactor (h);
    struct bgexec_task *task;
    flux_future_t *f;

    ok (bgexec_start (h, bg) == 0,
        "kill: bgexec_start works");
    /* Pump the reactor until all ranks have started (cb_on_start fired). */
    while (sc.cb_on_start == 0)
        flux_reactor_run (r, FLUX_REACTOR_ONCE);
    ok (sc.cb_on_start == 1,
        "kill: cb_on_start fired");
    ok (bgexec_active_count (bg) == 2,
        "kill: 2 tasks active (got %d)", bgexec_active_count (bg));

    f = bgexec_kill (bg, NULL, SIGTERM);
    ok (f != NULL,
        "kill: bgexec_kill returned a future");
    ok (flux_future_wait_for (f, 5.) == 0 && flux_future_get (f, NULL) == 0,
        "kill: kill RPC succeeded");
    flux_future_destroy (f);

    ok (flux_reactor_run (r, 0) >= 0,
        "kill: reactor ran to completion");
    ok (sc.cb_on_complete == 1,
        "kill: cb_on_complete fired");
    task = bgexec_get_task (bg, 0);
    ok (task != NULL && bgexec_task_signaled (task) == SIGTERM,
        "kill: rank 0 terminated by SIGTERM (got %d)",
        task ? bgexec_task_signaled (task) : -1);
    bgexec_destroy (bg);
}

/* Build the deterministic label bgexec uses internally, so the test can
 * start a "previous incarnation" process the recovery wait can find.
 */
static char *make_label (int rank)
{
    char idbuf[21];
    char *label;
    if (flux_job_id_encode (TEST_JOBID, "f58plain", idbuf, sizeof (idbuf)) < 0)
        BAIL_OUT ("flux_job_id_encode failed");
    if (asprintf (&label, "shell-%d-%s", rank, idbuf) < 0)
        BAIL_OUT ("asprintf failed");
    return label;
}

/* Start a waitable background process with the bgexec label for 'rank'
 * (as a prior incarnation would), and leave it unwaited.
 */
static void prior_start (flux_t *h, int rank, char **av, int ac)
{
    flux_cmd_t *cmd;
    flux_future_t *f;
    char *label = make_label (rank);

    if (!(cmd = flux_cmd_create (ac, av, environ))
        || flux_cmd_set_label (cmd, label) < 0)
        BAIL_OUT ("prior_start: cmd setup failed");
    f = flux_rexec_bg (h,
                       SERVER_NAME,
                       rank,
                       FLUX_SUBPROCESS_FLAGS_WAITABLE,
                       cmd);
    if (!f)
        BAIL_OUT ("prior_start: flux_rexec_bg failed");
    ok (flux_rpc_get (f, NULL) == 0,
        "recover: prior incarnation started rank %d", rank);
    flux_future_destroy (f);
    flux_cmd_destroy (cmd);
    free (label);
}

/* Recovery wait: a fresh bgexec reconstructs the per-rank labels and
 * collects status of processes started by a prior incarnation via
 * bgexec_wait(), without a start phase.  This is the restart recovery path.
 */
static void test_recover_wait (flux_t *h)
{
    char *av[] = { "true", NULL };
    struct scorecard sc;
    struct bgexec *bg;
    int i;

    /* Prior incarnation launches two waitable background processes. */
    for (i = 0; i < 2; i++)
        prior_start (h, i, av, ARRAY_SIZE (av) - 1);

    bg = setup (h, &sc, 2, av, ARRAY_SIZE (av) - 1);
    ok (bgexec_wait (h, bg) == 0,
        "recover: bgexec_wait works");
    ok (sc.cb_on_start == 1,
        "recover: cb_on_start fired (all tasks STARTED)");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "recover: reactor ran to completion");
    ok (sc.cb_on_complete == 1,
        "recover: cb_on_complete fired");
    ok (sc.exited_ranks == 2,
        "recover: both ranks reported exited (got %d)", sc.exited_ranks);
    ok (sc.errors == 0,
        "recover: no errors (got %d)", sc.errors);
    ok (bgexec_rc (bg) == 0,
        "recover: recovered exit status 0 (got 0x%04x)", bgexec_rc (bg));
    bgexec_destroy (bg);
}

/* Recovery wait on a nonexistent process: the wait RPC fails and the task
 * is surfaced via cb_on_error.
 */
static void test_recover_missing (flux_t *h)
{
    char *av[] = { "true", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 1, av, ARRAY_SIZE (av) - 1);

    ok (bgexec_wait (h, bg) == 0,
        "recover_missing: bgexec_wait works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "recover_missing: reactor ran to completion");
    ok (sc.errors == 1,
        "recover_missing: cb_on_error fired for missing process (got %d)",
        sc.errors);
    ok (sc.cb_on_complete == 1,
        "recover_missing: cb_on_complete fired");
    bgexec_destroy (bg);
}

/* Cancel before start: every task is pending, so all are discarded to
 * complete, cb_on_complete fires, and no task launches or errors.
 */
static void test_cancel_before_start (flux_t *h)
{
    char *av[] = { "true", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 3, av, ARRAY_SIZE (av) - 1);

    ok (bgexec_cancel (bg) == 0,
        "cancel: bgexec_cancel before start works");
    ok (bgexec_complete (bg) == 3,
        "cancel: all 3 pending tasks discarded to complete (got %d)",
        bgexec_complete (bg));
    ok (sc.cb_on_complete == 1,
        "cancel: cb_on_complete fired once (got %d)", sc.cb_on_complete);
    ok (sc.errors == 0,
        "cancel: discarded tasks did not fire cb_on_error (got %d)", sc.errors);
    ok (sc.cb_on_start == 0,
        "cancel: cb_on_start did not fire (nothing launched)");
    ok (bgexec_started_count (bg) == 0,
        "cancel: started_count is 0 (got %d)", bgexec_started_count (bg));
    /* A start after cancel must fail rather than re-expand the discarded
     * tasks.
     */
    ok (bgexec_start (h, bg) < 0 && errno == EINVAL,
        "cancel: bgexec_start after cancel fails with EINVAL");
    /* Reactor should have no outstanding work. */
    ok (flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT) >= 0,
        "cancel: reactor idle after cancel");
    bgexec_destroy (bg);
}

/* Cancel after all ranks have started: started tasks are left for kill+wait,
 * so cancel discards nothing.  Kill then collects them.
 */
static void test_cancel_after_start (flux_t *h)
{
    char *av[] = { "sleep", "30", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 2, av, ARRAY_SIZE (av) - 1);
    flux_reactor_t *r = flux_get_reactor (h);
    flux_future_t *f;

    ok (bgexec_start (h, bg) == 0,
        "cancel_after: bgexec_start works");
    while (sc.cb_on_start == 0)
        flux_reactor_run (r, FLUX_REACTOR_ONCE);
    ok (bgexec_started_count (bg) == 2,
        "cancel_after: started_count is 2 (got %d)",
        bgexec_started_count (bg));

    ok (bgexec_cancel (bg) == 0,
        "cancel_after: bgexec_cancel works");
    ok (bgexec_complete (bg) == 0,
        "cancel_after: started tasks not discarded by cancel (got %d)",
        bgexec_complete (bg));

    /* kill + wait collects the started tasks. */
    f = bgexec_kill (bg, NULL, SIGTERM);
    ok (f != NULL && flux_future_wait_for (f, 5.) == 0,
        "cancel_after: kill RPC completed");
    flux_future_destroy (f);
    ok (flux_reactor_run (r, 0) >= 0,
        "cancel_after: reactor ran to completion");
    ok (sc.cb_on_complete == 1,
        "cancel_after: cb_on_complete fired after kill+wait");
    bgexec_destroy (bg);
}

/* Cancel discards some pending ranks while another rank is still running,
 * then kill+wait collects the runner.  The discarded ranks are reported
 * through cb_on_exit at cancel time; the runner is reported when it exits.
 * No rank may appear in cb_on_exit twice -- a regression guard for the
 * exit_batch not being cleared on the cancel path.
 */
static void test_cancel_then_exit (flux_t *h)
{
    char *av[] = { "sleep", "30", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 3, av, ARRAY_SIZE (av) - 1);
    flux_reactor_t *r = flux_get_reactor (h);
    flux_future_t *f;

    /* Pace one launch per loop iteration, then run exactly one iteration so
     * a single rank's bg RPC is in flight while the other two remain PENDING
     * with no RPC issued.  Cancel then discards those two (reporting them
     * through cb_on_exit) and leaves the in-flight rank to become the runner.
     */
    ok (bgexec_set_max_per_loop (bg, 1) == 0,
        "cancel_then_exit: max_per_loop set to 1");
    ok (bgexec_start (h, bg) == 0,
        "cancel_then_exit: bgexec_start works");
    flux_reactor_run (r, FLUX_REACTOR_NOWAIT);

    ok (bgexec_cancel (bg) == 0,
        "cancel_then_exit: bgexec_cancel works");
    ok (bgexec_complete (bg) == 2,
        "cancel_then_exit: 2 unissued ranks discarded (got %d)",
        bgexec_complete (bg));
    ok (sc.exit_dups == 0,
        "cancel_then_exit: no rank reported twice through cb_on_exit so far");

    /* Collect the surviving runner; its exit must not re-report the
     * discarded ranks through a stale exit batch.
     */
    while (bgexec_started_count (bg) < 1)
        flux_reactor_run (r, FLUX_REACTOR_ONCE);
    f = bgexec_kill (bg, NULL, SIGKILL);
    ok (f != NULL && flux_future_wait_for (f, 5.) == 0,
        "cancel_then_exit: kill RPC completed");
    flux_future_destroy (f);
    ok (flux_reactor_run (r, 0) >= 0,
        "cancel_then_exit: reactor ran to completion");
    ok (sc.cb_on_complete == 1,
        "cancel_then_exit: cb_on_complete fired once (got %d)",
        sc.cb_on_complete);
    ok (sc.exited_ranks == 3,
        "cancel_then_exit: 3 rank-exits reported total (got %d)",
        sc.exited_ranks);
    ok (sc.exit_dups == 0,
        "cancel_then_exit: no rank reported twice through cb_on_exit");
    bgexec_destroy (bg);
}

/* Exit batching: several ranks running the same fast command exit close
 * together and are coalesced into fewer cb_on_exit batches than ranks.
 */
static void test_exit_batching (flux_t *h)
{
    char *av[] = { "true", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 4, av, ARRAY_SIZE (av) - 1);

    ok (bgexec_start (h, bg) == 0,
        "batching: bgexec_start works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "batching: reactor ran to completion");
    ok (sc.exited_ranks == 4,
        "batching: all 4 ranks reported exited (got %d)", sc.exited_ranks);
    ok (sc.exit_batches >= 1 && sc.exit_batches <= 4,
        "batching: exits coalesced into %d batch(es) for 4 ranks",
        sc.exit_batches);
    ok (sc.exit_batches < 4,
        "batching: fewer batches than ranks (coalescing occurred, got %d)",
        sc.exit_batches);
    bgexec_destroy (bg);
}

/* Targeted kill: signal only a subset of ranks by label; unlisted ranks
 * keep running.  Also exercises the empty-selection ENOENT path.
 */
static void test_kill_targeted (flux_t *h)
{
    char *av[] = { "sleep", "30", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 3, av, ARRAY_SIZE (av) - 1);
    flux_reactor_t *r = flux_get_reactor (h);
    struct idset *target;
    struct bgexec_task *task;
    flux_future_t *f;

    ok (bgexec_start (h, bg) == 0,
        "kill_targeted: bgexec_start works");
    while (sc.cb_on_start == 0)
        flux_reactor_run (r, FLUX_REACTOR_ONCE);

    /* Kill an empty set -> no child futures pushed -> ENOENT. */
    if (!(target = idset_create (0, IDSET_FLAG_AUTOGROW)))
        BAIL_OUT ("idset_create failed");
    ok (bgexec_kill (bg, target, SIGTERM) == NULL && errno == ENOENT,
        "kill_targeted: empty rank set returns ENOENT");

    /* Kill only rank 1. */
    idset_set (target, 1);
    f = bgexec_kill (bg, target, SIGTERM);
    ok (f != NULL && flux_future_wait_for (f, 5.) == 0,
        "kill_targeted: targeted kill RPC completed");
    flux_future_destroy (f);

    /* Pump until rank 1 completes; ranks 0 and 2 remain active. */
    while (bgexec_complete (bg) < 1)
        flux_reactor_run (r, FLUX_REACTOR_ONCE);
    task = bgexec_get_task (bg, 1);
    ok (task != NULL && bgexec_task_signaled (task) == SIGTERM,
        "kill_targeted: rank 1 terminated by SIGTERM (got %d)",
        task ? bgexec_task_signaled (task) : -1);
    ok (bgexec_active_count (bg) == 2,
        "kill_targeted: ranks 0 and 2 still active (got %d)",
        bgexec_active_count (bg));

    /* Clean up the survivors. */
    f = bgexec_kill (bg, NULL, SIGKILL);
    if (f) {
        flux_future_wait_for (f, 5.);
        flux_future_destroy (f);
    }
    flux_reactor_run (r, 0);
    idset_destroy (target);
    bgexec_destroy (bg);
}

/* Partial recovery: one prior process is present, one is missing.  The
 * present rank recovers status; the missing rank surfaces cb_on_error.  Both
 * count toward completion.
 */
static void test_recover_partial (flux_t *h)
{
    char *av[] = { "true", NULL };
    struct scorecard sc;
    struct bgexec *bg;
    struct bgexec_task *t0, *t1;

    /* Only rank 0 has a prior incarnation; rank 1 does not. */
    prior_start (h, 0, av, ARRAY_SIZE (av) - 1);

    bg = setup (h, &sc, 2, av, ARRAY_SIZE (av) - 1);
    ok (bgexec_wait (h, bg) == 0,
        "recover_partial: bgexec_wait works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "recover_partial: reactor ran to completion");
    ok (sc.errors == 1,
        "recover_partial: one rank surfaced cb_on_error (got %d)", sc.errors);
    ok (sc.last_error_rank == 1,
        "recover_partial: error was for the missing rank 1 (got %d)",
        sc.last_error_rank);
    /* Rank 0 recovered a real (zero) status; rank 1 carries a wait error. */
    t0 = bgexec_get_task (bg, 0);
    t1 = bgexec_get_task (bg, 1);
    ok (t0 != NULL && bgexec_task_status (t0) == 0,
        "recover_partial: rank 0 recovered exit status 0");
    ok (t1 != NULL && bgexec_task_errnum (t1) != 0,
        "recover_partial: rank 1 carries a wait error (errno %d)",
        t1 ? bgexec_task_errnum (t1) : 0);
    ok (t1 != NULL && bgexec_task_errmsg (t1) != NULL,
        "recover_partial: rank 1 carries a wait error string (got '%s')",
        (t1 && bgexec_task_errmsg (t1)) ? bgexec_task_errmsg (t1) : "(null)");
    ok (sc.cb_on_complete == 1,
        "recover_partial: cb_on_complete fired");
    ok (bgexec_complete (bg) == 2,
        "recover_partial: both tasks complete (got %d)",
        bgexec_complete (bg));
    bgexec_destroy (bg);
}

/* Accessors: bgexec_service_name, bgexec_set_max_per_loop (including the
 * EINVAL-on-0 guard), and bgexec_active_ranks (the idset of active ranks,
 * which shrinks as ranks are killed and collected).
 */
static void test_accessors (flux_t *h)
{
    char *av[] = { "sleep", "30", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 3, av, ARRAY_SIZE (av) - 1);
    flux_reactor_t *r = flux_get_reactor (h);
    struct idset *active;
    struct idset *target;
    flux_future_t *f;

    ok (streq (bgexec_service_name (bg), SERVER_NAME),
        "accessors: bgexec_service_name returns the service name (got '%s')",
        bgexec_service_name (bg));

    /* set_max_per_loop: 0 is rejected, -1 (no max) and a positive value ok. */
    ok (bgexec_set_max_per_loop (bg, 0) < 0 && errno == EINVAL,
        "accessors: bgexec_set_max_per_loop (0) fails with EINVAL");
    ok (bgexec_set_max_per_loop (bg, -1) == 0,
        "accessors: bgexec_set_max_per_loop (-1) works");
    ok (bgexec_set_max_per_loop (bg, 2) == 0,
        "accessors: bgexec_set_max_per_loop (2) works");

    ok (bgexec_start (h, bg) == 0,
        "accessors: bgexec_start works");
    while (sc.cb_on_start == 0)
        flux_reactor_run (r, FLUX_REACTOR_ONCE);

    /* All three ranks are active. */
    active = bgexec_active_ranks (bg);
    ok (active != NULL && idset_count (active) == 3
        && idset_test (active, 0)
        && idset_test (active, 1)
        && idset_test (active, 2),
        "accessors: active_ranks is {0,1,2} after start (count %d)",
        active ? (int)idset_count (active) : -1);
    idset_destroy (active);

    /* Kill rank 1 and collect it; active_ranks drops to {0,2}. */
    if (!(target = idset_create (0, IDSET_FLAG_AUTOGROW)))
        BAIL_OUT ("idset_create failed");
    idset_set (target, 1);
    f = bgexec_kill (bg, target, SIGTERM);
    ok (f != NULL && flux_future_wait_for (f, 5.) == 0,
        "accessors: targeted kill of rank 1 completed");
    flux_future_destroy (f);
    while (bgexec_complete (bg) < 1)
        flux_reactor_run (r, FLUX_REACTOR_ONCE);

    active = bgexec_active_ranks (bg);
    ok (active != NULL && idset_count (active) == 2
        && idset_test (active, 0)
        && !idset_test (active, 1)
        && idset_test (active, 2),
        "accessors: active_ranks is {0,2} after rank 1 exits (count %d)",
        active ? (int)idset_count (active) : -1);
    idset_destroy (active);

    /* Clean up the survivors. */
    f = bgexec_kill (bg, NULL, SIGKILL);
    if (f) {
        flux_future_wait_for (f, 5.);
        flux_future_destroy (f);
    }
    flux_reactor_run (r, 0);
    idset_destroy (target);
    bgexec_destroy (bg);
}

/* push_cmd validation (empty set, overlapping ranks) and the once-only
 * start/wait guards.
 */
static void test_push_and_start_guards (flux_t *h)
{
    char *av[] = { "true", NULL };
    struct scorecard sc;
    struct bgexec *bg;
    struct idset *ranks;
    flux_cmd_t *cmd = NULL;

    memset (&sc, 0, sizeof (sc));
    if (!(bg = bgexec_create (&test_ops, SERVER_NAME, TEST_JOBID, "shell",
                              &sc)))
        BAIL_OUT ("bgexec_create failed");
    if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW))
        || !(cmd = flux_cmd_create (ARRAY_SIZE (av) - 1, av, environ)))
        BAIL_OUT ("setup failed");

    /* Empty rank set is rejected. */
    ok (bgexec_push_cmd (bg, ranks, cmd) < 0 && errno == EINVAL,
        "push_guards: empty rank set fails with EINVAL");

    /* Push ranks [0-2], then a command overlapping rank 2 is rejected and
     * the object is left unchanged (total stays 3).
     */
    idset_range_set (ranks, 0, 2);
    ok (bgexec_push_cmd (bg, ranks, cmd) == 0,
        "push_guards: push of ranks [0-2] works");
    ok (bgexec_total (bg) == 3,
        "push_guards: total is 3 (got %d)", bgexec_total (bg));
    idset_range_clear (ranks, 0, INT_MAX);
    idset_range_set (ranks, 2, 4);
    ok (bgexec_push_cmd (bg, ranks, cmd) < 0 && errno == EEXIST,
        "push_guards: overlapping rank push fails with EEXIST");
    ok (bgexec_total (bg) == 3,
        "push_guards: total unchanged after rejected push (got %d)",
        bgexec_total (bg));

    /* A disjoint push still works. */
    idset_range_clear (ranks, 0, INT_MAX);
    idset_range_set (ranks, 3, 4);
    ok (bgexec_push_cmd (bg, ranks, cmd) == 0,
        "push_guards: disjoint push of ranks [3-4] works");
    ok (bgexec_total (bg) == 5,
        "push_guards: total is 5 (got %d)", bgexec_total (bg));

    /* Start once; a second start (or a wait) fails with EINVAL. */
    ok (bgexec_start (h, bg) == 0,
        "push_guards: first bgexec_start works");
    ok (bgexec_start (h, bg) < 0 && errno == EINVAL,
        "push_guards: second bgexec_start fails with EINVAL");
    ok (bgexec_wait (h, bg) < 0 && errno == EINVAL,
        "push_guards: bgexec_wait after start fails with EINVAL");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "push_guards: reactor ran to completion");

    flux_cmd_destroy (cmd);
    idset_destroy (ranks);
    bgexec_destroy (bg);
}

/* aux_set/aux_get round-trip and EINVAL guards on NULL bgexec. */
static void test_aux_and_guards (flux_t *h)
{
    char *av[] = { "true", NULL };
    struct scorecard sc;
    struct bgexec *bg = setup (h, &sc, 1, av, ARRAY_SIZE (av) - 1);
    int val = 42;

    ok (bgexec_aux_set (bg, "key", &val, NULL) == 0,
        "aux: bgexec_aux_set works");
    ok (bgexec_aux_get (bg, "key") == &val,
        "aux: bgexec_aux_get returns the stored value");

    /* NULL-bgexec guards. */
    ok (bgexec_aux_set (NULL, "k", &val, NULL) < 0 && errno == EINVAL,
        "guards: bgexec_aux_set (NULL) fails with EINVAL");
    ok (bgexec_aux_get (NULL, "k") == NULL,
        "guards: bgexec_aux_get (NULL) returns NULL");
    ok (bgexec_start (h, NULL) < 0 && errno == EINVAL,
        "guards: bgexec_start (NULL) fails with EINVAL");
    ok (bgexec_wait (h, NULL) < 0 && errno == EINVAL,
        "guards: bgexec_wait (NULL) fails with EINVAL");
    ok (bgexec_cancel (NULL) < 0 && errno == EINVAL,
        "guards: bgexec_cancel (NULL) fails with EINVAL");
    ok (bgexec_kill (NULL, NULL, SIGTERM) == NULL && errno == EINVAL,
        "guards: bgexec_kill (NULL) fails with EINVAL");
    ok (bgexec_push_cmd (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "guards: bgexec_push_cmd (NULL) fails with EINVAL");
    ok (bgexec_get_task (bg, -1) == NULL && errno == EINVAL,
        "guards: bgexec_get_task (rank<0) fails with EINVAL");
    ok (bgexec_get_task (bg, 99) == NULL && errno == ENOENT,
        "guards: bgexec_get_task (unknown rank) fails with ENOENT");
    bgexec_destroy (bg);
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    signal (SIGPIPE, SIG_IGN);
    h = rcmdsrv_create (SERVER_NAME);

    diag ("test_basic");
    test_basic (h);
    diag ("test_nonzero");
    test_nonzero (h);
    diag ("test_launch_failure");
    test_launch_failure (h);
    diag ("test_mixed");
    test_mixed (h);
    diag ("test_output");
    test_output (h);
    diag ("test_kill");
    test_kill (h);
    diag ("test_recover_wait");
    test_recover_wait (h);
    diag ("test_recover_missing");
    test_recover_missing (h);
    diag ("test_recover_partial");
    test_recover_partial (h);
    diag ("test_cancel_before_start");
    test_cancel_before_start (h);
    diag ("test_cancel_after_start");
    test_cancel_after_start (h);
    diag ("test_cancel_then_exit");
    test_cancel_then_exit (h);
    diag ("test_exit_batching");
    test_exit_batching (h);
    diag ("test_kill_targeted");
    test_kill_targeted (h);
    diag ("test_accessors");
    test_accessors (h);
    diag ("test_push_and_start_guards");
    test_push_and_start_guards (h);
    diag ("test_aux_and_guards");
    test_aux_and_guards (h);

    test_server_stop (h);
    flux_close (h);

    done_testing ();
    return 0;
}

// vi: ts=4 sw=4 expandtab
