/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <flux/core.h>
#include "src/common/libtap/tap.h"
#include "util_rpc.h"

static void reclaim_timeout (flux_reactor_t *r,
                             flux_watcher_t *w,
                             int revents,
                             void *arg)
{
    int *flag = arg;
    *flag = 1;
    diag ("matchtag_reclaim timed out");
}

static void reclaim_fake_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
}

/* N.B. flux_t handle's internal dispatcher is destroyed upon last message
 * handler unregister, so be sure a dispatcher is operating in the reactor
 * by creating a fake event handler that won't be invoked.
 */
int reclaim_matchtag (flux_t *h, int count, double timeout)
{
    struct flux_match match = FLUX_MATCH_EVENT;
    flux_msg_handler_t *mh;
    flux_reactor_t *r = flux_get_reactor (h);
    flux_watcher_t *timer;
    int orig_count = flux_matchtag_avail (h, 0);
    int expired = 0;

    if (!(mh = flux_msg_handler_create (h, match, reclaim_fake_cb, NULL)))
        BAIL_OUT ("flux_msg_handler_create failed");
    flux_msg_handler_start (mh);

    if (!(timer = flux_timer_watcher_create (r,
                                             timeout,
                                             0,
                                             reclaim_timeout,
                                             &expired)))
        BAIL_OUT ("flux_timer_watcher_create failed");
    flux_watcher_start (timer);

    while (!expired && flux_matchtag_avail (h, 0) - orig_count < count) {
        if (flux_reactor_run (r, FLUX_REACTOR_ONCE) < 0)
            BAIL_OUT ("flux_reactor_run failed");
    }
    flux_msg_handler_destroy (mh);
    flux_watcher_destroy (timer);

    if (expired)
        return -1;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
