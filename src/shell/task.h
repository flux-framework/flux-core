/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_TASK_H
#define SHELL_TASK_H

#include <flux/core.h>
#include "info.h"

#include <czmq.h>

struct shell_task;

typedef void (*shell_task_completion_f)(struct shell_task *task, void *arg);
typedef void (*shell_task_pre_exec_f) (struct shell_task *task, void *arg);
typedef void (*shell_task_io_ready_f)(struct shell_task *task,
                                      const char *name,
                                      void *arg);


struct shell_task {
    int index;
    int rank;
    int size;
    flux_subprocess_t *proc;
    flux_cmd_t *cmd;
    int rc;

    /* Hash of output channel subscribers for this task */
    zhashx_t *subscribers;

    shell_task_completion_f cb;
    void *cb_arg;

    shell_task_pre_exec_f pre_exec_cb;
    void *pre_exec_arg;

    shell_task_io_ready_f io_cb;
    void *io_cb_arg;
};

void shell_task_destroy (struct shell_task *task);

struct shell_task *shell_task_create (struct shell_info *info, int index);

int shell_task_start (struct shell_task *task,
                      flux_reactor_t *r,
                      shell_task_completion_f cb,
                      void *arg);

/* Send signal `signum` to shell task */
int shell_task_kill (struct shell_task *task, int signum);

#endif /* !SHELL_TASK_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
