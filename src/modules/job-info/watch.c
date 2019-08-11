/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* watch.c - handle job-info.eventlog-watch &
 * job-info.eventlog-watch-cancel for job-info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <flux/core.h>

#include "info.h"
#include "watch.h"
#include "allow.h"

struct watch_ctx {
    struct info_ctx *ctx;
    flux_msg_t *msg;
    flux_jobid_t id;
    flux_future_t *f;
    int offset;
    bool allow;
    bool cancel;
};

static void watch_continuation (flux_future_t *f, void *arg);

static void watch_ctx_destroy (void *data)
{
    if (data) {
        struct watch_ctx *ctx = data;
        flux_msg_destroy (ctx->msg);
        flux_future_destroy (ctx->f);
        free (ctx);
    }
}

static struct watch_ctx *watch_ctx_create (struct info_ctx *ctx,
                                           const flux_msg_t *msg,
                                           flux_jobid_t id)
{
    struct watch_ctx *w = calloc (1, sizeof (*w));
    int saved_errno;

    if (!w)
        return NULL;

    w->ctx = ctx;
    w->id = id;

    if (!(w->msg = flux_msg_copy (msg, true))) {
        flux_log_error (ctx->h, "%s: flux_msg_copy", __FUNCTION__);
        goto error;
    }

    return w;

error:
    saved_errno = errno;
    watch_ctx_destroy (w);
    errno = saved_errno;
    return NULL;
}

static int watch_key (struct watch_ctx *w)
{
    char key[64];
    int flags = (FLUX_KVS_WATCH | FLUX_KVS_WATCH_APPEND);

    if (w->f) {
        flux_future_destroy (w->f);
        w->f = NULL;
    }

    if (flux_job_kvs_key (key, sizeof (key), w->id, "eventlog") < 0) {
        flux_log_error (w->ctx->h, "%s: flux_job_kvs_key", __FUNCTION__);
        return -1;
    }

    if (!(w->f = flux_kvs_lookup (w->ctx->h, NULL, flags, key))) {
        flux_log_error (w->ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        return -1;
    }

    if (flux_future_then (w->f, -1, watch_continuation, w) < 0) {
        /* w->f cleanup handled in context destruction */
        flux_log_error (w->ctx->h, "%s: flux_future_then", __FUNCTION__);
        return -1;
    }

    return 0;
}

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

static void watch_continuation (flux_future_t *f, void *arg)
{
    struct watch_ctx *w = arg;
    struct info_ctx *ctx = w->ctx;
    const char *s;
    const char *input;
    const char *tok;
    size_t toklen;

    if (flux_kvs_lookup_get (f, &s) < 0) {
        if (errno == ENODATA) {
            if (flux_respond_error (ctx->h, w->msg, ENODATA, NULL) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
            goto done;
        }
        else if (errno != ENOENT)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_get", __FUNCTION__);
        goto error;
    }

    if (w->cancel) {
        if (flux_respond_error (ctx->h, w->msg, ENODATA, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        goto done;
    }

    if (!w->allow) {
        if (eventlog_allow (ctx, w->msg, s) < 0)
            goto error;
        w->allow = true;
    }

    input = s;
    while (eventlog_parse_next (&input, &tok, &toklen)) {
        if (flux_respond_pack (ctx->h, w->msg,
                               "{s:s#}",
                               "event", tok, toklen) < 0) {
            flux_log_error (ctx->h, "%s: flux_respond_pack",
                            __FUNCTION__);
            goto error;
        }
    }

    flux_future_reset (f);
    return;

error:
    if (flux_respond_error (ctx->h, w->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
done:
    /* flux future destroyed in watch_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->watchers, w);
}

void watch_cb (flux_t *h, flux_msg_handler_t *mh,
               const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    struct watch_ctx *w = NULL;
    flux_jobid_t id;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errmsg = "eventlog-watch request rejected without streaming RPC flag";
        goto error;
    }
    if (!(w = watch_ctx_create (ctx, msg, id)))
        goto error;

    if (watch_key (w) < 0)
        goto error;

    if (zlist_append (ctx->watchers, w) < 0) {
        flux_log_error (h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    zlist_freefn (ctx->watchers, w, watch_ctx_destroy, true);
    w = NULL;

    return;

error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    watch_ctx_destroy (w);
}

/* Cancel watch 'w' if it matches (sender, matchtag).
 * matchtag=FLUX_MATCHTAG_NONE matches any matchtag.
 */
static void watch_cancel (struct info_ctx *ctx,
                          struct watch_ctx *w,
                          const char *sender, uint32_t matchtag)
{
    uint32_t t;
    char *s;

    if (matchtag != FLUX_MATCHTAG_NONE
        && (flux_msg_get_matchtag (w->msg, &t) < 0 || matchtag != t))
        return;
    if (flux_msg_get_route_first (w->msg, &s) < 0)
        return;
    if (!strcmp (sender, s)) {
        if (flux_kvs_lookup_cancel (w->f) < 0)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_cancel",
                            __FUNCTION__);
        w->cancel = true;
    }
    free (s);
}

void watchers_cancel (struct info_ctx *ctx,
                      const char *sender, uint32_t matchtag)
{
    struct watch_ctx *w;

    w = zlist_first (ctx->watchers);
    while (w) {
        watch_cancel (ctx, w, sender, matchtag);
        w = zlist_next (ctx->watchers);
    }
}

void watch_cancel_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
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
    watchers_cancel (ctx, sender, matchtag);
    free (sender);
}

void watch_cleanup (struct info_ctx *ctx)
{
    struct watch_ctx *w;

    while ((w = zlist_pop (ctx->watchers))) {
        if (flux_kvs_lookup_cancel (w->f) < 0)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_cancel",
                                    __FUNCTION__);

        if (flux_respond_error (ctx->h, w->msg, ENOSYS, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error",
                            __FUNCTION__);
        watch_ctx_destroy (w);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
