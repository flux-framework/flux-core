/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* evenlog-watcher - track eventlog changes */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/fluid.h"

/* Module state.
 */
struct proxy_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    zlist_t *lookups;
};

/* Lookup context
 */
struct lookup_ctx {
    struct proxy_ctx *ctx;
    flux_msg_t *msg;
    int flags;
    flux_future_t *f;
};

void lookup_ctx_destroy (void *data)
{
    if (data) {
        struct lookup_ctx *ctx = data;
        flux_msg_destroy (ctx->msg);
        flux_future_destroy (ctx->f);
        free (ctx);
    }
}

struct lookup_ctx *lookup_ctx_create (struct proxy_ctx *ctx,
                                      const flux_msg_t *msg,
                                      int flags)
{
    struct lookup_ctx *l = calloc (1, sizeof (*l));
    int saved_errno;

    if (!l)
        return NULL;

    l->ctx = ctx;
    l->flags = flags;

    if (!(l->msg = flux_msg_copy (msg, true))) {
        flux_log_error (ctx->h, "%s: flux_msg_copy", __FUNCTION__);
        goto error;
    }

    return l;

error:
    saved_errno = errno;
    lookup_ctx_destroy (l);
    errno = saved_errno;
    return NULL;
}

/* 'pp' is an in/out parameter pointing to input buffer.
 * Set 'tok' to next \n-terminated token, and 'toklen' to its length.
 * Advance 'pp' past token.  Returns false when input is exhausted.
 */
static bool eventlog_parse_next (const char **pp, const char **tok,
                                 size_t *toklen)
{
    char *term;

    if (!(term = strchr (*pp, '\n')))
        return false;
    *tok = *pp;
    *toklen = term - *pp + 1;
    *pp = term + 1;
    return true;
}

static void lookup_continuation (flux_future_t *f, void *arg)
{
    struct lookup_ctx *l = arg;
    struct proxy_ctx *ctx = l->ctx;
    const char *s;
    const char *input;
    const char *tok;
    size_t toklen;

    if (flux_kvs_lookup_get (f, &s) < 0) {
        if (errno != ENODATA)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_get", __FUNCTION__);
        goto error;
    }

    input = s;
    while (eventlog_parse_next (&input, &tok, &toklen)) {
        if (flux_respond_pack (ctx->h, l->msg, "{s:s#}", "event", tok, toklen) < 0) {
            flux_log_error (ctx->h, "%s: flux_respond_pack", __FUNCTION__);
            goto error;
        }
    }

    /* if not watching, this is the only lookup_continuation we're
     * going to get, return ENODATA to indicate end */
    if (!(l->flags & FLUX_KVS_EVENTLOG_WATCH)) {
        errno = ENODATA;
        goto error;
    }
    else
        flux_future_reset (l->f);

    return;

error:
    /* flux future destroyed in lookup_ctx_destroy, which is called
     * via zlist_remove() */
    if (flux_respond_error (ctx->h, l->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
    zlist_remove (ctx->lookups, l);
}

static void lookup_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct proxy_ctx *ctx = arg;
    struct lookup_ctx *l = NULL;
    const char *key;
    int flags;
    int lookup_flags = 0;

    if (flux_request_unpack (msg, NULL, "{s:s s:i}",
                             "key", &key,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(l = lookup_ctx_create (ctx, msg, flags)))
        goto error;

    if (flags & FLUX_KVS_EVENTLOG_WATCH) {
        lookup_flags |= FLUX_KVS_WATCH;
        lookup_flags |= FLUX_KVS_WATCH_APPEND;
    }

    if (!(l->f = flux_kvs_lookup (h, NULL, lookup_flags, key))) {
        flux_log_error (h, "%s: flux_kvs_lookup", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (l->f, -1, lookup_continuation, l) < 0) {
        flux_log_error (h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    if (zlist_append (ctx->lookups, l) < 0) {
        flux_log_error (h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    zlist_freefn (ctx->lookups, l, lookup_ctx_destroy, true);
    l = NULL;

    return;

error:
    lookup_ctx_destroy (l);
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* Cancel lookup 'l' if it matches (sender, matchtag).
 * matchtag=FLUX_MATCHTAG_NONE matches any matchtag.
 */
static void lookup_cancel (struct proxy_ctx *ctx,
                           struct lookup_ctx *l,
                           const char *sender, uint32_t matchtag)
{
    uint32_t t;
    char *s;

    if (matchtag != FLUX_MATCHTAG_NONE
        && (flux_msg_get_matchtag (l->msg, &t) < 0 || matchtag != t))
        return;
    if (flux_msg_get_route_first (l->msg, &s) < 0)
        return;
    if (!strcmp (sender, s)) {
        if (flux_respond_error (ctx->h, l->msg, ENODATA, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        zlist_remove (ctx->lookups, l);
    }
    free (s);
}

/* Cancel all lookups that match (sender, matchtag). */
static void lookups_cancel (struct proxy_ctx *ctx,
                            const char *sender, uint32_t matchtag)
{
    struct lookup_ctx *l;

    l = zlist_first (ctx->lookups);
    while (l) {
        lookup_cancel (ctx, l, sender, matchtag);
        l = zlist_next (ctx->lookups);
    }
}

static void cancel_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct proxy_ctx *ctx = arg;
    uint32_t matchtag;
    char *sender;

    if (flux_request_unpack (msg, NULL, "{s:i}", "matchtag", &matchtag) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        return;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (h, "%s: flux_msg_get_route_first", __FUNCTION__);
        return;
    }
    lookups_cancel (ctx, sender, matchtag);
    free (sender);
}

static void disconnect_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    struct proxy_ctx *ctx = arg;
    char *sender;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        flux_log_error (h, "%s: flux_request_decode", __FUNCTION__);
        return;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (h, "%s: flux_msg_get_route_first", __FUNCTION__);
        return;
    }
    lookups_cancel (ctx, sender, FLUX_MATCHTAG_NONE);
    free (sender);
}

static void stats_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct proxy_ctx *ctx = arg;

    if (flux_respond_pack (h, msg, "{s:i}",
                           "lookups", zlist_size (ctx->lookups)) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static const struct flux_msg_handler_spec htab[] = {
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "eventlog-proxy.lookup",
      .cb           = lookup_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "eventlog-proxy.cancel",
      .cb           = cancel_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "eventlog-proxy.disconnect",
      .cb           = disconnect_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "eventlog-proxy.stats.get",
      .cb           = stats_cb,
      .rolemask     = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void proxy_ctx_destroy (struct proxy_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        if (ctx->lookups)
            zlist_destroy (&ctx->lookups);
        free (ctx);
        errno = saved_errno;
    }
}

static struct proxy_ctx *proxy_ctx_create (flux_t *h)
{
    struct proxy_ctx *ctx = calloc (1, sizeof (*ctx));
    if (!ctx)
        return NULL;
    ctx->h = h;
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (!(ctx->lookups = zlist_new ()))
        goto error;
    return ctx;
error:
    proxy_ctx_destroy (ctx);
    return NULL;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct proxy_ctx *ctx;
    int rc = -1;

    if (!(ctx = proxy_ctx_create (h))) {
        flux_log_error (h, "initialization error");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    proxy_ctx_destroy (ctx);
    return rc;
}

MOD_NAME ("eventlog-proxy");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
