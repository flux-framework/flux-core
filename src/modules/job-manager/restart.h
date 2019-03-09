/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_RESTART_H
#define _FLUX_JOB_MANAGER_RESTART_H

#include <flux/core.h>

#include "event.h"
#include "job.h"
#include "queue.h"

/* exposed for unit testing only */
int restart_count_char (const char *s, char c);

int restart_from_kvs (flux_t *h,
                      struct queue *queue,
                      struct event_ctx *event_ctx);

#endif /* _FLUX_JOB_MANAGER_RESTART_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

