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

/* In the following callbacks, 'msg' is a request or response message from
 * the job manager with payload defined by RFC 27.  The message's reference
 * count is decremented when the callback returns.
 */
struct schedutil_ops {
    /* Callback for ingesting R + metadata for jobs that have resources
     * Return 0 on success, -1 on failure with errno set.
     */
    int (*hello)(flux_t *h,
                 const flux_msg_t *msg,
                 const char *R,
                 void *arg);

    /* Callback for an alloc request.  'msg' is only valid for the
     * duration of this call.  You should either respond to the
     * request immediately (see alloc.h), or cache this information
     * for later response.
     */
    void (*alloc)(flux_t *h,
                  const flux_msg_t *msg,
                  void *arg);

    /* Callback for a free request.  R is looked up as a convenience.
     * 'msg' and 'R' are only valid for the duration of this call.
     * You should either respond to the request immediately (see
     * free.h), or cache this information for later response.
     */
    void (*free)(flux_t *h,
                 const flux_msg_t *msg,
                 const char *R,
                 void *arg);

    /* The job manager wants to cancel a pending alloc request.  The
     * scheduler should look up the job in its queue.  If not found,
     * do nothing.  If found, call schedutil_alloc_respond_cancel()
     * and dequeue.
     */
    void (*cancel)(flux_t *h,
                   const flux_msg_t *msg,
                   void *arg);
};

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_SCHEDUTIL_OPS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
