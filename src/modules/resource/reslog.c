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
    flux_t *h;
    zlist_t *pending;       // list of pending futures
    zlist_t *watchers;
};

static const char *auxkey = "flux::event_info";

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
    const char *name;
    json_t *context;
    struct reslog_watcher *w;

    if (json_unpack (event,
                     "{s:s s:o}",
                     "name", &name,
                     "context", &context) < 0) {
        flux_log (reslog->h, LOG_ERR, "error unpacking event for callback");
        return;
    }
    w = zlist_first (reslog->watchers);
    while (w) {
        if (w->cb)
            w->cb (reslog, name, context, w->arg);
        w = zlist_next (reslog->watchers);
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
    struct event_info *info = flux_future_aux_get (f, auxkey);
    int rc;

    if ((rc = flux_future_get (f, NULL)) < 0) {
        flux_log_error (reslog->h, "committing to %s", RESLOG_KEY);
        if (info->msg) {
            if (flux_respond_error (reslog->h, info->msg, errno, NULL) < 0)
                flux_log_error (reslog->h, "responding to request after post");
        }
        goto done;
    }
    else {
        if (info->msg) {
            if (flux_respond (reslog->h, info->msg, NULL) < 0)
                flux_log_error (reslog->h, "responding to request after post");
        }
    }
    notify_callbacks (reslog, info->event);
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
    if ((flags & EVENT_NO_COMMIT)) {
        if (!(f = flux_future_create (NULL, NULL)))
            goto error;
        flux_future_set_flux (f, reslog->h);
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
        if (!(f = flux_kvs_commit (reslog->h, NULL, 0, txn)))
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

void reslog_destroy (struct reslog *reslog)
{
    if (reslog) {
        int saved_errno = errno;
        if (reslog->pending) {
            flux_future_t *f;
            while ((f = zlist_pop (reslog->pending)))
                (void)post_handler (reslog, f);
            zlist_destroy (&reslog->pending);
        }
        zlist_destroy (&reslog->watchers);
        free (reslog);
        errno = saved_errno;
    }
}

struct reslog *reslog_create (flux_t *h)
{
    struct reslog *reslog;

    if (!(reslog = calloc (1, sizeof (*reslog))))
        return NULL;
    reslog->h = h;
    if (!(reslog->pending = zlist_new ())
        || !(reslog->watchers = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zlist_comparefn (reslog->watchers, watcher_compare);
    return reslog;
error:
    reslog_destroy (reslog);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
