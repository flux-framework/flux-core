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

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"

const int barrier_reduction_timeout_msec = 1;

typedef struct {
    zhash_t *barriers;
    flux_t h;
    bool timer_armed;
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
static int timeout_cb (flux_t h, void *arg);

static void freectx (ctx_t *ctx)
{
    zhash_destroy (&ctx->barriers);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "barriersrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->barriers = zhash_new ()))
            oom ();
        ctx->h = h;
        flux_aux_set (h, "barriersrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
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

static void free_zmsg (zmsg_t *zmsg)
{
    zmsg_destroy (&zmsg);
}

static int barrier_add_client (barrier_t *b, char *sender, zmsg_t **zmsg)
{
    if (zhash_insert (b->clients, sender, *zmsg) < 0)
        return -1;
    zhash_freefn (b->clients, sender, (zhash_free_fn *)free_zmsg);
    *zmsg = NULL; /* list owns it now */
    return 0;
}

static void send_enter_request (ctx_t *ctx, barrier_t *b)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_string (o, "name", b->name);
    util_json_object_add_int (o, "count", b->count);
    util_json_object_add_int (o, "nprocs", b->nprocs);
    util_json_object_add_int (o, "hopcount", 1);
    flux_request_send (ctx->h, o, "barrier.enter");
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
 * - client calling cmb_barrier ()
 * - downstream barrier plugin sending count upstream.
 * In the first case only, we track client uuid to handle disconnect and
 * notification upon barrier termination.
 */

static int enter_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    barrier_t *b;
    json_object *o = NULL;
    char *sender = NULL;
    const char *name;
    int count, nprocs, hopcount;

    if (flux_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
     || !(sender = flux_msg_sender (*zmsg))
     || util_json_object_get_string (o, "name", &name) < 0
     || util_json_object_get_int (o, "count", &count) < 0
     || util_json_object_get_int (o, "nprocs", &nprocs) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: ignoring bad message", __FUNCTION__);
        goto done;
    }

    if (!(b = zhash_lookup (ctx->barriers, name)))
        b = barrier_create (ctx, name, nprocs);

    /* Distinguish client (tracked) vs downstream barrier plugin (untracked).
     * A client, distinguished by hopcount > 0, can only enter barrier once.
     */
    if (util_json_object_get_int (o, "hopcount", &hopcount) < 0) {
        if (barrier_add_client (b, sender, zmsg) < 0) {
            flux_respond_errnum (ctx->h, zmsg, EEXIST);
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
    } else if (!flux_treeroot (ctx->h) && !ctx->timer_armed) {
        if (flux_tmouthandler_add (h, barrier_reduction_timeout_msec,
                                   true, timeout_cb, ctx) < 0) {
            flux_log (h, LOG_ERR, "flux_tmouthandler_add: %s",strerror (errno));
            goto done;
        }
        ctx->timer_armed = true;
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
    return 0;
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

static int disconnect_request_cb (flux_t h, int typemask, zmsg_t **zmsg,
                                  void *arg)
{
    ctx_t *ctx = arg;
    char *sender = flux_msg_sender (*zmsg);

    if (sender) {
        zhash_foreach (ctx->barriers, disconnect, sender);
        free (sender);
    }
    zmsg_destroy (zmsg);
    return 0;
}

static int send_enter_response (const char *key, void *item, void *arg)
{
    zmsg_t *zmsg = item;
    barrier_t *b = arg;
    zmsg_t *cpy;

    if (!(cpy = zmsg_dup (zmsg)))
        oom ();
    flux_respond_errnum (b->ctx->h, &cpy, b->errnum);
    return 0;
}

static int exit_event_send (flux_t h, const char *name, int errnum)
{
    json_object *o = util_json_object_new_object ();
    int rc;

    util_json_object_add_string (o, "name", name);
    util_json_object_add_int (o, "errnum", errnum);
    rc = flux_event_send (h, o, "barrier.exit");
    json_object_put (o);
    return rc;
}

static int exit_event_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    barrier_t *b;
    json_object *o = NULL;
    const char *name;
    int errnum;

    if (flux_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_string (o, "name", &name) < 0
            || util_json_object_get_int (o, "errnum", &errnum) < 0) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if ((b = zhash_lookup (ctx->barriers, name))) {
        b->errnum = errnum;
        zhash_foreach (b->clients, send_enter_response, b);
        zhash_delete (ctx->barriers, name);
    }
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int timeout_cb (flux_t h, void *arg)
{
    ctx_t *ctx = arg;

    assert (!flux_treeroot (h));
    ctx->timer_armed = false; /* one shot */

    zhash_foreach (ctx->barriers, timeout_reduction, ctx);
    return 0;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,     "barrier.enter",       enter_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "barrier.disconnect",  disconnect_request_cb },
    { FLUX_MSGTYPE_EVENT,       "barrier.exit",        exit_event_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (flux_event_subscribe (h, "barrier.") < 0) {
        err ("%s: flux_event_subscribe", __FUNCTION__);
        return -1;
    }
    if (flux_msghandler_addvec (h, htab, htablen, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_addvec: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("barrier");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
