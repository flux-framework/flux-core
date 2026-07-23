/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/wait.h>
#include <limits.h>
#include <stdbool.h>
#include <flux/core.h>
#include <flux/idset.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/aux.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "subprocess.h"
#include "bgexec.h"

enum task_state {
    BGEXEC_TASK_PENDING = 0, /* created; bg not yet issued or in flight   */
    BGEXEC_TASK_STARTED,     /* bg response success; wait outstanding     */
    BGEXEC_TASK_EXITED,      /* wait response received                    */
    BGEXEC_TASK_FAILED,      /* bg response error (pre-RUNNING failure)   */
};

struct bgexec_task {
    struct bgexec *bg;
    int rank;
    enum task_state state;
    flux_cmd_t *cmd;
    char *label;

    pid_t pid;
    int status;              /* raw wait status (valid once EXITED)       */
    int errnum;              /* launch/wait failure errno                 */
    char *errmsg;            /* launch/wait failure string                */

    flux_future_t *bg_future;   /* in-flight start RPC                    */
    flux_future_t *wait_future; /* in-flight wait RPC                     */
};

struct exec_cmd {
    struct idset *ranks;
    flux_cmd_t *cmd;
};

struct bgexec {
    flux_t *h;

    char *service;
    flux_jobid_t id;
    char *name;

    struct aux_item *aux;

    int max_start_per_loop;  /* Max bg RPCs started per event loop cb     */
    int total;               /* Total tasks expected to run               */
    int started;             /* Number of tasks that reached STARTED      */
    int complete;            /* Number of tasks that completed            */

    int exit_status;         /* Largest wait status of all exited tasks   */

    unsigned int active:1;

    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;

    struct idset *exit_batch;         /* Support for batched exit notify   */
    flux_watcher_t *exit_batch_timer; /* Timer for batched exit notify     */

    zlist_t *commands;
    zlist_t *tasks;
    struct idset *pushed_ranks;  /* union of ranks across pushed commands   */

    struct bgexec_ops *handlers;
    void *arg;
};

extern char **environ;

/*  Per-rank handle accessors
 */

int bgexec_task_rank (struct bgexec_task *task)
{
    return task ? task->rank : -1;
}

int bgexec_task_status (struct bgexec_task *task)
{
    return task ? task->status : 0;
}

int bgexec_task_signaled (struct bgexec_task *task)
{
    if (!task || !WIFSIGNALED (task->status))
        return 0;
    return WTERMSIG (task->status);
}

int bgexec_task_errnum (struct bgexec_task *task)
{
    return task ? task->errnum : 0;
}

const char *bgexec_task_errmsg (struct bgexec_task *task)
{
    return task ? task->errmsg : NULL;
}

flux_cmd_t *bgexec_task_cmd (struct bgexec_task *task)
{
    return task ? task->cmd : NULL;
}

/*  Aggregate accessors
 */

int bgexec_rc (struct bgexec *bg)
{
    if (!bg) {
        errno = EINVAL;
        return -1;
    }
    return bg->exit_status;
}

int bgexec_total (struct bgexec *bg)
{
    return bg ? bg->total : 0;
}

int bgexec_started_count (struct bgexec *bg)
{
    return bg ? bg->started : 0;
}

int bgexec_complete (struct bgexec *bg)
{
    return bg ? bg->complete : 0;
}

int bgexec_active_count (struct bgexec *bg)
{
    if (!bg)
        return 0;
    return bg->total - bg->complete;
}

struct idset *bgexec_active_ranks (struct bgexec *bg)
{
    struct bgexec_task *task;
    struct idset *ranks;

    if (!bg || !bg->tasks) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    /*  A task is active until it reaches a terminal state (EXITED or
     *  FAILED).  This includes PENDING tasks so the idset stays consistent
     *  with bgexec_active_count() during the launch ramp.
     */
    task = zlist_first (bg->tasks);
    while (task) {
        if (task->state != BGEXEC_TASK_EXITED
            && task->state != BGEXEC_TASK_FAILED
            && task->rank >= 0) {
            if (idset_set (ranks, task->rank) < 0)
                goto error;
        }
        task = zlist_next (bg->tasks);
    }
    return ranks;
error:
    idset_destroy (ranks);
    return NULL;
}

const char *bgexec_service_name (struct bgexec *bg)
{
    if (!bg)
        return NULL;
    return bg->service;
}

struct bgexec_task *bgexec_get_task (struct bgexec *bg, int rank)
{
    struct bgexec_task *task;

