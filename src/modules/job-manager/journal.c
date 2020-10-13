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
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "journal.h"

#include "src/common/libeventlog/eventlog.h"

#define EVENTS_MAXLEN 1000

struct journal {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zlist_t *listeners;
    /* holds most recent events for listeners */
    zlist_t *events;
    int events_maxlen;
};

struct journal_listener {
    const flux_msg_t *request;
    json_t *allow;
    json_t *deny;
};

static bool allow_deny_check (struct journal_listener *jl, const char *name)
{
    bool add_entry = true;

    if (jl->allow) {
        add_entry = false;
        if (json_object_get (jl->allow, name))
            add_entry = true;
    }

    if (add_entry && jl->deny) {
        if (json_object_get (jl->deny, name))
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
    struct journal_listener *jl;
    json_t *wrapped_entry = NULL;
    int saved_errno;

    if (!(wrapped_entry = wrap_events_entry (id, eventlog_seq, entry)))
        goto error;

    jl = zlist_first (journal->listeners);
    while (jl) {
        if (allow_deny_check (jl, name)) {
            if (flux_respond_pack (journal->ctx->h, jl->request,
                                   "{s:[O]}", "events", wrapped_entry) < 0)
                flux_log_error (journal->ctx->h, "%s: flux_respond_pack",
                                __FUNCTION__);
        }
        jl = zlist_next (journal->listeners);
    }

    if (zlist_size (journal->events) > journal->events_maxlen)
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
    saved_errno = errno;
    json_decref (wrapped_entry);
    errno = saved_errno;
    return -1;
}

static void journal_listener_destroy (void *data)
{
    struct journal_listener *jl = (struct journal_listener *)data;
    if (jl) {
        int saved_errno = errno;
        flux_msg_decref (jl->request);
        json_decref (jl->allow);
        json_decref (jl->deny);
        free (jl);
        errno = saved_errno;
    }
}

static struct journal_listener *journal_listener_create (const flux_msg_t *msg,
                                                         json_t *allow,
                                                         json_t *deny)
{
    struct journal_listener *jl;

    if (!(jl = calloc (1, sizeof (*jl))))
        goto error;
    jl->request = flux_msg_incref (msg);
    jl->allow = json_incref (allow);
    jl->deny = json_incref (deny);
    return jl;
 error:
    journal_listener_destroy (jl);
    return NULL;
}

static void journal_handle_request (flux_t *h,
                                    flux_msg_handler_t *mh,
                                    const flux_msg_t *msg,
                                    void *arg)
{
    struct job_manager *ctx = arg;
    struct journal *journal = ctx->journal;
    struct journal_listener *jl = NULL;
    const char *errstr = NULL;
    json_t *allow = NULL;
    json_t *deny = NULL;
    json_t *a = NULL;
    json_t *wrapped_entry;

    if (flux_request_unpack (msg, NULL, "{s?o s?o}",
                             "allow", &allow,
                             "deny", &deny) < 0)
        goto error;

    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errstr = "job-manager.events requires streaming RPC flag";
        goto error;
    }

    if (allow && !json_is_object (allow)) {
        errno = EPROTO;
        errstr = "job-manager.events allow should be an object";
        goto error;
    }

    if (deny && !json_is_object (deny)) {
        errno = EPROTO;
        errstr = "job-manager.events deny should be an object";
        goto error;
    }

    if (!(jl = journal_listener_create (msg, allow, deny)))
        goto error;

    if (zlist_append (journal->listeners, jl) < 0) {
        errno = ENOMEM;
        goto error;
    }
    zlist_freefn (journal->listeners, jl, journal_listener_destroy, true);

    wrapped_entry = zlist_first (journal->events);
    while (wrapped_entry) {
        const char *name;

        if (json_unpack (wrapped_entry,
                         "{s:{s:s}}",
                         "entry",
                           "name", &name) < 0) {
            flux_log (ctx->h, LOG_ERR, "invalid wrapped entry");
            goto error;
        }

        if (allow_deny_check (jl, name)) {
            if (!a) {
                if (!(a = json_array ()))
                    goto nomem;
            }
            if (json_array_append (a, wrapped_entry) < 0)
                goto nomem;
        }
        wrapped_entry = zlist_next (journal->events);
    }

    if (a && json_array_size (a) > 0) {
        if (flux_respond_pack (ctx->h, jl->request,
                               "{s:O}", "events", a) < 0) {
            flux_log_error (ctx->h, "%s: flux_respond_pack",
                            __FUNCTION__);
            goto error;
        }
    }

    json_decref (a);
    return;

nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    journal_listener_destroy (jl);
    json_decref (a);
}

