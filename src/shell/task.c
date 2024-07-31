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
 *    . FLUX_URI
 *    . correct HOSTNAME if set in job environment
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
#define FLUX_SHELL_PLUGIN_NAME NULL

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdlib.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libjob/idf58.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "internal.h"
#include "task.h"
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
        aux_destroy (&task->aux);
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

struct shell_task *shell_task_create (flux_shell_t *shell,
                                      int index,
                                      int taskid)
{
    struct shell_info *info = shell->info;
    struct shell_task *task;
    const char *key;
    json_t *entry;
    size_t i;

    if (!(task = shell_task_new ()))
        return NULL;

    task->index = index;
    task->rank = taskid;
    task->size = info->total_ntasks;
    if (!(task->cmd = flux_cmd_create (0, NULL, NULL)))
        goto error;
    json_array_foreach (info->jobspec->command, i, entry) {
        if (flux_cmd_argv_append (task->cmd, json_string_value (entry)) < 0)
            goto error;
    }
    json_object_foreach (info->jobspec->environment, key, entry) {
        if (flux_cmd_setenvf (task->cmd,
                              1,
                              key,
                              "%s",
                              json_string_value (entry)) < 0)
            goto error;
    }
    if (flux_cmd_setenvf (task->cmd, 1, "FLUX_TASK_LOCAL_ID", "%d", index) < 0)
        goto error;
    if (flux_cmd_setenvf (task->cmd, 1, "FLUX_TASK_RANK", "%d", task->rank) < 0)
        goto error;
    if (flux_cmd_setenvf (task->cmd, 1, "FLUX_JOB_SIZE", "%d", task->size) < 0)
        goto error;
    if (flux_cmd_setenvf (task->cmd,
                          1,
                          "FLUX_JOB_NNODES",
                          "%d",
                          info->shell_size) < 0)
        goto error;

    if (flux_cmd_setenvf (task->cmd,
                          1,
                          "FLUX_JOB_ID",
                          "%s",
                          idf58 (info->jobid)) < 0)
        goto error;

    /* Always unset FLUX_PROXY_REMOTE since this never makes sense
     * in the environment of a job task.
     */
    flux_cmd_unsetenv (task->cmd, "FLUX_PROXY_REMOTE");
    flux_cmd_unsetenv (task->cmd, "FLUX_URI");
    if (getenv ("FLUX_URI")) {
        if (flux_cmd_setenvf (task->cmd,
                              1,
                              "FLUX_URI",
                              "%s",
                              getenv ("FLUX_URI")) < 0)
            goto error;
    }
    flux_cmd_unsetenv (task->cmd, "FLUX_KVS_NAMESPACE");
    if (getenv ("FLUX_KVS_NAMESPACE")) {
        if (flux_cmd_setenvf (task->cmd,
                              1,
                              "FLUX_KVS_NAMESPACE",
                              "%s",
                              getenv ("FLUX_KVS_NAMESPACE")) < 0)
            goto error;
    }

    /* If HOSTNAME is set in job environment it is almost certain to be
     * incorrect. Overwrite with the correct hostname.
     */
    if (flux_cmd_getenv (task->cmd, "HOSTNAME")
        && flux_cmd_setenvf (task->cmd,
                             1,
                             "HOSTNAME",
                             "%s",
                             shell->hostname) < 0)
        goto error;
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

int shell_task_start (struct flux_shell *shell,
                      struct shell_task *task,
                      shell_task_completion_f cb,
                      void *arg)
{
    int flags = 0;
    flux_reactor_t *r = shell->r;
    flux_subprocess_hooks_t hooks = {
        .pre_exec = subproc_preexec_hook,
        .pre_exec_arg = task,
    };

    if (shell->nosetpgrp)
        flags |= FLUX_SUBPROCESS_FLAGS_NO_SETPGRP;

    task->proc = flux_local_exec_ex (r,
                                     flags,
                                     task->cmd,
                                     &subproc_ops,
                                     &hooks,
                                     NULL,
                                     NULL);
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

int shell_task_running (struct shell_task *task)
{
    if (!task->proc)
        return 0;
    return (flux_subprocess_state (task->proc) == FLUX_SUBPROCESS_RUNNING);
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

static json_t *get_task_object (flux_shell_task_t *task)
{
    json_error_t err;
    json_t *o;
    char key [128];

    if (!task)
        return NULL;

    /* Save one copy of object per task "state", since the information
     *  and available fields may be different as the task state evolves.
     */
    if (snprintf (key,
                  sizeof (key),
                  "shell::task:%s",
                  task_state (task)) >= sizeof (key))
        return NULL;
    if ((o = aux_get (task->aux, key)))
        return o;

    if (!(o = json_pack_ex (&err, 0, "{ s:i s:i s:s }",
                                     "localid", task->index,
                                     "rank", task->rank,
                                     "state", task_state (task))))
        return NULL;

    if ((task->proc && add_process_info_to_json (task->proc, o) < 0)
        || aux_set (&task->aux, key, o, (flux_free_f) json_decref) < 0) {
        json_decref (o);
        return NULL;
    }
    return o;
}

int flux_shell_task_get_info (flux_shell_task_t *task, char **json_str)
{
    json_t *o = NULL;
    if (!task || !json_str) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = get_task_object (task)))
        return -1;
    *json_str = json_dumps (o, JSON_COMPACT);
    return (*json_str ? 0 : -1);
}

int flux_shell_task_info_vunpack (flux_shell_task_t *task,
                                  const char *fmt,
                                  va_list ap)
{
    json_t *o;
    json_error_t err;
    if (!task || !fmt) {
        errno = EINVAL;
        return -1;
    }
    if (!(o = get_task_object (task)))
        return -1;
    return json_vunpack_ex (o, &err, 0, fmt, ap);
}

int flux_shell_task_info_unpack (flux_shell_task_t *task,
                                 const char *fmt,
                                 ...)
{
    int rc;
    va_list ap;
    va_start (ap, fmt);
    rc = flux_shell_task_info_vunpack (task, fmt, ap);
    va_end (ap);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
