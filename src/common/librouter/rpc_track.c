/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libccan/ccan/str/str.h"

#include "rpc_track.h"
#include "msg_hash.h"

struct rpc_track {
    zhashx_t *hash;
    msg_hash_type_t type;
};

void rpc_track_destroy (struct rpc_track *rt)
{
    if (rt) {
        int saved_errno = errno;
        zhashx_destroy (&rt->hash);
        free (rt);
        errno = saved_errno;
    }
}

struct rpc_track *rpc_track_create (msg_hash_type_t type)
{
    struct rpc_track *rt;

    if (!(rt = calloc (1, sizeof (*rt))))
        return NULL;
    rt->type = type;
    if (!(rt->hash = msg_hash_create (type)))
        goto error;
    return rt;
error:
    rpc_track_destroy (rt);
    return NULL;
}

static bool response_is_error (const flux_msg_t *msg)
{
    int errnum;

    if (flux_msg_get_errnum (msg, &errnum) < 0
        || errnum == 0)
        return false;
    return true;
}

static bool request_is_disconnect (const flux_msg_t *msg)
{
    const char *topic;
    const char *cp;

    if (flux_msg_get_topic (msg, &topic) < 0
        || !(cp = strstr (topic, ".disconnect"))
        || strlen (cp) > 11)
        return false;
    return true;
}

/* Avoid putting messages in the hash that have ambiguous hash keys;
 * specifically, avoid RFC 27 sched alloc RPCs, which are regular RPCs
 * that don't use the matchtag field (setting it to FLUX_MATCHTAG_NONE),
 * instead using payload elements to match requests and responses.
 */
static bool message_is_hashable (const flux_msg_t *msg)
{
    uint32_t matchtag;

    if (flux_msg_get_matchtag (msg, &matchtag) < 0
        || matchtag == FLUX_MATCHTAG_NONE)
        return false;
    return true;
}

static void rpc_track_disconnect (struct rpc_track *rt, const flux_msg_t *msg)
{
    const char *uuid;
    zlistx_t *values;
    const flux_msg_t *req;

    if (!(uuid = flux_msg_route_first (msg))
        || !(values = zhashx_values (rt->hash)))
        return;
    req = zlistx_first (values);
    while (req) {
        const char *uuid2 = flux_msg_route_first (req);
        if (uuid2 && streq (uuid, uuid2))
            zhashx_delete (rt->hash, req);
        req = zlistx_next (values);
    }
    zlistx_destroy (&values);
}

void rpc_track_update (struct rpc_track *rt, const flux_msg_t *msg)
{
    int type;

    if (!rt || !msg || flux_msg_get_type (msg, &type) < 0)
        return;
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            if (message_is_hashable (msg)
                && (!flux_msg_is_streaming (msg) || response_is_error (msg)))
                zhashx_delete (rt->hash, msg);
            break;
        case FLUX_MSGTYPE_REQUEST:
            if (!flux_msg_is_noresponse (msg)
                && message_is_hashable (msg))
                zhashx_insert (rt->hash, msg, (flux_msg_t *)msg);
            else if (request_is_disconnect (msg))
                rpc_track_disconnect (rt, msg);
            break;
        default:
            break;
    }
}

void rpc_track_purge (struct rpc_track *rt, rpc_respond_f fun, void *arg)
{
    const flux_msg_t *msg;

    if (rt) {
        msg = zhashx_first (rt->hash);
        while (msg) {
            if (fun)
                fun (msg, arg);
            msg = zhashx_next (rt->hash);
        }
        zhashx_purge (rt->hash);
    }
}

int rpc_track_count (struct rpc_track *rt)
{
    return rt ? zhashx_size (rt->hash) : 0;
}

// vi:ts=4 sw=4 expandtab
