/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* event.c - batch up eventlog updates into a timed commit */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <flux/core.h>

#include "util.h"
#include "event.h"
#include "src/common/libjob/job_util_private.h"

const double batch_timeout = 0.01;

struct event_ctx {
    flux_t *h;
    struct event_batch *batch;
    flux_watcher_t *timer;
    zlist_t *pending;
};

struct event_batch {
    struct event_ctx *ctx;
    flux_kvs_txn_t *txn;
    zlist_t *callbacks;
    flux_future_t *f;
};

struct event_callback {
    event_completion_f cb;
    void *arg;
};

struct event_batch *event_batch_create (struct event_ctx *ctx);
void event_batch_destroy (struct event_batch *batch);

/* Batch commit has completed.
 * Destroy 'batch', which notifies any registered callbacks.
 */
void commit_continuation (flux_future_t *f, void *arg)
{
    struct event_batch *batch = arg;
    struct event_ctx *ctx = batch->ctx;

    zlist_remove (ctx->pending, batch);
    event_batch_destroy (batch);
}

/* Close the current batch, if any, and commit it.
 */
void event_batch_commit (struct event_ctx *ctx)
{
    struct event_batch *batch = ctx->batch;
    if (batch) {
        ctx->batch = NULL;
        if (!(batch->f = flux_kvs_commit (ctx->h, NULL, 0, batch->txn)))
            goto error;
        if (flux_future_then (batch->f, -1., commit_continuation, batch) < 0)
            goto error;
        if (zlist_append (ctx->pending, batch) < 0)
            goto nomem;
    }
    return;
nomem:
    errno = ENOMEM;
error: // unlikely (e.g. ENOMEM)
    flux_log_error (ctx->h, "%s: aborting reactor", __FUNCTION__);
    flux_reactor_stop_error (flux_get_reactor (ctx->h));
    event_batch_destroy (batch);
}

void timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct event_ctx *ctx = arg;
    event_batch_commit (ctx);
}

void event_batch_destroy (struct event_batch *batch)
{
    if (batch) {
        int saved_errno = errno;
        struct event_callback *ec;

        flux_kvs_txn_destroy (batch->txn);
        if (batch->f)
            (void)flux_future_wait_for (batch->f, -1);
        if (batch->callbacks) {
            while ((ec = zlist_pop (batch->callbacks))) {
                ec->cb (batch->f, ec->arg);
                free (ec);
            }
            zlist_destroy (&batch->callbacks);
        }
        flux_future_destroy (batch->f);
        free (batch);
        errno = saved_errno;
    }
}

struct event_batch *event_batch_create (struct event_ctx *ctx)
{
    struct event_batch *batch;

    if (!(batch = calloc (1, sizeof (*batch))))
        return NULL;
    if (!(batch->txn = flux_kvs_txn_create ()))
        goto error;
    if (!(batch->callbacks = zlist_new ()))
        goto nomem;
    batch->ctx = ctx;
    return batch;
nomem:
    errno = ENOMEM;
error:
    event_batch_destroy (batch);
    return NULL;
}

/* Append event to batch, registering callback 'cb' if non-NULL.
 */
int event_batch_append (struct event_batch *batch,
                        const char *key, const char *event,
                        event_completion_f cb, void *arg)
{
    if (cb) {
        struct event_callback *ec;
        if (!(ec = calloc (1, sizeof (*ec))))
            return -1;
        ec->cb = cb;
        ec->arg = arg;
        if (zlist_push (batch->callbacks, ec) < 0) {
            free (ec);
            errno = ENOMEM;
            return -1;
        }
    }
    if (flux_kvs_txn_put (batch->txn, FLUX_KVS_APPEND, key, event) < 0) {
        if (cb) {
            struct event_callback *ec;
            int saved_errno = errno;
            ec = zlist_pop (batch->callbacks);
            free (ec);
            errno = saved_errno;
        }
        return -1;
    }
    return 0;
}

/* Create a new "batch" if there is none.
 * No-op if batch already started.
 */
int event_batch_start (struct event_ctx *ctx)
{
    if (!ctx->batch) {
        if (!(ctx->batch = event_batch_create (ctx)))
            return -1;
        flux_timer_watcher_reset (ctx->timer, batch_timeout, 0.);
        flux_watcher_start (ctx->timer);
    }
    return 0;
}

int event_log (struct event_ctx *ctx, flux_jobid_t id,
               event_completion_f cb, void *arg,
               const char *name, const char *context)
{
    char key[64];
    char *event = NULL;
    int saved_errno;

    if (job_util_jobkey (key, sizeof (key), true, id, "eventlog") < 0)
        return -1;
    if (!(event = flux_kvs_event_encode (name, context)))
        return -1;
    if (event_batch_start (ctx) < 0)
        goto error;
    if (event_batch_append (ctx->batch, key, event, cb, arg) < 0)
        goto error;
    free (event);
    return 0;
error:
    saved_errno = errno;
    free (event);
    errno = saved_errno;
    return -1;
}

int event_log_fmt (struct event_ctx *ctx, flux_jobid_t id,
                   event_completion_f cb, void *arg, const char *name,
                   const char *fmt, ...)
{
    va_list ap;
    char context[FLUX_KVS_MAX_EVENT_CONTEXT + 1];
    int n;

    va_start (ap, fmt);
    n = vsnprintf (context, sizeof (context), fmt, ap);
    va_end (ap);
    if (n < 0 || n >= sizeof (context)) {
        errno = EINVAL;
        return -1;
    }
    return event_log (ctx, id, cb, arg, name, context);
}

/* N.B. any in-flight batches are destroyed here.
 * If they are not yet fulfilled, user callbacks may synchronously block on
 * flux_future_get().
 */
void event_ctx_destroy (struct event_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_watcher_destroy (ctx->timer);
        event_batch_commit (ctx);
        if (ctx->pending) {
            struct event_batch *batch;
            while ((batch = zlist_pop (ctx->pending)))
                event_batch_destroy (batch);
        }
        zlist_destroy (&ctx->pending);
        free (ctx);
        errno = saved_errno;
    }
}

struct event_ctx *event_ctx_create (flux_t *h)
{
    struct event_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    if (!(ctx->timer = flux_timer_watcher_create (flux_get_reactor (h),
                                                  0., 0., timer_cb, ctx)))
        goto error;
    if (!(ctx->pending = zlist_new ()))
        goto nomem;
    return ctx;
nomem:
    errno = ENOMEM;
error:
    event_ctx_destroy (ctx);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

