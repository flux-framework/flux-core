/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_JOBSPEC_H
#define _SHELL_JOBSPEC_H

#include <flux/core.h>
#include <jansson.h>

struct jobspec {
    json_t *jobspec;
    int version;                // jobspec version
    int task_count;             // number of tasks in job
    int slot_count;             // number of task slots
    int cores_per_slot;         // number of cores per task slot
    int slots_per_node;         // number of slots per node (-1=unspecified)
    int node_count;             // number of nodes (-1=unspecified)
    json_t *command;
    const char *cwd;
    json_t *resources;
    json_t *tasks;
    json_t *environment;
    json_t *options;            // attributes.system.shell.options, if any
};

int jobspec_parse (struct jobspec *job, json_error_t *error);
void set_error (json_error_t *error, const char *fmt, ...);
void jobspec_destroy (struct jobspec *job);

#endif /* !_SHELL_JOBSPEC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