    if (!bg || rank < 0) {
        errno = EINVAL;
        return NULL;
    }
    task = zlist_first (bg->tasks);
    while (task) {
        if (task->rank == rank)
            return task;
        task = zlist_next (bg->tasks);
    }
    errno = ENOENT;
    return NULL;
}

int bgexec_aux_set (struct bgexec *bg,
                    const char *key,
                    void *val,
                    flux_free_f free_fn)
{
    if (!bg) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&bg->aux, key, val, free_fn);
}

void *bgexec_aux_get (struct bgexec *bg, const char *key)
{
    if (!bg) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (bg->aux, key);
}

/*  Exit batching: coalesce task exits that happen within 0.01s into a
 *  single on_exit callback.
 */

static void exit_notify (struct bgexec *bg)
{
    if (bg->handlers->on_exit)
        (*bg->handlers->on_exit) (bg, bg->arg, bg->exit_batch);
    /*  Clear the batch unconditionally.  bgexec_cancel() adds discarded
     *  ranks directly (with no timer) and calls exit_notify(): if the clear
     *  were gated on the timer those ranks would linger and reappear in a
     *  later on_exit() when a still-running task exits.
     */
    flux_watcher_destroy (bg->exit_batch_timer);
    bg->exit_batch_timer = NULL;
    idset_range_clear (bg->exit_batch, 0, INT_MAX);
}

static void exit_batch_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct bgexec *bg = arg;
    exit_notify (bg);
}

static void exit_batch_append (struct bgexec *bg, struct bgexec_task *task)
{
    if (task->rank >= 0 && idset_set (bg->exit_batch, task->rank) < 0) {
        flux_log_error (bg->h, "bgexec: exit_batch_append:idset_set");
        return;
    }
    if (!bg->exit_batch_timer) {
        flux_reactor_t *r = flux_get_reactor (bg->h);
        bg->exit_batch_timer = flux_timer_watcher_create (r,
                                                          0.01,
                                                          0.,
                                                          exit_batch_cb,
                                                          bg);
        if (!bg->exit_batch_timer) {
            flux_log_error (bg->h, "bgexec: exit_batch_append:timer create");
            return;
        }
        flux_watcher_start (bg->exit_batch_timer);
    }
}

static void add_completed (struct bgexec *bg, struct bgexec_task *task)
{
    exit_batch_append (bg, task);

    if (++bg->complete == bg->total) {
        exit_notify (bg);
        if (bg->handlers->on_complete)
            (*bg->handlers->on_complete) (bg, bg->arg);
    }
}

/*  Feed any retained early-output from a wait response to on_output.
 */
static void deliver_output (struct bgexec *bg,
                            struct bgexec_task *task,
                            json_t *output)
{
    size_t i;

    if (!output || !bg->handlers->on_output)
        return;
    for (i = 0; i < json_array_size (output); i++) {
        const char *stream = NULL;
        char *data = NULL;
        int len = 0;
        if (iodecode (json_array_get (output, i),
                      &stream,
                      NULL,
                      &data,
                      &len,
                      NULL) < 0)
            continue;
        if (data && len > 0)
            (*bg->handlers->on_output) (bg,
                                        task,
                                        stream,
                                        data,
                                        len,
                                        bg->arg);
        free (data);
    }
}

static void task_set_error (struct bgexec_task *task,
                            int errnum,
                            const char *errmsg)
{
    task->errnum = errnum;
    free (task->errmsg);
    task->errmsg = errmsg ? strdup (errmsg) : NULL;
}

/*  wait RPC response: transition STARTED -> EXITED and collect status.
 */
static void wait_continuation (flux_future_t *f, void *arg)
{
    struct bgexec_task *task = arg;
    struct bgexec *bg = task->bg;
    int status;
    json_t *output = NULL;

    if (flux_rpc_get_unpack (f,
                             "{s:i s?o}",
                             "status", &status,
                             "output", &output) < 0) {
        /* wait failed: e.g. the process was not found on a recovery wait
         * because it did not survive.  Surface via on_error and complete
         * the task.
         */
        task->state = BGEXEC_TASK_FAILED;
        task_set_error (task, errno, future_strerror (f, errno));
        if (bg->handlers->on_error)
            (*bg->handlers->on_error) (bg, task, bg->arg);
        goto done;
    }
    deliver_output (bg, task, output);
    task->status = status;
    task->state = BGEXEC_TASK_EXITED;
    if (status > bg->exit_status)
        bg->exit_status = status;
done:
    flux_future_destroy (f);
    task->wait_future = NULL;
    add_completed (bg, task);
}

