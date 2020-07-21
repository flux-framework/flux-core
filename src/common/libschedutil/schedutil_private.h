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
    const struct schedutil_ops *ops;
    void *cb_arg;
    zlistx_t *outstanding_futures;
};

/* Track futures that need to be destroyed on scheduler unload.
 * Return 0 on success and -1 on error.
 */
int schedutil_add_outstanding_future (schedutil_t *util, flux_future_t *fut);
int schedutil_remove_outstanding_future (schedutil_t *util,
                                         flux_future_t *fut);

/* (Un-)register callbacks for alloc, free, cancel.
 */
int schedutil_ops_register (schedutil_t *util);
void schedutil_ops_unregister (schedutil_t *util);

#endif /* HAVE_SCHEDUTIL_PRIVATE_H */
