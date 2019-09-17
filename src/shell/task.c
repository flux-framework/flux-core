/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Set up task and execute with completion callback
 *
 * Command and arguments
 *    From jobspec (required).
 *
 * Environment
 *    If set in jobspec, use that, inherit the shell's.
 *    In addition, set the following runtime variables:
 *    . FLUX_TASK_LOCAL_ID
 *    . FLUX_TASK_RANK
 *    . FLUX_JOB_SIZE
 *    . FLUX_JOB_NNODES
 *    . FLUX_JOB_ID
 *    . FLUX_URI (if not running standalone)
 *
 * Current working directory
 *    Ignore - shell should already be in it.
 *
 * Upon task completion, set task->rc and call shell_task_completion_f
 * supplied to shell task start.
 *
 * Each running task adds reactor handlers that are removed on
 * completion.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdlib.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "task.h"
#include "internal.h"
#include "info.h"

struct channel_watcher {
    flux_shell_task_io_f cb;
    void *arg;
};

/*  free() wrapper for zhashx_destructor_fn
 */
static void zh_free (void **x)
{
    if (x && *x) {
        free (*x);
        *x = NULL;
    }
}

void shell_task_destroy (struct shell_task *task)
{
    if (task) {
        int saved_errno = errno;
        flux_cmd_destroy (task->cmd);
        flux_subprocess_destroy (task->proc);
        zhashx_destroy (&task->subscribers);
        free (task);
        errno = saved_errno;
    }
}

struct shell_task *shell_task_new (void)
{
    struct shell_task *task = calloc (1, sizeof (*task));
    if (!task)
        goto error;
    if (!(task->subscribers = zhashx_new ()))
        goto error;
    zhashx_set_destructor (task->subscribers, zh_free);
    return (task);
error:
    shell_task_destroy (task);
    return NULL;
}

struct shell_task *shell_task_create (flux_shell_t *shell, int index)
{
    struct shell_info *info = shell->info;
    struct shell_task *task;
    const char *key;
    json_t *entry;
    size_t i;

    if (!(task = shell_task_new ()))
        return NULL;

    task->index = index;
    task->rank = info->rankinfo.global_basis + index;
    task->size = info->jobspec->task_count;
    if (!(task->cmd = flux_cmd_create (0,
                                       NULL,
                                       info->jobspec->environment ? NULL
                                                                  : environ)))
        goto error;
    json_array_foreach (info->jobspec->command, i, entry) {
        if (flux_cmd_argv_append (task->cmd, json_string_value (entry)) < 0)
            goto error;
    }
    if (info->jobspec->environment) {
        json_object_foreach (info->jobspec->environment, key, entry) {
            if (flux_cmd_setenvf (task->cmd,
                                  1,
                                  key,
                                  "%s",
                                  json_string_value (entry)) < 0)
                goto error;
        }
    }
    if (flux_cmd_setenvf (task->cmd, 1, "FLUX_TASK_LOCAL_ID", "%d", index) < 0)
        goto error;
    if (flux_cmd_setenvf (task->cmd, 1, "FLUX_TASK_RANK", "%d", task->rank) < 0)
        goto error;
    if (flux_cmd_setenvf (task->cmd, 1, "FLUX_JOB_SIZE", "%d", task->size) < 0)
        goto error;
    if (flux_cmd_setenvf (task->cmd, 1, "FLUX_JOB_NNODES", "%d",
                          info->shell_size) < 0)
        goto error;
    if (flux_cmd_setenvf (task->cmd, 1, "FLUX_JOB_ID", "%ju",
                          (uintmax_t)info->jobid) < 0)
        goto error;
    flux_cmd_unsetenv (task->cmd, "FLUX_URI");
    if (getenv ("FLUX_URI")) {
        if (flux_cmd_setenvf (task->cmd, 1, "FLUX_URI", "%s",
                              getenv ("FLUX_URI")) < 0)
            goto error;
    }
    flux_cmd_unsetenv (task->cmd, "FLUX_KVS_NAMESPACE");
    if (getenv ("FLUX_KVS_NAMESPACE")) {
        if (flux_cmd_setenvf (task->cmd, 1, "FLUX_KVS_NAMESPACE", "%s",
                              getenv ("FLUX_KVS_NAMESPACE")) < 0)
            goto error;
    }
    return task;
error:
    shell_task_destroy (task);
    return NULL;
}

static void subproc_channel_cb (flux_subprocess_t *p, const char *stream)
{
    flux_shell_task_t *task = flux_subprocess_aux_get (p, "flux::task");
    struct channel_watcher *cw = zhashx_lookup (task->subscribers, stream);
    if (cw)
        (*cw->cb) (task, stream, cw->arg);
}

static void subproc_completion_cb (flux_subprocess_t *p)
{
    struct shell_task *task = flux_subprocess_aux_get (p, "flux::task");

    if ((task->rc = flux_subprocess_exit_code (p)) < 0) {
        if ((task->rc = flux_subprocess_signaled (p)) >= 0)
            task->rc += 128;
    }

    if (task->cb)
        task->cb (task, task->cb_arg);
}