static int task_issue_wait (struct bgexec_task *task)
{
    struct bgexec *bg = task->bg;
    flux_future_t *f;

    if (!(f = flux_rexec_wait (bg->h,
                               bg->service,
                               task->rank,
                               -1,
                               task->label)))
        return -1;
    if (flux_future_then (f, -1., wait_continuation, task) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    task->wait_future = f;
    return 0;
}

/*  bg RPC response: transition PENDING -> STARTED (and issue wait) or
 *  PENDING -> FAILED.
 */
static void bg_continuation (flux_future_t *f, void *arg)
{
    struct bgexec_task *task = arg;
    struct bgexec *bg = task->bg;
    int pid;

    if (flux_rpc_get_unpack (f, "{s:i}", "pid", &pid) < 0) {
        task->state = BGEXEC_TASK_FAILED;
        task_set_error (task, errno, future_strerror (f, errno));
        flux_future_destroy (f);
        task->bg_future = NULL;
        if (bg->handlers->on_error)
            (*bg->handlers->on_error) (bg, task, bg->arg);
        add_completed (bg, task);
        return;
    }
    task->pid = pid;
    task->state = BGEXEC_TASK_STARTED;
    flux_future_destroy (f);
    task->bg_future = NULL;

    if (++bg->started == bg->total) {
        if (bg->handlers->on_start)
            (*bg->handlers->on_start) (bg, bg->arg);
    }

    /* Pipelined: issue this rank's wait immediately.  A failure here leaves
     * the subprocess running on the server with no wait outstanding: it is
     * orphaned (will run to completion uncollected), so say so.
     */
    if (task_issue_wait (task) < 0) {
        task->state = BGEXEC_TASK_FAILED;
        task_set_error (task,
                        errno,
                        "started but failed to issue wait: "
                        "subprocess orphaned");
        if (bg->handlers->on_error)
            (*bg->handlers->on_error) (bg, task, bg->arg);
        add_completed (bg, task);
    }
}

static int task_issue_bg (struct bgexec_task *task)
{
    struct bgexec *bg = task->bg;
    flux_future_t *f;

    if (!(f = flux_rexec_bg (bg->h,
                             bg->service,
                             task->rank,
                             FLUX_SUBPROCESS_FLAGS_WAITABLE,
                             task->cmd)))
        return -1;
    if (flux_future_then (f, -1., bg_continuation, task) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    task->bg_future = f;
    return 0;
}

/*  Count tasks still awaiting their bg RPC to be issued.
 */
static int count_startable (struct bgexec *bg)
{
    struct bgexec_task *task;
    int count = 0;

    task = zlist_first (bg->tasks);
    while (task) {
        if (task->state == BGEXEC_TASK_PENDING && !task->bg_future)
            count++;
        task = zlist_next (bg->tasks);
    }
    return count;
}

static void bgexec_stop (struct bgexec *bg)
{
    flux_watcher_stop (bg->prep);
    flux_watcher_stop (bg->check);
}

/*  Issue up to 'max' bg RPCs for PENDING tasks (-1 for no limit).
 */
static int start_pending (struct bgexec *bg, int max)
{
    struct bgexec_task *task;
    int count = 0;

    task = zlist_first (bg->tasks);
    while (task && (max < 0 || count < max)) {
        if (task->state == BGEXEC_TASK_PENDING && !task->bg_future) {
            if (task_issue_bg (task) < 0)
                return -1;
            count++;
        }
        task = zlist_next (bg->tasks);
    }
    return 0;
}

static void prep_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct bgexec *bg = arg;

    if (count_startable (bg) > 0) {
        flux_watcher_start (bg->idle);
        flux_watcher_start (bg->check);
    }
    else
        bgexec_stop (bg);
}

static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct bgexec *bg = arg;

    flux_watcher_stop (bg->idle);
    flux_watcher_stop (bg->check);
    if (start_pending (bg, bg->max_start_per_loop) < 0) {
        /*  A whole-object launch-pacing failure (e.g. flux_rexec_bg()
         *  itself failed).  Report it with a NULL task and stop the loop.
         *  The as-yet-unlaunched PENDING tasks are left non-terminal, so
         *  on_complete will not fire: the caller must treat on_error() with
         *  a NULL task as fatal and destroy the object.
         */
        flux_log_error (bg->h, "bgexec: start_pending failed");
        bgexec_stop (bg);
        if (bg->handlers->on_error)
            (*bg->handlers->on_error) (bg, NULL, bg->arg);
    }
}

