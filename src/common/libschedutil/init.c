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
                               const struct schedutil_ops *ops,
                               void *arg)
{
    schedutil_t *util;

    /* ops->prioritize is optional */
    if (!h
        || !ops
        || !ops->alloc
        || !ops->free
        || !ops->cancel) {
        errno = EINVAL;
        return NULL;
    }
    if (!(util = calloc (1, sizeof (*util))))
        return NULL;

    util->h = h;
    util->ops = ops;
    util->cb_arg = arg;
    if (!(util->outstanding_futures = zlistx_new ()))
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
