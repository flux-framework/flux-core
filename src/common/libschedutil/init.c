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
#include <errno.h>
#include <flux/core.h>

#include "src/common/libutil/aux.h"

#include "init.h"
#include "schedutil_private.h"

schedutil_t *schedutil_create (flux_t *h,
                               op_alloc_f *alloc_cb,
                               op_free_f *free_cb,
                               op_exception_f *exception_cb,
                               void *cb_arg)
{
    schedutil_t *util;

    if (!h || !alloc_cb || !free_cb || !exception_cb) {
        errno = EINVAL;
        return NULL;
    }
    if (!(util = calloc(1, sizeof(*util))))
        return NULL;

    util->h = h;
    util->alloc_cb = alloc_cb;
    util->free_cb = free_cb;
    util->exception_cb = exception_cb;
    util->cb_arg = cb_arg;
    if (schedutil_ops_register (util) < 0)
        goto error;
    if (flux_event_subscribe (h, "job-exception") < 0)
        goto error;

    return util;

error:
    schedutil_destroy (util);
    return NULL;
}

void schedutil_destroy (schedutil_t *util)
{
    if (util) {
        int saved_errno = errno;
        schedutil_ops_unregister (util);
        free (util);
        errno = saved_errno;
    }
    return;
}
