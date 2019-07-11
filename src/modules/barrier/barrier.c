/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* distributed barrier service
 *
 * Each client sends a barrier.enter request with (name, nprocs) tuple.
 * The request is cached on the local broker rank, and after a short
 * delay to await concurrent requests, a count is sent upstream
 * via the internal barrier.update request (no response).  Once the
 * count reaches nprocs a barrier.exit event is published.  Upon receiving
 * the barrier.exit event, cached barrier.enter requests on all ranks are
 * answered.
 *
 * The barrier.exit event contains an errnum field.  If zero, the barrier
 * completed successfully.  If non-zero, the barrier is aborted with an
 * error.  An error may occur if a client tries to enter the barrier twice,
 * or a client disconnects before the barrier completes.
 *
 * Notes:
 * - Guests may use the barrier service.
 * - Barrier names must be unique, per user, across the instance.
 * - Upon receipt of a barrier.enter or barrier.update request, a timer
 *   is started to open a short window in time, within which concurrent
 *   requests may be batched.  After expiration, a combined request is
 *   sent upstream.
 * - The timer is re-armed if another request is received.  This process
 *   may continue until the barrier is complete.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"

const double reduction_timeout = 0.001; // sec

struct barrier_ctx {
    zhash_t *barriers;
    flux_t *h;
    uint32_t rank;
};

struct barrier {
    char *name;
    int nprocs;
    int count;
    zhash_t *clients;
    struct barrier_ctx *ctx;
    int errnum;
    flux_watcher_t *timer;
    bool timer_armed;
    uint32_t owner;
};

static int exit_event_send (flux_t *h,
                            const char *name,
                            uint32_t owner,
                            int errnum);

static void reduction_timeout_cb (flux_reactor_t *r,
                                  flux_watcher_t *w,
                                  int revents,
                                  void *arg);

static void barrier_ctx_destroy (struct barrier_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        zhash_destroy (&ctx->barriers);
        free (ctx);
        errno = saved_errno;
    }
}

static struct barrier_ctx *barrier_ctx_create (flux_t *h)
{
    struct barrier_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->barriers = zhash_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_get_rank (h, &ctx->rank) < 0)
        goto error;
    ctx->h = h;
    return ctx;
error:
    barrier_ctx_destroy (ctx);
    return NULL;
}

static void barrier_destroy (struct barrier *b)
{
    if (b) {
        int saved_errno = errno;
        flux_log (b->ctx->h, LOG_DEBUG, "destroy %s %d", b->name, b->nprocs);
        zhash_destroy (&b->clients);
        free (b->name);
        flux_watcher_destroy (b->timer);
        free (b);
        errno = saved_errno;
    }
}

