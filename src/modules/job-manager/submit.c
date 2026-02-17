/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* submit.c - handle job-manager.submit request from job-ingest */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/errno_safe.h"

#include "job.h"
#include "alloc.h"
#include "event.h"
#include "wait.h"
#include "queue.h"
#include "jobtap-internal.h"

#include "submit.h"

struct submit {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
};

/* Submit event requires special handling to use job->t_submit (from
 * job-ingest) as the event timestamp.
 */
static int submit_post_event (struct job_manager *ctx, struct job *job)
{
    json_t *entry = NULL;
    int rv = -1;

    entry = eventlog_entry_pack (job->t_submit,
                                 "submit",
                                 "{s:I s:i s:i s:i}",
                                 "userid", (json_int_t) job->userid,
                                 "urgency", job->urgency,
                                 "flags", job->flags,
                                 "version", 1);
    if (!entry)
        return -1;

    rv = event_job_post_entry (ctx->event, job, 0, entry);
    ERRNO_SAFE_WRAP (json_decref, entry);
    return rv;
}

static void set_errorf (json_t *errors,
                        flux_jobid_t id,
                        const char *fmt, ...)
{
    va_list ap;
    char buf[256];
    json_t *o;

    va_start (ap, fmt);
    vsnprintf (buf, sizeof (buf), fmt, ap);
    va_end (ap);
    if (!(o = json_pack ("[Is]", id, buf))
        || json_array_append_new (errors, o)) {
        json_decref (o);
        return;
    }
}

static int submit_job (struct job_manager *ctx,
                       struct job *job,
                       json_t *errors)
{
    flux_error_t e;
    flux_error_t error = {{0}};

    if (queue_submit_check (ctx->queue, job->jobspec_redacted, &e) < 0) {
        set_errorf (errors, job->id, "%s", e.text);
        return -1;
    }
    if (zhashx_insert (ctx->active_jobs, &job->id, job) < 0) {
        set_errorf (errors, job->id, "hash insert failed");
        return -1;
    }
    /* Post the submit event.
     */
    if (submit_post_event (ctx, job) < 0) {
        set_errorf (errors, job->id, "error posting submit event");
        goto error;
    }
    /* Post the job.create callback.  Since the plugin might post events,
     * it is called _after_ the submit event is posted, since submit SHALL
     * be the first event per RFC 21.
     */
    /* Call job.validate callback.
     */
    if (jobtap_call_create (ctx->jobtap, job, &error) < 0
        || jobtap_validate (ctx->jobtap, job, &error) < 0
        || jobtap_check_dependencies (ctx->jobtap, job, false, &error) < 0) {
        set_errorf (errors,
                    job->id,
                    "%s",
                    strlen (error.text) > 0 ? error.text : "rejected by plugin");
        goto error_post_invalid;
    }
    /* Call job.new callback now that job is accepted, so plugins
     * may account for this job.
     */
    (void) jobtap_call (ctx->jobtap, job, "job.new", NULL);

    /* Add this job to current commit batch. This pauses event processing
     *  for the job until the current batch is committed to the KVS. This
     *  ensures that the job eventlog is available in the KVS before further
     *  state transitions for the job are made (i.e. before the job is
     *  allocated resources and is started by the job exec system.)
     */
    if (event_batch_add_job (ctx->event, job) < 0)
        goto error;

    /* Post the validate event.
     */
    if (event_job_post_pack (ctx->event, job, "validate", 0, NULL) < 0) {
        set_errorf (errors, job->id, "error posting validate event");
        goto error_post_invalid;
    }
    if ((job->flags & FLUX_JOB_WAITABLE))
        wait_notify_active (ctx->wait, job);
    if (ctx->max_jobid < job->id)
        ctx->max_jobid = job->id;
    return 0;
error_post_invalid:
    /* Let journal consumers know this job did not pass validation and
     * all data concerning it should be expunged.
     */
    (void)event_job_post_pack (ctx->event,
                               job,
                               "invalidate",
                               EVENT_NO_COMMIT,
                               NULL);
    (void) jobtap_call (ctx->jobtap, job, "job.destroy", NULL);
error:
    zhashx_delete (ctx->active_jobs, &job->id);
    return -1;
}


/* handle submit request (from job-ingest module)
 * This is a batched request for one or more jobs already validated
 * by the ingest module, and already instantiated in the KVS.
 * The user isn't handed the jobid though until we accept the job here.
 */
static void submit_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    json_t *jobs;
    size_t index;
    json_t *o;
    json_t *errors = NULL;
    flux_msg_t *response = NULL;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0) {
        flux_log_error (h, "%s", __FUNCTION__);
        goto error;
    }
    if (!(errors = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    /* Process each job sequentially, noting any failures (such as rejection
     * by a validator plugin) in 'errors' which becomes the response payload.
     */
    json_array_foreach (jobs, index, o) {
        struct job *job;
        if (!(job = job_create_from_json (o)))
            goto error; // fail all on unlikely EPROTO/ENOMEM
        submit_job (ctx, job, errors);
        job_decref (job);
    }
    /* Attach response to commit batch, to maintain the invariant that the
     * job ID is only returned to the user after the submit event is committed.
     */
    if (!(response = flux_response_derive (msg, 0))
        || flux_msg_pack (response, "{s:O}", "errors", errors) < 0
        || event_batch_respond (ctx->event, response) < 0) {
        flux_log_error (h, "error enqueuing response to submit request");
        goto error;
    }
    flux_msg_decref (response);
    json_decref (errors);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    flux_msg_decref (response);
    json_decref (errors);
}

void submit_ctx_destroy (struct submit *submit)
{
    if (submit) {
        int saved_errno = errno;
        flux_msg_handler_delvec (submit->handlers);
        free (submit);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {   FLUX_MSGTYPE_REQUEST,
        "job-manager.submit",
        submit_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct submit *submit_ctx_create (struct job_manager *ctx)
{
    struct submit *submit;

    if (!(submit = calloc (1, sizeof (*submit))))
        return NULL;
    submit->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &submit->handlers) < 0)
        goto error;
    return submit;
error:
    submit_ctx_destroy (submit);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
