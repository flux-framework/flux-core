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
#include <jansson.h>
#include <flux/core.h>

#include "info.h"
#include "lookup.h"
#include "allow.h"
#include "util.h"

struct lookup_ctx {
    struct info_ctx *ctx;
    flux_msg_t *msg;
    flux_jobid_t id;
    json_t *keys;
    bool check_eventlog;
    int flags;
    bool active;
    flux_future_t *f;
    bool allow;
};

static void info_lookup_continuation (flux_future_t *fall, void *arg);

static void lookup_ctx_destroy (void *data)
{
    if (data) {
        struct lookup_ctx *ctx = data;
        flux_msg_destroy (ctx->msg);
        json_decref (ctx->keys);
        flux_future_destroy (ctx->f);
        free (ctx);
    }
}

static struct lookup_ctx *lookup_ctx_create (struct info_ctx *ctx,
                                             const flux_msg_t *msg,
                                             flux_jobid_t id,
                                             json_t *keys,
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

    if (!(l->keys = json_copy (keys))) {
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

static void lookup_ctx_set_future (struct lookup_ctx *l, flux_future_t *f)
{
    if (l->f)
        flux_future_destroy (l->f);
    l->f = f;
}

static flux_future_t *lookup_key (struct lookup_ctx *l, const char *key,
                                  flux_continuation_f c)
{
    flux_future_t *f = NULL;
    char path[64];

    if (flux_job_kvs_key (path, sizeof (path), l->active, l->id, key) < 0) {
        flux_log_error (l->ctx->h, "%s: flux_job_kvs_key", __FUNCTION__);
        goto error;
    }

    if (!(f = flux_kvs_lookup (l->ctx->h, NULL, 0, path))) {
        flux_log_error (l->ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        goto error;
    }

    if (c) {
        if (flux_future_then (f, -1, c, l) < 0) {
            flux_log_error (l->ctx->h, "%s: flux_future_then", __FUNCTION__);
            goto error;
        }
    }

    return f;

error:
    flux_future_destroy (f);
    return NULL;
}

static flux_future_t *lookup_keys (struct lookup_ctx *l, flux_continuation_f c)
{
    flux_future_t *fall = NULL;
    flux_future_t *f = NULL;
    size_t index;
    json_t *key;

    if (!(fall = flux_future_wait_all_create ())) {
        flux_log_error (l->ctx->h, "%s: flux_wait_all_create", __FUNCTION__);
        goto error;
    }
    flux_future_set_flux (fall, l->ctx->h);

    if (l->check_eventlog) {
        if (!(f = lookup_key (l, "eventlog", NULL)))
            goto error;
        if (flux_future_push (fall, "eventlog", f) < 0) {
            flux_future_destroy (f);
            flux_log_error (l->ctx->h, "%s: flux_future_push", __FUNCTION__);
            goto error;
        }
    }

    json_array_foreach(l->keys, index, key) {
        const char *keystr;
        if (!(keystr = json_string_value (key))) {
            errno = EINVAL;
            goto error;
        }
        if (!(f = lookup_key (l, keystr, NULL)))
            goto error;
        if (flux_future_push (fall, keystr, f) < 0) {
            flux_future_destroy (f);
            flux_log_error (l->ctx->h, "%s: flux_future_push", __FUNCTION__);
            goto error;
        }
    }

    if (flux_future_then (fall, -1, c, l) < 0) {
        flux_log_error (l->ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    return fall;

error:
    flux_future_destroy (fall);
    return NULL;
}

static int check_lookup_error (struct lookup_ctx *l)
{
    if (errno == ENOENT && l->active) {
        flux_future_t *fnext;
        /* transition / try the inactive key */
        l->active = false;
        if (!(fnext = lookup_keys (l, info_lookup_continuation)))
            return -1;
        lookup_ctx_set_future (l, fnext);
        return 0;
    }
    else if (errno != ENOENT)
        flux_log_error (l->ctx->h, "%s: flux_kvs_lookup_get", __FUNCTION__);
    return -1;
}

static void info_lookup_continuation (flux_future_t *fall, void *arg)
{
    struct lookup_ctx *l = arg;
    struct info_ctx *ctx = l->ctx;
    const char *s;
    size_t index;
    json_t *key;
    json_t *o = NULL;
    char *data = NULL;

    if (!l->allow) {
        flux_future_t *f;

        if (!(f = flux_future_get_child (fall, "eventlog"))) {
            flux_log_error (ctx->h, "%s: flux_future_get_child", __FUNCTION__);
            goto error;
        }

        if (flux_kvs_lookup_get (f, &s) < 0) {
            if (check_lookup_error (l) < 0)
                goto error;
            return;
        }

        if (eventlog_allow (ctx, l->msg, s) < 0)
            goto error;
        l->allow = true;
    }

    if (!(o = json_object ()))
        goto enomem;

    json_array_foreach(l->keys, index, key) {
        flux_future_t *f;
        const char *keystr;
        json_t *str = NULL;

        if (!(keystr = json_string_value (key))) {
            errno = EINVAL;
            goto error;
        }

        if (!(f = flux_future_get_child (fall, keystr))) {
            flux_log_error (ctx->h, "%s: flux_future_get_child", __FUNCTION__);
            goto error;
        }

        if (flux_kvs_lookup_get (f, &s) < 0) {
            if (check_lookup_error (l) < 0)
                goto error;
            return;
        }

        if (!(str = json_string (s)))
            goto enomem;

        if (json_object_set_new (o, keystr, str) < 0) {
            json_decref (str);
            goto enomem;
        }
    }

    /* must have been allowed earlier or above, otherwise should have
     * taken error path */
    assert (l->allow);

    if (!(data = json_dumps (o, JSON_COMPACT)))
        goto enomem;

    if (flux_respond (ctx->h, l->msg, 0, data) < 0) {
        flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
        goto error;
    }

    goto done;

enomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (ctx->h, l->msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);

done:
    /* flux future destroyed in lookup_ctx_destroy, which is called
     * via zlist_remove() */
    json_decref (o);
    free (data);
    zlist_remove (ctx->lookups, l);
}

/* If keys array doesn't contain eventlog, flag that we'll need to do
 * an eventlog check.
 */
static int check_keys_for_eventlog (struct lookup_ctx *l)
{
    size_t index;
    json_t *key;

    json_array_foreach(l->keys, index, key) {
        const char *keystr;
        if (!(keystr = json_string_value (key))) {
            errno = EINVAL;
            return -1;
        }
        if (!strcmp (keystr, "eventlog"))
            return 0;
    }

    l->check_eventlog = true;
    return 0;
}

void lookup_cb (flux_t *h, flux_msg_handler_t *mh,
                const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    struct lookup_ctx *l = NULL;
    json_t *keys;
    flux_future_t *f;
    flux_jobid_t id;
    uint32_t rolemask;
    int flags;

    if (flux_request_unpack (msg, NULL, "{s:I s:o s:i}",
                             "id", &id,
                             "keys", &keys,
                             "flags", &flags) < 0) {
        flux_log_error (h, "%s: flux_request_unpack", __FUNCTION__);
        goto error;
    }

    if (!(l = lookup_ctx_create (ctx, msg, id, keys, flags)))
        goto error;

    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;

    /* if rpc from owner, no need to do guest access check */
    if ((rolemask & FLUX_ROLE_OWNER))
        l->allow = true;
    else {
        if (check_keys_for_eventlog (l) < 0)
            goto error;
    }

    if (!(f = lookup_keys (l, info_lookup_continuation)))
        goto error;
    lookup_ctx_set_future (l, f);

    if (zlist_append (ctx->lookups, l) < 0) {
        flux_log_error (h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    zlist_freefn (ctx->lookups, l, lookup_ctx_destroy, true);
    l = NULL;

    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    lookup_ctx_destroy (l);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
