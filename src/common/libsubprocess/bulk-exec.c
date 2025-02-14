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
#include "src/common/libmissing/macros.h"
#define EXIT_CODE(x) __W_EXITCODE(x,0)

#include <flux/core.h>
#include <flux/idset.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/aux.h"
#include "src/common/libjob/idf58.h"
#include "ccan/str/str.h"
#include "bulk-exec.h"

struct exec_cmd {
    struct idset *ranks;
    flux_cmd_t *cmd;
    int flags;
};

struct bulk_exec {
    flux_t *h;

    char *service;
    flux_jobid_t id;
    char *name;

    struct aux_item *aux;

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

extern char **environ;

int bulk_exec_rc (struct bulk_exec *exec)
{
    if (!exec) {
        errno = EINVAL;
        return -1;
    }
    return exec->exit_status;
}

int bulk_exec_started_count (struct bulk_exec *exec)
{
    if (!exec || !exec->processes)
        return 0;
    return zlist_size (exec->processes);
}

int bulk_exec_total (struct bulk_exec *exec)
{
    if (!exec || !exec->processes)
        return 0;
    return exec->total;
}

int bulk_exec_complete (struct bulk_exec *exec)
{
    if (!exec || !exec->processes)
        return 0;
    return exec->complete;
}

int bulk_exec_active_count (struct bulk_exec *exec)
{
    if (!exec || !exec->processes)
        return 0;
    return exec->total - exec->complete;
}

struct idset *bulk_exec_active_ranks (struct bulk_exec *exec)
{
    flux_subprocess_t *p;
    struct idset *ranks;

    if (!exec || !exec->processes) {
        errno = EINVAL;
        return NULL;
    }

    if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;

    p = zlist_first (exec->processes);
    while (p) {
        if (flux_subprocess_active (p)) {
            int rank = flux_subprocess_rank (p);
            if (rank >= 0 && idset_set (ranks, rank) < 0) {
                goto error;
            }
        }
        p = zlist_next (exec->processes);
    }
    return ranks;
error:
    idset_destroy (ranks);
    return NULL;
}

int bulk_exec_write (struct bulk_exec *exec,
                     const char *stream,
                     const char *buf,
                     size_t len)
{
    flux_subprocess_t *p;

    if (!exec || !stream || !buf || len <= 0) {
        errno = EINVAL;
        return -1;
    }

    p = zlist_first (exec->processes);
    while (p) {
        if (flux_subprocess_write (p, stream, buf, len) < len)
            return -1;
        p = zlist_next (exec->processes);
    }
    return 0;
}

int bulk_exec_close (struct bulk_exec *exec, const char *stream)
{
    flux_subprocess_t *p;

    if (!exec || !stream) {
        errno = EINVAL;
        return -1;
    }

    p = zlist_first (exec->processes);
    while (p) {
        if (flux_subprocess_close (p, stream) < 0)
            return -1;
        p = zlist_next (exec->processes);
    }
    return 0;
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

static void exit_batch_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct bulk_exec *exec = arg;
    exec_exit_notify (exec);
}

/*  Append completed subprocess 'p' to the current batch for exit
 *   notification. If this is the first exited process in the batch,
 *   then start a timer which will fire and call the function to
 *   notify bulk_exec user of the batch of subprocess exits.
 *
 *  This approach avoids unnecessarily calling into user's callback
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

static void exec_add_completed (struct bulk_exec *exec, flux_subprocess_t *p)
{
    /* Append this process to the current batch for notification */
    exit_batch_append (exec, p);

    if (++exec->complete == exec->total) {
        exec_exit_notify (exec);
        if (exec->handlers->on_complete)
            (*exec->handlers->on_complete) (exec, exec->arg);
    }
}

static void exec_complete_cb (flux_subprocess_t *p)
{
    int status = flux_subprocess_status (p);
    struct bulk_exec *exec = flux_subprocess_aux_get (p, "job-exec::exec");

    if (status > exec->exit_status)
        exec->exit_status = status;

    exec_add_completed (exec, p);
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
    else if (state == FLUX_SUBPROCESS_FAILED) {
        int errnum = flux_subprocess_fail_errno (p);
        int code = EXIT_CODE(1);

        if (errnum == EPERM || errnum == EACCES)
            code = EXIT_CODE(126);
        else if (errnum == ENOENT)
            code = EXIT_CODE(127);
        else if (errnum == EHOSTUNREACH) {
            /*  Do not set a "failure" exit code for a lost job shell.
             *  This is because if the child job is an instance of Flux
             *  that wants to continue running after losing a broker, then
             *  we don't want to force a nonzero instance exit code which
             *  would make the job appear to have failed. If the instance
             *  does exit due to a node failure, then a nonzero exit code
             *  will be set later anyway by the resultant job exception.
             */
            code = 0;
        }

        if (code > exec->exit_status)
            exec->exit_status = code;

        if (exec->handlers->on_error)
            (*exec->handlers->on_error) (exec, p, exec->arg);

        exec_add_completed (exec, p);
    }
}

