/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_WAIT_H
#define _FLUX_JOB_MANAGER_WAIT_H

#include <stdbool.h>
#include <flux/core.h>

#include "job-manager.h"

void wait_notify_inactive (struct waitjob *wait, struct job *job);
void wait_notify_inactive_remove (struct waitjob *wait, struct job *job);
void wait_notify_active (struct waitjob *wait, struct job *job);

struct waitjob *wait_ctx_create (struct job_manager *ctx);
void wait_ctx_destroy (struct waitjob *wait);

void wait_disconnect_rpc (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg);

#endif /* ! _FLUX_JOB_MANAGER_WAIT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
