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
    op_alloc_f *alloc_cb;
    op_free_f *free_cb;
    op_exception_f *exception_cb;
    void *cb_arg;
};

/* (Un-)register callbacks for alloc, free, exception.
 */
int schedutil_ops_register (schedutil_t *util);
void schedutil_ops_unregister (schedutil_t *util);
