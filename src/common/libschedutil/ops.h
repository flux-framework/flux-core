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

struct schedutil_ops {
    /* Here are the resources that the scheduler can manage.
     * Assume all are down until marked up.
     * (N.B. resource.acquire RPC is skipped if this callback is NULL)
     */
    void (*resource_acquire)(flux_t *h, const char *resobj, void *arg);

    /* Scheduler should mark idset execution targets id's up/down.
     */
    void (*resource_down)(flux_t *h, const char *idset, void *arg);
    void (*resource_up)(flux_t *h, const char *idset, void *arg);

    /* Callback for ingesting R + metadata for jobs that have resources
     * Return 0 on success, -1 on failure with errno set.
     */
    int (*hello)(flux_t *h,
                 flux_jobid_t id,
                 int priority,
                 uint32_t userid,
                 double t_submit,
                 const char *R,
                 void *arg);

    /* Hello handshake has completed. Scheduler should call schedutil_ready()
     * when it is ready to begin handling alloc/free/cancel requests.
     * Return 0 on success, -1 on failure with errno set.
     */
    int (*ready)(flux_t *h, void *arg);

    /* Callback for an alloc request.  jobspec is looked up as a convenience.
     * Decode msg with schedutil_alloc_request_decode().
     * 'msg' and 'jobspec' are only valid for the duration of this call.
     * You should either respond to the request immediately (see alloc.h),
     * or cache this information for later response.
     */
    void (*alloc)(flux_t *h,
                  const flux_msg_t *msg,
                  const char *jobspec,
                  void *arg);

    /* Callback for a free request.  R is looked up as a convenience.
     * Decode msg with schedutil_free_request_decode().
     * 'msg' and 'R' are only valid for the duration of this call.
     * You should either respond to the request immediately (see free.h),
     * or cache this information for later response.
     */
    void (*free)(flux_t *h, const flux_msg_t *msg, const char *R, void *arg);

    /* The job manager wants to cancel a pending alloc request.
     * The scheduler should look up the job in its queue.  If not found, do
     * nothing.  If found, call schedutil_alloc_respond_cancel() and dequeue.
     * (The cancel request itself does not receive a response)
     */
    void (*cancel)(flux_t *h, flux_jobid_t id, void *arg);
};

#endif /* !_FLUX_SCHEDUTIL_OPS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
