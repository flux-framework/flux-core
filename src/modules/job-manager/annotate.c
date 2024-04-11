/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* annotate - user requests to annotate a job
 *
 * Purpose:
 *   Handle job-manager.annotate RPC
 *
 * Input:
 * - job id, annotations
 *
 * Action:
 *  -update annotations
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <ctype.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libjob/idf58.h"

#include "job.h"
#include "event.h"
#include "annotate.h"
#include "job-manager.h"

struct annotate {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
};

static void annotations_clear (struct job *job)
{
    if (job->annotations) {
        json_decref (job->annotations);
        job->annotations = NULL;
    }
}

int update_annotation_recursive (json_t *orig, const char *path, json_t *new)
{
    if (jpath_update (orig, path, new) < 0
        || jpath_clear_null (orig) < 0)
        return -1;
    return 0;
}

int annotations_update (struct job *job, const char *path, json_t *annotations)
{
    if (!json_is_object (annotations)) {
        errno = EINVAL;
        return -1;
    }
    if (annotations) {
        if (!job->annotations) {
            if (!(job->annotations = json_object ())) {
                errno = ENOMEM;
                return -1;
            }
        }
        if (update_annotation_recursive (job->annotations,
                                         path,
                                         annotations) < 0)
            return -1;
        /* Special case: if user cleared all entries, assume we no
         * longer need annotations object.  If cleared, caller
         * will handle advertisement of the clear.
         */
        if (!json_object_size (job->annotations))
            annotations_clear (job);
    }
    return 0;
}

void annotations_clear_and_publish (struct job_manager *ctx,
                                    struct job *job,
                                    const char *key)
{
    if (job->annotations) {
        if (key)
            (void)json_object_del (job->annotations, key);
        else
            (void)json_object_clear (job->annotations);
        if (json_object_size (job->annotations) == 0) {
            annotations_clear (job);
            if (event_job_post_pack (ctx->event,
                                     job,
                                     "annotations",
                                     EVENT_NO_COMMIT,
                                     "{s:n}",
                                     "annotations") < 0) {
                flux_log_error (ctx->h,
                                "error posting null annotations event for %s",
                                idf58 (job->id));
            }
        }
    }
}

int annotations_update_and_publish (struct job_manager *ctx,
                                    struct job *job,
                                    json_t *annotations)
{
    int rc = -1;
    json_t *tmp = NULL;

    if (annotations_update (job, ".", annotations) < 0)
        return -1;
    if (job->annotations) {
        /* deep copy necessary for journal history, as
         * job->annotations can be modified in future */
        if (!(tmp = json_deep_copy (job->annotations))) {
            errno = ENOMEM;
            return -1;
        }
    }
    if (event_job_post_pack (ctx->event,
                             job,
                             "annotations",
                             EVENT_NO_COMMIT,
                             "{s:O?}",
                             "annotations", tmp) < 0)
        goto error;
    rc = 0;
error:
    json_decref (tmp);
    return rc;
}

void annotate_memo_request (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct job_manager *ctx = arg;
    struct flux_msg_cred cred;
    flux_jobid_t id;
    json_t *memo = NULL;
    struct job *job;
    const char *errstr = NULL;
    int no_commit = 0;
    json_t *tmp = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s?b s:o}",
                             "id", &id,
                             "volatile", &no_commit,
                             "memo", &memo) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        if (!(job = zhashx_lookup (ctx->inactive_jobs, &id)))
            errstr = "unknown job id";
        else
            errstr = "job is inactive";
        errno = ENOENT;
        goto error;
    }
    if (flux_msg_cred_authorize (cred, job->userid) < 0) {
        errstr = "guests can only add a memo to their own jobs";
        goto error;
    }
    if (event_job_post_pack (ctx->event,
                             job,
                             "memo",
                             no_commit ? EVENT_NO_COMMIT : 0,
                             "O",
                             memo) < 0) {
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    json_decref (tmp);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (tmp);
}


void annotate_ctx_destroy (struct annotate *annotate)
{
    if (annotate) {
        int saved_errno = errno;
        flux_msg_handler_delvec (annotate->handlers);
        free (annotate);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.memo",
        annotate_memo_request,
        FLUX_ROLE_USER
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct annotate *annotate_ctx_create (struct job_manager *ctx)
{
    struct annotate *annotate;

    if (!(annotate = calloc (1, sizeof (*annotate))))
        return NULL;
    annotate->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &annotate->handlers) < 0)
        goto error;
    return annotate;
error:
    annotate_ctx_destroy (annotate);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
