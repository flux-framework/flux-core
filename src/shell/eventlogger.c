/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libeventlog/eventlog.h"
#include "eventlogger.h"

struct eventlog_batch {
    zlist_t *entries;
    flux_kvs_txn_t *txn;
    flux_watcher_t *timer;
    struct eventlogger *ev;
};

struct eventlogger {
    int refcount;
    flux_t *h;
    double batch_timeout;
    double commit_timeout;
    zlist_t *pending;
    struct eventlog_batch *current;
    struct eventlogger_ops ops;
    void *arg;
};

int eventlogger_set_commit_timeout (struct eventlogger *ev, double timeout)
{
    if (!ev || (timeout < 0. && timeout != -1.)) {
        errno = EINVAL;
        return -1;
    }
    ev->commit_timeout = timeout;
    return 0;
}

static void eventlog_batch_destroy (struct eventlog_batch *batch)
{
    if (batch) {
        if (batch->entries)
            zlist_destroy (&batch->entries);
        flux_kvs_txn_destroy (batch->txn);
        flux_watcher_destroy (batch->timer);
        free (batch);
    }
}

static void eventlogger_batch_complete (struct eventlogger *ev,
                                        struct eventlog_batch *batch)
{
    zlist_remove (ev->pending, batch);
    if (--ev->refcount == 0 && ev->ops.idle)
        (*ev->ops.idle) (ev, ev->arg);
}

static int eventlogger_batch_start (struct eventlogger *ev,
                                    struct eventlog_batch *batch)
{
    if (zlist_append (ev->pending, batch) < 0)
        return -1;
    zlist_freefn (ev->pending,
                  batch,
                  (zlist_free_fn *) eventlog_batch_destroy,
                  true);

    /*  If refcount just increased to 1, notify that eventlogger is busy */
    if (++ev->refcount == 1 && ev->ops.busy)
        (*ev->ops.busy) (ev, ev->arg);
    return 0;
}

static void eventlog_batch_error (struct eventlog_batch *batch, int errnum)
{
    struct eventlogger *ev = batch->ev;
    json_t *entry;
    if (!ev->ops.err)
        return;
    entry = zlist_first (batch->entries);
    while (entry) {
        (*ev->ops.err) (ev, errnum, entry);
        entry = zlist_next (batch->entries);
    }
}

static void commit_cb (flux_future_t *f, void *arg)
{
    struct eventlog_batch *batch = arg;
    if (flux_future_get (f, NULL) < 0)
        eventlog_batch_error (batch, errno);
    eventlogger_batch_complete (batch->ev, batch);
    flux_future_destroy (f);
}

static void
timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct eventlog_batch *batch = arg;
    struct eventlogger *ev = batch->ev;
    flux_t *h = ev->h;
    double timeout = ev->commit_timeout;
    flux_future_t *f = NULL;
    int flags = FLUX_KVS_TXN_COMPACT;

    if (!(f = flux_kvs_commit (h, NULL, flags, batch->txn))
        || flux_future_then (f, timeout, commit_cb, batch) < 0) {
        eventlog_batch_error (batch, errno);
        return;
    }
    batch->ev->current = NULL;
}

static struct eventlog_batch * eventlog_batch_create (struct eventlogger *ev)
{
    struct eventlog_batch *batch = calloc (1, sizeof (*batch));
    if (!batch)
        return NULL;
    flux_reactor_t *r = flux_get_reactor (ev->h);
    batch->ev = ev;
    batch->entries = zlist_new ();
    batch->txn = flux_kvs_txn_create ();
    batch->timer = flux_timer_watcher_create (r,
                                              ev->batch_timeout, 0.,
                                              timer_cb,
                                              batch);
    if (!batch->entries || !batch->txn || !batch->timer) {
        eventlog_batch_destroy (batch);
        return NULL;
    }
    flux_watcher_start (batch->timer);
    return batch;
}

void eventlogger_destroy (struct eventlogger *ev)
{
    if (ev) {
        if (ev->pending)
            zlist_destroy (&ev->pending);
        free (ev);
    }
}

struct eventlogger *eventlogger_create (flux_t *h,
                                        double timeout,
                                        struct eventlogger_ops *ops,
                                        void *arg)
{
    struct eventlogger *ev = calloc (1, sizeof (*ev));
    if (ev) {
        ev->pending = zlist_new ();
        if (!ev->pending) {
            eventlogger_destroy (ev);
            return NULL;
        }
        ev->h = h;
        ev->batch_timeout = timeout;
        ev->commit_timeout = -1.;
        ev->current = NULL;
        ev->ops = *ops;
        ev->arg = arg;
    }
    return ev;
}

static struct eventlog_batch * eventlog_batch_get (struct eventlogger *ev)
{
    struct eventlog_batch *batch = ev->current;
    if (!batch) {
        if (!(ev->current = eventlog_batch_create (ev)))
            return NULL;
        batch = ev->current;
        eventlogger_batch_start (ev, batch);
    }
    return batch;
}

static int append_wait (struct eventlogger *ev,
                        const char *path,
                        const char *entrystr)
{
    /*  append_wait also appends all pending transactions synchronously  */
    struct eventlog_batch *batch = eventlog_batch_get (ev);
    if (!batch)
        return -1;

    if (flux_kvs_txn_put (ev->current->txn,
                          FLUX_KVS_APPEND,
                          path, entrystr) < 0)
        return -1;

    return eventlogger_flush (ev);
}

static int append_async (struct eventlogger *ev,
                         const char *path,
                         json_t *entry,
                         const char *entrystr)
{
    struct eventlog_batch *batch = eventlog_batch_get (ev);

    if (!batch)
        return -1;
    if (flux_kvs_txn_put (ev->current->txn,
                          FLUX_KVS_APPEND,
                          path,
                          entrystr) < 0)
            return -1;

    if (zlist_append (batch->entries, entry) < 0)
        return -1;
    json_incref (entry);
    zlist_freefn (batch->entries,
                  entry,
                  (zlist_free_fn *) json_decref,
                  true);
    return 0;
}

int eventlogger_append_entry (struct eventlogger *ev,
                              int flags,
                              const char *path,
                              json_t *entry)
{
    char *entrystr = NULL;
    int rc = -1;

    if (!(entrystr = eventlog_entry_encode (entry)))
        return -1;

    if (flags & EVENTLOGGER_FLAG_WAIT)
        rc = append_wait (ev, path, entrystr);
    else
        rc = append_async (ev, path, entry, entrystr);
    free (entrystr);
    return rc;
}

int eventlogger_append (struct eventlogger *ev,
                        int flags,
                        const char *path,
                        const char *name,
                        const char *context)
{
    int rc = -1;
    json_t *entry = NULL;

    if (!(entry = eventlog_entry_create (0., name, context)))
        goto out;
    rc = eventlogger_append_entry (ev, flags, path, entry);
out:
    json_decref (entry);
    return rc;
}

int eventlogger_flush (struct eventlogger *ev)
{
    int rc = -1;
    flux_future_t *f = NULL;
    struct eventlog_batch *batch;
    int flags = FLUX_KVS_TXN_COMPACT;

    if (!(batch = eventlog_batch_get (ev)))
        return -1;

    if (!(f = flux_kvs_commit (ev->h, NULL, flags, ev->current->txn))
        || flux_future_wait_for (f, ev->commit_timeout) < 0)
        goto out;
    if ((rc = flux_future_get (f, NULL)) < 0)
        eventlog_batch_error (batch, errno);

    eventlogger_batch_complete (ev, ev->current);
    ev->current = NULL;
out:
    flux_future_destroy (f);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
