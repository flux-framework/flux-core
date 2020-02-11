/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shutdown.c - orderly distributed shutdown
 *
 * We must keep the overlay network up long enough to be sure every broker
 * is informed about the shutdown.  Here is the protocol:
 *
 * 1. Rank 0 broker publishes a shutdown event, to which all brokers subscribe
 * 2. Each broker (including 0) handles the event.
 * 3. Each broker starts a doomsday timer
 * 4. If a broker has descendants, it waits for them to disconnect
 * 5. Once all descendants disconnect, broker stops timer and calls callback.
 * 6. The callback should disconnect from parent (if any) and exit.
 * 7. Ultimately the rank 0 brokers exits with exit code of rc1/2/3.
 *
 * The rank 0 broker calls shutdown_instance().
 * All ranks register a shutdown callback which is called on state change, e.g.
 * - when shutdown begins: !shutdown_is_complete() and !shutdown_is_expired()
 * - when shutdown completes: shutdown_is_compete() and !shutdown_is_expired()
 * - when shutdown expires: shutdown_is_expired()
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libutil/kary.h"

#include "overlay.h"
#include "shutdown.h"


struct shutdown {
    flux_t *h;
    overlay_t *overlay;
    flux_msg_handler_t **handlers;
    flux_watcher_t *timer;
    double grace;
    bool expired;
    bool complete;

    shutdown_cb_f cb;
    void *arg;
};

static void shutdown_begin (struct shutdown *s)
{
    assert (s->complete == false);
    assert (s->expired == false);
    if (s->cb)
        s->cb (s, s->arg);
}

static void shutdown_complete (struct shutdown *s)
{
    s->complete = true;
    flux_watcher_stop (s->timer);
    overlay_set_monitor_cb (s->overlay, NULL, NULL);
    if (s->cb)
        s->cb (s, s->arg);
}

static void shutdown_timeout (struct shutdown *s)
{
    s->expired = true;
    overlay_set_monitor_cb (s->overlay, NULL, NULL);
    if (s->cb)
        s->cb (s, s->arg);
}

static void grace_timeout_cb (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    struct shutdown *s = arg;
    shutdown_timeout (s);
}

void monitor_cb (overlay_t *overlay, void *arg)
{
    struct shutdown *s = arg;
    if (overlay_get_child_peer_count (overlay) == 0)
        shutdown_complete (s);
}

void shutdown_event_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct shutdown *s = arg;

    shutdown_begin (s);
    if (overlay_get_child_peer_count (s->overlay) == 0)
        shutdown_complete (s);
    else {
        overlay_set_monitor_cb (s->overlay, monitor_cb, s);
        flux_watcher_start (s->timer);
    }
}

void publish_continuation (flux_future_t *f, void *arg)
{
    struct shutdown *s = arg;

    if (flux_future_get (f, NULL) < 0)
        flux_log_error (s->h, "publishing shutdown event");
    flux_future_destroy (f);
}

/* Called from rank 0 only */
void shutdown_instance (struct shutdown *s)
{
    if (overlay_get_child_peer_count (s->overlay) == 0) {
        shutdown_begin (s);
        shutdown_complete (s);
    }
    else {
        flux_future_t *f;

        if (!(f = flux_event_publish (s->h, "shutdown", 0, NULL))) {
            flux_log_error (s->h, "publishing shutdown event");
            return;
        }
        if (flux_future_then (f, -1., publish_continuation, s) < 0) {
            flux_log_error (s->h, "registering continuation for shutdown");
            flux_future_destroy (f);
            return;
        }
    }
}

void shutdown_set_callback (struct shutdown *s, shutdown_cb_f cb, void *arg)
{
    s->cb = cb;
    s->arg = arg;
}

bool shutdown_is_expired (struct shutdown *s)
{
    return s->expired;
}

bool shutdown_is_complete (struct shutdown *s)
{
    return s->complete;
}

void shutdown_destroy (struct shutdown *s)
{
    if (s) {
        int saved_errno = errno;
        flux_msg_handler_delvec (s->handlers);
        if (s->h)
            (void)flux_event_unsubscribe (s->h, "shutdown");
        flux_watcher_destroy (s->timer);
        free (s);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,  "shutdown", shutdown_event_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

struct shutdown *shutdown_create (flux_t *h,
                                  double grace,
                                  uint32_t size,
                                  int tbon_k,
                                  overlay_t *overlay)
{
    struct shutdown *s;

    if (!(s = calloc (1, sizeof (*s))))
        return NULL;
    s->h = h;
    s->overlay = overlay;

    /* If grace is zero, select a default based on the maximum number of
     * TBON levels in the instance since the latency at rank 0 will be based
     * on the number of hops from the most distant leaf node.
     * N.B. a size=1 instance has 1 level.
     */
    if (grace == 0) {
        int levels = kary_levelof (tbon_k, size - 1) + 1;
        s->grace = levels * 2; // e.g. 2s for size=1, 4s for size=3 k=2
    }
    else
        s->grace = grace;

    if (flux_msg_handler_addvec (h, htab, s, &s->handlers) < 0)
        goto error;
    if (!(s->timer = flux_timer_watcher_create (flux_get_reactor (h),
                                                s->grace,
                                                0.,
                                                grace_timeout_cb,
                                                s)))
        goto error;
    if (flux_event_subscribe (s->h, "shutdown") < 0)
        goto error;
    return s;
error:
    shutdown_destroy (s);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
