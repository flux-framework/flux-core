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

/* destroy future - zlistx_dsetructor_t footprint */
static void future_destructor (void **item)
{
    if (*item) {
        flux_future_destroy (*item);
        *item = NULL;
    }
}


schedutil_t *schedutil_create (flux_t *h,
                               schedutil_alloc_cb_f *alloc_cb,
                               schedutil_free_cb_f *free_cb,
                               schedutil_cancel_cb_f *cancel_cb,
                               void *cb_arg)
{
    schedutil_t *util;

    if (!h || !alloc_cb || !free_cb || !cancel_cb) {
        errno = EINVAL;
        return NULL;
    }
    if (!(util = calloc(1, sizeof(*util))))
        return NULL;

    util->h = h;
    util->alloc_cb = alloc_cb;
    util->free_cb = free_cb;
    util->cancel_cb = cancel_cb;
    util->cb_arg = cb_arg;
    if (!(util->outstanding_futures = zlistx_new ())
        || !(util->alloc_queue = zlistx_new ()))
        goto error;
    zlistx_set_destructor (util->outstanding_futures, future_destructor);
    if (schedutil_ops_register (util) < 0)
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
        zlistx_destroy (&util->outstanding_futures);
        zlistx_destroy (&util->alloc_queue);
        schedutil_ops_unregister (util);
        free (util);
        errno = saved_errno;
    }
}

int schedutil_add_outstanding_future (schedutil_t *util, flux_future_t *fut)
{
    if (zlistx_add_end (util->outstanding_futures, fut) == NULL)
        return -1;
    return 0;
}

int schedutil_remove_outstanding_future (schedutil_t *util, flux_future_t *fut)
{
    if (!zlistx_find (util->outstanding_futures, fut))
        return -1;
    if (zlistx_detach_cur (util->outstanding_futures) == NULL)
        return -1;
    return 0;
}

int schedutil_enqueue_alloc (schedutil_t *util, flux_future_t *f)
{
    if (schedutil_add_outstanding_future (util, f) < 0
        || zlistx_add_end (util->alloc_queue, f) == NULL)
        return -1;
    return 0;
}

flux_future_t *schedutil_peek_alloc (schedutil_t *util)
{
    return zlistx_first (util->alloc_queue);
}

int schedutil_dequeue_alloc (schedutil_t *util)
{
    flux_future_t *f = zlistx_first (util->alloc_queue);
    if (f) {
        if (!zlistx_detach_cur (util->alloc_queue))
            return -1;
        return schedutil_remove_outstanding_future (util, f);
    }
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
