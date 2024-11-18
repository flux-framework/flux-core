/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_H
#define _FLUX_JOB_MANAGER_H

#include "src/common/libczmqcontainers/czmq_containers.h"

struct job_manager {
    flux_t *h;
    flux_msg_handler_t **handlers;
    zhashx_t *active_jobs;
    zhashx_t *inactive_jobs;
    int running_jobs; // count of jobs in RUN | CLEANUP state
    flux_jobid_t max_jobid; // largest jobid allocated thus far
    uid_t owner;
    struct conf *conf;
    struct start *start;
    struct alloc *alloc;
    struct housekeeping *housekeeping;
    struct event *event;
    struct submit *submit;
    struct drain *drain;
    struct waitjob *wait;
    struct raise *raise;
    struct kill *kill;
    struct annotate *annotate;
    struct journal *journal;
    struct purge *purge;
    struct queue_ctx *queue;
    struct update *update;
    struct jobtap *jobtap;
};

#endif /* !_FLUX_JOB_MANAGER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
