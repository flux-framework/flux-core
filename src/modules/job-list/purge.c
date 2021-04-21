/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* purge.c - purge jobs */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <assert.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "purge.h"
#include "stats.h"

static int remove_one_inactive_job (struct job_state_ctx *jsctx)
{
    struct job *job = NULL;
    flux_jobid_t id;
    flux_jobid_t *idptr = NULL;

    zlistx_last (jsctx->inactive);
    job = zlistx_detach_cur (jsctx->inactive);
    assert (job != NULL);
    job_stats_remove_inactive (&jsctx->stats, job);
    id = job->id;
    zhashx_delete (jsctx->index, &job->id);

    if (!(idptr = malloc (sizeof (flux_jobid_t)))) {
        flux_log_error (jsctx->h, "%s: malloc", __FUNCTION__);
        goto enomem;
    }
    (*idptr) = id;

    /* we don't care about the contents stored in the purged_jobs
     * hash, just store ptr to this id.  The destructor set on
     * purged_jobids will free this memory. */
    if (zhashx_insert (jsctx->purged_jobids, idptr, idptr) < 0) {
        flux_log_error (jsctx->h, "%s: zhashx_insert", __FUNCTION__);
        free (idptr);
        goto enomem;
    }
    return 0;

enomem:
    errno = ENOMEM;
    return -1;
}

void purge_cb (flux_t *h, flux_msg_handler_t *mh,
               const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    int count = 0;

    if (flux_request_unpack (msg, NULL, "{s?:i}", "count", &count) < 0) {
        errno = EPROTO;
        goto error;
    }
    if (count < 0) {
        errno = EPROTO;
        goto error;
    }

    /* count=0 means remove all */
    if (!count
        || zlistx_size (ctx->jsctx->inactive) < count)
        count = zlistx_size (ctx->jsctx->inactive);

    while (count) {
        if (remove_one_inactive_job (ctx->jsctx) < 0)
            goto error;
        count--;
    }

    if (flux_respond (h, msg, NULL) < 0) {
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
        goto error;
    }

    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
