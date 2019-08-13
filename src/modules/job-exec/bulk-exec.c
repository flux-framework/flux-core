/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#define EXIT_CODE(x) __W_EXITCODE(x,0)

#include <flux/core.h>
#include <flux/idset.h>
#include <czmq.h>

#include "bulk-exec.h"

struct exec_cmd {
    struct idset *ranks;
    flux_cmd_t *cmd;
    int flags;
};

struct bulk_exec {
    flux_t *h;

    int max_start_per_loop;  /* Max subprocess started per event loop cb */
    int total;               /* Total processes expected to run */
    int started;             /* Number of processes that have reached start */
    int complete;            /* Number of processes that have completed */

    int exit_status;         /* Largest wait status of all complete procs */

    unsigned int active:1;

    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;

    struct idset *exit_batch;         /* Support for batched exit notify */
    flux_watcher_t *exit_batch_timer; /* Timer for batched exit notify */

    flux_subprocess_ops_t ops;

    zlist_t *commands;
    zlist_t *processes;

    struct bulk_exec_ops *handlers;
    void *arg;
};

int bulk_exec_rc (struct bulk_exec *exec)
{
    return (exec->exit_status);
}

static void exec_state_cb (flux_subprocess_t *p, flux_subprocess_state_t state)
{
    struct bulk_exec *exec = flux_subprocess_aux_get (p, "job-exec::exec");
    if (state == FLUX_SUBPROCESS_RUNNING) {
        if (++exec->started == exec->total) {
            if (exec->handlers->on_start)
                (*exec->handlers->on_start) (exec, exec->arg);
        }
    }
    else if (state == FLUX_SUBPROCESS_FAILED
            || state == FLUX_SUBPROCESS_EXEC_FAILED) {
        int errnum = flux_subprocess_fail_errno (p);
        int code = EXIT_CODE(1);

        if (errnum == EPERM || errnum == EACCES)
            code = EXIT_CODE(126);
        else if (errnum == ENOENT)
            code = EXIT_CODE(127);
        else if (errnum == EHOSTUNREACH)
            code = EXIT_CODE(68);

        if (code > exec->exit_status)
            exec->exit_status = code;

        if (exec->handlers->on_error)
            (*exec->handlers->on_error) (exec, p, exec->arg);
    }
}

static int exec_exit_notify (struct bulk_exec *exec)
{
    if (exec->handlers->on_exit)
        (*exec->handlers->on_exit) (exec, exec->arg, exec->exit_batch);
    if (exec->exit_batch_timer) {
        flux_watcher_destroy (exec->exit_batch_timer);
        exec->exit_batch_timer = NULL;
        idset_range_clear (exec->exit_batch, 0, INT_MAX);
    }
    return 0;
}

static void exit_batch_cb (flux_reactor_t *r, flux_watcher_t *w,
                          int revents, void *arg)
{
    struct bulk_exec *exec = arg;
    exec_exit_notify (exec);
}

/*  Append completed subprocess 'p' to the current batch for exit
 *   notification. If this is the first exited process in the batch,
 *   then start a timer which will fire and call the function to
 *   notify bulk_exec user of the batch of subprocess exits.
 *
 *  This appraoch avoids unecessarily calling into user's callback
 *   multiple times when all tasks exit within 0.01s.
 */
static void exit_batch_append (struct bulk_exec *exec, flux_subprocess_t *p)
{
    int rank = flux_subprocess_rank (p);
    if (idset_set (exec->exit_batch, rank) < 0) {
        flux_log_error (exec->h, "exit_batch_append:idset_set");
        return;
    }
    if (!exec->exit_batch_timer) {
        flux_reactor_t *r = flux_get_reactor (exec->h);
        /*  XXX: batch timer should eventually be configurable by caller */
        exec->exit_batch_timer =
            flux_timer_watcher_create (r, 0.01, 0.,
                                       exit_batch_cb,
                                       exec);
        if (!exec->exit_batch_timer) {
            flux_log_error (exec->h, "exit_batch_append:timer create");
            return;
        }
        flux_watcher_start (exec->exit_batch_timer);
    }
}

static void exec_complete_cb (flux_subprocess_t *p)
{
    int status = flux_subprocess_status (p);
    struct bulk_exec *exec = flux_subprocess_aux_get (p, "job-exec::exec");

    if (status > exec->exit_status)
        exec->exit_status = status;

    /* Append this process to the current batch for notification */
    exit_batch_append (exec, p);

    if (++exec->complete == exec->total) {
        exec_exit_notify (exec);
        if (exec->handlers->on_complete)
            (*exec->handlers->on_complete) (exec, exec->arg);
    }
}

