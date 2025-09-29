/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* publisher.c - manage subscriptions
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <flux/core.h>

#include "src/common/librouter/subhash.h"
#include "ccan/str/str.h"

#include "modhash.h"
#include "publisher.h"


struct publisher {
    struct broker *ctx;
    flux_msg_handler_t **handlers;
};

static void subscribe_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct publisher *pub = arg;
    const char *uuid;
    const char *topic;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        goto error;
    if ((uuid = flux_msg_route_first (msg))) {
        module_t *p;
        if (!(p = modhash_lookup (pub->ctx->modhash, uuid))
            || module_subscribe (p, topic) < 0)
            goto error;
    }
    else {
        if (subhash_subscribe (pub->ctx->sub, topic) < 0)
            goto error;
    }
    if (!flux_msg_is_noresponse (msg)
        && flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to subscribe request");
    return;
error:
    if (!flux_msg_is_noresponse (msg)
        && flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to subscribe request");
}

static void unsubscribe_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct publisher *pub = arg;
    const char *uuid;
    const char *topic;

    if (flux_request_unpack (msg, NULL, "{ s:s }", "topic", &topic) < 0)
        goto error;
    if ((uuid = flux_msg_route_first (msg))) {
        module_t *p;
        if (!(p = modhash_lookup (pub->ctx->modhash, uuid))
            || module_unsubscribe (p, topic) < 0)
            goto error;
    }
    else {
        if (subhash_unsubscribe (pub->ctx->sub, topic) < 0)
            goto error;
    }
    if (!flux_msg_is_noresponse (msg)
        && flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to unsubscribe request");
    return;
error:
    if (!flux_msg_is_noresponse (msg)
        && flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to unsubscribe request");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "event.subscribe",  subscribe_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "event.unsubscribe",  unsubscribe_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void publisher_destroy (struct publisher *pub)
{
    if (pub) {
        int saved_errno = errno;
        flux_msg_handler_delvec (pub->handlers);
        free (pub);
        errno = saved_errno;
    }
}

struct publisher *publisher_create (struct broker *ctx)
{
    struct publisher *pub;

    if (!(pub = calloc (1, sizeof (*pub))))
        return NULL;
    pub->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, pub, &pub->handlers) < 0) {
        publisher_destroy (pub);
        return NULL;
    }
    return pub;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
