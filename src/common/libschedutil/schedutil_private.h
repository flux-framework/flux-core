/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_SCHEDUTIL_PRIVATE_H
#define HAVE_SCHEDUTIL_PRIVATE_H 1

#include <czmq.h>
#include <flux/core.h>

#include "init.h"


struct schedutil_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    schedutil_alloc_cb_f *alloc_cb;
    schedutil_free_cb_f *free_cb;
    schedutil_cancel_cb_f *cancel_cb;
    void *cb_arg;
    zlistx_t *outstanding_futures;
    zlistx_t *alloc_queue;
};

/* Track futures that need to be destroyed on scheduler unload.
 * Return 0 on success and -1 on error.
 */
int schedutil_add_outstanding_future (schedutil_t *util, flux_future_t *fut);
int schedutil_remove_outstanding_future (schedutil_t *util,
                                         flux_future_t *fut);

/* Enqueue and dequeue pending alloc requests while they are waiting on
 *  async operations to complete. Ensures alloc callback of scheduler is
 *  called in the same order as alloc requests recvd by schedutil.
 */
int schedutil_enqueue_alloc (schedutil_t *util, flux_future_t *f);
flux_future_t *schedutil_peek_alloc (schedutil_t *util);
int schedutil_dequeue_alloc (schedutil_t *util);

/* (Un-)register callbacks for alloc, free, cancel.
 */
int schedutil_ops_register (schedutil_t *util);
void schedutil_ops_unregister (schedutil_t *util);

#endif /* HAVE_SCHEDUTIL_PRIVATE_H */
