/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* bgexec - background/waitable bulk subprocess execution
 *
 * Launch a set of subprocesses across broker ranks and collect their exit
 * status, built on the RFC 42 background primitives flux_rexec_bg(3) +
 * flux_rexec_wait(3) rather than the streaming flux_rexec_ex(3).  The start
 * and wait phases are decoupled: because the subprocess server retains an
 * exited-but-unwaited process until it is waited on, the wait may be
 * re-issued by a client that started the processes, went away, and came
 * back.  Each subprocess is addressed by a deterministic per-rank label
 * derived from (name, rank, jobid), so wait needs no state persisted from
 * the start.
 *
 * Task accounting
 * ---------------
 * Each rank is a "task" with a state: PENDING (created, not yet launched),
 * STARTED (launch succeeded, wait outstanding), EXITED (wait collected
 * status), or FAILED (launch failed before the process ran).  EXITED and
 * FAILED are terminal.  The accessors report on this set:
 *
 *   bgexec_total()         tasks expected to run (fixed once commands are
 *                          pushed)
 *   bgexec_started_count() tasks that have reached STARTED
 *   bgexec_complete()      tasks in a terminal state (EXITED + FAILED)
 *   bgexec_active_count()  tasks not yet terminal (total - complete),
 *   bgexec_active_ranks()  and the ranks they occupy
 *
 * on_complete fires when complete == total.  A task counts toward complete
 * whether it exited, failed to launch, or was discarded by bgexec_cancel();
 * every task reaches a terminal state exactly once, so the counts are
 * monotonic within the lifetime of one bgexec object.
 *
 * Recovery
 * --------
 * The bgexec object is designed to be recoverable across a client restart.
 * The caller may destroy the bgexec object without disturbing its subprocs.
 * On restart, the caller creates a fresh bgexec object, re-pushes the same
 * commands (regenerating the same labels), and calls bgexec_wait() instead
 * of bgexec_start().
 *
 * Every task begins at PENDING as before, but skips the launch phase:
 * bgexec_wait() marks each task STARTED and issues its wait by label
 * directly.  A process that already exited in the previous incarnation is
 * collected from the server's retained state, and one still running is
 * waited on as usual; either way the task reaches EXITED when the wait
 * response arrives.  From the new object's point of view the accounting is
 * otherwise indistinguishable from a normal run.
 *
 * If a subprocess did not survive, so the server has no retained state
 * for its label, the recovery wait fails rather than returning a status.
 * The task moves to FAILED and on_error() fires with the wait errno and
 * message on the task; it still counts toward completion.  The error
 * reports that the label is unknown to the server, but cannot distinguish
 * whether the subprocess ran and exited (its status now lost) or never
 * started, so the caller must treat a failed recovery wait as a lost
 * subprocess of unknown disposition.
 */

#ifndef _SUBPROCESS_BGEXEC_H
#define _SUBPROCESS_BGEXEC_H 1

#include <flux/core.h>
#include <flux/idset.h>

struct bgexec;
struct bgexec_task;

/*  Per-rank handle accessors.  The task handle is owned by the bgexec
 *  object and is valid for the lifetime of that object.
 */

/* Broker rank the task runs on, or -1 if unknown. */
int bgexec_task_rank (struct bgexec_task *task);

/* Raw wait status (identical to the value flux_subprocess_status(3) would
 * return: decode with WIFEXITED/WEXITSTATUS/WIFSIGNALED/WTERMSIG).  Only
 * meaningful once the task has exited.
 */
int bgexec_task_status (struct bgexec_task *task);

/* Terminating signal number if the task was killed by a signal, else 0. */
int bgexec_task_signaled (struct bgexec_task *task);

/* errno from on_error() launch failure, else 0.
 */
int bgexec_task_errnum (struct bgexec_task *task);

/* Human readable error string from on_error() launch failure, else NULL. */
const char *bgexec_task_errmsg (struct bgexec_task *task);

/* The command the task runs (e.g. for labeling output). */
flux_cmd_t *bgexec_task_cmd (struct bgexec_task *task);

typedef void (*bgexec_cb_f) (struct bgexec *bg, void *arg);

typedef void (*bgexec_exit_f) (struct bgexec *bg,
                               void *arg,
                               const struct idset *ranks);

typedef void (*bgexec_error_f) (struct bgexec *bg,
                                struct bgexec_task *task,
                                void *arg);

typedef void (*bgexec_io_f) (struct bgexec *bg,
                             struct bgexec_task *task,
                             const char *stream,
                             const char *data,
                             int data_len,
                             void *arg);

