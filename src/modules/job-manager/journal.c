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

#include "conf.h"
#include "journal.h"

#define DEFAULT_JOURNAL_SIZE_LIMIT 1000

struct journal {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    struct flux_msglist *listeners;
    /* holds most recent events for listeners */
    zlist_t *events;
    int size_limit;
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

/* wrap the eventlog entry in another object with the job id and
 * eventlog_seq.
 *
 * The job id is necessary so listeners can determine which job the
 * event is associated with.
 *
 * The eventlog sequence number is necessary so users can determine if the
 * event is a duplicate if they are reading events from another source
 * (i.e. they could be reading events from the job's eventlog in the
 * KVS).
 */
static json_t *wrap_events_entry (flux_jobid_t id,
                                  int eventlog_seq,
                                  json_t *entry)
{
    json_t *wrapped_entry;
    if (!(wrapped_entry = json_pack ("{s:I s:i s:O}",
                                     "id", id,
                                     "eventlog_seq", eventlog_seq,
                                     "entry", entry))) {
        errno = ENOMEM;
        return NULL;
    }
    return wrapped_entry;
}

static void json_decref_wrapper (void *data)
{
    json_t *o = (json_t *)data;
    json_decref (o);
}

int journal_process_event (struct journal *journal,
                           flux_jobid_t id,
                           int eventlog_seq,
                           const char *name,
                           json_t *entry)
{
    const flux_msg_t *msg;
    json_t *wrapped_entry = NULL;

    if (!(wrapped_entry = wrap_events_entry (id, eventlog_seq, entry)))
        goto error;

    msg = flux_msglist_first (journal->listeners);
    while (msg) {
        if (allow_deny_check (msg, name)) {
            if (flux_respond_pack (journal->ctx->h, msg,
                                   "{s:[O]}", "events", wrapped_entry) < 0)
                flux_log_error (journal->ctx->h, "%s: flux_respond_pack",
                                __FUNCTION__);
        }
        msg = flux_msglist_next (journal->listeners);
    }

    if (zlist_size (journal->events) > journal->size_limit)
        zlist_remove (journal->events, zlist_head (journal->events));
    if (zlist_append (journal->events, json_incref (wrapped_entry)) < 0)
        goto nomem;
    zlist_freefn (journal->events,
                  wrapped_entry,
                  json_decref_wrapper,
                  true);

    json_decref (wrapped_entry);
    return 0;

nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, wrapped_entry);
    return -1;
}

static void filter_destroy (struct journal_filter *filter)
{
    ERRNO_SAFE_WRAP (free, filter);
}

static void journal_handle_request (flux_t *h,
                                    flux_msg_handler_t *mh,
                                    const flux_msg_t *msg,
                                    void *arg)
{
    struct job_manager *ctx = arg;
    struct journal *journal = ctx->journal;
    struct journal_filter *filter;
    const char *errstr = NULL;
    json_t *a = NULL;
    json_t *wrapped_entry;

    if (!(filter = calloc (1, sizeof (*filter))))
        goto error;
    if (flux_request_unpack (msg, NULL, "{s?o s?o}",
                             "allow", &filter->allow,
                             "deny", &filter->deny) < 0
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

    if (flux_msglist_append (journal->listeners, msg) < 0)
        goto error;

    wrapped_entry = zlist_first (journal->events);
    while (wrapped_entry) {
        const char *name;
        flux_jobid_t id;

        if (json_unpack (wrapped_entry,
                         "{s:I s:{s:s}}",
                         "id", &id,
                         "entry",
                           "name", &name) < 0) {
            flux_log (h, LOG_ERR, "invalid wrapped entry");
            goto error;
        }

        /* ensure job has not been purged */
        if (!zhashx_lookup (ctx->active_jobs, &id)
            && !zhashx_lookup (ctx->inactive_jobs, &id))
            goto next;

        if (allow_deny_check (msg, name)) {
            if (!a) {
                if (!(a = json_array ()))
                    goto nomem;
            }
            if (json_array_append (a, wrapped_entry) < 0)
                goto nomem;
        }
next:
        wrapped_entry = zlist_next (journal->events);
    }

    if (a && json_array_size (a) > 0) {
        if (flux_respond_pack (h, msg, "{s:O}", "events", a) < 0)
            flux_log_error (h,
                            "error responding to job-manager.events-journal");
    }

    json_decref (a);
    return;

nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to job-manager.events-journal");
    json_decref (a);
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

static int journal_parse_config (const flux_conf_t *conf,
                                 flux_error_t *error,
                                 void *arg)
{
    struct journal *journal = arg;
    flux_error_t e;
    int size_limit = -1;

    if (flux_conf_unpack (conf,
                          &e,
                          "{s?{s?i}}",
                          "job-manager",
                            "journal-size-limit", &size_limit) < 0)
        return errprintf (error,
                          "job-manager.journal-size-limit: %s",
                          e.text);
    if (size_limit > 0) {
        journal->size_limit = size_limit;
        /* Drop some entries if the journal size is reduced below
         * what is currently stored.
         */
        while (zlist_size (journal->events) > journal->size_limit)
            zlist_remove (journal->events, zlist_head (journal->events));
    }
    return 1; // indicates to conf.c that callback wants updates
}

void journal_ctx_destroy (struct journal *journal)
{
    if (journal) {
        int saved_errno = errno;
        flux_t *h = journal->ctx->h;

        conf_unregister_callback (journal->ctx->conf, journal_parse_config);

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
        if (journal->events)
            zlist_destroy (&journal->events);
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
    flux_error_t error;

    if (!(journal = calloc (1, sizeof (*journal))))
        return NULL;
    journal->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &journal->handlers) < 0)
        goto error;
    if (!(journal->listeners = flux_msglist_create ()))
        goto error;
    if (!(journal->events = zlist_new ()))
        goto nomem;
    journal->size_limit = DEFAULT_JOURNAL_SIZE_LIMIT;

    if (conf_register_callback (ctx->conf,
                                &error,
                                journal_parse_config,
                                journal) < 0) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "error parsing job-manager config: %s",
                  error.text);
        goto error;
    }

    return journal;
nomem:
    errno = ENOMEM;
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

