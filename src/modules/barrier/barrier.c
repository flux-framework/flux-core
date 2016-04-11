/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

const double barrier_reduction_timeout_sec = 0.001;

typedef struct {
    zhash_t *barriers;
    flux_t h;
    bool timer_armed;
    flux_watcher_t *timer;
    uint32_t rank;
} ctx_t;

typedef struct _barrier_struct {
    char *name;
    int nprocs;
    int count;
    zhash_t *clients;
    ctx_t *ctx;
    int errnum;
} barrier_t;

static int exit_event_send (flux_t h, const char *name, int errnum);
static void timeout_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg);

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    if (ctx) {
        zhash_destroy (&ctx->barriers);
        if (ctx->timer)
            flux_watcher_destroy (ctx->timer);
        free (ctx);
    }
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "flux::barrier");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->barriers = zhash_new ())) {
            errno = ENOMEM;
            goto error;
        }
        if (flux_get_rank (h, &ctx->rank) < 0) {
            flux_log_error (h, "flux_get_rank");
            goto error;
        }
        if (!(ctx->timer = flux_timer_watcher_create (flux_get_reactor (h),
                       barrier_reduction_timeout_sec, 0., timeout_cb, ctx) )) {
            flux_log_error (h, "flux_timer_watacher_create");
            goto error;
        }
        ctx->h = h;
        flux_aux_set (h, "flux::barrier", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    return NULL;
}

static void barrier_destroy (void *arg)
{
    barrier_t *b = arg;

    zhash_destroy (&b->clients);
    free (b->name);
    free (b);
    return;
}

static barrier_t *barrier_create (ctx_t *ctx, const char *name, int nprocs)
{
    barrier_t *b;

    b = xzmalloc (sizeof (barrier_t));
    b->name = xstrdup (name);
    b->nprocs = nprocs;
    if (!(b->clients = zhash_new ()))
        oom ();
    b->ctx = ctx;
    zhash_insert (ctx->barriers, b->name, b);
    zhash_freefn (ctx->barriers, b->name, barrier_destroy);

    return b;
}

static int barrier_add_client (barrier_t *b, char *sender, const flux_msg_t *msg)
{
    flux_msg_t *cpy = flux_msg_copy (msg, true);
    if (!cpy || zhash_insert (b->clients, sender, cpy) < 0)
        return -1;
    zhash_freefn (b->clients, sender, (zhash_free_fn *)flux_msg_destroy);
    return 0;
}

static void send_enter_request (ctx_t *ctx, barrier_t *b)
{
    json_object *o = Jnew ();
    flux_rpc_t *rpc;

    Jadd_str (o, "name", b->name);
    Jadd_int (o, "count", b->count);
    Jadd_int (o, "nprocs", b->nprocs);
    Jadd_int (o, "hopcount", 1);

    if (!(rpc = flux_rpc (ctx->h, "barrier.enter", Jtostr (o),
                          FLUX_NODEID_UPSTREAM, FLUX_RPC_NORESPONSE))) {
        flux_log_error (ctx->h, "sending barrier.enter request");
        goto done;
    }
done:
    if (rpc)
        flux_rpc_destroy (rpc);
    json_object_put (o);
}

/* We have held onto our count long enough.  Send it upstream.
 */

static int timeout_reduction (const char *key, void *item, void *arg)
{
    ctx_t *ctx = arg;
    barrier_t *b = item;

    if (b->count > 0) {
        send_enter_request (ctx, b);
        b->count = 0;
    }
    return 0;
}

/* Barrier entry happens in two ways:
 * - client calling flux_barrier ()
 * - downstream barrier plugin sending count upstream.
 * In the first case only, we track client uuid to handle disconnect and
 * notification upon barrier termination.
 */

