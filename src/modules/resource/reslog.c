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
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libeventlog/eventlog.h"
#include "ccan/str/str.h"

#include "resource.h"
#include "inventory.h"
#include "reslog.h"

struct reslog_watcher {
    reslog_cb_f cb;
    void *arg;
};

struct event_info {
    json_t *event;          // JSON form of event
    const flux_msg_t *msg;  // optional request to be answered on commit
};

struct reslog {
    struct resource_ctx *ctx;
    zlist_t *pending;       // list of pending futures
    zlist_t *watchers;
    zlistx_t *eventlog;
    int journal_max;
    struct flux_msglist *consumers;
    flux_msg_handler_t **handlers;
};

static const char *auxkey = "flux::event_info";

static bool match_event (json_t *entry, const char *val)
{
    const char *name;
    if (eventlog_entry_parse (entry, NULL, &name, NULL) == 0
        && streq (name, val))
        return true;
    return false;
}

/* zlist_compare_fn() footprint */
static int watcher_compare (void *item1, void *item2)
{
    struct reslog_watcher *w1 = item1;
    struct reslog_watcher *w2 = item2;
    if (w1 && w2 && w1->cb == w2->cb)
        return 0;
    return -1;
}

/* Call registered callbacks, if any, with the event name that just completed.
 */
static void notify_callbacks (struct reslog *reslog, json_t *event)
{
    flux_t *h = reslog->ctx->h;
    const char *name;
    json_t *context;
    struct reslog_watcher *w;

    if (json_unpack (event,
                     "{s:s s:o}",
                     "name", &name,
                     "context", &context) < 0) {
        flux_log (h, LOG_ERR, "error unpacking event for callback");
        return;
    }
    w = zlist_first (reslog->watchers);
    while (w) {
        if (w->cb)
            w->cb (reslog, name, context, w->arg);
        w = zlist_next (reslog->watchers);
    }
}

static int notify_one_consumer (struct reslog *reslog,
                                const flux_msg_t *msg,
                                json_t *entry)
{
    flux_t *h = reslog->ctx->h;
    int rc;

    if (!match_event (entry, "resource-define")) {
        rc = flux_respond_pack (h,
                                msg,
                                "{s:[O]}",
                                "events", entry);
    }
    else {
        rc = flux_respond_pack (h,
                                msg,
                                "{s:[O] s:O}",
                                "events", entry,
                                "R", inventory_get (reslog->ctx->inventory));
    }
    return rc;
}

static void notify_consumers (struct reslog *reslog, json_t *entry)
{
    flux_t *h = reslog->ctx->h;
    const flux_msg_t *msg;

    msg = flux_msglist_first (reslog->consumers);
    while (msg) {
        if (notify_one_consumer (reslog, msg, entry) < 0) {
            flux_log_error (h, "error responding to journal request");
            flux_msglist_delete (reslog->consumers);
        }
        msg = flux_msglist_next (reslog->consumers);
    }
}

static void event_info_destroy (struct event_info *info)
{
    if (info) {
        json_decref (info->event);
        flux_msg_decref (info->msg);
        ERRNO_SAFE_WRAP (free, info);
    }
}

static struct event_info *event_info_create (json_t *event,
                                             const flux_msg_t *request)
{
    struct event_info *info;

    if (!(info = calloc (1, sizeof (*info))))
        return NULL;
    info->event = json_incref (event);
    info->msg = flux_msg_incref (request);
    return info;
}

int post_handler (struct reslog *reslog, flux_future_t *f)
{
    flux_t *h = reslog->ctx->h;
    struct event_info *info = flux_future_aux_get (f, auxkey);
    int rc;

    if ((rc = flux_future_get (f, NULL)) < 0) {
        flux_log_error (h, "committing to %s", RESLOG_KEY);
        if (info->msg) {
            if (flux_respond_error (h, info->msg, errno, NULL) < 0)
                flux_log_error (h, "responding to request after post");
        }
        goto done;
    }
    else {
        if (info->msg) {
            if (flux_respond (h, info->msg, NULL) < 0)
                flux_log_error (h, "responding to request after post");
        }
    }
    notify_callbacks (reslog, info->event);
    notify_consumers (reslog, info->event);
done:
    zlist_remove (reslog->pending, f);
    flux_future_destroy (f);

    if ((f = zlist_first (reslog->pending))
        && (info = flux_future_aux_get (f, auxkey))
        && info->msg == NULL)
        flux_future_fulfill (f, NULL, NULL);

    return rc;
}

static void post_continuation (flux_future_t *f, void *arg)
{
    struct reslog *reslog = arg;

    (void)post_handler (reslog, f);
}

int reslog_sync (struct reslog *reslog)
{
    flux_future_t *f;
    while ((f = zlist_pop (reslog->pending))) {
        if (post_handler (reslog, f) < 0)
            return -1;
    }
    return 0;
}

int reslog_post_pack (struct reslog *reslog,
                      const flux_msg_t *request,
                      double timestamp,
                      const char *name,
                      int flags,
                      const char *fmt,
                      ...)
{
    flux_t *h = reslog->ctx->h;
    va_list ap;
    json_t *event;
    char *val = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    struct event_info *info;

    va_start (ap, fmt);
    event = eventlog_entry_vpack (timestamp, name, fmt, ap);
    va_end (ap);
    if (!event)
        return -1;
    if (!zlistx_add_end (reslog->eventlog, event)) {
        errno = ENOMEM;
        return -1;
    }
    if ((flags & EVENT_NO_COMMIT)) {
        if (!(f = flux_future_create (NULL, NULL)))
            goto error;
        flux_future_set_flux (f, h);
        if (zlist_size (reslog->pending) == 0)
            flux_future_fulfill (f, NULL, NULL);
    }
    else {
        if (!(val = eventlog_entry_encode (event)))
            goto error;
        if (!(txn = flux_kvs_txn_create ()))
            goto error;
        if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, RESLOG_KEY, val) < 0)
            goto error;
        if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
            goto error;
    }
    if (!(info = event_info_create (event, request)))
        goto error;
    if (flux_future_aux_set (f,
                             auxkey,
                             info,
                             (flux_free_f)event_info_destroy) < 0) {
        event_info_destroy (info);
        goto error;
    }
    if (flux_future_then (f, -1, post_continuation, reslog) < 0)
        goto error;
    if (zlist_append (reslog->pending, f) < 0)
        goto nomem;
    free (val);
    flux_kvs_txn_destroy (txn);
    json_decref (event);
    return 0;