static void task_destroy (struct bgexec_task *task)
{
    if (task) {
        int saved_errno = errno;
        flux_future_destroy (task->bg_future);
        flux_future_destroy (task->wait_future);
        flux_cmd_destroy (task->cmd);
        free (task->label);
        free (task->errmsg);
        free (task);
        errno = saved_errno;
    }
}

/*  Build the deterministic per-rank label "<name>-<rank>-<jobid.f58plain>".
 */
static char *task_label_create (struct bgexec *bg, int rank)
{
    char idbuf[21];
    char *label;

    if (flux_job_id_encode (bg->id, "f58plain", idbuf, sizeof (idbuf)) < 0)
        return NULL;
    if (asprintf (&label, "%s-%d-%s", bg->name, rank, idbuf) < 0)
        return NULL;
    return label;
}

static struct bgexec_task *task_create (struct bgexec *bg,
                                        int rank,
                                        flux_cmd_t *cmd)
{
    struct bgexec_task *task;

    if (!(task = calloc (1, sizeof (*task))))
        return NULL;
    task->bg = bg;
    task->rank = rank;
    task->pid = -1;
    task->state = BGEXEC_TASK_PENDING;
    if (!(task->cmd = flux_cmd_copy (cmd))
        || !(task->label = task_label_create (bg, rank))
        || flux_cmd_set_label (task->cmd, task->label) < 0)
        goto error;
    return task;
error:
    task_destroy (task);
    return NULL;
}

/*  Expand all pushed commands into per-rank task objects.
 */
static int expand_tasks (struct bgexec *bg)
{
    struct exec_cmd *cmd;

    cmd = zlist_first (bg->commands);
    while (cmd) {
        uint32_t rank = idset_first (cmd->ranks);
        while (rank != IDSET_INVALID_ID) {
            struct bgexec_task *task;
            if (!(task = task_create (bg, rank, cmd->cmd)))
                return -1;
            if (zlist_append (bg->tasks, task) < 0) {
                task_destroy (task);
                errno = ENOMEM;
                return -1;
            }
            zlist_freefn (bg->tasks,
                          task,
                          (zlist_free_fn *) task_destroy,
                          true);
            rank = idset_next (cmd->ranks, rank);
        }
        cmd = zlist_next (bg->commands);
    }
    return 0;
}

int bgexec_start (flux_t *h, struct bgexec *bg)
{
    flux_reactor_t *r;

    if (!h || !bg || bg->active) {
        errno = EINVAL;
        return -1;
    }
    bg->h = h;
    /*  Commit to this run before expand_tasks(): on partial failure it may
     *  have appended some tasks, so the object is not safely re-startable
     *  and must be destroyed.  Setting active now makes a retry fail EINVAL
     *  rather than double-expand.
     */
    bg->active = 1;
    if (expand_tasks (bg) < 0)
        return -1;
    r = flux_get_reactor (h);
    bg->prep = flux_prepare_watcher_create (r, prep_cb, bg);
    bg->check = flux_check_watcher_create (r, check_cb, bg);
    bg->idle = flux_idle_watcher_create (r, NULL, NULL);
    if (!bg->prep || !bg->check || !bg->idle)
        return -1;
    flux_watcher_start (bg->prep);
    return 0;
}

int bgexec_wait (flux_t *h, struct bgexec *bg)
{
    struct bgexec_task *task;

    if (!h || !bg || bg->active) {
        errno = EINVAL;
        return -1;
    }
    bg->h = h;
    bg->active = 1;
    if (expand_tasks (bg) < 0)
        return -1;

    /* Wait-only entry: the processes are already running, so there is no
     * bg (launch) phase.  Mark every task STARTED first, then fire on_start
     * once, then issue the waits.  Doing all the STARTED marking before any
     * callback keeps on_start's "all tasks have reached STARTED" contract
     * exact, and ensures no per-task wait failure (which fires on_error /
     * on_complete via add_completed) is observed before on_start.
     */
    task = zlist_first (bg->tasks);
    while (task) {
        task->state = BGEXEC_TASK_STARTED;
        bg->started++;
        task = zlist_next (bg->tasks);
    }
    if (bg->started == bg->total && bg->handlers->on_start)
        (*bg->handlers->on_start) (bg, bg->arg);

    task = zlist_first (bg->tasks);
    while (task) {
        struct bgexec_task *next = zlist_next (bg->tasks);
        if (task_issue_wait (task) < 0) {
            task->state = BGEXEC_TASK_FAILED;
            task_set_error (task, errno, "failed to issue wait request");
            if (bg->handlers->on_error)
                (*bg->handlers->on_error) (bg, task, bg->arg);
            add_completed (bg, task);
        }
        task = next;
    }
    return 0;
}