static bool match_journal_listener (struct journal_listener *jl,
                                    uint32_t matchtag,
                                    const char *sender)
{
    uint32_t t;
    char *s = NULL;
    bool found = false;

    if (!flux_msg_get_matchtag (jl->request, &t)
        && matchtag == t
        && !flux_msg_get_route_first (jl->request, &s)
        && !strcmp (sender, s))
        found = true;
    free (s);
    return found;
}

static void journal_cancel_request (flux_t *h, flux_msg_handler_t *mh,
                                    const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    struct journal *journal = ctx->journal;
    struct journal_listener *jl;
    uint32_t matchtag;
    char *sender = NULL;

    if (flux_request_unpack (msg, NULL, "{s:i}", "matchtag", &matchtag) < 0
        || flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (h, "error decoding events-cancel request");
        return;
    }
    jl = zlist_first (journal->listeners);
    while (jl) {
        if (match_journal_listener (jl, matchtag, sender))
            break;
        jl = zlist_next (journal->listeners);
    }
    if (jl) {
        if (flux_respond_error (h, jl->request, ENODATA, NULL) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
        zlist_remove (journal->listeners, jl);
    }
    free (sender);
}

static int create_zlist_and_append (zlist_t **lp, void *item)
{
    if (!*lp && !(*lp = zlist_new ())) {
        errno = ENOMEM;
        return -1;
    }
    if (zlist_append (*lp, item) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void journal_listeners_disconnect_rpc (flux_t *h,
                                       flux_msg_handler_t *mh,
                                       const flux_msg_t *msg,
                                       void *arg)
{
    struct job_manager *ctx = arg;
    struct journal *journal = ctx->journal;
    struct journal_listener *jl;
    char *sender;
    zlist_t *tmplist = NULL;

    if (flux_msg_get_route_first (msg, &sender) < 0)
        return;
    jl = zlist_first (journal->listeners);
    while (jl) {
        char *tmpsender;
        if (flux_msg_get_route_first (jl->request, &tmpsender) == 0) {
            if (!strcmp (sender, tmpsender)) {
                /* cannot remove from zlist while iterating, so we
                 * store off entries to remove on another list */
                if (create_zlist_and_append (&tmplist, jl) < 0) {
                    flux_log_error (h, "job-manager.disconnect: "
                                    "failed to remove journal listener");
                    free (tmpsender);
                    goto error;
                }
            }
            free (tmpsender);
        }
        jl = zlist_next (journal->listeners);
    }
    if (tmplist) {
        while ((jl = zlist_pop (tmplist)))
            zlist_remove (journal->listeners, jl);
    }
    free (sender);
error:
    zlist_destroy (&tmplist);
}

void journal_ctx_destroy (struct journal *journal)
{
    if (journal) {
        int saved_errno = errno;
        flux_msg_handler_delvec (journal->handlers);
        if (journal->listeners) {
            struct journal_listener *jl;
            while ((jl = zlist_pop (journal->listeners))) {
                if (flux_respond_error (journal->ctx->h,
                                        jl->request,
                                        ENODATA, NULL) < 0)
                    flux_log_error (journal->ctx->h, "%s: flux_respond_error",
                                    __FUNCTION__);
                journal_listener_destroy (jl);
            }
            zlist_destroy (&journal->listeners);
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
        "job-manager.events",
        journal_handle_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.events-cancel",
        journal_cancel_request,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct journal *journal_ctx_create (struct job_manager *ctx)
{
    struct journal *journal;
    flux_conf_error_t err;

    if (!(journal = calloc (1, sizeof (*journal))))
        return NULL;
    journal->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &journal->handlers) < 0)
        goto error;
    if (!(journal->listeners = zlist_new ()))
        goto nomem;
    if (!(journal->events = zlist_new ()))
        goto nomem;
    journal->events_maxlen = EVENTS_MAXLEN;

    if (flux_conf_unpack (flux_get_conf (ctx->h),
                          &err,
                          "{s?{s?i}}",
                          "job-manager",
                            "events_maxlen",
                            &journal->events_maxlen) < 0) {
        flux_log (ctx->h, LOG_ERR,
                  "error reading job-manager config: %s",
                  err.errbuf);
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
        return zlist_size (journal->listeners);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