nomem:
    errno = ENOMEM;
error:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    ERRNO_SAFE_WRAP (free, val);
    ERRNO_SAFE_WRAP (json_decref, event);
    return -1;
}

void reslog_remove_callback (struct reslog *reslog, reslog_cb_f cb, void *arg)
{
    if (reslog) {
        struct reslog_watcher w = { .cb = cb, .arg = arg };
        zlist_remove (reslog->watchers, &w);
    }
}

int reslog_add_callback (struct reslog *reslog, reslog_cb_f cb, void *arg)
{
    struct reslog_watcher *w;

    if (!reslog) {
        errno = EINVAL;
        return -1;
    }
    if (!(w = calloc (1, sizeof (*w))))
        return -1;
    w->cb = cb;
    w->arg = arg;
    if (zlist_append (reslog->watchers, w) < 0) {
        free (w);
        errno = ENOMEM;
        return -1;
    }
    zlist_freefn (reslog->watchers, w, free, true);
    return 0;
}

// returns true if streaming should continue
static bool send_backlog (struct reslog *reslog, const flux_msg_t *msg)
{
    flux_t *h = reslog->ctx->h;
    json_t *entry = zlistx_first (reslog->eventlog);
    while (entry) {
        if (notify_one_consumer (reslog, msg, entry) < 0)
            goto error;
        entry = zlistx_next (reslog->eventlog);
    }
    if (flux_respond_pack (h, msg, "{s:[]}", "events") < 0) // delimiter
        goto error;
    return true;
error:
    flux_log_error (h, "error responding to journal request");
    return false;
}

static void journal_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct reslog *reslog = arg;
    const char *errstr = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errstr = "journal requires streaming RPC flag";
        goto error;
    }
    if (!send_backlog (reslog, msg))
        return;
    if (flux_msglist_append (reslog->consumers, msg) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to journal request");
}

static void journal_cancel_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct reslog *reslog = arg;

    if (flux_msglist_cancel (h, reslog->consumers, msg) < 0)
        flux_log_error (h, "error handling journal-cancel");
}

void reslog_disconnect (struct reslog *reslog, const flux_msg_t *msg)
{
    flux_t *h = reslog->ctx->h;
    if (flux_msglist_disconnect (reslog->consumers, msg) < 0) {
        flux_log_error (h, "error handling resource.disconnect (journal)");
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "resource.journal",
        journal_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "resource.journal-cancel",
        journal_cancel_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void reslog_destroy (struct reslog *reslog)
{
    if (reslog) {
        int saved_errno = errno;
        flux_msg_handler_delvec (reslog->handlers);
        if (reslog->pending) {
            flux_future_t *f;
            while ((f = zlist_pop (reslog->pending)))
                (void)post_handler (reslog, f);
            zlist_destroy (&reslog->pending);
        }
        if (reslog->consumers) {
            const flux_msg_t *msg;
            flux_t *h = reslog->ctx->h;

            msg = flux_msglist_first (reslog->consumers);
            while (msg) {
                if (flux_respond_error (h, msg, ENODATA, NULL) < 0)
                    flux_log_error (h, "error responding to journal request");
                flux_msglist_delete (reslog->consumers);
                msg = flux_msglist_next (reslog->consumers);
            }
            flux_msglist_destroy (reslog->consumers);
        }
        zlist_destroy (&reslog->watchers);
        zlistx_destroy (&reslog->eventlog);
        free (reslog);
        errno = saved_errno;
    }
}

static void entry_destructor (void **item)
{
    if (*item) {
        json_decref (*item);
        *item = NULL;
    }
}

static void *entry_duplicator (const void *item)
{
    return json_incref ((json_t *) item);
}

struct reslog *reslog_create (struct resource_ctx *ctx,
                              json_t *eventlog,
                              int journal_max)
{
    struct reslog *reslog;

    if (!(reslog = calloc (1, sizeof (*reslog))))
        return NULL;
    reslog->ctx = ctx;
    reslog->journal_max = journal_max;
    if (!(reslog->pending = zlist_new ())
        || !(reslog->watchers = zlist_new ()))
        goto nomem;
    zlist_comparefn (reslog->watchers, watcher_compare);
    if (!(reslog->eventlog = zlistx_new ()))
        goto nomem;
    zlistx_set_destructor (reslog->eventlog, entry_destructor);
    zlistx_set_duplicator (reslog->eventlog, entry_duplicator);
    if (eventlog) {
        size_t index;
        json_t *entry;
        json_array_foreach (eventlog, index, entry) {
            // historical resource-define events are not helpful
            if (match_event (entry, "resource-define"))
                continue;
            if (!zlistx_add_end (reslog->eventlog, entry))
                goto nomem;
        }
    }
    if (!(reslog->consumers = flux_msglist_create ()))
        goto error;
    if (flux_msg_handler_addvec (ctx->h, htab, reslog, &reslog->handlers) < 0)
        goto error;
    return reslog;
nomem:
    errno = ENOMEM;
error:
    reslog_destroy (reslog);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