/*  Callbacks fire from the reactor while the bgexec object is being driven.
 *  A callback MUST NOT destroy the bgexec object; destroy it from outside
 *  the reactor context (typically after on_complete or a NULL-task
 *  on_error has returned).
 *
 *  on_start fires once, only if EVERY task reaches STARTED.  On the launch
 *  path (bgexec_start) a single launch failure means on_start never fires,
 *  even though other ranks are running -- gate on on_error/on_complete, not
 *  the absence of on_start.  On the wait path (bgexec_wait) all tasks are
 *  marked STARTED up front, so on_start always fires.
 *
 *  on_error(task=NULL) signals a whole-object launch-pacing failure: no
 *  further tasks will start and on_complete will NOT fire.  Treat it as
 *  fatal and destroy the object.  on_error(task!=NULL) reports one task's
 *  launch or wait failure; that task still counts toward on_complete.
 *
 *  on_exit reports each rank exactly once.  A task discarded by
 *  bgexec_cancel() is reported through on_exit with a zero exit status
 *  (it never ran), indistinguishable by status alone from a clean exit.
 */
struct bgexec_ops {
    bgexec_cb_f    on_start;    /* all tasks have reached STARTED        */
    bgexec_exit_f  on_exit;     /* a batch of tasks has exited           */
    bgexec_cb_f    on_complete; /* all tasks have exited or failed       */
    bgexec_error_f on_error;    /* a task failed to launch or be waited  */
    bgexec_io_f    on_output;   /* early telemetry retained by wait      */
};

struct bgexec *bgexec_create (struct bgexec_ops *ops,
                              const char *service,
                              flux_jobid_t id,
                              const char *name,
                              void *arg);

void bgexec_destroy (struct bgexec *bg);

void *bgexec_aux_get (struct bgexec *bg, const char *key);

int bgexec_aux_set (struct bgexec *bg,
                    const char *key,
                    void *val,
                    flux_free_f free_fn);

/*  Set maximum number of flux_rexec_bg(3) calls per event loop iteration.
 *  The default is 1.  Pass -1 for no max; 0 is rejected (EINVAL).
 */
int bgexec_set_max_per_loop (struct bgexec *bg, int max);

/*  Register a command to run on a non-empty set of ranks.  Must be called
 *  before bgexec_start(), bgexec_wait(), or bgexec_cancel(); pushing after
 *  any of those fails with EINVAL.  A rank may appear in only one pushed
 *  command (labels are per-rank); pushing a rank already covered by an
 *  earlier command fails with EEXIST and leaves the object unchanged.  An
 *  empty rank set is rejected with EINVAL.
 */
int bgexec_push_cmd (struct bgexec *bg,
                     const struct idset *ranks,
                     flux_cmd_t *cmd);

/*  Start all pushed commands with flux_rexec_bg(3) and wait for them with
 *  flux_rexec_wait(3).  If the bgexec object is destroyed, subprocesses
 *  are left to run on their own.  If any exit while not being waited for,
 *  the subprocess server retains state so that bgexec_wait(3) can collect
 *  it later.  Fails with EINVAL if the object has already been started or
 *  waited on (start and wait are each once-only and mutually exclusive).
 */
int bgexec_start (flux_t *h, struct bgexec *bg);

/*  Wait on all pushed commands with flux_rexec_wait(3).
 *  This is for re-attaching after destroying and recreating the bgexec
 *  object.  Fails with EINVAL if the object has already been started or
 *  waited on.
 */
int bgexec_wait (flux_t *h, struct bgexec *bg);

/*  Cancel: discard every task that has not yet been launched (mark it
 *  complete, counting toward on_complete) and stop issuing new starts.
 *  Tasks whose start RPC is already in flight complete normally, and
 *  already-started tasks are left for bgexec_kill() + wait to collect.
 */
int bgexec_cancel (struct bgexec *bg);

/*  Send signal to ranks.  Set ranks=NULL for all.  Kills by label.
 */
flux_future_t *bgexec_kill (struct bgexec *bg,
                            const struct idset *ranks,
                            int signum);

/*  Log per-rank kill errors for a failed bgexec_kill() RPC.
 */
void bgexec_kill_log_error (flux_future_t *f, flux_jobid_t id);

/* Returns max wait status returned from all exited tasks */
int bgexec_rc (struct bgexec *bg);

/* Returns total number of tasks expected to run */
int bgexec_total (struct bgexec *bg);

/* Returns number of tasks that have reached STARTED */
int bgexec_started_count (struct bgexec *bg);

/* Returns number of tasks that have completed (exited or failed) */
int bgexec_complete (struct bgexec *bg);

/* Returns number of tasks that are still active */
int bgexec_active_count (struct bgexec *bg);

/* Returns idset of ranks on which tasks are still active */
struct idset *bgexec_active_ranks (struct bgexec *bg);

/* Get subprocess remote exec service name (never returns NULL) */
const char *bgexec_service_name (struct bgexec *bg);

/* Get the per-rank task handle for a rank, or NULL with errno set. */
struct bgexec_task *bgexec_get_task (struct bgexec *bg, int rank);

#endif /* !_SUBPROCESS_BGEXEC_H */

/* vi: ts=4 sw=4 expandtab
 */
