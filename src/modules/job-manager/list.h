/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_LIST_H
#define _FLUX_JOB_MANAGER_LIST_H

#include <jansson.h>
#include "queue.h"

/* Handle a 'list' request - to list the queue.
 */
void list_handle_request (flux_t *h, struct queue *queue,
                          const flux_msg_t *msg);

/* exposed for unit testing only */
json_t *list_one_job (struct job *job, json_t *attrs);
json_t *list_job_array (struct queue *queue, int max_entries, json_t *attrs);

#endif /* ! _FLUX_JOB_MANAGER_LIST_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
