/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* journal.c - job event journaling and streaming to listeners
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libjob/idf58.h"
#include "ccan/str/str.h"

#include "conf.h"
#include "job.h"
#include "journal.h"

struct journal {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    struct flux_msglist *listeners;
};

struct journal_filter { // stored as aux item in request message
    json_t *allow;      // allow, deny are owned by message
    json_t *deny;
};

static bool allow_deny_check (const flux_msg_t *msg, const char *name)
{
    bool add_entry = true;
    struct journal_filter *filter = flux_msg_aux_get (msg, "filter");

    if (filter->allow) {
        add_entry = false;
        if (json_object_get (filter->allow, name))
            add_entry = true;
    }

    if (add_entry && filter->deny) {
        if (json_object_get (filter->deny, name))
            add_entry = false;
    }

    return add_entry;
}

static bool allow_all (const flux_msg_t *msg)
{
    struct journal_filter *filter = flux_msg_aux_get (msg, "filter");
    if (filter->allow || filter->deny)
        return false;
    return true;
}

int journal_process_event (struct journal *journal,
                           flux_jobid_t id,
                           const char *name,
                           json_t *entry)
{
    struct job_manager *ctx = journal->ctx;
    const flux_msg_t *msg;
    json_t *o;

    if (!(o = json_pack ("{s:I s:[O]}",
                         "id", id,
                         "events", entry)))
        goto error;
    if (streq (name, "validate")) {
        struct job *job;
        if (!(job = zhashx_lookup (ctx->active_jobs, &id))
            || !job->jobspec_redacted
            || json_object_set (o, "jobspec", job->jobspec_redacted) < 0)
            goto error;
    }
    else if (streq (name, "alloc")) {
        struct job *job;
        if (!(job = zhashx_lookup (ctx->active_jobs, &id))
            || !job->R_redacted
            || json_object_set (o, "R", job->R_redacted) < 0)
            goto error;
    }
    msg = flux_msglist_first (journal->listeners);
    while (msg) {
        if (allow_deny_check (msg, name)
            && flux_respond_pack (ctx->h, msg, "O", o) < 0) {
            flux_log_error (ctx->h,
                            "error responding to"
                            " job-manager.events-journal request");
        }
        msg = flux_msglist_next (journal->listeners);
    }
    json_decref (o);
    return 0;
error:
    flux_log_error (ctx->h,
                    "error preparing journal response for %s %s",
                    idf58 (id),
                    name);
    json_decref (o);
    return 0;
}

static void filter_destroy (struct journal_filter *filter)
{
    ERRNO_SAFE_WRAP (free, filter);
}

static int send_job_events (struct job_manager *ctx,
                            const flux_msg_t *msg,
                            struct job *job)
{
    json_t *eventlog;
    json_t *o = NULL;

    if (allow_all (msg)) {
        eventlog = json_incref (job->eventlog);
    }
    else {
        size_t index;
        json_t *entry;
        const char *name;

        if (!(eventlog = json_array ()))
            goto nomem;
        json_array_foreach (job->eventlog, index, entry) {
            if (eventlog_entry_parse (entry, NULL, &name, NULL) < 0)
                goto error;
            if (!allow_deny_check (msg, name))
                continue;
            if (json_array_append (eventlog, entry) < 0)
                goto nomem;
        }
    }
    if (!(o = json_pack ("{s:I s:O}",
                         "id", job->id,
                         "events", eventlog)))
        goto nomem;
    if (job->jobspec_redacted) {
        if (json_object_set (o, "jobspec", job->jobspec_redacted) < 0)
            goto nomem;
    }
    if (job->R_redacted) {
        if (json_object_set (o, "R", job->R_redacted) < 0)
            goto nomem;
    }
    if (flux_respond_pack (ctx->h, msg, "O", o) < 0)
        goto error;
    json_decref (o);
    json_decref (eventlog);
    return 0;
 nomem:
    errno = ENOMEM;
 error:
    ERRNO_SAFE_WRAP (json_decref, o);
    ERRNO_SAFE_WRAP (json_decref, eventlog);
    return -1;
}

/* The entire backlog must be sent to a journal consumer before
 * any new events can be generated, event if it's large.
 */
