/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_PRIORITY_H
#define _FLUX_JOB_MANAGER_PRIORITY_H

#include "queue.h"
#include "event.h"

/* Handle a 'priority' request - job priority adjustment
 */
void priority_handle_request (flux_t *h, struct queue *queue,
                              struct event_ctx *event_ctx,
                              const flux_msg_t *msg);


#endif /* ! _FLUX_JOB_MANAGER_PRIORITY_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
