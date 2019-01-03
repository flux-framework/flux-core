/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* dummy job manager for test */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

const char *eventlog_path = "test.ingest.eventlog";

/* KVS commit completed.
 * Respond to original request which was copied and passed as 'arg'.
 */
static void commit_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    flux_msg_t *msg = arg;

    if (flux_future_get (f, NULL) < 0) {
        if (flux_respond_error (h, msg, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    }
    else {
        if (flux_respond (h, msg, 0, NULL) < 0)
            flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    }
    flux_msg_destroy (msg);
    flux_future_destroy (f);
}

/* Given a JSON job object, encode a KVS eventlog entry
 * to represent its submission, timestamped now.  Caller must free.
 */
static char *create_eventlog_entry (json_t *job)
{
    char context[128];
    int priority;
    flux_jobid_t id;
    uint32_t userid;
    double t_submit;
    int n;
    char *event;

    if (json_unpack (job, "{s:I s:i s:i s:f}", "id", &id,
                                               "userid", &userid,
                                               "priority", &priority,
                                               "t_submit", &t_submit) < 0)
        goto error_inval;
    n = snprintf (context, sizeof (context),
                  "id=%llu priority=%d userid=%lu t_submit=%lf",
                  (unsigned long long)id,
                  priority,
                  (unsigned long)userid,
                  t_submit);
    if (n >= sizeof (context))
        goto error_inval;
    if (!(event = flux_kvs_event_encode ("submit", context)))
        goto error;
    return event;
error_inval:
    errno = EINVAL;
error:
    return NULL;
}

/* Given a JSON array of job records, add an eventlog
 * update for each job to a KVS transaction and return it.
 */
static flux_kvs_txn_t *create_eventlog_txn (json_t *jobs)
{
    flux_kvs_txn_t *txn;
    size_t index;
    json_t *job;

    if (!(txn = flux_kvs_txn_create ()))
        return NULL;
    json_array_foreach (jobs, index, job) {
        char *event = create_eventlog_entry (job);
        if (!event)
            goto error;
        if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, eventlog_path, event) < 0) {
            int saved_errno = errno;
            free (event);
            errno = saved_errno;
            goto error;
        }
        free (event);
    }
    return txn;
error:
    flux_kvs_txn_destroy (txn);
    return NULL;
}

static void submit_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    json_t *jobs;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    flux_msg_t *cpy = NULL;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0)
        goto error;
    if (!(cpy = flux_msg_copy (msg, false)))
        goto error;
    if (!(txn = create_eventlog_txn (jobs)))
        goto error;
    if (!(f = flux_kvs_commit (h, 0, txn)))
        goto error;
    if (flux_future_then (f, -1., commit_continuation, cpy) < 0)
        goto error;
    flux_kvs_txn_destroy (txn);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_future_destroy (f);
    flux_msg_destroy (cpy);
    flux_kvs_txn_destroy (txn);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "job-manager.submit", submit_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char *argv[])
{
    flux_msg_handler_t **handlers = NULL;
    int rc = -1;

    if (flux_msg_handler_addvec (h, htab, NULL, &handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_handler_delvec (handlers);
    return rc;
}

MOD_NAME ("job-manager");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
