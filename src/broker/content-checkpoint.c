/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
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
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "content-checkpoint.h"
#include "content-cache.h"

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
    const char *s;

    assert (msg);

    if (flux_rpc_get (f, &s) < 0)
        goto error;

    if (flux_respond (checkpoint->h, msg, s) < 0)
        flux_log_error (checkpoint->h, "error responding to checkpoint-get");
    flux_future_destroy (f);
    return;

error:
    if (flux_respond_error (checkpoint->h, msg, errno, NULL) < 0)
        flux_log_error (checkpoint->h, "error responding to checkpoint-get");
    flux_future_destroy (f);
}

void content_checkpoint_get_request (flux_t *h, flux_msg_handler_t *mh,
                                     const flux_msg_t *msg, void *arg)
{
    struct content_checkpoint *checkpoint = arg;
    const char *topic = "content.checkpoint-get";
    uint32_t rank = FLUX_NODEID_UPSTREAM;
    const char *s = NULL;
    const flux_msg_t *msgcpy = flux_msg_incref (msg);
    flux_future_t *f = NULL;

    if (checkpoint->rank == 0) {
        if (!content_cache_backing_loaded (checkpoint->cache)) {
            errno = ENOSYS;
            goto error;
        }
        topic = "content-backing.checkpoint-get";
        rank = 0;
    }

    if (flux_request_decode (msg, NULL, &s) < 0)
        goto error;

    if (!(f = flux_rpc (h, topic, s, rank, 0))
        || flux_future_aux_set (f,
                                "msg",
                                (void *)msgcpy,
                                (flux_free_f)flux_msg_decref) < 0
        || flux_future_then (f,
                             -1,
                             checkpoint_get_continuation,
                             checkpoint) < 0) {
        flux_log_error (h, "error starting checkpoint-get RPC");
        goto error;
    }

    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to checkpoint-get request");
    flux_future_destroy (f);
    flux_msg_decref (msgcpy);
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

void content_checkpoint_put_request (flux_t *h, flux_msg_handler_t *mh,
                                     const flux_msg_t *msg, void *arg)
{
    struct content_checkpoint *checkpoint = arg;
    const char *topic = "content.checkpoint-put";
    uint32_t rank = FLUX_NODEID_UPSTREAM;
    const char *s = NULL;
    const flux_msg_t *msgcpy = flux_msg_incref (msg);
    flux_future_t *f = NULL;

    if (checkpoint->rank == 0) {
        if (!content_cache_backing_loaded (checkpoint->cache)) {
            errno = ENOSYS;
            goto error;
        }
        topic = "content-backing.checkpoint-put";
        rank = 0;
    }

    if (flux_request_decode (msg, NULL, &s) < 0)
        goto error;

    if (!(f = flux_rpc (h, topic, s, rank, 0))
        || flux_future_aux_set (f,
                                "msg",
                                (void *)msgcpy,
                                (flux_free_f)flux_msg_decref) < 0
        || flux_future_then (f,
                             -1,
                             checkpoint_put_continuation,
                             checkpoint) < 0) {
        flux_log_error (h, "error starting checkpoint-put RPC");
        goto error;
    }

    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to checkpoint-put request");
    flux_future_destroy (f);
    flux_msg_decref (msgcpy);
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
