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

struct job_manager {
    flux_t *h;
    flux_msg_handler_t **handlers;
    struct queue *queue;
    struct start_ctx *start_ctx;
    struct alloc_ctx *alloc_ctx;
    struct event_ctx *event_ctx;
    struct submit_ctx *submit_ctx;
    struct drain *drain;
};

#endif /* !_FLUX_JOB_MANAGER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
