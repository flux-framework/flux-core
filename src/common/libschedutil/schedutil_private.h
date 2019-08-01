/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <czmq.h>
#include <flux/core.h>

#include "init.h"

struct schedutil_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    schedutil_alloc_cb_f *alloc_cb;
    schedutil_free_cb_f *free_cb;
    schedutil_exception_cb_f *exception_cb;
    schedutil_idle_f *idle_cb;
    schedutil_busy_f *busy_cb;
    void *cb_arg;
    zlistx_t *outstanding_futures;
};

/*
 * Add/remove futures that have associated outstandings messages whose response
 * is blocked on the future's fulfillment.  Schedutil will automatically reply
 * to the msg with ENOSYS and destroy the future when the scheduler gets
 * unloaded.
 * Return 0 on success and -1 on error.
 */
int schedutil_add_outstanding_future (schedutil_t *util, flux_future_t *fut);
int schedutil_remove_outstanding_future (schedutil_t *util,
                                         flux_future_t *fut);

/* (Un-)register callbacks for alloc, free, exception.
 */
int schedutil_ops_register (schedutil_t *util);
void schedutil_ops_unregister (schedutil_t *util);

/* Testing interfaces
 *
 * Check to see if the scheduler has the debug flag set such
 * that responses should hang, forcing outstanding requests to exist.
 */
bool schedutil_hang_responses (const schedutil_t *util);
