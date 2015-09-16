/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>

#include "message.h"
#include "reactor.h"
#include "dispatch.h"
#include "response.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/coproc.h"

#define MSG_WATCHER_SIG 2000

struct dispatch {
    flux_t h;
    zlist_t *msg_watchers; /* all msg_watchers are on this list */
    zlist_t *msg_waiters;  /* waiting coproc watchers are also on this list */
    struct msg_watcher *current;
    flux_watcher_t *handle_w;
};

#define MSG_WATCHER_MAGIC 0x44433322
struct msg_watcher {
    int magic;
    flux_t h;
    struct flux_match match;
    flux_msg_watcher_f fn;
    void *arg;
    flux_free_f arg_free;
    flux_watcher_t *w;

    /* coproc */
    coproc_t coproc;
    zlist_t *backlog;
    struct flux_match wait_match;
};

static void handle_cb (flux_t h, flux_watcher_t *w, int revents, void *arg);


static void dispatch_destroy (void *arg)
{
    struct dispatch *d = arg;

    flux_watcher_destroy (d->handle_w);
    zlist_destroy (&d->msg_watchers);
    zlist_destroy (&d->msg_waiters);
    free (d);
}

static struct dispatch *dispatch_get (flux_t h)
{
    struct dispatch *d = flux_aux_get (h, "flux::dispatch");
    if (!d) {
        d = xzmalloc (sizeof (*d));
        d->msg_watchers = zlist_new ();
        d->msg_waiters = zlist_new ();
        if (!d->msg_watchers || !d->msg_waiters)
            oom ();
        d->h = h;
        d->handle_w = flux_handle_watcher_create (h, FLUX_POLLIN, handle_cb, d);
        if (!d->handle_w)
            oom ();
        flux_aux_set (h, "flux::dispatch", d, dispatch_destroy);
    }
    return d;
}

static void copy_match (struct flux_match *dst,
                        const struct flux_match src)
{
    if (dst->topic_glob)
        free (dst->topic_glob);
    *dst = src;
    dst->topic_glob = src.topic_glob ? xstrdup (src.topic_glob) : NULL;
}

static struct msg_watcher *find_msg_watcher (struct dispatch *d,
                                                  const flux_msg_t *msg)
{
    struct msg_watcher *w = zlist_first (d->msg_watchers);
    while (w) {
        if (flux_msg_cmp (msg, w->match))
            break;
        w = zlist_next (d->msg_watchers);
    }
    return w;
}

static struct msg_watcher *find_waiting_msg_watcher (struct dispatch *d,
                                                          const flux_msg_t *msg)
{
    struct msg_watcher *w = zlist_first (d->msg_waiters);
    while (w) {
        if (flux_msg_cmp (msg, w->wait_match))
            break;
        w = zlist_next (d->msg_waiters);
    }
    return w;
}

static int backlog_append (struct msg_watcher *w, flux_msg_t **msg)
{
    if (!w->backlog && !(w->backlog = zlist_new ()))
        oom ();
    if (zlist_append (w->backlog, *msg) < 0)
        oom ();
    *msg = NULL;
    return 0;
}

static int backlog_flush (struct msg_watcher *w)
{
    int errnum = 0;
    int rc = 0;

    if (w->backlog) {
        flux_msg_t *msg;
        while ((msg = zlist_pop (w->backlog))) {
            if (flux_requeue (w->h, msg, FLUX_RQ_TAIL) < 0) {
                if (errnum < errno) {
                    errnum = errno;
                    rc = -1;
                }
                flux_msg_destroy (msg);
            }
        }
    }
    if (errnum > 0)
        errno = errnum;
    return rc;
}