static flux_subprocess_ops_t subproc_ops = {
    .on_completion = subproc_completion_cb,
    .on_state_change = NULL,
    .on_channel_out = subproc_channel_cb,
    .on_stdout = subproc_channel_cb,
    .on_stderr = subproc_channel_cb,
};

static void subproc_preexec_hook (flux_subprocess_t *p, void *arg)
{
    flux_shell_task_t *task = arg;
    if (task->pre_exec_cb)
        (*task->pre_exec_cb) (task, task->pre_exec_arg);
}

int shell_task_start (struct shell_task *task,
                      flux_reactor_t *r,
                      shell_task_completion_f cb,
                      void *arg)
{
    int flags = 0;
    flux_subprocess_hooks_t hooks = {
        .pre_exec = subproc_preexec_hook,
        .pre_exec_arg = task,
    };

    task->proc = flux_local_exec (r, flags, task->cmd, &subproc_ops, &hooks);
    if (!task->proc)
        return -1;
    if (flux_subprocess_aux_set (task->proc, "flux::task", task, NULL) < 0) {
        flux_subprocess_destroy (task->proc);
        task->proc = NULL;
        return -1;
    }
    task->cb = cb;
    task->cb_arg = arg;
    return 0;
}

int shell_task_kill (struct shell_task *task, int signum)
{
    int rc;
    flux_future_t *f;
    if (!task->proc) {
        errno = ENOENT;
        return -1;
    }
    if (!(f = flux_subprocess_kill (task->proc, signum)))
        return -1;

    rc = flux_future_get (f, NULL);
    flux_future_destroy (f);
    return rc;
}

/*  flux_shell_task_t API implementation:
 */

flux_cmd_t *flux_shell_task_cmd (flux_shell_task_t *task)
{
    if (!task) {
        errno = EINVAL;
        return NULL;
    }
    return task->cmd;
}

flux_subprocess_t *flux_shell_task_subprocess (flux_shell_task_t *task)
{
    if (!task) {
        errno = EINVAL;
        return NULL;
    }
    return task->proc;
}

int flux_shell_task_channel_subscribe (flux_shell_task_t *task,
                                       const char *name,
                                       flux_shell_task_io_f cb,
                                       void *arg)
{
    struct channel_watcher *cw;
    if (!task || !name || !cb) {
        errno = EINVAL;
        return -1;
    }
    cw = calloc (1, sizeof (*cw));
    if (!cw)
        return -1;
    cw->cb = cb;
    cw->arg = arg;
    if (zhashx_insert (task->subscribers, name, cw) < 0) {
        free (cw);
        errno = EEXIST;
        return -1;
    }
    return 0;
}

static int add_process_info_to_json (flux_subprocess_t *p, json_t *o)
{
    pid_t pid;
    json_t *obj = NULL;

    /*  Add "pid" to task.info object if pid is valid
     */
    if ((pid = flux_subprocess_pid (p)) > (pid_t) 0) {
        obj = json_integer (pid);
        if (!obj || json_object_set_new (o, "pid", obj) < 0)
            goto error;
    }

    /*  Add wait_status, and signaled or exitcode for exited processes.
     */
    if (flux_subprocess_state (p) == FLUX_SUBPROCESS_EXITED) {
        int status = flux_subprocess_status (p);
        int termsig;
        int exitcode;

        obj = json_integer (status);
        if (!obj || json_object_set_new (o, "wait_status", obj) < 0)
            goto error;

        /*  Set "standard" exit code to 128 + signal if signaled, with
         *   "signaled" = termsig. O/w, set signaled == 0 and exitcode
         *   to real exit code.
         */
        if (WIFSIGNALED (status)) {
            termsig = WTERMSIG (status);
            exitcode = 128 + termsig;
        }
        else {
            termsig = 0;
            exitcode = WEXITSTATUS (status);
        }

        obj = json_integer (termsig);
        if (!obj || json_object_set_new (o, "signaled", obj) < 0)
            goto error;
        obj = json_integer (exitcode);
        if (!obj || json_object_set_new (o, "exitcode", obj) < 0)
            goto error;
    }
    return 0;
error:
    json_decref (obj);
    return -1;
}

static const char * task_state (flux_shell_task_t *task)
{
    if (task->proc) {
        flux_subprocess_state_t state = flux_subprocess_state (task->proc);
        return flux_subprocess_state_string (state);
    }
    else if (task->in_pre_exec)
        return "Exec";
    else
        return "Init";
}

int flux_shell_task_get_info (flux_shell_task_t *task, char **json_str)
{
    json_error_t err;
    json_t *o = NULL;
    if (!task || !json_str) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = json_pack_ex (&err, 0, "{ s:i s:i s:s }",
                                     "localid", task->index,
                                     "rank", task->rank,
                                     "state", task_state (task))))
        return -1;
    if (task->proc && add_process_info_to_json (task->proc, o) < 0) {
        json_decref (o);
        return -1;
    }
    *json_str = json_dumps (o, JSON_COMPACT);
    json_decref (o);
    return (*json_str ? 0 : -1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
