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
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "eventlog.h"
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
    char *ns;
    double batch_timeout;
    double commit_timeout;
    zlist_t *pending;
    struct eventlog_batch *current;
    struct eventlogger_ops ops;
    void *arg;
};

static void eventlogger_decref (struct eventlogger *ev)
{
    if (ev && --ev->refcount == 0) {
        free (ev->ns);
        if (ev->pending) {
            assert (zlist_size (ev->pending) == 0);
            zlist_destroy (&ev->pending);
        }
        free (ev);
    }
}

static void eventlogger_incref (struct eventlogger *ev)
{
    ev->refcount++;
}

int eventlogger_setns (struct eventlogger *ev, const char *ns)
{
    char *s = NULL;
    if (!ev || !ns) {
        errno = EINVAL;
        return -1;
    }
    if (!(s = strdup (ns)))
        return -1;
    free (ev->ns);
    ev->ns = s;
    return 0;
}

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
        flux_watcher_destroy (batch->timer);
        flux_kvs_txn_destroy (batch->txn);
        free (batch);
    }
}

static void eventlogger_batch_complete (struct eventlog_batch *batch)
{
    struct eventlogger *ev = batch->ev;

    /*  zlist_remove destroys batch */
    zlist_remove (ev->pending, batch);

    /*  If no more batches on list, notify idle */
    if (zlist_size (ev->pending) == 0) {
        if (ev->ops.idle)
            (*ev->ops.idle) (ev, ev->arg);
        eventlogger_decref (ev);
    }
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

    /*  If we were idle before, now notify that eventlogger is busy */
    if (zlist_size (ev->pending) == 1) {
        if (ev->ops.busy)
            (*ev->ops.busy) (ev, ev->arg);
        eventlogger_incref (ev);
    }
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
        (*ev->ops.err) (ev, ev->arg, errnum, entry);
        entry = zlist_next (batch->entries);
    }
    /*  zlist_remove destroys batch */
    zlist_remove (ev->pending, batch);
}

static void commit_cb (flux_future_t *f, void *arg)
{
    struct eventlog_batch *batch = arg;
    eventlogger_batch_complete (batch);
    flux_future_destroy (f);
}

static flux_future_t *eventlogger_commit_batch (struct eventlogger *ev,
                                                struct eventlog_batch *batch)
{
    flux_future_t *f = NULL;
    flux_future_t *fc = NULL;
    int flags = FLUX_KVS_TXN_COMPACT;

    if (!batch) {
        /*  If batch is NULL, return a fulfilled future immediately.
         *
         *  Note: There isn't much we can do if flux_future_create()
         *   fails, until the eventlogger gets a logging interface.
         *   Just return the NULL future to indicate some kind of
         *   failure occurred. Likely other parts of the system are
         *   in big trouble anyway...
         */
        if ((f = flux_future_create (NULL, NULL)))
            flux_future_fulfill (f, NULL, NULL);
    }
    else {
        /*  Otherwise, stop any pending timer watcher and start a
         *   kvs commit operation. Call eventlogger_batch_complete()
         *   when the commit is done, and return a future to the caller
         *   that will be fulfilled on return from that function.
         */
        flux_watcher_stop (batch->timer);
        if (!(fc = flux_kvs_commit (ev->h, ev->ns, flags, batch->txn)))
            return NULL;
        if (!(f = flux_future_and_then (fc, commit_cb, batch)))
            flux_future_destroy (fc);
    }
    return f;
}

static flux_future_t *commit_batch (struct eventlogger *ev,
                                    struct eventlog_batch **batchp)
{
    struct eventlog_batch *batch = ev->current;
    if (batchp)
        *batchp = batch;
    ev->current = NULL;
    return eventlogger_commit_batch (ev, batch);
}

flux_future_t *eventlogger_commit (struct eventlogger *ev)
{
    return commit_batch (ev, NULL);
}

static void timer_commit_cb (flux_future_t *f, void *arg)
{
    struct eventlog_batch *batch = arg;
    if (flux_future_get (f, NULL) < 0)
        eventlog_batch_error (batch, errno);
    flux_future_destroy (f);
}

static void
timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct eventlog_batch *batch = arg;
    double timeout = batch->ev->commit_timeout;
    flux_future_t *f = NULL;

    batch->ev->current = NULL;
    if (!(f = eventlogger_commit_batch (batch->ev, batch))
        || flux_future_then (f, timeout, timer_commit_cb, batch) < 0) {
        eventlog_batch_error (batch, errno);
        flux_future_destroy (f);
    }
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
    eventlogger_decref (ev);
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
        ev->refcount = 1;
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

int eventlogger_append_vpack (struct eventlogger *ev,
                              int flags,
                              const char *path,
                              const char *name,
                              const char *fmt,
                              va_list ap)
{
    int rc;
    json_t *entry = NULL;

    if (!(entry = eventlog_entry_vpack (0., name, fmt, ap)))
        return -1;
    rc = eventlogger_append_entry (ev, flags, path, entry);
    json_decref (entry);
    return rc;
}

int eventlogger_append_pack (struct eventlogger *ev,
                             int flags,
                             const char *path,
                             const char *name,
                             const char *fmt, ...)
{
    int rc = -1;
    va_list ap;
    va_start (ap, fmt);
    rc = eventlogger_append_vpack (ev, flags, path, name, fmt, ap);
    va_end (ap);
    return rc;
}

int eventlogger_flush (struct eventlogger *ev)
{
    struct eventlog_batch *batch;
    int rc = -1;
    flux_future_t *f;

    if (!(f = commit_batch (ev, &batch))
        || flux_future_wait_for (f, ev->commit_timeout) < 0)
        goto out;
    if ((rc = flux_future_get (f, NULL)) < 0)
        eventlog_batch_error (batch, errno);
out:
    flux_future_destroy (f);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
