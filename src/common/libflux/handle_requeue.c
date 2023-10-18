/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>

#include "message.h"
#include "msg_deque.h"
#include "handle.h"
#include "handle_private.h"

/* Caveat: FLUX_O_TRACE and message counters will show requeued messages
 * being received again as though they were new.
 */

int handle_requeue_push_back (flux_t *h, const flux_msg_t *msg)
{
    if (!h || !msg)
        goto inval;
    h = lookup_clone_ancestor (h);
    if ((h->flags & FLUX_O_NOREQUEUE))
        goto inval;
    if (msg_deque_push_back (h->queue,
                             (flux_msg_t *)flux_msg_incref (msg)) < 0) {
        flux_msg_decref (msg);
        return -1;
    }
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

int handle_requeue_push_front (flux_t *h, const flux_msg_t *msg)
{
    if (!h || !msg)
        goto inval;
    h = lookup_clone_ancestor (h);
    if ((h->flags & FLUX_O_NOREQUEUE))
        goto inval;
    if (msg_deque_push_front (h->queue,
                              (flux_msg_t *)flux_msg_incref (msg)) < 0) {
        flux_msg_decref (msg);
        return -1;
    }
    return 0;
inval:
    errno = EINVAL;
    return -1;
}

// vi:ts=4 sw=4 expandtab
