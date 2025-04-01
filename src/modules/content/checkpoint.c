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
    zhashx_t *hash;
    unsigned int hash_dirty;
};

struct checkpoint_data {
    struct content_checkpoint *checkpoint;
    json_t *value;
    uint8_t dirty:1;
    bool in_progress;
    int refcount;
};

static struct checkpoint_data *
checkpoint_data_incref (struct checkpoint_data *data)
{
    if (data)
        data->refcount++;
    return data;
}

static void checkpoint_data_decref (struct checkpoint_data *data)
{
    if (data && --data->refcount == 0) {
        if (data->dirty)
            data->checkpoint->hash_dirty--;
        json_decref (data->value);
        free (data);
    }
}

/* zhashx_destructor_fn */
static void checkpoint_data_decref_wrapper (void **arg)
{
    if (arg) {
        struct checkpoint_data *data = *arg;
        checkpoint_data_decref (data);
    }
}

static struct checkpoint_data *
checkpoint_data_create (struct content_checkpoint *checkpoint,
                        json_t *value)
{
    struct checkpoint_data *data = NULL;

    if (!(data = calloc (1, sizeof (*data))))
        return NULL;
    data->checkpoint = checkpoint;
    data->value = json_incref (value);
    data->refcount = 1;
    return data;
}

static int checkpoint_data_update (struct content_checkpoint *checkpoint,
                                   const char *key,
                                   json_t *value)
{
    struct checkpoint_data *data = NULL;

    if (!(data = checkpoint_data_create (checkpoint, value)))
        return -1;

    zhashx_update (checkpoint->hash, key, data);
    data->dirty = 1;
    checkpoint->hash_dirty++;
    return 0;
}

static void checkpoint_get_continuation (flux_future_t *f, void *arg)
{
    struct content_checkpoint *checkpoint = arg;
    const flux_msg_t *msg = flux_future_aux_get (f, "msg");
    const char *key;
    json_t *value = NULL;

    assert (msg);

    if (flux_request_unpack (msg, NULL, "{s:s}", "key", &key) < 0)
        goto error;

    if (flux_rpc_get_unpack (f, "{s:o}", "value", &value) < 0)
        goto error;

    if (checkpoint_data_update (checkpoint, key, value) < 0)
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

void content_checkpoint_get_request (flux_t *h, flux_msg_handler_t *mh,
                                     const flux_msg_t *msg, void *arg)
{
    struct content_checkpoint *checkpoint = arg;
    const char *key;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:s}", "key", &key) < 0)
        goto error;

    if (checkpoint->rank == 0
        && !content_cache_backing_loaded (checkpoint->cache)) {
        struct checkpoint_data *data = zhashx_lookup (checkpoint->hash, key);
        if (!data) {
            errstr = "checkpoint key unavailable";
            errno = ENOENT;
            goto error;
        }
        if (flux_respond_pack (h, msg,
                               "{s:O}",
                               "value", data->value) < 0)
            flux_log_error (h, "error responding to checkpoint-get");
        return;
    }

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

    if (!(f = flux_rpc_pack (checkpoint->h, topic, rank, 0,
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

void content_checkpoint_put_request (flux_t *h, flux_msg_handler_t *mh,
                                     const flux_msg_t *msg, void *arg)
{
    struct content_checkpoint *checkpoint = arg;
    const char *key;
    json_t *value;
    const char *errstr = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:o}",
                             "key", &key,
                             "value", &value) < 0)
        goto error;

    if (checkpoint->rank == 0) {
        if (checkpoint_data_update (checkpoint, key, value) < 0)
            goto error;

        if (!content_cache_backing_loaded (checkpoint->cache)) {
            if (flux_respond (h, msg, NULL) < 0)
                flux_log_error (checkpoint->h, "error responding to checkpoint-put");
            return;
        }
    }

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

static void checkpoint_flush_continuation (flux_future_t *f, void *arg)
{
    struct checkpoint_data *data = arg;
    int rv;

    assert (data);
    if ((rv = flux_rpc_get (f, NULL)) < 0)
        flux_log_error (data->checkpoint->h, "checkpoint flush rpc");
    if (!rv) {
        data->dirty = 0;
        data->checkpoint->hash_dirty--;
    }
    data->in_progress = false;
    checkpoint_data_decref (data);
    flux_future_destroy (f);
}

static int checkpoint_flush (struct content_checkpoint *checkpoint,
                             struct checkpoint_data *data)
{
    if (data->dirty && !data->in_progress) {
        const char *key = zhashx_cursor (checkpoint->hash);
        const char *topic = "content-backing.checkpoint-put";
        flux_future_t *f;
        if (!(f = flux_rpc_pack (checkpoint->h, topic, 0, 0,
                                 "{s:s s:O}",
                                 "key", key,
                                 "value", data->value))
            || flux_future_then (f,
                                 -1,
                                 checkpoint_flush_continuation,
                                 (void *)checkpoint_data_incref (data)) < 0) {
            flux_log_error (checkpoint->h, "%s: checkpoint flush", __FUNCTION__);
            flux_future_destroy (f);
            return -1;
        }
        data->in_progress = true;
    }
    return 0;
}

int checkpoints_flush (struct content_checkpoint *checkpoint)
{
    int last_errno = 0;
    int rc = 0;

    if (checkpoint->hash_dirty > 0) {
        struct checkpoint_data *data = zhashx_first (checkpoint->hash);
        while (data) {
            if (checkpoint_flush (checkpoint, data) < 0) {
                last_errno = errno;
                rc = -1;
                /* A few errors we will consider "unrecoverable", so
                 * break out */
                if (errno == ENOSYS
                    || errno == ENOMEM)
                    break;
            }
            data = zhashx_next (checkpoint->hash);
        }
    }
    if (rc < 0)
        errno = last_errno;
    return rc;
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
        zhashx_destroy (&checkpoint->hash);
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

    if (!(checkpoint->hash = zhashx_new ()))
        goto nomem;
    zhashx_set_destructor (checkpoint->hash, checkpoint_data_decref_wrapper);

    if (flux_msg_handler_addvec (h, htab, checkpoint, &checkpoint->handlers) < 0)
        goto error;
    return checkpoint;

nomem:
    errno = ENOMEM;
error:
    content_checkpoint_destroy (checkpoint);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
