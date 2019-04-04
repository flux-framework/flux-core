/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* lookup.c - lookup in job-info */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <flux/core.h>

#include "info.h"
#include "lookup.h"
#include "allow.h"
#include "util.h"

struct lookup_ctx {
    struct info_ctx *ctx;
    flux_msg_t *msg;
    flux_jobid_t id;
    char *key;
    int flags;
    bool active;
    flux_future_t *f;
    bool allow;
};

static void lookup_ctx_destroy (void *data)
{
    if (data) {
        struct lookup_ctx *ctx = data;
        flux_msg_destroy (ctx->msg);
        flux_future_destroy (ctx->f);
        free (ctx);
    }
}

static struct lookup_ctx *lookup_ctx_create (struct info_ctx *ctx,
                                             const flux_msg_t *msg,
                                             flux_jobid_t id,
                                             const char *key,
                                             int flags)
{
    struct lookup_ctx *l = calloc (1, sizeof (*l));
    int saved_errno;

    if (!l)
        return NULL;

    l->ctx = ctx;
    l->id = id;
    l->flags = flags;
    l->active = true;

    if (!(l->key = strdup (key))) {
        errno = ENOMEM;
        goto error;
    }

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

static int lookup_key (struct lookup_ctx *l, const char *key,
                       flux_continuation_f c)
{
    char path[64];

    if (l->f) {
        flux_future_destroy (l->f);
        l->f = NULL;
    }

    if (flux_job_kvs_key (path, sizeof (path), l->active, l->id, key) < 0) {
        flux_log_error (l->ctx->h, "%s: flux_job_kvs_key", __FUNCTION__);
        return -1;
    }

    if (!(l->f = flux_kvs_lookup (l->ctx->h, NULL, 0, path))) {
        flux_log_error (l->ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        return -1;
    }

    if (flux_future_then (l->f, -1, c, l) < 0) {
        flux_log_error (l->ctx->h, "%s: flux_future_then", __FUNCTION__);
        return -1;
    }

    return 0;
}

static void lookup_continuation (flux_future_t *f, void *arg)
{
    struct lookup_ctx *l = arg;
    struct info_ctx *ctx = l->ctx;
    const char *s;

    if (flux_kvs_lookup_get (f, &s) < 0) {
        if (errno == ENOENT && l->active) {
            /* transition / try the inactive key */
            l->active = false;
            if (lookup_key (l, l->key, lookup_continuation) < 0)
                goto error;
            return;
        }
        else if (errno != ENOENT)
            flux_log_error (ctx->h, "%s: flux_kvs_lookup_get", __FUNCTION__);
        goto error;
    }

    if (!l->allow) {
        if (eventlog_allow (ctx, l->msg, s) < 0)
            goto error;
        l->allow = true;
    }

    if (flux_respond_pack (ctx->h, l->msg, "{s:s}", l->key, s) < 0) {
        flux_log_error (ctx->h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    /* flux future destroyed in lookup_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->lookups, l);
    return;

error:
    if (flux_respond_error (ctx->h, l->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);

    /* flux future destroyed in lookup_ctx_destroy, which is called
     * via zlist_remove() */
    zlist_remove (ctx->lookups, l);
}

void lookup_cb (flux_t *h, flux_msg_handler_t *mh,
                const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    struct lookup_ctx *l = NULL;
    const char *key;
    flux_jobid_t id;
    int flags;

    if (flux_request_unpack (msg, NULL, "{s:I s:s s:i}",
                             "id", &id,
                             "key", &key,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(l = lookup_ctx_create (ctx, msg, id, key, flags)))
        goto error;

    if (lookup_key (l, l->key, lookup_continuation) < 0)
        goto error;

    if (zlist_append (ctx->lookups, l) < 0) {
        flux_log_error (h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    zlist_freefn (ctx->lookups, l, lookup_ctx_destroy, true);

    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    lookup_ctx_destroy (l);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