static void enter_request_cb (flux_t h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    barrier_t *b;
    json_object *o = NULL;
    char *sender = NULL;
    const char *name;
    int count, nprocs, hopcount;
    const char *json_str;

    if (flux_request_decode (msg, NULL, &json_str) < 0
     		|| flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (ctx->h, "%s: decoding request", __FUNCTION__);
        goto done;
    }
    if (!(o = Jfromstr (json_str))
     	        || !Jget_str (o, "name", &name)
     	        || !Jget_int (o, "count", &count)
     	        || !Jget_int (o, "nprocs", &nprocs)) {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s: decoding request", __FUNCTION__);
        goto done;
    }

    if (!(b = zhash_lookup (ctx->barriers, name)))
        b = barrier_create (ctx, name, nprocs);

    /* Distinguish client (tracked) vs downstream barrier plugin (untracked).
     * A client, distinguished by hopcount > 0, can only enter barrier once.
     */
    if (!Jget_int (o, "hopcount", &hopcount)) {
        if (barrier_add_client (b, sender, msg) < 0) {
            flux_respond (ctx->h, msg, EEXIST, NULL);
            flux_log (ctx->h, LOG_ERR,
                        "abort %s due to double entry by client %s",
                        name, sender);
            if (exit_event_send (ctx->h, b->name, ECONNABORTED) < 0)
                flux_log (ctx->h, LOG_ERR, "exit_event_send: %s", strerror (errno));
            goto done;
        }
    }

    /* If the count has been reached, terminate the barrier;
     * o/w set timer to pass count upstream and zero it here.
     */
    b->count += count;
    if (b->count == b->nprocs) {
        if (exit_event_send (ctx->h, b->name, 0) < 0)
            flux_log (ctx->h, LOG_ERR, "exit_event_send: %s", strerror (errno));
    } else if (ctx->rank > 0 && !ctx->timer_armed) {
        flux_timer_watcher_reset (ctx->timer, barrier_reduction_timeout_sec, 0.);
        flux_watcher_start (ctx->timer);
        ctx->timer_armed = true;
    }
done:
    if (o)
        json_object_put (o);
    if (sender)
        free (sender);
}

/* Upon client disconnect, abort any pending barriers it was
 * participating in.
 */

static int disconnect (const char *key, void *item, void *arg)
{
    barrier_t *b = item;
    ctx_t *ctx = b->ctx;
    char *sender = arg;

    if (zhash_lookup (b->clients, sender)) {
        if (exit_event_send (ctx->h, b->name, ECONNABORTED) < 0)
            flux_log (ctx->h, LOG_ERR, "exit_event_send: %s", strerror (errno));
    }
    return 0;
}

static void disconnect_request_cb (flux_t h, flux_msg_handler_t *w,
                                   const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    char *sender;

    if (flux_msg_get_route_first (msg, &sender) < 0)
        return;
    zhash_foreach (ctx->barriers, disconnect, sender);
    free (sender);
}

static int send_enter_response (const char *key, void *item, void *arg)
{
    flux_msg_t *msg = item;
    barrier_t *b = arg;

    flux_respond (b->ctx->h, msg, b->errnum, NULL);
    return 0;
}

static int exit_event_send (flux_t h, const char *name, int errnum)
{
    JSON o = Jnew ();
    flux_msg_t *msg = NULL;
    int rc = -1;

    Jadd_str (o, "name", name);
    Jadd_int (o, "errnum", errnum);
    if (!(msg = flux_event_encode ("barrier.exit", Jtostr (o))))
        goto done;
    if (flux_send (h, msg, 0) < 0)
        goto done;
    rc = 0;
done:
    Jput (o);
    flux_msg_destroy (msg);
    return rc;
}

static void exit_event_cb (flux_t h, flux_msg_handler_t *w,
                           const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    barrier_t *b;
    const char *json_str;
    json_object *o = NULL;
    const char *name;
    int errnum;

    if (flux_event_decode (msg, NULL, &json_str) < 0) {
        flux_log_error (h, "%s: decoding event", __FUNCTION__);
        goto done;
    }
    if (!(o = Jfromstr (json_str))
                || !Jget_str (o, "name", &name)
                || !Jget_int (o, "errnum", &errnum)) {
        errno = EPROTO;
        flux_log_error (h, "%s: decoding event", __FUNCTION__);
        goto done;
    }
    if ((b = zhash_lookup (ctx->barriers, name))) {
        b->errnum = errnum;
        zhash_foreach (b->clients, send_enter_response, b);
        zhash_delete (ctx->barriers, name);
    }
done:
    Jput (o);
}

static void timeout_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    ctx_t *ctx = arg;

    assert (ctx->rank != 0);
    ctx->timer_armed = false; /* one shot */

    zhash_foreach (ctx->barriers, timeout_reduction, ctx);
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,     "barrier.enter",       enter_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "barrier.disconnect",  disconnect_request_cb },
    { FLUX_MSGTYPE_EVENT,       "barrier.exit",        exit_event_cb },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t h, int argc, char **argv)
{
    int rc = -1;
    ctx_t *ctx = getctx (h);

    if (!ctx)
        goto done;
    if (flux_event_subscribe (h, "barrier.") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_unreg;
    }
    rc = 0;
done_unreg:
    flux_msg_handler_delvec (htab);
done:
    return rc;
}

MOD_NAME ("barrier");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