static void exec_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct bulk_exec *exec = flux_subprocess_aux_get (p, "job-exec::exec");
    const char *s;
    int len;

    if (!(s = flux_subprocess_getline (p, stream, &len))) {
        flux_log_error (exec->h, "flux_subprocess_getline");
        return;
    }
    if (len) {
        int rank = flux_subprocess_rank (p);
        if (exec->handlers->on_output)
            (*exec->handlers->on_output) (exec, p, stream, s, len, exec->arg);
        else
            flux_log (exec->h, LOG_INFO, "rank %d: %s: %s", rank, stream, s);
    }
}

static void exec_cmd_destroy (void *arg)
{
    struct exec_cmd *cmd = arg;
    idset_destroy (cmd->ranks);
    flux_cmd_destroy (cmd->cmd);
    free (cmd);
}

static struct exec_cmd *exec_cmd_create (const struct idset *ranks,
                                         flux_cmd_t *cmd,
                                         int flags)
{
    struct exec_cmd *c = calloc (1, sizeof (*c));
    if (!c)
        return NULL;
    if (!(c->ranks = idset_copy (ranks)))
        goto err;
    if (!(c->cmd = flux_cmd_copy (cmd)))
        goto err;
    c->flags = flags;
    return (c);
err:
    exec_cmd_destroy (c);
    return NULL;
}

static void subprocess_destroy_finish (flux_future_t *f, void *arg)
{
    flux_subprocess_t *p = arg;
    if (flux_future_get (f, NULL) < 0) {
        flux_t *h = flux_subprocess_aux_get (p, "flux_t");
        flux_log_error (h, "subprocess_kill: %ju: %s",
                        (uintmax_t) flux_subprocess_pid,
                        flux_strerror (errno));
    }
    flux_subprocess_destroy (p);
    flux_future_destroy (f);
}

static int subprocess_destroy (flux_t *h, flux_subprocess_t *p)
{
    flux_future_t *f = flux_subprocess_kill (p, SIGKILL);
    if (!f || flux_future_then (f, -1., subprocess_destroy_finish, p) < 0)
        return -1;
    return 0;
}

static int exec_start_cmd (struct bulk_exec *exec,
                           struct exec_cmd *cmd,
                           int max)
{
    int count = 0;
    uint32_t rank;
    rank = idset_first (cmd->ranks);
    while (rank != IDSET_INVALID_ID && (max < 0 || count < max)) {
        flux_subprocess_t *p = flux_rexec (exec->h,
                                           rank,
                                           cmd->flags,
                                           cmd->cmd,
                                           &exec->ops);
        if (!p)
            return -1;
        if (flux_subprocess_aux_set (p, "job-exec::exec", exec, NULL) < 0
           || zlist_append (exec->processes, p) < 0) {
            if (subprocess_destroy (exec->h, p) < 0)
                flux_log_error (exec->h, "Unable to destroy pid %ju",
                        (uintmax_t) flux_subprocess_pid (p));
            return -1;
        }
        zlist_freefn (exec->processes, p,
                     (zlist_free_fn *) flux_subprocess_unref,
                     true);

        idset_clear (cmd->ranks, rank);
        rank = idset_next (cmd->ranks, rank);
        count++;
    }
    return count;
}

void bulk_exec_stop (struct bulk_exec *exec)
{
    flux_watcher_stop (exec->prep);
    flux_watcher_stop (exec->check);
}

static int exec_start_cmds (struct bulk_exec *exec, int max)
{
    while (zlist_size (exec->commands) && (max != 0)) {
        struct exec_cmd *cmd = zlist_first (exec->commands);
        int rc = exec_start_cmd (exec, cmd, max);
        if (rc < 0) {
            flux_log_error (exec->h, "exec_start_cmd failed");
            return -1;
        }
        if (idset_count (cmd->ranks) == 0)
            zlist_remove (exec->commands, cmd);
        if (max > 0)
            max -= rc;

    }
    return 0;
}

static void prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    struct bulk_exec *exec = arg;

    /* Don't block in reactor if there are commands to run */
    if (zlist_size (exec->commands) > 0) {
        flux_watcher_start (exec->idle);
        flux_watcher_start (exec->check);
    }
    else
        bulk_exec_stop (exec);
}

static void check_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    struct bulk_exec *exec = arg;
    flux_watcher_stop (exec->idle);
    flux_watcher_stop (exec->check);
    if (exec_start_cmds (exec, exec->max_start_per_loop) < 0) {
        bulk_exec_stop (exec);
        if (exec->handlers->on_error)
            (*exec->handlers->on_error) (exec, NULL, exec->arg);
    }
}

