/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <flux/core.h>

#include "resource.h"
#include "inventory.h"
#include "drain.h"
#include "rutil.h"
#include "monitor.h"
#include "exclude.h"
#include "status.h"

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"

struct status {
    struct resource_ctx *ctx;
    flux_msg_handler_t **handlers;
};

static json_t *prepare_response (struct status *status)
{
    struct resource_ctx *ctx = status->ctx;
    const struct idset *down = monitor_get_down (ctx->monitor);
    const struct idset *exclude = exclude_get (ctx->exclude);
    const json_t *R;
    json_t *o = NULL;
    json_t *drain_info = NULL;

    if (!(R = inventory_get (ctx->inventory))
        || !(drain_info = drain_get_info (ctx->drain)))
        goto error;
    if (!(o = json_pack ("{s:O s:O}", "R", R, "drain", drain_info))) {
        errno = ENOMEM;
        goto error;
    }
    if (rutil_set_json_idset (o, "online", monitor_get_up (ctx->monitor)) < 0
        || rutil_set_json_idset (o, "offline", down) < 0
        || rutil_set_json_idset (o, "exclude", exclude) < 0)
        goto error;
    json_decref (drain_info);
    return o;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    ERRNO_SAFE_WRAP (json_decref, drain_info);
    return NULL;
}

static void status_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct status *status = arg;
    json_t *o = NULL;
    flux_error_t error;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        errprintf (&error, "error decoding request: %s", strerror (errno));
        goto error;
    }
    if (status->ctx->rank != 0) {
        errprintf (&error, "this RPC only works on rank 0");
        errno = EPROTO;
        goto error;
    }
    if (!(o = prepare_response (status))) {
        errprintf (&error, "error preparing response: %s", strerror (errno));
        goto error;
    }
    if (flux_respond_pack (h, msg, "O", o) < 0)
        flux_log_error (h, "error responding to resource.status request");
    json_decref (o);
    return;
error:
    if (flux_respond_error (h, msg, errno, error.text) < 0)
        flux_log_error (h, "error responding to resource.status request");
    json_decref (o);
}

static const struct flux_msg_handler_spec htab[] = {
    {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .topic_glob = "resource.status",
        .cb = status_cb,
        .rolemask = FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void status_destroy (struct status *status)
{
    if (status) {
        int saved_errno = errno;
        flux_msg_handler_delvec (status->handlers);
        free (status);
        errno = saved_errno;
    }
}

struct status *status_create (struct resource_ctx *ctx)
{
    struct status *status;

    if (!(status = calloc (1, sizeof (*status))))
        return NULL;
    status->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, status, &status->handlers) < 0)
        goto error;
    return status;
error:
    status_destroy (status);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