int bgexec_cancel (struct bgexec *bg)
{
    struct bgexec_task *task;
    bool discarded = false;

    if (!bg) {
        errno = EINVAL;
        return -1;
    }
    /*  Stop the pacing loop so no further bg RPCs are issued, and mark the
     *  object active so a later bgexec_start()/bgexec_wait() fails EINVAL
     *  rather than re-expanding tasks on top of the discarded ones.
     */
    bgexec_stop (bg);
    bg->active = 1;

    /*  If cancel arrives before bgexec_start()/bgexec_wait() has expanded
     *  the pushed commands, expand them now so the pending work can be
     *  discarded.
     */
    if (zlist_size (bg->tasks) == 0 && expand_tasks (bg) < 0)
        return -1;

    /*  Discard tasks that never launched (PENDING with no start RPC in
     *  flight): count them complete and add them to the exit batch, but do
     *  not fire on_error -- they were abandoned, not failed.  Tasks with a
     *  start RPC in flight complete via bg_continuation; STARTED tasks are
     *  left for bgexec_kill() + wait to collect.
     */
    task = zlist_first (bg->tasks);
    while (task) {
        if (task->state == BGEXEC_TASK_PENDING && !task->bg_future) {
            task->state = BGEXEC_TASK_EXITED;
            if (task->rank >= 0
                && idset_set (bg->exit_batch, task->rank) < 0)
                flux_log_error (bg->h, "bgexec_cancel: idset_set");
            bg->complete++;
            discarded = true;
        }
        task = zlist_next (bg->tasks);
    }
    if (discarded) {
        exit_notify (bg);
        if (bg->complete == bg->total && bg->handlers->on_complete)
            (*bg->handlers->on_complete) (bg, bg->arg);
    }
    return 0;
}

int bgexec_set_max_per_loop (struct bgexec *bg, int max)
{
    if (!bg || max == 0) {
        errno = EINVAL;
        return -1;
    }
    bg->max_start_per_loop = max;
    return 0;
}

static void exec_cmd_destroy (void *arg)
{
    struct exec_cmd *cmd = arg;
    if (cmd) {
        int saved_errno = errno;
        idset_destroy (cmd->ranks);
        flux_cmd_destroy (cmd->cmd);
        free (cmd);
        errno = saved_errno;
    }
}

static struct exec_cmd *exec_cmd_create (const struct idset *ranks,
                                         flux_cmd_t *cmd)
{
    struct exec_cmd *c = calloc (1, sizeof (*c));
    if (!c)
        return NULL;
    if (!(c->ranks = idset_copy (ranks))
        || !(c->cmd = flux_cmd_copy (cmd)))
        goto error;
    return c;
error:
    exec_cmd_destroy (c);
    return NULL;
}

int bgexec_push_cmd (struct bgexec *bg,
                     const struct idset *ranks,
                     flux_cmd_t *cmd)
{
    struct exec_cmd *c;

    if (!bg || !ranks || !cmd || idset_count (ranks) == 0 || bg->active) {
        errno = EINVAL;
        return -1;
    }
    /*  Per-rank labels are derived from (name, rank, jobid), so a rank may
     *  appear in only one pushed command; a duplicate would collide on the
     *  server and double-count bg->total.  Reject an overlap up front.
     */
    if (idset_has_intersection (bg->pushed_ranks, ranks)) {
        errno = EEXIST;
        return -1;
    }
    if (idset_add (bg->pushed_ranks, ranks) < 0)
        return -1;
    if (!(c = exec_cmd_create (ranks, cmd)))
        goto error;
    if (zlist_append (bg->commands, c) < 0) {
        exec_cmd_destroy (c);
        errno = ENOMEM;
        goto error;
    }
    zlist_freefn (bg->commands, c, exec_cmd_destroy, true);
    bg->total += idset_count (ranks);
    return 0;
error:
    /*  Roll back the rank registration so the object is unchanged on
     *  failure (no intersection was found above, so subtracting is safe).
     */
    idset_subtract (bg->pushed_ranks, ranks);
    return -1;
}