void bulk_exec_destroy (struct bulk_exec *exec)
{
    zlist_destroy (&exec->processes);
    zlist_destroy (&exec->commands);
    idset_destroy (exec->exit_batch);
    flux_watcher_destroy (exec->prep);
    flux_watcher_destroy (exec->check);
    flux_watcher_destroy (exec->idle);
    free (exec);
}

struct bulk_exec * bulk_exec_create (struct bulk_exec_ops *ops, void *arg)
{
    flux_subprocess_ops_t sp_ops = {
        .on_completion =   exec_complete_cb,
        .on_state_change = exec_state_cb,
        .on_stdout =       exec_output_cb,
        .on_stderr =       exec_output_cb,
    };
    struct bulk_exec *exec = calloc (1, sizeof (*exec));
    if (!exec)
        return NULL;
    exec->ops = sp_ops;
    exec->handlers = ops;
    exec->arg = arg;
    exec->processes = zlist_new ();
    exec->commands = zlist_new ();
    exec->exit_batch = idset_create (0, IDSET_FLAG_AUTOGROW);
    exec->max_start_per_loop = 1;

    return exec;
}

int bulk_exec_set_max_per_loop (struct bulk_exec *exec, int max)
{
    if (max == 0) {
        errno = EINVAL;
        return -1;
    }
    exec->max_start_per_loop = max;
    return 0;
}

int bulk_exec_push_cmd (struct bulk_exec *exec,
                       const struct idset *ranks,
                       flux_cmd_t *cmd,
                       int flags)
{
    struct exec_cmd *c = exec_cmd_create (ranks, cmd, flags);
    if (!c)
        return -1;

    if (zlist_append (exec->commands, c) < 0) {
        exec_cmd_destroy (c);
        return -1;
    }
    zlist_freefn (exec->commands, c, exec_cmd_destroy, true);

    exec->total += idset_count (ranks);
    if (exec->active) {
        flux_watcher_start (exec->prep);
        flux_watcher_start (exec->check);
    }

    return 0;
}

int bulk_exec_start (flux_t *h, struct bulk_exec *exec)
{
    flux_reactor_t *r = flux_get_reactor (h);
    exec->h = h;
    exec->prep = flux_prepare_watcher_create (r, prep_cb, exec);
    exec->check = flux_check_watcher_create (r, check_cb, exec);
    exec->idle = flux_idle_watcher_create (r, NULL, NULL);
    if (!exec->prep || !exec->check || !exec->idle)
        return -1;
    flux_watcher_start (exec->prep);
    exec->active = 1;
    return 0;
}

/*  Cancel all pending commands.
 */
int bulk_exec_cancel (struct bulk_exec *exec)
{
    struct exec_cmd *cmd = zlist_first (exec->commands);
    if (!cmd)
        return 0;

    while (cmd) {
        uint32_t rank = idset_first (cmd->ranks);
        while (rank != IDSET_INVALID_ID) {
            exec->complete++;
            if (idset_set (exec->exit_batch, rank) < 0)
                flux_log_error (exec->h, "bulk_exec_cance: idset_set");
            rank = idset_next (cmd->ranks, rank);
        }
        cmd = zlist_next (exec->commands);
    }
    zlist_purge (exec->commands);
    exec_exit_notify (exec);

    if (exec->complete == exec->total) {
        if (exec->handlers->on_complete)
            (*exec->handlers->on_complete) (exec, exec->arg);
    }
    return 0;
}

flux_future_t *bulk_exec_kill (struct bulk_exec *exec, int signum)
{
    flux_subprocess_t *p = zlist_first (exec->processes);
    flux_future_t *cf = NULL;

    if (!(cf = flux_future_wait_all_create ()))
        return NULL;
    flux_future_set_flux (cf, exec->h);

    if (!p) {
        flux_future_fulfill_error (cf, ENOENT, NULL);
        return (cf);
    }
    while (p) {
        if (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING) {
            flux_future_t *f = NULL;
            char s[64];
            if (!(f = flux_subprocess_kill (p, signum))) {
                int err = errno;
                const char *errstr = flux_strerror (errno);
                if ((f = flux_future_create (NULL, NULL)))
                    flux_future_fulfill_error (f, err, errstr);
                else
                    flux_future_fulfill_error (cf, err, "Internal error");
            }
            (void) snprintf (s, sizeof (s)-1, "%u",
                            flux_subprocess_rank (p));
            if (flux_future_push (cf, s, f) < 0) {
                fprintf (stderr, "flux_future_push: %s\n", strerror (errno));
                flux_future_destroy (f);
            }
        }
        p = zlist_next (exec->processes);
    }
    return cf;
}

/* vi: ts=4 sw=4 expandtab
 */