static int send_backlog (struct job_manager *ctx,
                         const flux_msg_t *msg,
                         bool full)
{
    struct job *job;
    int job_count = zhashx_size (ctx->active_jobs);

    if (full)
        job_count += zhashx_size (ctx->inactive_jobs);

    if (job_count > 0) {
        flux_log (ctx->h,
                  LOG_DEBUG,
                  "begin sending journal backlog: %d jobs",
                  job_count);
    }

    if (full) {
        job = zhashx_first (ctx->inactive_jobs);
        while (job) {
            if (send_job_events (ctx, msg, job) < 0)
                return -1;
            job = zhashx_next (ctx->inactive_jobs);
        }
    }
    job = zhashx_first (ctx->active_jobs);
    while (job) {
        if (send_job_events (ctx, msg, job) < 0)
            return -1;
        job = zhashx_next (ctx->active_jobs);
    }

    if (job_count > 0) {
        flux_log (ctx->h,
                  LOG_DEBUG,
                  "finished sending journal backlog");
    }
    /* Send a special response with id = FLUX_JOB_ANY to demarcate the
     * backlog from ongoing events.  The consumer may ignore this message.
     */
    if (flux_respond_pack (ctx->h,
                           msg,
                           "{s:I s:[]}",
                           "id", FLUX_JOBID_ANY,
                           "events") < 0)
        return -1;
    return 0;
}

static void journal_handle_request (flux_t *h,
                                    flux_msg_handler_t *mh,
                                    const flux_msg_t *msg,
                                    void *arg)
{
    struct job_manager *ctx = arg;
    const char *topic = "unknown";
    struct journal *journal = ctx->journal;
    struct journal_filter *filter;
    int full = 0;
    const char *errstr = NULL;

    if (!(filter = calloc (1, sizeof (*filter))))
        goto error;
    if (flux_request_unpack (msg,
                             &topic,
                             "{s?o s?o s?b}",
                             "allow", &filter->allow,
                             "deny", &filter->deny,
                             "full", &full) < 0
        || flux_msg_aux_set (msg, "filter", filter,
                             (flux_free_f)filter_destroy) < 0) {
        filter_destroy (filter);
        goto error;
    }
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errstr = "job-manager.events requires streaming RPC flag";
        goto error;
    }

    if (filter->allow && !json_is_object (filter->allow)) {
        errno = EPROTO;
        errstr = "job-manager.events allow should be an object";
        goto error;
    }

    if (filter->deny && !json_is_object (filter->deny)) {
        errno = EPROTO;
        errstr = "job-manager.events deny should be an object";
        goto error;
    }

    if (send_backlog (ctx, msg, full) < 0) {
        flux_log_error (h, "error responding to %s", topic);
        return;
    }
    if (flux_msglist_append (journal->listeners, msg) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to %s", topic);
}

static void journal_cancel_request (flux_t *h, flux_msg_handler_t *mh,
                                    const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;

    if (flux_msglist_cancel (h, ctx->journal->listeners, msg) < 0)
        flux_log_error (h, "error handling job-manager.events-journal-cancel");
}

void journal_listeners_disconnect_rpc (flux_t *h,
                                       flux_msg_handler_t *mh,
                                       const flux_msg_t *msg,
                                       void *arg)
{
    struct job_manager *ctx = arg;

    if (flux_msglist_disconnect (ctx->journal->listeners, msg) < 0)
        flux_log_error (h, "error handling job-manager.disconnect (journal)");
}

void journal_ctx_destroy (struct journal *journal)
{
    if (journal) {
        int saved_errno = errno;
        flux_t *h = journal->ctx->h;

        flux_msg_handler_delvec (journal->handlers);
        if (journal->listeners) {
            const flux_msg_t *msg;

            msg = flux_msglist_first (journal->listeners);
            while (msg) {
                if (flux_respond_error (h, msg, ENODATA, NULL) < 0)
                    flux_log_error (h, "error responding to journal request");
                flux_msglist_delete (journal->listeners);
                msg = flux_msglist_next (journal->listeners);
            }
            flux_msglist_destroy (journal->listeners);
        }
        free (journal);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.events-journal",
        journal_handle_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.events-journal-cancel",
        journal_cancel_request,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct journal *journal_ctx_create (struct job_manager *ctx)
{
    struct journal *journal;

    if (!(journal = calloc (1, sizeof (*journal))))
        return NULL;
    journal->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &journal->handlers) < 0)
        goto error;
    if (!(journal->listeners = flux_msglist_create ()))
        goto error;
    return journal;
error:
    journal_ctx_destroy (journal);
    return NULL;
}

int journal_listeners_count (struct journal *journal)
{
    if (journal)
        return flux_msglist_count (journal->listeners);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

