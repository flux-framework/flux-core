/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_START_H
#define _FLUX_JOB_MANAGER_START_H

#include <flux/core.h>

#include "job-manager.h"

struct start *start_ctx_create (struct job_manager *ctx);
void start_ctx_destroy (struct start *start);

int start_send_request (struct start *start, struct job *job);

int start_send_expiration_update (struct start *start,
                                  struct job *job,
                                  json_t *context);

void start_disconnect_rpc (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg);

#endif /* ! _FLUX_JOB_MANAGER_START_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
