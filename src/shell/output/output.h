/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef SHELL_OUTPUT_H
#define SHELL_OUTPUT_H

#include <flux/shell.h>

#include "output/conf.h"
#include "output/filehash.h"
#include "output/client.h"
#include "output/kvs.h"
#include "output/task.h"
#include "output/service.h"

struct shell_output {
    flux_shell_t *shell;
    int refcount;
    struct output_config *conf;
    struct output_client *client;
    struct output_service *service;
    struct kvs_output *kvs;
    struct filehash *files;
    struct task_output_list *task_outputs;
    struct file_entry *stdout_fp;
    struct file_entry *stderr_fp;
};

int shell_output_write_entry (struct shell_output *out,
                              const char *type,
                              json_t *context);

/* Increment/decrement shell output "open" count. Once refcount goes to
 * zero, shell output destinations will be flushed and closed.
 */
void shell_output_incref (struct shell_output *out);
void shell_output_decref (struct shell_output *out);

#endif /* !SHELL_OUTPUT_H */
