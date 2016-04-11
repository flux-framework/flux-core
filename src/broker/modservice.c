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
#include <getopt.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdarg.h>
#include <json.h>
#include <argz.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/getrusage_json.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/xzmalloc.h"

#include "module.h"
#include "modservice.h"

typedef struct {
    flux_t h;
    module_t *p;
    zlist_t *handlers;
    flux_watcher_t *w_prepare;
    flux_watcher_t *w_check;
} ctx_t;

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    flux_msg_handler_t *w;
    while ((w = zlist_pop (ctx->handlers)))
        flux_msg_handler_destroy (w);
    zlist_destroy (&ctx->handlers);
    flux_watcher_destroy (ctx->w_prepare);
    flux_watcher_destroy (ctx->w_check);
    free (ctx);
}

static ctx_t *getctx (flux_t h, module_t *p)
{
    ctx_t *ctx = flux_aux_get (h, "flux::modservice");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->handlers = zlist_new ()))
            oom ();
        ctx->h = h;
        ctx->p = p;
        flux_aux_set (h, "flux::modservice", ctx, freectx);
    }
    return ctx;
}


/* Route string will not include the endpoints.
 * FIXME: t/lua/t0002-rpc.t tries to ping with an array object.
 * This should be caught at a lower level - see issue #181
 */
static void ping_cb (flux_t h, flux_msg_handler_t *w,
                     const flux_msg_t *msg, void *arg)
{
    module_t *p = arg;
    const char *json_str;
    JSON o = NULL;
    char *route = NULL;
    char *route_plus_uuid = NULL;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!(o = Jfromstr (json_str))
                    || !json_object_is_type (o, json_type_object)) {
        errno = EPROTO;
        goto done;
    }
    if (!(route = flux_msg_get_route_string (msg)))
        goto done;
    route_plus_uuid = xasprintf ("%s!%.5s", route, module_get_uuid (p));
    Jadd_str (o, "route", route_plus_uuid);
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0,
                                rc < 0 ? NULL : Jtostr (o)) < 0)
        FLUX_LOG_ERROR (h);
    Jput (o);
    if (route_plus_uuid)
        free (route_plus_uuid);
    if (route)
        free (route);
}

static void stats_get_cb (flux_t h, flux_msg_handler_t *w,
                          const flux_msg_t *msg, void *arg)
{
    flux_msgcounters_t mcs;
    JSON out = Jnew ();

    flux_get_msgcounters (h, &mcs);
    Jadd_int (out, "#request (tx)", mcs.request_tx);
    Jadd_int (out, "#request (rx)", mcs.request_rx);
    Jadd_int (out, "#response (tx)", mcs.response_tx);
    Jadd_int (out, "#response (rx)", mcs.response_rx);
    Jadd_int (out, "#event (tx)", mcs.event_tx);
    Jadd_int (out, "#event (rx)", mcs.event_rx);
    Jadd_int (out, "#keepalive (tx)", mcs.keepalive_tx);
    Jadd_int (out, "#keepalive (rx)", mcs.keepalive_rx);

    if (flux_respond (h, msg, 0, Jtostr (out)) < 0)
        FLUX_LOG_ERROR (h);
    Jput (out);
}

static void stats_clear_event_cb (flux_t h, flux_msg_handler_t *w,
                                  const flux_msg_t *msg, void *arg)
{
    flux_clr_msgcounters (h);
}

static void stats_clear_request_cb (flux_t h, flux_msg_handler_t *w,
                                    const flux_msg_t *msg, void *arg)
{
    flux_clr_msgcounters (h);
    if (flux_respond (h, msg, 0, NULL) < 0)
        FLUX_LOG_ERROR (h);
}

static void rusage_cb (flux_t h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    JSON out = NULL;
    int rc = -1;

    if (getrusage_json (RUSAGE_THREAD, &out) < 0)
        goto done;
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0,
                              rc < 0 ? NULL : Jtostr (out)) < 0)
        FLUX_LOG_ERROR (h);
    Jput (out);
}

static void shutdown_cb (flux_t h, flux_msg_handler_t *w,
                         const flux_msg_t *msg, void *arg)
{
    flux_reactor_stop (flux_get_reactor (h));
}

/* Reactor loop is about to block.
 */
static void prepare_cb (flux_reactor_t *r, flux_watcher_t *w,
                        int revents, void *arg)
{
    ctx_t *ctx = arg;
    flux_msg_t *msg = flux_keepalive_encode (0, FLUX_MODSTATE_SLEEPING);
    if (!msg || flux_send (ctx->h, msg, 0) < 0)
        flux_log_error (ctx->h, "error sending keepalive");
    flux_msg_destroy (msg);
}

/* Reactor loop just unblocked.
 */
static void check_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    ctx_t *ctx = arg;
    flux_msg_t *msg = flux_keepalive_encode (0, FLUX_MODSTATE_RUNNING);
    if (!msg || flux_send (ctx->h, msg, 0) < 0)
        flux_log_error (ctx->h, "error sending keepalive");
    flux_msg_destroy (msg);
}

static void register_event (ctx_t *ctx, const char *name,
                            flux_msg_handler_f cb)
{
    struct flux_match match = FLUX_MATCH_EVENT;
    flux_msg_handler_t *w;

    match.topic_glob = xasprintf ("%s.%s", module_get_name (ctx->p), name);
    if (!(w = flux_msg_handler_create (ctx->h, match, cb, ctx->p)))
        err_exit ("flux_msg_handler_create");
    flux_msg_handler_start (w);
    if (zlist_append (ctx->handlers, w) < 0)
        oom ();
    if (flux_event_subscribe (ctx->h, match.topic_glob) < 0)
        err_exit ("%s: flux_event_subscribe %s",
                  __FUNCTION__, match.topic_glob);
    free (match.topic_glob);
}

static void register_request (ctx_t *ctx, const char *name,
                              flux_msg_handler_f cb)
{
    struct flux_match match = FLUX_MATCH_REQUEST;
    flux_msg_handler_t *w;

    match.topic_glob = xasprintf ("%s.%s", module_get_name (ctx->p), name);
    if (!(w = flux_msg_handler_create (ctx->h, match, cb, ctx->p)))
        err_exit ("flux_msg_handler_create");
    flux_msg_handler_start (w);
    if (zlist_append (ctx->handlers, w) < 0)
        oom ();
    free (match.topic_glob);
}

void modservice_register (flux_t h, module_t *p)
{
    ctx_t *ctx = getctx (h, p);
    flux_reactor_t *r = flux_get_reactor (h);

    register_request (ctx, "shutdown", shutdown_cb);
    register_request (ctx, "ping", ping_cb);
    register_request (ctx, "stats.get", stats_get_cb);
    register_request (ctx, "stats.clear", stats_clear_request_cb);
    register_request (ctx, "rusage", rusage_cb);

    register_event   (ctx, "stats.clear", stats_clear_event_cb);

    if (!(ctx->w_prepare = flux_prepare_watcher_create (r, prepare_cb, ctx)))
        err_exit ("flux_prepare_watcher_create");
    if (!(ctx->w_check = flux_check_watcher_create (r, check_cb, ctx)))
        err_exit ("flux_check_watcher_create");
    flux_watcher_start (ctx->w_prepare);
    flux_watcher_start (ctx->w_check);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
