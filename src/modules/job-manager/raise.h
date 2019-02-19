/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_RAISE_H
#define _FLUX_JOB_MANAGER_RAISE_H

#include <stdint.h>
#include "queue.h"
#include "alloc.h"

/* Hande a request to raise an exception on job.
 */
void raise_handle_request (flux_t *h, struct queue *queue,
                           struct alloc_ctx *ctx,
                           const flux_msg_t *msg);

/* exposed for unit testing only */
int raise_check_type (const char *type);
int raise_check_severity (int severity);
int raise_allow (uint32_t rolemask, uint32_t userid, uint32_t job_userid);

#endif /* ! _FLUX_JOB_MANAGER_RAISE_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
