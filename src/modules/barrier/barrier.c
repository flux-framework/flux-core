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

const double barrier_reduction_timeout_sec = 0.001;

struct barrier_ctx {
    zhash_t *barriers;
    flux_t *h;
    bool timer_armed;
    flux_watcher_t *timer;
    uint32_t rank;
};

struct barrier {
    char *name;
    int nprocs;
    int count;
    zhash_t *clients;
    struct barrier_ctx *ctx;
    int errnum;
};

static int exit_event_send (flux_t *h, const char *name, int errnum);
static void timeout_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg);

static void barrier_ctx_destroy (struct barrier_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        zhash_destroy (&ctx->barriers);
        flux_watcher_destroy (ctx->timer);
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
    ctx->timer = flux_timer_watcher_create (flux_get_reactor (h),
                                            barrier_reduction_timeout_sec,
                                            0.,
                                            timeout_cb,
                                            ctx);
    if (!ctx->timer)
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
        free (b);
        errno = saved_errno;
    }
}

static struct barrier *barrier_create (struct barrier_ctx *ctx,
                                       const char *name,
                                       int nprocs)
{
    struct barrier *b;

    if (!(b = calloc (1, sizeof (*b))))
        return NULL;
    if (!(b->name = strdup (name)))
        goto error;
    b->nprocs = nprocs;
    if (!(b->clients = zhash_new ())) {
        errno = ENOMEM;
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
    flux_msg_t *cpy = flux_msg_copy (msg, true);
    if (!cpy || zhash_insert (b->clients, sender, cpy) < 0)
        return -1;
    zhash_freefn (b->clients, sender, (zhash_free_fn *)flux_msg_destroy);
    return 0;
}

static void send_enter_request (struct barrier_ctx *ctx, struct barrier *b)
{
    flux_future_t *f;

    if (!(f = flux_rpc_pack (ctx->h, "barrier.enter", FLUX_NODEID_UPSTREAM,
                             FLUX_RPC_NORESPONSE, "{s:s s:i s:i s:b}",
                             "name", b->name,
                             "count", b->count,
                             "nprocs", b->nprocs,
                             "internal", true))) {
        flux_log_error (ctx->h, "sending barrier.enter request");
        goto done;
    }
done:
    flux_future_destroy (f);
}

/* Barrier entry happens in two ways:
 * - client calling flux_barrier ()
 * - downstream barrier plugin sending count upstream.
 * In the first case only, we track client uuid to handle disconnect and
 * notification upon barrier termination.
 */

static void enter_request_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    struct barrier_ctx *ctx = arg;
    struct barrier *b;
    char *sender = NULL;
    const char *name;
    int count, nprocs, internal;

    if (flux_request_unpack (msg, NULL, "{s:s s:i s:i s:b !}",
                             "name", &name,
                             "count", &count,
                             "nprocs", &nprocs,
                             "internal", &internal) < 0)
        goto error;
    if (flux_msg_get_route_first (msg, &sender) < 0)
        goto error;

    if (!(b = zhash_lookup (ctx->barriers, name))) {
        if (!(b = barrier_create (ctx, name, nprocs)))
            goto error;
        zhash_update (ctx->barriers, b->name, b);
        zhash_freefn (ctx->barriers, b->name, (zhash_free_fn *)barrier_destroy);
        if (ctx->rank == 0)
            flux_log (ctx->h, LOG_DEBUG, "create %s %d", name, nprocs);

    }

    /* Distinguish client (tracked) vs downstream barrier plugin (untracked).
     * A client (internal == false) can only enter barrier once.
     */
    if (internal == false) {
        if (barrier_add_client (b, sender, msg) < 0) {
            flux_log (ctx->h, LOG_ERR, "abort %s due to double entry by %s",
                      name, sender);
            if (exit_event_send (ctx->h, b->name, ECONNABORTED) < 0)
                flux_log_error (ctx->h, "exit_event_send");
            errno = EEXIST;
            goto error;
        }
    }

    /* If the count has been reached, terminate the barrier;
     * o/w set timer to pass count upstream and zero it here.
     */
    b->count += count;
    if (b->count == b->nprocs) {
        if (exit_event_send (ctx->h, b->name, 0) < 0)
            flux_log_error (ctx->h, "exit_event_send");
    }
    else if (ctx->rank > 0 && !ctx->timer_armed) {
        flux_timer_watcher_reset (ctx->timer,
                                  barrier_reduction_timeout_sec,
                                  0.);
        flux_watcher_start (ctx->timer);
        ctx->timer_armed = true;
    }
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
    char *sender;
    const char *key;
    struct barrier *b;

    if (flux_msg_get_route_first (msg, &sender) < 0)
        return;
    FOREACH_ZHASH (ctx->barriers, key, b) {
        if (zhash_lookup (b->clients, sender)) {
            if (exit_event_send (h, b->name, ECONNABORTED) < 0)
                flux_log_error (h, "exit_event_send");
        }
    }
    free (sender);
}

static int exit_event_send (flux_t *h, const char *name, int errnum)
{
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (!(msg = flux_event_pack ("barrier.exit", "{s:s s:i}",
                                 "name", name,
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

    if (flux_event_unpack (msg, NULL, "{s:s s:i !}",
                           "name", &name,
                           "errnum", &errnum) < 0) {
        flux_log_error (h, "%s: decoding event", __FUNCTION__);
        return;
    }
    if ((b = zhash_lookup (ctx->barriers, name))) {
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
        zhash_delete (ctx->barriers, name);
    }
}

static void timeout_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    struct barrier_ctx *ctx = arg;
    const char *key;
    struct barrier *b;

    assert (ctx->rank != 0);
    ctx->timer_armed = false; /* one shot */

    FOREACH_ZHASH (ctx->barriers, key, b) {
        if (b->count > 0) {
            send_enter_request (ctx, b);
            b->count = 0;
        }
    }
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "barrier.enter",       enter_request_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "barrier.disconnect",  disconnect_request_cb, 0 },
    { FLUX_MSGTYPE_EVENT,   "barrier.exit",        exit_event_cb, 0 },
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
