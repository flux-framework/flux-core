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

#include <czmq.h>

struct job_manager {
    flux_t *h;
    flux_msg_handler_t **handlers;
    zhashx_t *active_jobs;
    struct start *start;
    struct alloc *alloc;
    struct event *event;
    struct submit *submit;
    struct drain *drain;
    struct waitjob *wait;
    struct simulator *simulator;
};

#endif /* !_FLUX_JOB_MANAGER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
