/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_SCHEDUTIL_OPS_H
#define _FLUX_SCHEDUTIL_OPS_H

#include <flux/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callback for an alloc request.  jobspec is looked up as a convenience.
 * Decode msg with schedutil_alloc_request_decode().
 * 'msg' and 'jobspec' are only valid for hte duration of this call.
 * You should either respond to the request immediately (see alloc.h),
 * or cache this information for later response.
 */
typedef void (schedutil_alloc_cb_f)(flux_t *h,
                                    const flux_msg_t *msg,
                                    const char *jobspec,
                                    void *arg);

/* Callback for a free request.  R is looked up as a convenience.
 * Decode msg with schedutil_free_request_decode().
 * 'msg' and 'R' are only valid for the duration of this call.
 * You should either respond to the request immediately (see free.h),
 * or cache this information for later response.
 */
typedef void (schedutil_free_cb_f)(flux_t *h,
                                   const flux_msg_t *msg,
                                   const char *R,
                                   void *arg);

/* The job manager wants to cancel a pending alloc request.
 * The scheduler should look up the job in its queue.  If not found, do nothing.
 * If found, call schedutil_alloc_respond_cancel() and dequeue.
 */
typedef void (schedutil_cancel_cb_f)(flux_t *h,
                                     flux_jobid_t id,
                                     void *arg);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_SCHEDUTIL_OPS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
