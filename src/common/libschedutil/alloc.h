/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_SCHEDUTIL_ALLOC_H
#define _FLUX_SCHEDUTIL_ALLOC_H

#include <stdint.h>
#include <flux/core.h>

#include "init.h"

enum {
    FLUX_SCHED_ALLOC_SUCCESS = 0,
    FLUX_SCHED_ALLOC_NOTE    = 1,
    FLUX_SCHED_ALLOC_DENIED  = 2,
    FLUX_SCHED_ALLOC_CANCEL  = 3,
};

/* Decode an alloc request message.
 * Return 0 on success, -1 on error with errno set.
 */
int schedutil_alloc_request_decode (const flux_msg_t *msg,
                                    flux_jobid_t *id,
                                    int *priority,
                                    uint32_t *userid,
                                    double *t_submit);

/* Respond to alloc request message - update annotation.
 * A job's annotation may be updated any number of times before alloc request
 * is finally terminated with alloc_respond_denied() or alloc_respond_R().
 * Return 0 on success, -1 on error with errno set.
 */
int schedutil_alloc_respond_note (schedutil_t *util, const flux_msg_t *msg,
                                  const char *note);

/* Respond to alloc request message - the job cannot run.
 * Include human readable error message in 'note'.
 * Return 0 on success, -1 on error with errno set.
 */
int schedutil_alloc_respond_denied (schedutil_t *util, const flux_msg_t *msg,
                                    const char *note);

/* Respond to alloc request message - allocate R.
 * R is committed to the KVS first, then the response is sent.
 * If something goes wrong after this function returns, the reactor is stopped.
 */
int schedutil_alloc_respond_R (schedutil_t *util, const flux_msg_t *msg,
                               const char *R, const char *note);

/* Respond to an alloc request message - canceled.
 * N.B. 'msg' is the alloc request, not the cancel request.
 */
int schedutil_alloc_respond_cancel (schedutil_t *util, const flux_msg_t *msg);

#endif /* !_FLUX_SCHEDUTIL_ALLOC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
