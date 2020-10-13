/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* monitor.c - track execution targets joining/leaving the instance
 *
 * On rank 0 only:
 * - post 'online' / 'offline' events to resource.eventlog on 'up' set changes,
 *   relative to initial 'online' set posted with 'restart' event.
 * - publish 'resource.monitor-restart' event.
 *
 * On other ranks:
 * - send up to rank 0 at startup, and on receipt of monitor-restart event.
 * - send down to rank 0 at teardown.
 * - messages are batched and reduced on the way to rank 0.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"

#include "resource.h"
#include "reslog.h"
#include "monitor.h"
#include "rutil.h"

/* This is a simple batching monitor RPCs to avoid a storm at rank 0.
 * They will tend to come in waves as rc1 completes at each tree level,
 * and again in reverse for rc3, but ranks within a level
 * that complete relatively close in time can can be batched.
 */
static const double batch_timeout_seconds = 0.1;

struct batch {
    struct idset *up;
    struct idset *down;
};

struct monitor {
    struct resource_ctx *ctx;
    struct batch *batch;
    flux_watcher_t *batch_timer;

    struct idset *up;

    flux_msg_handler_t **handlers;

    monitor_cb_f cb;
    void *cb_arg;
    struct idset *down; // cached result of monitor_get_down()
};

static int monitor_reduce (flux_t *h, struct idset *up, struct idset *dn);

const struct idset *monitor_get_up (struct monitor *monitor)
{
    return monitor->up;
}

const struct idset *monitor_get_down (struct monitor *monitor)
{
    uint32_t size = monitor->ctx->size;
    unsigned int id;

    if (!monitor->down) {
        if (!(monitor->down = idset_create (size, 0)))
            return NULL;
    }
    for (id = 0; id < size; id++) {
        if (idset_test (monitor->up, id))
            (void)idset_clear (monitor->down, id);
        else
            (void)idset_set (monitor->down, id);
    }
    return monitor->down;
}

void monitor_set_callback (struct monitor *monitor, monitor_cb_f cb, void *arg)
{
    monitor->cb = cb;
    monitor->cb_arg = arg;
}

static void batch_destroy (struct batch *batch)
{
    if (batch) {
        int saved_errno = errno;
        idset_destroy (batch->up);
        idset_destroy (batch->down);
        free (batch);
        errno = saved_errno;
    }
}

static struct batch *batch_create (struct monitor *monitor)
{
    struct batch *b;

    if (!(b = calloc (1, sizeof (*b))))
        return NULL;
    if (!(b->up = idset_create (monitor->ctx->size, 0)))
        goto error;
    if (!(b->down = idset_create (monitor->ctx->size, 0)))
        goto error;
    flux_timer_watcher_reset (monitor->batch_timer, batch_timeout_seconds, 0.);
    flux_watcher_start (monitor->batch_timer);
    return b;
error:
    batch_destroy (b);
    return NULL;
}


/* Leader: batch contains up/down info from one or more downstream peers.
 * Update monitor->up and post online/offline events to resource.eventlog.
 * Avoid posting event if nothing changed, e.g. if 'monitor-force-up' option
 * pre-set all targets up.
 */
static int batch_timeout_leader (struct monitor *monitor)
{
    struct batch *b = monitor->batch;
    flux_t *h = monitor->ctx->h;
    struct idset *cpy;
    int rc = -1;
    char *online = NULL;
    char *offline = NULL;

    if (!(cpy = idset_copy (monitor->up)))
        return -1;
    if (idset_count (b->up) > 0) {
        if (!(online = idset_encode (b->up, IDSET_FLAG_RANGE)))
            goto done;
        flux_log (h, LOG_DEBUG, "monitor-batch: up %s", online);
        if (rutil_idset_add (monitor->up, b->up) < 0)
            goto done;
    }
    if (idset_count (b->down) > 0) {
        if (!(offline = idset_encode (b->down, IDSET_FLAG_RANGE)))
            goto done;
        flux_log (h, LOG_DEBUG, "monitor-batch: down %s", online);
        if (rutil_idset_sub (monitor->up, b->down) < 0)
            goto done;
    }
    if (!idset_equal (monitor->up, cpy)) {
        if (online && reslog_post_pack (monitor->ctx->reslog,
                                       NULL,
                                       "online",
                                       "{s:s}",
                                       "idset",
                                       online) < 0) {
            flux_log_error (h, "monitor-batch: error posting online event");
            goto done;
        }
        if (offline && reslog_post_pack (monitor->ctx->reslog,
                                         NULL,
                                         "offline",
                                         "{s:s}",
                                         "idset",
                                         offline) < 0) {
            flux_log_error (h, "monitor-batch: error posting offline event");
            goto done;
        }
        if (monitor->cb)
            monitor->cb (monitor, monitor->cb_arg);
    }
    rc = 0;
done:
    free (online);
    free (offline);
    idset_destroy (cpy);
    return rc;
}

/* The batch timer has expired.
 */
static void batch_timeout (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct monitor *monitor = arg;
    flux_t *h = monitor->ctx->h;
    int rc;

    if (monitor->batch) {
        if (monitor->ctx->rank == 0)
            rc = batch_timeout_leader (monitor);
        else
            rc = monitor_reduce (h, monitor->batch->up, monitor->batch->down);
        if (rc < 0)
            flux_log_error (h, "monitor-batch");
        batch_destroy (monitor->batch);
        monitor->batch = NULL;
    }
}

