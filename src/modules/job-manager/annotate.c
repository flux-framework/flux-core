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
#include <flux/core.h>

#include "job.h"
#include "event.h"
#include "annotate.h"
#include "job-manager.h"

struct annotate {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
};

void annotations_clear (struct job *job, bool *cleared)
{
    if (job->annotations) {
        json_decref (job->annotations);
        job->annotations = NULL;
        if (cleared)
            (*cleared) = true;
    }
}

/* we want to delete items set to 'null', so this is not the same
 * as json_object_update_recursive() in jansson 2.13.1
 */
int update_annotation_recursive (struct job *job, json_t *orig, json_t *new)
{
    const char *key;
    json_t *value;

    assert (job && orig && new);

    json_object_foreach (new, key, value) {
        if (!json_is_null (value)) {
            json_t *orig_value = json_object_get (orig, key);

            if (json_is_object (value)) {
                if (!json_is_object (orig_value)) {
                    json_t *o = json_object ();
                    if (!o || json_object_set_new (orig, key, o) < 0) {
                        errno = ENOMEM;
                        json_decref (o);
                        return -1;
                    }
                    orig_value = o;
                }
                if (update_annotation_recursive (job, orig_value, value) < 0)
                    return -1;
                /* if object is now empty, remove it */
                if (!json_object_size (orig_value))
                    (void)json_object_del (orig, key);
            }
            else {
                if (json_object_set (orig, key, value) < 0) {
                    errno = ENOMEM;
                    return -1;
                }
            }
        }
        else
            /* not an error if key doesn't exist in orig */
            (void)json_object_del (orig, key);
    }

    return 0;
}

int annotations_update (flux_t *h, struct job *job, json_t *annotations)
{
    if (annotations) {
        if (!job->annotations) {
            if (!(job->annotations = json_object ())) {
                errno = ENOMEM;
                return -1;
            }
        }
        if (job->annotations) {
            if (update_annotation_recursive (job,
                                             job->annotations,
                                             annotations) < 0)
                return -1;
            /* Special case: if user cleared all entries, assume we no
             * longer need annotations object.  If cleared, caller
             * will handle advertisement of the clear.
             */
            if (!json_object_size (job->annotations))
                annotations_clear (job, NULL);
        }
    }

    return 0;
}

void annotate_handle_request (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct job_manager *ctx = arg;
    struct flux_msg_cred cred;
    flux_jobid_t id;
    json_t *annotations = NULL;
    struct job *job;
    const char *errstr = NULL;
    json_t *tmp = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I s:o}",
                                        "id", &id,
                                        "annotations", &annotations) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        errstr = "unknown job id";
        errno = ENOENT;
        goto error;
    }
    if (flux_msg_cred_authorize (cred, job->userid) < 0) {
        errstr = "guests can only annotate their own jobs";
        goto error;
    }
    if (annotations_update (ctx->h, job, annotations) < 0)
        goto error;
    if (job->annotations) {
        /* deep copy necessary for journal history, as
         * job->annotations can be modified in future */
        if (!(tmp = json_deep_copy (job->annotations)))
            goto nomem;
    }
    if (event_job_post_pack (ctx->event,
                             job,
                             "annotations",
                             EVENT_JOURNAL_ONLY,
                             "{s:O?}",
                             "annotations", tmp) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    json_decref (tmp);
    return;
nomem:
    errno = ENOMEM;
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
        "job-manager.annotate",
        annotate_handle_request,
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
