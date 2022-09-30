/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* idsync.c - code to sync job ids if job-list not yet aware of them */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/job_hash.h"

#include "idsync.h"

void idsync_data_destroy (void *data)
{
    if (data) {
        struct idsync_data *isd = data;
        flux_msg_destroy (isd->msg);
        json_decref (isd->attrs);
        flux_future_destroy (isd->f_lookup);
        free (isd);
    }
}

void idsync_data_destroy_wrapper (void **data)
{
    if (data) {
        struct idsync_data **isd = (struct idsync_data **) data;
        idsync_data_destroy (*isd);
    }
}

struct idsync_data *idsync_data_create (struct list_ctx *ctx,
                                        flux_jobid_t id,
                                        const flux_msg_t *msg,
                                        json_t *attrs,
                                        flux_future_t *f_lookup)
{
    struct idsync_data *isd = NULL;
    int saved_errno;

    isd = calloc (1, sizeof (*isd));
    if (!isd)
        goto error_enomem;
    isd->ctx = ctx;
    isd->id = id;
    if (!(isd->msg = flux_msg_copy (msg, false)))
        goto error;
    isd->attrs = json_incref (attrs);
    isd->f_lookup = f_lookup;
    return isd;

 error_enomem:
    errno = ENOMEM;
 error:
    saved_errno = errno;
    idsync_data_destroy (isd);
    errno = saved_errno;
    return NULL;
}

static void idsync_waits_list_destroy (void **data)
{
    if (data)
        zlistx_destroy ((zlistx_t **) data);
}

struct idsync_ctx *idsync_ctx_create (flux_t *h)
{
    struct idsync_ctx *isctx = NULL;
    int saved_errno;

    if (!(isctx = calloc (1, sizeof (*isctx)))) {
        flux_log_error (h, "calloc");
        return NULL;
    }
    isctx->h = h;

    if (!(isctx->lookups = zlistx_new ()))
        goto error;

    zlistx_set_destructor (isctx->lookups, idsync_data_destroy_wrapper);

    if (!(isctx->waits = job_hash_create ()))
        goto error;

    zhashx_set_destructor (isctx->waits, idsync_waits_list_destroy);

    return isctx;

error:
    saved_errno = errno;
    idsync_ctx_destroy (isctx);
    errno = saved_errno;
    return NULL;
}

void idsync_ctx_destroy (struct idsync_ctx *isctx)
{
    if (isctx) {
        struct idsync_data *isd;
        isd = zlistx_first (isctx->lookups);
        while (isd) {
            if (isd->f_lookup) {
                if (flux_future_get (isd->f_lookup, NULL) < 0)
                    flux_log_error (isctx->h, "%s: flux_future_get",
                                    __FUNCTION__);
            }
            isd = zlistx_next (isctx->lookups);
        }
        zlistx_destroy (&isctx->lookups);
        zhashx_destroy (&isctx->waits);
        free (isctx);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