static void reduce_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct monitor *monitor = arg;
    const char *u;
    const char *d;
    struct idset *up = NULL;
    struct idset *dn = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s s:s}", "up", &u, "down", &d) < 0)
        goto error;
    if (!(up = idset_decode (u)))
        goto error;
    if (!(dn = idset_decode (d)))
        goto error;
    if (!monitor->batch) {
        if (!(monitor->batch = batch_create (monitor)))
            goto error;
    }
    if (rutil_idset_sub (monitor->batch->up, dn) < 0)
        goto error;
    if (rutil_idset_sub (monitor->batch->down, up) < 0)
        goto error;
    if (rutil_idset_add (monitor->batch->down, dn) < 0)
        goto error;
    if (rutil_idset_add (monitor->batch->up, up) < 0)
        goto error;
    idset_destroy (up);
    idset_destroy (dn);
    return;
error:
    flux_log_error (h, "goodbye: error processing request");
    idset_destroy (up);
    idset_destroy (dn);
}

static int monitor_reduce (flux_t *h, struct idset *up, struct idset *dn)
{
    flux_future_t *f = NULL;
    char *u = NULL;
    char *d = NULL;
    int rc = -1;

    if (up && !(u = idset_encode (up, IDSET_FLAG_RANGE)))
        goto done;
    if (dn && !(d = idset_encode (dn, IDSET_FLAG_RANGE)))
        goto done;
    if (!(f = flux_rpc_pack (h,
                             "resource.monitor-reduce",
                             FLUX_NODEID_UPSTREAM,
                             FLUX_RPC_NORESPONSE,
                             "{s:s s:s}",
                             "up",
                             u ? u : "",
                             "down",
                             d ? d : "")))
        goto done;
    rc = 0;
done:
    ERRNO_SAFE_WRAP (free, u);
    ERRNO_SAFE_WRAP (free, d);
    flux_future_destroy (f);
    return rc;
}

static int monitor_reduce_self (struct monitor *monitor, bool up)
{
    struct idset *ids;
    int rc = -1;

    if (!(ids = idset_create (monitor->ctx->size, 0)))
        goto done;
    if (idset_set (ids, monitor->ctx->rank) < 0)
        goto done;
    if (up)
        rc = monitor_reduce (monitor->ctx->h, ids, NULL);
    else
        rc = monitor_reduce (monitor->ctx->h, NULL, ids);
done:
    idset_destroy (ids);
    return rc;
}

/* Start reduction (again) when requested by rank 0.
 */
static void reload_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct monitor *monitor = arg;

    if (flux_event_decode (msg, NULL, NULL) < 0) {
        flux_log_error (h, "monitor-reload: error parsing event message");
        return;
    }
    if (monitor_reduce_self (monitor, true) < 0)
        flux_log_error (h, "monitor-reload: error sending reduce message");
}

/* Tell any loaded ranks to restart monitor reduction.
 */
static int publish_reload (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_event_publish (h, "resource.monitor-reload", 0, NULL)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "resource.monitor-reduce", reduce_cb, 0 },
    { FLUX_MSGTYPE_EVENT,    "resource.monitor-reload", reload_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void monitor_destroy (struct monitor *monitor)
{
    if (monitor) {
        int saved_errno = errno;
        if (monitor->ctx->rank > 0) {
            if (monitor_reduce_self (monitor, false) < 0)
                flux_log_error (monitor->ctx->h, "monitor-reduce failed");
        }
        batch_destroy (monitor->batch);
        flux_watcher_destroy (monitor->batch_timer);
        flux_msg_handler_delvec (monitor->handlers);
        idset_destroy (monitor->up);
        idset_destroy (monitor->down);
        free (monitor);
        errno = saved_errno;
    }
}

struct monitor *monitor_create (struct resource_ctx *ctx,
                                bool monitor_force_up)
{
    struct monitor *monitor;

    if (!(monitor = calloc (1, sizeof (*monitor))))
        return NULL;
    monitor->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, monitor, &monitor->handlers) < 0)
        goto error;
    if (!(monitor->batch_timer = flux_timer_watcher_create (
                                                flux_get_reactor (ctx->h),
                                                batch_timeout_seconds,
                                                0.,
                                                batch_timeout,
                                                monitor)))
        goto error;
    if (monitor->ctx->rank > 0) {
        if (monitor_reduce_self (monitor, true) < 0)
            goto error;
        if (flux_event_subscribe (ctx->h, "resource.monitor-reload") < 0)
            goto error;
    }
    else {
        /* Initialize up to "0" unless 'monitor_force_up' is true.
         * N.B. Initial up value will appear in 'restart' event posted
         * to resource.eventlog.
         */
        if (!(monitor->up = idset_create (ctx->size, 0)))
            goto error;
        if (monitor_force_up) {
            if (idset_range_set (monitor->up, 0, ctx->size - 1) < 0)
                goto error;
        }
        else {
            if (idset_set (monitor->up, 0) < 0)
                goto error;
        }
        if (publish_reload (ctx->h) < 0)
            goto error;
    }
    return monitor;
error:
    monitor_destroy (monitor);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
