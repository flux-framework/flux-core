/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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
#include <ctype.h>
#include <flux/core.h>

#include "job.h"
#include "event.h"
#include "validate.h"
#include "job-manager.h"

struct validate {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zlist_t *lookups;
};

static void validate_respond (flux_t *h,
                              const flux_msg_t *msg,
                              struct job *job)
{
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i s:f s:i s:i}",
                           "userid",
                           job->userid,
                           "priority",
                           job->priority,
                           "t_submit",
                           job->t_submit,
                           "flags",
                           job->flags,
                           "state",
                           job->state)  < 0)
        flux_log_error (h, "error responding to validate request");
}

static void lookup_eventlog_continuation (flux_future_t *f, void *arg)
{
    struct job_manager *ctx = arg;
    const flux_msg_t *msg = flux_future_aux_get (f, "request");
    const char *eventlog;
    flux_jobid_t id;
    struct job *job;
    const char *errstr = NULL;

    if (flux_kvs_lookup_get (f, &eventlog) < 0) {
        if (errno == ENOENT)
            errstr = "invalid job ID";
        goto error;
    }
    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        goto error;
    if (!(job = job_create_from_eventlog (id, eventlog)))
        goto error;
    validate_respond (ctx->h, msg, job);
    job_decref (job);
    flux_future_destroy (f);
    return;
error:
    if (flux_respond_error (ctx->h, msg, errno, errstr) < 0)
        flux_log_error (ctx->h, "error responding to validate request");
    flux_future_destroy (f);
}

/* Kick off a KVS lookup of job id's eventlog.
 * This function threads a future onto the validate->lookups list.
 * The request message is stored in the future's aux hash, and
 * is responded to by the continuation.
 */
static int lookup_eventlog (struct job_manager *ctx,
                            flux_jobid_t id,
                            const flux_msg_t *msg)
{
    flux_future_t *f;
    char key[128];

    if (flux_job_kvs_key (key, sizeof (key), id, "eventlog") < 0)
        return -1;
    if (!(f = flux_kvs_lookup (ctx->h, NULL, 0, key)))
        return -1;
    if (flux_future_then (f, -1, lookup_eventlog_continuation, ctx) < 0)
        goto error;
    if (flux_future_aux_set (f,
                             "request",
                             (void *)flux_msg_incref (msg),
                             (flux_free_f)flux_msg_decref) < 0) {
        flux_msg_decref (msg);
        goto error;
    }
    if (zlist_append (ctx->validate->lookups, f) < 0) {
        errno = ENOMEM;
        goto error;
    }
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

static void validate_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct job_manager *ctx = arg;
    struct job *job;
    const char *errstr = NULL;
    flux_jobid_t id;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        goto error;
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        if (lookup_eventlog (ctx, id, msg) < 0) {
            errstr = "error starting KVS lookup of job eventlog";
            goto error;
        }
    }
    else
        validate_respond (h, msg, job);;
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to validate request");
}

void validate_ctx_destroy (struct validate *validate)
{
    if (validate) {
        int saved_errno = errno;
        flux_msg_handler_delvec (validate->handlers);
        if (validate->lookups) {
            flux_future_t *f;
            while ((f = zlist_pop (validate->lookups)))
                flux_future_destroy (f);
            zlist_destroy (&validate->lookups);
        }
        free (validate);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.validate",
        validate_cb,
        FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct validate *validate_ctx_create (struct job_manager *ctx)
{
    struct validate *validate;

    if (!(validate = calloc (1, sizeof (*validate))))
        return NULL;
    validate->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &validate->handlers) < 0)
        goto error;
    if (!(validate->lookups = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    return validate;
error:
    validate_ctx_destroy (validate);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
