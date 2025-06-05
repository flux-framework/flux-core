/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* See RFC 10 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <inttypes.h>
#include <assert.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "checkpoint.h"
#include "cache.h"

struct content_checkpoint {
    flux_t *h;
    flux_msg_handler_t **handlers;
    uint32_t rank;
    struct content_cache *cache;
};

static void checkpoint_get_continuation (flux_future_t *f, void *arg)
{
    struct content_checkpoint *checkpoint = arg;
    const flux_msg_t *msg = flux_future_aux_get (f, "msg");
    json_t *value = NULL;

    assert (msg);

    if (flux_rpc_get_unpack (f, "{s:o}", "value", &value) < 0)
        goto error;

    if (flux_respond_pack (checkpoint->h, msg, "{s:O}", "value", value) < 0)
        flux_log_error (checkpoint->h, "error responding to checkpoint-get");

    flux_future_destroy (f);
    return;

error:
    if (flux_respond_error (checkpoint->h, msg, errno, NULL) < 0)
        flux_log_error (checkpoint->h, "error responding to checkpoint-get");
    flux_future_destroy (f);
}

static int checkpoint_get_forward (struct content_checkpoint *checkpoint,
                                   const flux_msg_t *msg,
                                   const char *key,
                                   const char **errstr)
{
    const char *topic = "content.checkpoint-get";
    uint32_t rank = FLUX_NODEID_UPSTREAM;
    flux_future_t *f = NULL;

    /* if we're on rank 0, go directly to backing store */
    if (checkpoint->rank == 0) {
        topic = "content-backing.checkpoint-get";
        rank = 0;
    }

    if (!(f = flux_rpc_pack (checkpoint->h,
                             topic,
                             rank,
                             0,
                             "{s:s}",
                             "key", key))
        || flux_future_then (f,
                             -1,
                             checkpoint_get_continuation,
                             checkpoint) < 0)
        goto error;

    if (flux_future_aux_set (f,
                             "msg",
                             (void *)flux_msg_incref (msg),
                             (flux_free_f)flux_msg_decref) < 0) {
        flux_msg_decref (msg);
        goto error;
    }

    return 0;

error:
    (*errstr) = "error starting checkpoint-get RPC";
    flux_future_destroy (f);
    return -1;
}

void content_checkpoint_get_request (flux_t *h,
                                     flux_msg_handler_t *mh,
                                     const flux_msg_t *msg,
                                     void *arg)
{
    struct content_checkpoint *checkpoint = arg;
    const char *key;
    const char *errstr = NULL;

    if (checkpoint->rank == 0
        && !content_cache_backing_loaded (checkpoint->cache)) {
        errstr = "checkpoint get unavailable, no backing store";
        errno = ENOSYS;
        goto error;
    }

    if (flux_request_unpack (msg, NULL, "{s:s}", "key", &key) < 0)
        goto error;

    if (checkpoint_get_forward (checkpoint,
                                msg,
                                key,
                                &errstr) < 0)
        goto error;

    return;

error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to checkpoint-get request");
}

static void checkpoint_put_continuation (flux_future_t *f, void *arg)
{
    struct content_checkpoint *checkpoint = arg;
    const flux_msg_t *msg = flux_future_aux_get (f, "msg");
    const char *s;

    assert (msg);

    if (flux_rpc_get (f, &s) < 0)
        goto error;

    if (flux_respond (checkpoint->h, msg, s) < 0)
        flux_log_error (checkpoint->h, "error responding to checkpoint-put");
    flux_future_destroy (f);
    return;

error:
    if (flux_respond_error (checkpoint->h, msg, errno, NULL) < 0)
        flux_log_error (checkpoint->h, "error responding to checkpoint-put");
    flux_future_destroy (f);
}

static int checkpoint_put_forward (struct content_checkpoint *checkpoint,
                                   const flux_msg_t *msg,
                                   const char *key,
                                   json_t *value,
                                   const char **errstr)
{
    const char *topic = "content.checkpoint-put";
    uint32_t rank = FLUX_NODEID_UPSTREAM;
    flux_future_t *f = NULL;

    /* if we're on rank 0, go directly to backing store */
    if (checkpoint->rank == 0) {
        topic = "content-backing.checkpoint-put";
        rank = 0;
    }

    if (!(f = flux_rpc_pack (checkpoint->h,
                             topic,
                             rank,
                             0,
                             "{s:s s:O}",
                             "key", key,
                             "value", value))
        || flux_future_then (f,
                             -1,
                             checkpoint_put_continuation,
                             checkpoint) < 0)
        goto error;

    if (flux_future_aux_set (f,
                             "msg",
                             (void *)flux_msg_incref (msg),
                             (flux_free_f)flux_msg_decref) < 0) {
        flux_msg_decref (msg);
        goto error;
    }

    return 0;

error:
    (*errstr) = "error starting checkpoint-put RPC";
    flux_future_destroy (f);
    return -1;
}

void content_checkpoint_put_request (flux_t *h,
                                     flux_msg_handler_t *mh,
                                     const flux_msg_t *msg,
                                     void *arg)
{
    struct content_checkpoint *checkpoint = arg;
    const char *key;
    json_t *value;
    const char *errstr = NULL;

    if (checkpoint->rank == 0
        && !content_cache_backing_loaded (checkpoint->cache)) {
        errstr = "checkpoint put unavailable, no backing store";
        errno = ENOSYS;
        goto error;
    }

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:o}",
                             "key", &key,
                             "value", &value) < 0)
        goto error;

    if (checkpoint_put_forward (checkpoint,
                                msg,
                                key,
                                value,
                                &errstr) < 0)
        goto error;

    return;

error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to checkpoint-put request");
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "content.checkpoint-get",
        content_checkpoint_get_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "content.checkpoint-put",
        content_checkpoint_put_request,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void content_checkpoint_destroy (struct content_checkpoint *checkpoint)
{
    if (checkpoint) {
        int saved_errno = errno;
        flux_msg_handler_delvec (checkpoint->handlers);
        free (checkpoint);
        errno = saved_errno;
    }
}

struct content_checkpoint *content_checkpoint_create (
    flux_t *h,
    uint32_t rank,
    struct content_cache *cache)
{
    struct content_checkpoint *checkpoint;

    if (!(checkpoint = calloc (1, sizeof (*checkpoint))))
        return NULL;
    checkpoint->h = h;
    checkpoint->rank = rank;
    checkpoint->cache = cache;

    if (flux_msg_handler_addvec (h, htab, checkpoint, &checkpoint->handlers) < 0)
        goto error;
    return checkpoint;

error:
    content_checkpoint_destroy (checkpoint);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
