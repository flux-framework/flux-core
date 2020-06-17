/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shutdown.c - initiate instance shutdown, manage local state transitions
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <assert.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/kary.h"

#include "overlay.h"
#include "state_machine.h"
#include "shutdown.h"
#include "broker.h"


struct shutdown {
    struct broker *ctx;
    flux_msg_handler_t **handlers;
    flux_watcher_t *timer;
    double grace;
    bool instance_shutdown;
    flux_future_t *f_notify;
    flux_future_t *f_publish;
};

static bool is_leaf_node (struct broker *ctx)
{
    if (kary_childof (ctx->tbon_k, ctx->size, ctx->rank, 0) == KARY_NONE)
        return true;
    return false;
}

static void notify_parent_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);

    if (flux_future_get (f, NULL) < 0)
        flux_log_error (h, "sending shutdown.notify request");
}

static flux_future_t *notify_parent (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_rpc (h, "shutdown.notify", NULL, FLUX_NODEID_UPSTREAM, 0))
      || flux_future_then (f, -1., notify_parent_continuation, NULL) < 0) {
        flux_future_destroy (f);
        flux_log_error (h, "sending shutdown.notify request");
        return NULL;
    }
    return f;
}

static void publish_shutdown_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    if (flux_future_get (f, NULL) < 0)
        flux_log_error (h, "publishing shutdown.all event");
}

static flux_future_t *publish_shutdown (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_event_publish (h, "shutdown.all", 0, NULL))
      || flux_future_then (f, -1., publish_shutdown_continuation, NULL) < 0) {
        flux_future_destroy (f);
        flux_log_error (h, "publishing shutdown.all event");
        return NULL;
    }
    return f;
}

static void grace_timeout_cb (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    struct shutdown *s = arg;
    int count = overlay_get_child_peer_count (s->ctx->overlay);

    if (count > 0) {
        flux_log (s->ctx->h, LOG_ERR, "grace expired with %d children", count);
        state_machine_post (s->ctx->state_machine, "children-timeout");
        flux_watcher_stop (s->timer);
        overlay_set_monitor_cb (s->ctx->overlay, NULL, NULL);
    }
}

void monitor_cb (struct overlay *overlay, void *arg)
{
    struct shutdown *s = arg;
    int count = overlay_get_child_peer_count (overlay);

    if (count == 0) {
        state_machine_post (s->ctx->state_machine, "children-complete");
        flux_watcher_stop (s->timer);
        overlay_set_monitor_cb (overlay, NULL, NULL);
    }
}

/* Handle shutdown.notify request.
 * If leader rank, publish shutdown message.
 * If follower rank, notify parent so we can get word to the leader rank.
 */
static void shutdown_notify_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    struct shutdown *s = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (s->ctx->rank == 0) {
        if (!s->instance_shutdown) {
            if (!(s->f_publish = publish_shutdown (h)))
                goto error;
            s->instance_shutdown = true;
        }
    }
    else {
        if (!s->instance_shutdown && !s->f_notify) {
            if (!(s->f_notify = notify_parent (h)))
                goto error;
        }
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to shutdown.notify request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to shutdown.notify request");

}

/* Handle receipt of shutdown.all event message.
 * Post shutdown-abort event to state machine.
 */
static void shutdown_all_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct shutdown *s = arg;

    if (!s->instance_shutdown) {
        state_machine_shutdown (s->ctx->state_machine);
        s->instance_shutdown = true;
    }
}

/* State machine is telling us that broker has entered SHUTDOWN state.
 * If leader rank, publish shutdown.all event.
 * If follower rank, notify parent so we can get word to the leader rank.
 */
void shutdown_notify (struct shutdown *s)
{
    if (s->ctx->rank == 0) {
        if (!s->instance_shutdown) {
            s->f_publish = publish_shutdown (s->ctx->h);
            s->instance_shutdown = true;
        }
    }
    else {
        if (!s->instance_shutdown && !s->f_notify)
            s->f_notify = notify_parent (s->ctx->h);
    }

    /* Immediately post events if shutdown has already completed.
     * Otherwise, set child disconnect monitor in motion, with timeout.
     */
    if (is_leaf_node (s->ctx))
        state_machine_post (s->ctx->state_machine, "children-none");
    else if (overlay_get_child_peer_count (s->ctx->overlay) == 0)
        state_machine_post (s->ctx->state_machine, "children-complete");
    else {
        overlay_set_monitor_cb (s->ctx->overlay, monitor_cb, s);
        flux_watcher_start (s->timer);
    }
}

void shutdown_destroy (struct shutdown *s)
{
    if (s) {
        int saved_errno = errno;
        flux_msg_handler_delvec (s->handlers);
        (void)flux_event_unsubscribe (s->ctx->h, "shutdown");
        flux_future_destroy (s->f_notify);
        flux_future_destroy (s->f_publish);
        flux_watcher_destroy (s->timer);
        free (s);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,   "shutdown.all",     shutdown_all_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "shutdown.notify",  shutdown_notify_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

/* Select a timeout based on the maximum number of TBON levels in the instance
 * since the latency at rank 0 will be based on the number of hops from the
 * most distant leaf node.  N.B. a size=1 instance has 1 level.
 */
static double guess_grace (int tbon_k, uint32_t size)
{
    int levels = kary_levelof (tbon_k, size - 1) + 1;
    double grace = levels * 2; // e.g. 2s for size=1, 4s for size=3 k=2

    return grace;
}

struct shutdown *shutdown_create (struct broker *ctx)
{
    struct shutdown *s;

    if (!(s = calloc (1, sizeof (*s))))
        return NULL;
    s->ctx = ctx;
    if (ctx->shutdown_grace > 0.)
        s->grace = ctx->shutdown_grace;
    else
        s->grace = guess_grace (ctx->tbon_k, ctx->size);

    if (flux_msg_handler_addvec (ctx->h, htab, s, &s->handlers) < 0)
        goto error;
    if (!(s->timer = flux_timer_watcher_create (ctx->reactor,
                                                s->grace,
                                                0.,
                                                grace_timeout_cb,
                                                s)))
        goto error;
    if (flux_event_subscribe (ctx->h, "shutdown") < 0)
        goto error;
    return s;
error:
    shutdown_destroy (s);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
