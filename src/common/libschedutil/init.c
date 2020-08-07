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
    if (schedutil_ops_register (util) < 0)
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
        zlistx_destroy (&util->alloc_queue);
        schedutil_ops_unregister (util);
        free (util);
        errno = saved_errno;
    }
    return;
}

bool schedutil_hang_responses (const schedutil_t *util)
{
    return flux_module_debug_test (util->h, DEBUG_HANG_RESPONSES, false);
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
