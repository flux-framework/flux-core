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

struct shell_output {
    flux_shell_t *shell;
    int refcount;
    struct output_config *conf;
    struct output_client *client;
    struct kvs_output *kvs;
    struct idset *active_shells;
    struct filehash *files;
    struct task_output_list *task_outputs;
    struct file_entry *stdout_fp;
    struct file_entry *stderr_fp;
};

#endif /* !SHELL_OUTPUT_H */