static struct barrier *barrier_create (struct barrier_ctx *ctx,
                                       const char *name,
                                       int nprocs,
                                       uint32_t owner)
{
    struct barrier *b;

    if (!(b = calloc (1, sizeof (*b))))
        return NULL;
    b->owner = owner;
    if (!(b->name = strdup (name)))
        goto error;
    b->nprocs = nprocs;
    if (!(b->clients = zhash_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (ctx->rank > 0) {
        b->timer = flux_timer_watcher_create (flux_get_reactor (ctx->h),
                                              reduction_timeout,
                                              0.,
                                              reduction_timeout_cb,
                                              b);
        if (!b->timer)
            goto error;
    }
    b->ctx = ctx;
    return b;
error:
    barrier_destroy (b);
    return NULL;
}

static int barrier_add_client (struct barrier *b,
                               char *sender,
                               const flux_msg_t *msg)
{
    flux_msg_t *cpy = flux_msg_copy (msg, false);
    if (!cpy)
        return -1;
    if (zhash_insert (b->clients, sender, cpy) < 0) {
        flux_msg_destroy (cpy);
        return -1;
    }
    zhash_freefn (b->clients, sender, (zhash_free_fn *)flux_msg_destroy);
    return 0;
}

static char *barrier_key (const char *name, uint32_t owner)
{
    char *key;

    if (asprintf (&key, "%"PRIu32":%s", owner, name) < 0)
        return NULL;
    return key;
}

static int barrier_delete (struct barrier_ctx *ctx,
                           const char *name,
                           uint32_t owner)
{
    char *key;

    if (!(key = barrier_key (name, owner)))
        return -1;
    zhash_delete (ctx->barriers, key);
    free (key);
    return 0;
}

static struct barrier *barrier_lookup (struct barrier_ctx *ctx,
                                       const char *name,
                                       uint32_t owner)
{
    struct barrier *b;
    char *key;

    if (!(key = barrier_key (name, owner)))
        return NULL;
    b = zhash_lookup (ctx->barriers, key);
    free (key);
    return b;
}

static int barrier_insert (struct barrier_ctx *ctx, struct barrier *b)
{
    char *key;
    int rc = -1;

    if (!(key = barrier_key (b->name, b->owner)))
        return -1;
    if (zhash_insert (ctx->barriers, key, b) < 0)
        goto out;
    zhash_freefn (ctx->barriers, key, (zhash_free_fn *)barrier_destroy);
    rc = 0;
out:
    free (key);
    return rc;
}

static struct barrier *barrier_lookup_create (struct barrier_ctx *ctx,
                                              const char *name,
                                              int nprocs,
                                              uint32_t owner)
{
    struct barrier *b;

    if (!(b = barrier_lookup (ctx, name, owner))) {
        if (!(b = barrier_create (ctx, name, nprocs, owner)))
            return NULL;
        if (barrier_insert (ctx, b) < 0) {
            barrier_destroy (b);
            return NULL;
        }
        if (ctx->rank == 0)
            flux_log (ctx->h, LOG_DEBUG, "create %s %d", name, nprocs);
    }
    return b;
}

/* If the count has been reached, terminate the barrier;
 * o/w set timer to pass count upstream and zero it here.
 */
static int barrier_update (struct barrier *b, int count)
{
    b->count += count;
    if (b->count == b->nprocs) {
        if (exit_event_send (b->ctx->h, b->name, b->owner, 0) < 0) {
            flux_log_error (b->ctx->h, "exit_event_send");
            return -1;
        }
    }
    else if (b->ctx->rank > 0 && !b->timer_armed) {
        flux_timer_watcher_reset (b->timer, reduction_timeout, 0.);
        flux_watcher_start (b->timer);
        b->timer_armed = true;
    }
    return 0;
}

static void send_update_request (flux_t *h, struct barrier *b)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (h,
                             "barrier.update",
                             FLUX_NODEID_UPSTREAM,
                             FLUX_RPC_NORESPONSE,
                             "{s:s s:i s:i s:i}",
                             "name", b->name,
                             "count", b->count,
                             "nprocs", b->nprocs,
                             "owner", b->owner))) {
        flux_log_error (h, "sending barrier.update request");
        goto done;
    }
done:
    flux_future_destroy (f);
}

/* Handle count update from downstream barrier module.
 * No response is expected.
 */
static void update_request_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    struct barrier_ctx *ctx = arg;
    struct barrier *b;
    const char *name;
    int count, nprocs;
    int owner;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:i s:i s:i !}",
                             "name", &name,
                             "count", &count,
                             "nprocs", &nprocs,
                             "owner", &owner) < 0) {
        flux_log_error (h, "barrier.update request");
        return;
    }
    if (!(b = barrier_lookup_create (ctx, name, nprocs, owner))) {
        flux_log_error (h, "barrier_lookup_create");
        return;
    }
    barrier_update (b, count);
}

/* Handle client request to enter barrier.
 * Response is normally deferred until barrier is complete.
 */
static void enter_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    struct barrier_ctx *ctx = arg;
    struct barrier *b;
    char *sender = NULL;
    const char *name;
    int nprocs;
    uint32_t owner;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:i !}",
                             "name", &name,
                             "nprocs", &nprocs) < 0)
        goto error;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto error;
    if (flux_msg_get_userid (msg, &owner) < 0)
        goto error;
    if (!(b = barrier_lookup_create (ctx, name, nprocs, owner)))
        goto error;

    if (barrier_add_client (b, sender, msg) < 0) {
        flux_log (ctx->h, LOG_ERR, "abort %s due to double entry by %s",
                  name, sender);
        if (exit_event_send (ctx->h, b->name, b->owner, ECONNABORTED) < 0)
            flux_log_error (ctx->h, "exit_event_send");
        errno = EEXIST;
        goto error;
    }
    if (barrier_update (b, 1) < 0)
        goto error;
    free (sender);
    return;
