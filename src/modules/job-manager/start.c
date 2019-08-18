/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* start.c - exec interface
 *
 * Interface is built so that exec service loads after job-manager
 * and dynamically registers its service name.  It is possible for
 * another instance of the service to be registered after that one,
 * to override it.
 *
 * Use case: simulator initial program overrides "normal" exec service.
 *
 * STARTUP:
 *
 * Exec service sends job-manager.exec-hello request with its service name,
 * {"service":s}.  Job-manager responds with success/failure.
 *
 * Active jobs are scanned and hello fails if any jobs have outstanding
 * start request (e.g. to existing exec service).
 *
 * OPERATION:
 *
 * Job manager makes a <exec_service>.start request once resources are
 * allocated.  The request is made without matchtag, so the job id must
 * be present in all response payloads.
 *
 * A response looks like this:
 * {"id":I "type":s "data":o}
 *
 * where "type" determines the content of data:
 *
 * "start" - indicates job shells have started
 *           data: {}
 *
 * "release" - release R fragment to job-manager
 *             data: {"ranks":s "final":b}
 *
 * "exception" - raise an exception (0 is fatal)
 *               data: {"severity":i "type":s "note"?:s}
 *
 * "finish" - data: {"status":i}
 *
 * Responses stream back until a 'release' response is received with
 * final=true.  This means all resources allocated to the job are no
 * longer in use by the exec system.
 *
 * TEARDOWN:
 *
 * If an ENOSYS (or other "normal RPC error" response is returned to an
 * alloc request, it is assumed that the current service is unloading
 * or a fatal error has occurred.  Start requests are paused waiting
 * for another hello.
 *
 * No attempt is made to restart the interface with a previously overridden
 * exec service.
 *
 * NOTES:
 * - The "finish" response may be preceded by "release" final=false responses.
 * - The "finish" response must precede the "release" final=true response.
 * - For now, release responses with final=false are ignored, and resources
 *   are released to the scheduler only upon receipt of release final=true.
 * - A normal RPC error response, while logged at LOG_ERR level, has no
 *   effect on a particular job, nor does it tear down the interface as
 *   with alloc.
 * - Even if an exception is raised, the "release" final=true response
 *   is required.  "start" and "finish" may or may not be sent depending
 *   on when the exception occurs.
 * - Response message topic strings are checked against registered service,
 *   so as long as services use unique service names, no confusion is possible
 *   between service instances, e.g. due to multiple in-flight ENOSYS or
 *   whatever.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <assert.h>

#include "job.h"
#include "event.h"

#include "start.h"
#include "simulator.h"

struct start {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    char *topic;
};

static void hello_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    struct start *start = ctx->start;
    struct job *job;
    const char *service_name;

    if (flux_request_unpack (msg, NULL, "{s:s}", "service", &service_name) < 0)
        goto error;
    flux_log (h, LOG_DEBUG, "%s: %s said hello", __FUNCTION__, service_name);
    /* If existing exec service is loaded, ensure it is idle before
     * allowing new exec service to override.
     */
    if (start->topic) {
        job = zhashx_first (ctx->active_jobs);
        while (job) {
            if (job->start_pending) {
                errno = EINVAL;
                goto error;
            }
            job = zhashx_next (ctx->active_jobs);
        }
        free (start->topic);
        start->topic = NULL;
    }
    if (asprintf (&start->topic, "%s.start", service_name) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_log (h, LOG_DEBUG, "%s: registering %s as the start topic",
              __FUNCTION__, start->topic);
    /* Response has been sent, now take action on jobs in run state.
     */
    job = zhashx_first (ctx->active_jobs);
    while (job) {
        if (job->state == FLUX_JOB_RUN) {
            if (event_job_action (ctx->event, job) < 0)
                flux_log_error (h, "%s: event_job_action id=%ju", __FUNCTION__,
                                (uintmax_t)job->id);
        }
        job = zhashx_next (ctx->active_jobs);
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void interface_teardown (struct start *start, char *s, int errnum)
{
    if (start->topic) {
        struct job_manager *ctx = start->ctx;
        struct job *job;

        flux_log (ctx->h, LOG_DEBUG, "start: stop due to %s: %s",
                  s, flux_strerror (errnum));

        free (start->topic);
        start->topic = NULL;

        job = zhashx_first (ctx->active_jobs);
        while (job) {
            if (job->start_pending) {
                if ((job->flags & FLUX_JOB_DEBUG))
                    (void)event_job_post_pack (ctx->event, job,
                                               "debug.start-lost",
                                               "{ s:s }", "note", s);
                job->start_pending = 0;
            }
            job = zhashx_next (ctx->active_jobs);
        }
    }
}

static void start_response_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    struct start *start = ctx->start;
    const char *topic;
    flux_jobid_t id;
    const char *type;
    json_t *data;
    struct job *job;

    if (flux_response_decode (msg, &topic, NULL) < 0)
        goto teardown; // e.g. ENOSYS
    if (!start->topic || strcmp (start->topic, topic) != 0) {
        flux_log_error (h, "start: topic=%s not registered", topic);
        goto error;
    }
    if (flux_msg_unpack (msg, "{s:I s:s s:o}", "id", &id,
                                               "type", &type,
                                               "data", &data) < 0) {
        flux_log_error (h, "start response payload");
        goto error;
    }
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        flux_log (h, LOG_ERR, "start response: id=%ju not active",
                  (uintmax_t)id);
        errno = EINVAL;
        goto error;
    }
    if (!strcmp (type, "start")) {
        sim_received_start_response (ctx->simulator);
        if (event_job_post_pack (ctx->event, job, "start", NULL) < 0)
            goto error_post;
    }
    else if (!strcmp (type, "release")) {
        const char *idset;
        int final;
        if (json_unpack (data, "{s:s s:b}", "ranks", &idset,
                                            "final", &final) < 0) {
            errno = EPROTO;
            flux_log_error (h, "start: release response: malformed data");
            goto error;
        }
        if (final) // final release is end-of-stream
            job->start_pending = 0;
        if (event_job_post_pack (ctx->event, job, "release",
                                 "{ s:s s:b }",
                                 "ranks", idset,
                                 "final", final) < 0)
            goto error_post;
    }
    else if (!strcmp (type, "exception")) {
        int xseverity;
        const char *xtype;
        const char *xnote = NULL;
        if (json_unpack (data, "{s:i s:s s?:s}", "severity", &xseverity,
                                                 "type", &xtype,
                                                 "note", &xnote) < 0) {
            errno = EPROTO;
            flux_log_error (h, "start: exception response: malformed data");
            goto error;
        }
        if (event_job_post_pack (ctx->event, job, "exception",
                                 "{ s:s s:i s:i s:s }",
                                 "type", xtype,
                                 "severity", xseverity,
                                 "userid", FLUX_USERID_UNKNOWN,
                                 "note", xnote ? xnote : "")  < 0)
            goto error_post;
    }
    else if (!strcmp (type, "finish")) {
        int status;
        if (json_unpack (data, "{s:i}", "status", &status) < 0) {
            errno = EPROTO;
            flux_log_error (h, "start: finish response: malformed data");
            goto error;
        }
        if (event_job_post_pack (ctx->event, job, "finish",
                                 "{ s:i }", "status", status) < 0)
            goto error_post;
    }
    else {
        flux_log (h, LOG_ERR, "start: unknown response type=%s", type);
        goto error;
    }
    return;
error_post:
    flux_log_error (h, "start: failed to post event type=%s", type);
error:
    return;
teardown:
    interface_teardown (start, "start response error", errno);
}

/* Send <exec_service>.start request for job.
 * Idempotent.
 */
int start_send_request (struct start *start, struct job *job)
{
    struct job_manager *ctx = start->ctx;
    flux_msg_t *msg;

    assert (job->state == FLUX_JOB_RUN);
    if (!job->start_pending && start->topic != NULL) {
        if (!(msg = flux_request_encode (start->topic, NULL)))
            return -1;
        if (flux_msg_pack (msg, "{s:I s:i}",
                                "id", job->id,
                                "userid", job->userid) < 0)
            goto error;
        if (flux_send (ctx->h, msg, 0) < 0)
            goto error;
        flux_msg_destroy (msg);
        job->start_pending = 1;
        if ((job->flags & FLUX_JOB_DEBUG))
            (void)event_job_post_pack (ctx->event, job,
                                       "debug.start-request", NULL);
    }
    return 0;
error:
    flux_msg_destroy (msg);
    return -1;
}

void start_ctx_destroy (struct start *start)
{
    if (start) {
        int saved_errno = errno;;
        flux_msg_handler_delvec (start->handlers);
        free (start->topic);
        free (start);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.exec-hello", hello_cb, 0},
    { FLUX_MSGTYPE_RESPONSE, "*.start", start_response_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

struct start *start_ctx_create (struct job_manager *ctx)
{
    struct start *start;

    if (!(start = calloc (1, sizeof (*start))))
        return NULL;
    start->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &start->handlers) < 0)
        goto error;
    return start;
error:
    start_ctx_destroy (start);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
