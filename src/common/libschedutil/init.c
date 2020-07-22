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

/* flux module debug --setbit 0x8000 sched
 * flux module debug --clearbit 0x8000 sched
 */
enum module_debug_flags {
    /* alloc and free responses received while this is set
     * will never get a response
     */
    DEBUG_HANG_RESPONSES = 0x8000, // 16th bit
};

int schedutil_init (schedutil_t *util)
{
    /* ops->resource_acquire is defined, so start resource acquisition.
     * Upon receipt of first acquire response, su_hello_begin() is called
     * if ops->hello is defined.
     */
    if (util->ops->resource_acquire) {
        if (su_resource_begin (util) < 0)
            return -1;
    }
    /* ops->resource_acquire is not defined, so start with su_hello_begin()
     * if ops->hello is defined.
     */
    else if (util->ops->hello) {
        if (su_hello_begin (util) < 0)
            return -1;
    }
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

schedutil_t *schedutil_create (flux_t *h,
                               const struct schedutil_ops *ops,
                               void *arg)
{
    schedutil_t *util;

    if (!h || !ops) {
        errno = EINVAL;
        return NULL;
    }
    if (!(util = calloc(1, sizeof(*util))))
        return NULL;

    util->h = h;
    util->ops = ops;
    util->cb_arg = arg;
    if ((util->outstanding_futures = zlistx_new ()) == NULL)
        goto error;
    if (!(util->f_hello = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (su_ops_register (util) < 0)
        goto error;

    return util;

error:
    schedutil_destroy (util);
    return NULL;
}

static void respond_to_outstanding_msgs (schedutil_t *util)
{
    int rc = 0;
    flux_future_t *fut;
    flux_msg_t *msg;
    for (fut = zlistx_first (util->outstanding_futures);
         fut;
         fut = zlistx_next (util->outstanding_futures))
        {
            msg = flux_future_aux_get (fut, "schedutil::msg");
            rc = flux_respond_error (util->h,
                                     msg,
                                     ENOSYS,
                                     "automatic ENOSYS response "
                                     "from schedutil");
            if (rc != 0) {
                flux_log (util->h,
                          LOG_ERR,
                          "schedutil: error in responding to "
                          "outstanding messages");
            }
            flux_future_destroy (fut);
        }
    zlistx_purge (util->outstanding_futures);
}

void schedutil_destroy (schedutil_t *util)
{
    if (util) {
        int saved_errno = errno;
        respond_to_outstanding_msgs (util);
        zlistx_destroy (&util->outstanding_futures);
        if (util->f_hello) {
            flux_future_t *f;
            while ((f = zlist_pop (util->f_hello)))
                flux_future_destroy (f);
            zlist_destroy (&util->f_hello);
        }
        flux_future_destroy (util->f_res);
        su_ops_unregister (util);
        free (util);
        errno = saved_errno;
    }
    return;
}

bool su_hang_responses (const schedutil_t *util)
{
    return flux_module_debug_test (util->h, DEBUG_HANG_RESPONSES, false);
}

int su_add_outstanding_future (schedutil_t *util, flux_future_t *fut)
{
    if (zlistx_add_end (util->outstanding_futures, fut) == NULL)
        return -1;
    return 0;
}

int su_remove_outstanding_future (schedutil_t *util, flux_future_t *fut)
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