int flux_sleep_on (flux_t h, struct flux_match match)
{
    struct dispatch *d = dispatch_get (h);
    int rc = -1;

    if (!d->current || !d->current->coproc) {
        errno = EINVAL;
        goto done;
    }
    struct msg_watcher *mw = d->current;
    copy_match (&mw->wait_match, match);
    if (zlist_append (d->msg_waiters, mw) < 0)
        oom ();
    if (coproc_yield (mw->coproc) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int coproc_cb (coproc_t c, void *arg)
{
    struct msg_watcher *mw = arg;
    flux_msg_t *msg;
    int type;
    int rc = -1;
    if (!(msg = flux_recv (mw->h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            rc = 0;
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    mw->fn (mw->h, mw->w, msg, mw->arg);
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int resume_coproc (struct msg_watcher *mw)
{
    struct dispatch *d = dispatch_get (mw->h);
    int coproc_rc, rc = -1;

    d->current = mw;
    if (coproc_resume (mw->coproc) < 0)
        goto done;
    if (!coproc_returned (mw->coproc, &coproc_rc)) {
        rc = 0;
        goto done;
    }
    if (backlog_flush (mw) < 0)
        goto done;
    rc = coproc_rc;
done:
    d->current = NULL;
    return rc;
}

static int start_coproc (struct msg_watcher *mw)
{
    struct dispatch *d = dispatch_get (mw->h);
    int coproc_rc, rc = -1;

    d->current = mw;
    if (!mw->coproc && !(mw->coproc = coproc_create (coproc_cb)))
        goto done;
    if (coproc_start (mw->coproc, mw) < 0)
        goto done;
    if (!coproc_returned (mw->coproc, &coproc_rc)) {
        rc = 0;
        goto done;
    }
    if (backlog_flush (mw) < 0)
        goto done;
    rc = coproc_rc;
done:
    d->current = NULL;
    return rc;
}

static void handle_cb (flux_t reactor_h, flux_watcher_t *hw,
                       int revents, void *arg)
{
    struct dispatch *d = arg;
    struct msg_watcher *mw;
    flux_msg_t *msg = NULL;
    int type;

    if (revents & FLUX_POLLERR)
        goto fatal;
    if (!(msg = flux_recv (d->h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            goto fatal;
        else
            goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    /* Message matches a coproc that yielded.
     * Resume, arranging for msg to be returned next by flux_recv().
     */
    if ((mw = find_waiting_msg_watcher (d, msg))) {
        if (flux_requeue (d->h, msg, FLUX_RQ_HEAD) < 0)
            goto fatal;
        zlist_remove (d->msg_waiters, mw);
        if (resume_coproc (mw) < 0)
            goto fatal;
    /* Message matches a handler.
     * If coproc already running, queue message as backlog.
     * Else if FLUX_O_COPROC, start coproc.
     * If coprocs not enabled, call handler directly.
     */
    } else if ((mw = find_msg_watcher (d, msg))) {
        if (mw->coproc && coproc_started (mw->coproc)) {
            if (backlog_append (mw, &msg) < 0) /* msg now property of backlog */
                goto fatal;
        } else if ((flux_flags_get (d->h) & FLUX_O_COPROC)) {
            if (flux_requeue (d->h, msg, FLUX_RQ_HEAD) < 0)
                goto fatal;
            if (start_coproc (mw) < 0)
                goto fatal;
        } else {
            mw->fn (d->h, mw->w, msg, mw->arg);
        }
    /* Message matched nothing.
     * Respond with ENOSYS if it was a request.
     * Else log it if FLUX_O_TRACE
     */
    } else {
        if (type == FLUX_MSGTYPE_REQUEST) {
            if (flux_respond (d->h, msg, ENOSYS, NULL))
                goto done;
        } else if (flux_flags_get (d->h) & FLUX_O_TRACE) {
            const char *topic = NULL;
            (void)flux_msg_get_topic (msg, &topic);
            fprintf (stderr, "nomatch: %s '%s'\n", flux_msg_typestr (type),
                     topic ? topic : "");
        }
    }
done:
    flux_msg_destroy (msg);
    return;
fatal:
    flux_msg_destroy (msg);
    flux_reactor_stop_error (reactor_h);
    FLUX_FATAL (d->h);
}

static void msg_watcher_start (void *impl, flux_t h, flux_watcher_t *w)
{
    struct dispatch *d = dispatch_get (h);
    struct msg_watcher *mw = impl;

    if (mw) {
        assert (mw->magic == MSG_WATCHER_MAGIC);
        flux_watcher_start (h, d->handle_w);
        if (zlist_push (d->msg_watchers, mw) < 0)
            oom ();
        mw->h = h;
    }
}

static void msg_watcher_stop (void *impl, flux_t h, flux_watcher_t *w)
{
    struct dispatch *d = dispatch_get (h);
    struct msg_watcher *mw = impl;

    if (mw) {
        assert (mw->magic == MSG_WATCHER_MAGIC);
        zlist_remove (d->msg_waiters, mw);
        zlist_remove (d->msg_watchers, mw);
        if (zlist_size (d->msg_watchers) == 0)
            flux_watcher_stop (h, d->handle_w);
    }
}

static void msg_watcher_destroy (void *impl, flux_watcher_t *w)
{
    struct msg_watcher *mw = impl;

    if (mw) {
        assert (mw->magic == MSG_WATCHER_MAGIC);
        if (mw->match.topic_glob)
            free (mw->match.topic_glob);
        if (mw->coproc)
            coproc_destroy (mw->coproc);
        if (mw->backlog) {
            flux_msg_t *msg;
            while ((msg = zlist_pop (mw->backlog)))
                flux_msg_destroy (msg);
            zlist_destroy (&mw->backlog);
        }
        if (mw->wait_match.topic_glob)
            free (mw->wait_match.topic_glob);
        if (mw->arg_free)
            mw->arg_free (mw->arg);
        mw->magic = ~MSG_WATCHER_MAGIC;
        free (mw);
    }
}

flux_watcher_t *flux_msg_watcher_create (const struct flux_match match,
                                         flux_msg_watcher_f cb, void *arg)
{
    struct watcher_ops ops = {
        .start = msg_watcher_start,
        .stop = msg_watcher_stop,
        .destroy = msg_watcher_destroy,
    };
    struct msg_watcher *mw = xzmalloc (sizeof (*mw));
    flux_watcher_t *w;

    mw->magic = MSG_WATCHER_MAGIC;
    copy_match (&mw->match, match);
    mw->fn = cb;
    mw->arg = arg;
    w = flux_watcher_create (mw, ops, MSG_WATCHER_SIG, NULL, NULL);
    mw->w = w;

    return w;
}

int flux_msg_watcher_addvec (flux_t h, struct flux_msghandler tab[], void *arg)
{
    int i;
    struct flux_match match = FLUX_MATCH_ANY;

    for (i = 0; ; i++) {
        if (!tab[i].typemask && !tab[i].topic_glob && !tab[i].cb)
            break; /* FLUX_MSGHANDLER_TABLE_END */
        match.typemask = tab[i].typemask;
        match.topic_glob = tab[i].topic_glob;
        tab[i].w = flux_msg_watcher_create (match, tab[i].cb, arg);
        if (!tab[i].w)
            goto error;
        flux_watcher_start (h, tab[i].w);
    }
    return 0;
error:
    while (i >= 0) {
        if (tab[i].w) {
            flux_watcher_stop (h, tab[i].w);
            flux_watcher_destroy (tab[i].w);
            tab[i].w = NULL;
        }
        i--;
    }
    return -1;
}

void flux_msg_watcher_delvec (flux_t h, struct flux_msghandler tab[])
{
    int i;

    for (i = 0; ; i++) {
        if (!tab[i].typemask && !tab[i].topic_glob && !tab[i].cb)
            break; /* FLUX_MSGHANDLER_TABLE_END */
        if (tab[i].w) {
            flux_watcher_stop (h, tab[i].w);
            flux_watcher_destroy (tab[i].w);
            tab[i].w = NULL;
        }
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