static void exec_output_cb (flux_subprocess_t *p, const char *stream)
{
    struct bulk_exec *exec = flux_subprocess_aux_get (p, "job-exec::exec");
    const char *s;
    int len;

    if ((len = flux_subprocess_read (p, stream, &s)) < 0) {
        flux_log_error (exec->h, "flux_subprocess_read");
        return;
    }
    if (len) {
        int rank = flux_subprocess_rank (p);
        if (exec->handlers->on_output)
            (*exec->handlers->on_output) (exec, p, stream, s, len, exec->arg);
        else {
            flux_log (exec->h,
                      LOG_INFO,
                      "rank %d: %s: %.*s",
                      rank,
                      stream,
                      len,
                      s);
        }
    }
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
                                         flux_cmd_t *cmd,
                                         int flags)
{
    struct exec_cmd *c = calloc (1, sizeof (*c));
    if (!c)
        return NULL;
    if (!(c->ranks = idset_copy (ranks))) {
        fprintf (stderr, "exec_cmd_create: idset_copy failed");
        goto err;
    }
    if (!(c->cmd = flux_cmd_copy (cmd))) {
        fprintf (stderr, "exec_cmd_create: flux_cmd_copy failed");
        goto err;
    }
    /* bulk-exec always uses unbuffered reads for performance */
    c->flags = flags | FLUX_SUBPROCESS_FLAGS_LOCAL_UNBUF;
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
        flux_log_error (h,
                        "subprocess_kill: %ju: %s",
                        (uintmax_t) flux_subprocess_pid (p),
                        future_strerror (f, errno));
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
        /* Set the unit name for the "sdexec" service.  This is done here
         * for each rank instead of once in bulk_exec_push_cmd() to ensure
         * the name is unique when there are multiple brokers per node.
         * Ex: shell-0-fTE9HHdZvi3.service.
         * (N.B. systemd doesn't like "ƒ" in the unit name hence f58plain).
         */
        if (streq (exec->service, "sdexec")) {
            char idbuf[21];
            char name[128];
            if (flux_job_id_encode (exec->id,
                                    "f58plain",
                                    idbuf,
                                    sizeof (idbuf)) < 0)
                return -1;
            snprintf (name,
                      sizeof (name),
                      "%s-%lu-%s.service",
                      exec->name,
                      (unsigned long)rank,
                      idbuf);
            if (flux_cmd_setopt (cmd->cmd, "SDEXEC_NAME", name) < 0
                || flux_cmd_setopt (cmd->cmd,
                                    "SDEXEC_PROP_Description",
                                    "User workload") < 0) {
                flux_log_error (exec->h, "Unable to set sdexec options");
                return -1;
            }
        }
        flux_subprocess_t *p = flux_rexec_ex (exec->h,
                                              bulk_exec_service_name (exec),
                                              rank,
                                              cmd->flags,
                                              cmd->cmd,
                                              &exec->ops,
                                              flux_llog,
                                              exec->h);
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
                     (zlist_free_fn *) flux_subprocess_destroy,
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


static void prep_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
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

static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
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
    if (exec) {
        int saved_errno = errno;
        zlist_destroy (&exec->processes);
        zlist_destroy (&exec->commands);
        idset_destroy (exec->exit_batch);
        flux_watcher_destroy (exec->prep);
        flux_watcher_destroy (exec->check);
        flux_watcher_destroy (exec->idle);
        flux_watcher_destroy (exec->exit_batch_timer);
        aux_destroy (&exec->aux);
        free (exec->name);
        free (exec->service);
        free (exec);
        errno = saved_errno;
    }
}

