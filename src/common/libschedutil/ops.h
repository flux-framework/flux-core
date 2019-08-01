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

/* An exception occurred for job 'id'.
 * If the severity is zero, and there is an allocation pending for 'id',
 * you must fail it immediately using schedutil_alloc_respond_denied(),
 * setting the note field to something like "alloc aborted due to
 * exception type=%s"
 */
typedef void (schedutil_exception_cb_f)(flux_t *h,
                                        flux_jobid_t id,
                                        const char *type,
                                        int severity,
                                        void *arg);

/* Callback for when the schedutil library becomes idle.
 * More specifically, idle means that all oustanding requests or futures within
 * the schedutil library have been responded to and fulfilled, respectively.
 * Useful for determining when to properly reply to any `quiescent` requests
 * sent during a simulation.
 */
typedef void (schedutil_idle_f)(flux_t *h,
                                void *arg);

/* Callback for when the schedutil library becomes busy.
 * More specifically, bust means that an oustanding request or future now exists
 * within the schedutil library. Useful for determining when to delay replying
 * to any `quiescent` requests sent during a simulation.
 */
typedef void (schedutil_busy_f)(flux_t *h,
                                void *arg);

#endif /* !_FLUX_SCHEDUTIL_OPS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
