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

/* Handle a 'list' request - to list the queue.
 */
void list_handle_request (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg);

/* exposed for unit testing only */
int list_append_job (json_t *jobs, struct job *job);

#endif /* ! _FLUX_JOB_MANAGER_LIST_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
