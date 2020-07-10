/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* join.c - wait for TBON parent to become ready (or error)
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/log.h"

#include "broker.h"
#include "state_machine.h"

#include "join.h"

const double join_timeout = 5;

struct join {
    struct broker *ctx;
    flux_msg_handler_t **handlers;
    flux_watcher_t *timer;
    flux_future_t *f_wait;
    zlist_t *waiters;
};

/* Notify any waiters that RUN state has been entered or surpassed.
 */
void join_notify (struct join *join, broker_state_t state)
{
    flux_t *h = join->ctx->h;
    const flux_msg_t *msg;
    int rc;

    while ((msg = zlist_pop (join->waiters))) {
        if (state == STATE_RUN)
            rc = flux_respond (h, msg, NULL);
        else
            rc = flux_respond_error (h,
                                     msg,
                                     ENODATA,
                                     "parent broker is shutting down");
        if (rc < 0)
            flux_log_error (h, "error responding to join.wait-ready");
        flux_msg_decref (msg);
    }
}

static void wait_ready_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct join *join = arg;
    const char *errstr = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        errstr = "error decoding join.wait-ready request";
        goto error;
    }
    switch (state_machine_get_state (join->ctx->state_machine)) {
        /* STATE_RUN has not yet been reached - enqueue request for later
         * processing by join_notify().
         */
        case STATE_NONE:
        case STATE_JOIN:
        case STATE_INIT:
            if (zlist_append (join->waiters,
                              (void *)flux_msg_incref (msg)) < 0) {
                flux_msg_decref (msg);
                errstr = "unable to enqueue join.wait-ready request";
                errno = ENOMEM;
                goto error;
            }
            break;
        /* Currently in STATE_RUN - respond immediately with success.
         */
        case STATE_RUN:
            if (flux_respond (h, msg, NULL) < 0)
                flux_log_error (h, "error responding to join.wait-ready");
            break;
        /* STATE_RUN was surpassed - respond immediately with error.
         */
        default:
            errno = ENODATA;
            errstr = "parent broker is shutting down";
            goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to join.wait-ready");
}

static void join_stop (struct join *join)
{
    flux_future_destroy (join->f_wait);
    join->f_wait = NULL;
    flux_watcher_stop (join->timer);
}

static void join_timeout_cb (flux_reactor_t *r,
                             flux_watcher_t *w,
                             int revents,
                             void *arg)
{
    struct join *join = arg;

    state_machine_post (join->ctx->state_machine, "parent-timeout");
    join_stop (join);
}

static void wait_ready_continuation (flux_future_t *f, void *arg)
{
    struct join *join = arg;

    if (flux_rpc_get (f, NULL) < 0)
        state_machine_post (join->ctx->state_machine, "parent-fail");
    else
        state_machine_post (join->ctx->state_machine, "parent-ready");
    join_stop (join);
}

int join_start (struct join *join)
{
    flux_future_t *f;

    if (!(f = flux_rpc (join->ctx->h,
                        "join.wait-ready",
                        NULL,
                        FLUX_NODEID_UPSTREAM,
                        0)))
        return -1;
    if (flux_future_then (f, -1, wait_ready_continuation, join) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_watcher_start (join->timer);
    join->f_wait = f;
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "join.wait-ready", wait_ready_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void join_destroy (struct join *join)
{
    if (join) {
        int saved_errno = errno;
        flux_msg_handler_delvec (join->handlers);
        flux_future_destroy (join->f_wait);
        if (join->waiters) {
            const flux_msg_t *msg;
            while ((msg = zlist_pop (join->waiters)))
                flux_msg_decref (msg);
            zlist_destroy (&join->waiters);
        }
        flux_watcher_destroy (join->timer);
        free (join);
        errno = saved_errno;
    }
}

struct join *join_create (struct broker *ctx)
{
    struct join *join;

    if (!(join = calloc (1, sizeof (*join))))
        return NULL;
    join->ctx = ctx;
    if (!(join->waiters = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_msg_handler_addvec (ctx->h, htab, join, &join->handlers) < 0)
        goto error;
    if (!(join->timer= flux_timer_watcher_create (ctx->reactor,
                                                  join_timeout,
                                                  0.,
                                                  join_timeout_cb,
                                                  join)))
        goto error;
    return join;
error:
    join_destroy (join);
    return NULL;
}


/*
 * vi:ts=4 sw=4 expandtab
 */