error:
    flux_respond_error (ctx->h, msg, errno, NULL);
    free (sender);
}

/* Upon client disconnect, abort any pending barriers it was
 * participating in.
 */
static void disconnect_request_cb (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    struct barrier_ctx *ctx = arg;
    char *sender = NULL;
    const char *key;
    struct barrier *b;
    uint32_t userid;
    uint32_t rolemask;

    if (flux_msg_get_userid (msg, &userid) < 0)
        goto error;
    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto error;
    FOREACH_ZHASH (ctx->barriers, key, b) {
        if (zhash_lookup (b->clients, sender)) {
            if (!(rolemask & FLUX_ROLE_OWNER) && b->owner != userid) {
                flux_log (h, LOG_ERR,
                          "client userid mismatch %"PRIu32" != %"PRIu32,
                          userid, b->owner);
            }
            else if (exit_event_send (h, b->name, b->owner, ECONNABORTED) < 0)
                flux_log_error (h, "exit_event_send");
        }
    }
    free (sender);
    return;
error:
    flux_log_error (h, "barrier.disconnect");
    free (sender);
}

static int exit_event_send (flux_t *h,
                            const char *name,
                            uint32_t owner,
                            int errnum)
{
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (!(msg = flux_event_pack ("barrier.exit", "{s:s s:i s:i}",
                                 "name", name,
                                 "owner", owner,
                                 "errnum", errnum)))
        goto done;
    if (flux_send (h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static void exit_event_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    struct barrier_ctx *ctx = arg;
    struct barrier *b;
    const char *name;
    int errnum;
    const char *key;
    flux_msg_t *req;
    int owner;

    if (flux_event_unpack (msg, NULL, "{s:s s:i s:i !}",
                           "name", &name,
                           "owner", &owner,
                           "errnum", &errnum) < 0) {
        flux_log_error (h, "%s: decoding event", __FUNCTION__);
        return;
    }
    if ((b = barrier_lookup (ctx, name, owner))) {
        b->errnum = errnum;
        FOREACH_ZHASH (b->clients, key, req) {
            int rc;
            if (b->errnum == 0)
                rc = flux_respond (h, req, NULL);
            else
                rc = flux_respond_error (h, req, b->errnum, NULL);
            if (rc < 0)
                flux_log_error (h, "%s: sending enter response", __FUNCTION__);
        }
        barrier_delete (ctx, name, owner);
    }
}

static void reduction_timeout_cb (flux_reactor_t *r, flux_watcher_t *w,
                                  int revents, void *arg)
{
    struct barrier *b = arg;

    assert (b->ctx->rank != 0);
    b->timer_armed = false; /* one shot */
    if (b->count > 0) {
        send_update_request (b->ctx->h, b);
        b->count = 0;
    }
}

static struct flux_msg_handler_spec htab[] = {
    {   FLUX_MSGTYPE_REQUEST,
        "barrier.enter",
        enter_request_cb,
        FLUX_ROLE_USER,
    },
    {   FLUX_MSGTYPE_REQUEST,
        "barrier.update",
        update_request_cb,
        0
    },
    {   FLUX_MSGTYPE_REQUEST,
        "barrier.disconnect",
        disconnect_request_cb,
        FLUX_ROLE_USER,
    },
    {   FLUX_MSGTYPE_EVENT,
        "barrier.exit",
        exit_event_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    struct barrier_ctx *ctx;
    flux_msg_handler_t **handlers = NULL;

    if (!(ctx = barrier_ctx_create (h))) {
        flux_log_error (h, "barrier_ctx_create");
        goto done;
    }
    if (flux_event_subscribe (h, "barrier.") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, ctx, &handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_delvec;
    }
    rc = 0;
done_delvec:
    flux_msg_handler_delvec (handlers);
done:
    barrier_ctx_destroy (ctx);
    return rc;
}

MOD_NAME ("barrier");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