struct bulk_exec * bulk_exec_create (struct bulk_exec_ops *ops,
                                     const char *service,
                                     flux_jobid_t id,
                                     const char *name,
                                     void *arg)
{
    flux_subprocess_ops_t sp_ops = {
        .on_completion =   exec_complete_cb,
        .on_state_change = exec_state_cb,
        .on_channel_out =  exec_output_cb,
        .on_stdout =       exec_output_cb,
        .on_stderr =       exec_output_cb,
    };
    struct bulk_exec *exec = calloc (1, sizeof (*exec));
    if (!exec
        || !(exec->service = strdup (service))
        || !(exec->name = strdup (name)))
        goto error;
    exec->id = id;
    exec->ops = sp_ops;
    exec->handlers = ops;
    exec->arg = arg;
    exec->processes = zlist_new ();
    exec->commands = zlist_new ();
    exec->exit_batch = idset_create (0, IDSET_FLAG_AUTOGROW);
    exec->max_start_per_loop = 1;

    return exec;
error:
    bulk_exec_destroy (exec);
    return NULL;
}

int bulk_exec_set_max_per_loop (struct bulk_exec *exec, int max)
{
    if (!exec || max == 0) {
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
    struct exec_cmd *c;

    if (!exec || !ranks || !cmd) {
        errno = EINVAL;
        return -1;
    }

    if (!(c = exec_cmd_create (ranks, cmd, flags)))
        return -1;

    if (zlist_append (exec->commands, c) < 0) {
        exec_cmd_destroy (c);
        errno = ENOMEM;
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
    flux_reactor_t *r;

    if (!h || !exec) {
        errno = EINVAL;
        return -1;
    }

    r = flux_get_reactor (h);
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
    struct exec_cmd *cmd;

    if (!exec) {
        errno = EINVAL;
        return -1;
    }

    if (!(cmd = zlist_first (exec->commands)))
        return 0;

    while (cmd) {
        uint32_t rank = idset_first (cmd->ranks);
        while (rank != IDSET_INVALID_ID) {
            exec->complete++;
            if (idset_set (exec->exit_batch, rank) < 0)
                flux_log_error (exec->h, "bulk_exec_cancel: idset_set");
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

/*  Loop through all child futures and print rank-specific errors
 */
void bulk_exec_kill_log_error (flux_future_t *f, flux_jobid_t id)
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
                      "%s: exec_kill: %s (rank %lu): %s",
                      idf58 (id),
                      flux_get_hostbyrank (h, rank),
                      (unsigned long)rank,
                      future_strerror (cf, errno));
        }
        name = flux_future_next_child (f);
    }
}

flux_future_t *bulk_exec_kill (struct bulk_exec *exec,
                               const struct idset *ranks,
                               int signum)
{
    flux_subprocess_t *p;
    flux_future_t *cf;

    if (!exec || signum < 0) {
        errno = EINVAL;
        return NULL;
    }

    if (!(cf = flux_future_wait_all_create ()))
        return NULL;
    flux_future_set_flux (cf, exec->h);

    p = zlist_first (exec->processes);
    while (p) {
        if ((!ranks || idset_test (ranks, flux_subprocess_rank (p)))
            && (flux_subprocess_state (p) == FLUX_SUBPROCESS_RUNNING
            || flux_subprocess_state (p) == FLUX_SUBPROCESS_INIT)) {
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
            (void) snprintf (s,
                             sizeof (s)-1,
                             "%u",
                             flux_subprocess_rank (p));
            if (flux_future_push (cf, s, f) < 0) {
                fprintf (stderr, "flux_future_push: %s\n", strerror (errno));
                flux_future_destroy (f);
            }
        }
        p = zlist_next (exec->processes);
    }

    /*  If no child futures were pushed into the wait_all future `cf`,
     *   then no signals were sent and we should immediately return ENOENT.
     */
    if (!flux_future_first_child (cf)) {
        flux_future_destroy (cf);
        errno = ENOENT;
        return NULL;
    }

    return cf;
}

int bulk_exec_aux_set (struct bulk_exec *exec,
                       const char *key,
                       void *val,
                       flux_free_f free_fn)
{
    if (!exec) {
        errno = EINVAL;
        return -1;
    }
    return (aux_set (&exec->aux, key, val, free_fn));
}

void * bulk_exec_aux_get (struct bulk_exec *exec, const char *key)
{
    if (!exec) {
        errno = EINVAL;
        return NULL;
    }
    return (aux_get (exec->aux, key));
}

const char *bulk_exec_service_name (struct bulk_exec *exec)
{
    if (!exec)
        return NULL;
    return exec->service;
}

flux_subprocess_t *bulk_exec_get_subprocess (struct bulk_exec *exec, int rank)
{
    flux_subprocess_t *p;

    if (!exec || rank < 0) {
        errno = EINVAL;
        return NULL;
    }

    p = zlist_first (exec->processes);
    while (p) {
        if (flux_subprocess_rank (p) == rank)
            return p;
        p = zlist_next (exec->processes);
    }
    errno = ENOENT;
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