/*  Loop through all child futures of a failed kill and log rank-specific
 *  errors.
 */
void bgexec_kill_log_error (flux_future_t *f, flux_jobid_t id)
{
    flux_t *h;
    const char *name;

    if (!f)
        return;
    h = flux_future_get_flux (f);
    name = flux_future_first_child (f);
    while (name) {
        flux_future_t *cf = flux_future_get_child (f, name);
        uint32_t rank = flux_rpc_get_nodeid (cf);
        if (flux_future_is_ready (cf)
            && flux_future_get (cf, NULL) < 0
            && errno != ESRCH
            && rank != FLUX_NODEID_ANY) {
            flux_log (h,
                      LOG_ERR,
                      "%s: bgexec_kill: %s (rank %lu): %s",
                      idf58 (id),
                      flux_get_hostbyrank (h, rank),
                      (unsigned long)rank,
                      future_strerror (cf, errno));
        }
        name = flux_future_next_child (f);
    }
}

/*  Send 'signum' to 'task' by label via a "<service>.kill" RPC.
 */
static flux_future_t *task_kill (struct bgexec_task *task, int signum)
{
    struct bgexec *bg = task->bg;
    char *topic;
    flux_future_t *f;

    if (asprintf (&topic, "%s.kill", bg->service) < 0)
        return NULL;
    f = flux_rpc_pack (bg->h,
                       topic,
                       task->rank,
                       0,
                       "{s:i s:i s:s}",
                       "pid", -1,
                       "signum", signum,
                       "label", task->label);
    ERRNO_SAFE_WRAP (free, topic);
    return f;
}

flux_future_t *bgexec_kill (struct bgexec *bg,
                            const struct idset *ranks,
                            int signum)
{
    struct bgexec_task *task;
    flux_future_t *cf;

    if (!bg || signum < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(cf = flux_future_wait_all_create ()))
        return NULL;
    flux_future_set_flux (cf, bg->h);

    task = zlist_first (bg->tasks);
    while (task) {
        /* Only started-but-not-completed tasks are killable. */
        if (task->state == BGEXEC_TASK_STARTED
            && (!ranks || idset_test (ranks, task->rank))) {
            flux_future_t *f;
            char s[16];
            if ((f = task_kill (task, signum))) {
                (void) snprintf (s, sizeof (s), "%d", task->rank);
                if (flux_future_push (cf, s, f) < 0)
                    flux_future_destroy (f);
            }
        }
        task = zlist_next (bg->tasks);
    }
    /*  If no child futures were pushed, no signals were sent: return
     *  ENOENT.
     */
    if (!flux_future_first_child (cf)) {
        flux_future_destroy (cf);
        errno = ENOENT;
        return NULL;
    }
    return cf;
}

void bgexec_destroy (struct bgexec *bg)
{
    if (bg) {
        int saved_errno = errno;
        zlist_destroy (&bg->tasks);
        zlist_destroy (&bg->commands);
        idset_destroy (bg->exit_batch);
        idset_destroy (bg->pushed_ranks);
        flux_watcher_destroy (bg->prep);
        flux_watcher_destroy (bg->check);
        flux_watcher_destroy (bg->idle);
        flux_watcher_destroy (bg->exit_batch_timer);
        aux_destroy (&bg->aux);
        free (bg->name);
        free (bg->service);
        free (bg);
        errno = saved_errno;
    }
}

struct bgexec *bgexec_create (struct bgexec_ops *ops,
                              const char *service,
                              flux_jobid_t id,
                              const char *name,
                              void *arg)
{
    struct bgexec *bg;

    if (!ops || !service || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(bg = calloc (1, sizeof (*bg)))
        || !(bg->service = strdup (service))
        || !(bg->name = strdup (name)))
        goto error;
    bg->id = id;
    bg->handlers = ops;
    bg->arg = arg;
    bg->max_start_per_loop = 1;
    if (!(bg->tasks = zlist_new ())
        || !(bg->commands = zlist_new ())
        || !(bg->exit_batch = idset_create (0, IDSET_FLAG_AUTOGROW))
        || !(bg->pushed_ranks = idset_create (0, IDSET_FLAG_AUTOGROW)))
        goto error;
    return bg;
error:
    bgexec_destroy (bg);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
