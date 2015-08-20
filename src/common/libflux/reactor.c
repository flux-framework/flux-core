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
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <czmq.h>

#include "handle.h"
#include "reactor.h"
#include "message.h"
#include "response.h"
#include "tagpool.h"
#include "ev_flux.h"

#include "src/common/libev/ev.h"
#include "src/common/libutil/ev_zmq.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/coproc.h"

struct reactor {
    flux_t h;

    struct ev_loop *loop;
    int loop_rc;

    zlist_t *msg_watchers; /* all msg_watchers are on this list */
    zlist_t *msg_waiters;  /* waiting coproc watchers are also on this list */
    struct flux_msg_watcher *current;
    struct ev_flux handle_w;
};

struct flux_msg_watcher {
    flux_t h;
    struct flux_match match;
    flux_msg_watcher_f fn;
    void *arg;
    flux_free_f arg_free;

    /* coproc */
    coproc_t coproc;
    zlist_t *backlog;
    struct flux_match wait_match;
};

struct flux_fd_watcher {
    flux_t h;
    flux_fd_watcher_f fn;
    void *arg;
    ev_io w;
};

struct flux_zmq_watcher {
    flux_t h;
    flux_zmq_watcher_f fn;
    void *arg;
    ev_zmq w;
};

struct flux_timer_watcher {
    flux_t h;
    flux_timer_watcher_f fn;
    void *arg;
    ev_timer w;
};

static void handle_cb (struct ev_loop *loop, struct ev_flux *hw, int revents);


static void reactor_destroy (void *arg)
{
    struct reactor *r = arg;

    zlist_destroy (&r->msg_watchers);
    zlist_destroy (&r->msg_waiters);
    ev_loop_destroy (r->loop);
    free (r);
}

static struct reactor *reactor_get (flux_t h)
{
    struct reactor *r = flux_aux_get (h, "flux::reactor");
    if (!r) {
        r = xzmalloc (sizeof (*r));
        r->loop = ev_loop_new (EVFLAG_AUTO);
        r->msg_watchers = zlist_new ();
        r->msg_waiters = zlist_new ();
        if (!r->loop || !r->msg_watchers || !r->msg_waiters)
            oom ();
        r->h = h;
        ev_flux_init (&r->handle_w, handle_cb, h, EV_READ);
        flux_aux_set (h, "flux::reactor", r, reactor_destroy);
    }
    return r;
}

int flux_reactor_start (flux_t h)
{
    struct reactor *r = reactor_get (h);
    r->loop_rc = 0;
    ev_run (r->loop, 0);
    return r->loop_rc;
}

void flux_reactor_stop (flux_t h)
{
    struct reactor *r = reactor_get (h);
    r->loop_rc = 0;
    ev_break (r->loop, EVBREAK_ALL);
}

void flux_reactor_stop_error (flux_t h)
{
    struct reactor *r = reactor_get (h);
    r->loop_rc = -1;
    ev_break (r->loop, EVBREAK_ALL);
}

static int events_to_libev (int events)
{
    int e = 0;
    if (events & FLUX_POLLIN)
        e |= EV_READ;
    if (events & FLUX_POLLOUT)
        e |= EV_WRITE;
    if (events & FLUX_POLLERR)
        e |= EV_ERROR;
    return e;
}

static int libev_to_events (int events)
{
    int e = 0;
    if (events & EV_READ)
        e |= FLUX_POLLIN;
    if (events & EV_WRITE)
        e |= FLUX_POLLOUT;
    if (events & EV_ERROR)
        e |= FLUX_POLLERR;
    return e;
}

/* Message watchers
 */

static void copy_match (struct flux_match *dst,
                        const struct flux_match src)
{
    if (dst->topic_glob)
        free (dst->topic_glob);
    *dst = src;
    dst->topic_glob = src.topic_glob ? xstrdup (src.topic_glob) : NULL;
}

static struct flux_msg_watcher *find_msg_watcher (struct reactor *r,
                                                  const flux_msg_t *msg)
{
    struct flux_msg_watcher *w = zlist_first (r->msg_watchers);
    while (w) {
        if (flux_msg_cmp (msg, w->match))
            break;
        w = zlist_next (r->msg_watchers);
    }
    return w;
}

static struct flux_msg_watcher *find_waiting_msg_watcher (struct reactor *r,
                                                          const flux_msg_t *msg)
{
    struct flux_msg_watcher *w = zlist_first (r->msg_waiters);
    while (w) {
        if (flux_msg_cmp (msg, w->wait_match))
            break;
        w = zlist_next (r->msg_waiters);
    }
    return w;
}

static int backlog_append (struct flux_msg_watcher *w, flux_msg_t **msg)
{
    if (!w->backlog && !(w->backlog = zlist_new ()))
        oom ();
    if (zlist_append (w->backlog, *msg) < 0)
        oom ();
    *msg = NULL;
    return 0;
}

static int backlog_flush (struct flux_msg_watcher *w)
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
    struct reactor *r = reactor_get (h);
    int rc = -1;

    if (!r->current || !r->current->coproc) {
        errno = EINVAL;
        goto done;
    }
    struct flux_msg_watcher *w = r->current;
    copy_match (&w->wait_match, match);
    if (zlist_append (r->msg_waiters, w) < 0)
        oom ();
    if (coproc_yield (w->coproc) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int coproc_cb (coproc_t c, void *arg)
{
    struct flux_msg_watcher *w = arg;
    flux_msg_t *msg;
    int type;
    int rc = -1;
    if (!(msg = flux_recv (w->h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            rc = 0;
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    w->fn (w->h, w, msg, w->arg);
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int resume_coproc (struct flux_msg_watcher *w)
{
    struct reactor *r = reactor_get (w->h);
    int coproc_rc, rc = -1;

    r->current = w;
    if (coproc_resume (w->coproc) < 0)
        goto done;
    if (!coproc_returned (w->coproc, &coproc_rc)) {
        rc = 0;
        goto done;
    }
    if (backlog_flush (w) < 0)
        goto done;
    rc = coproc_rc;
done:
    r->current = NULL;
    return rc;
}

static int start_coproc (struct flux_msg_watcher *w)
{
    struct reactor *r = reactor_get (w->h);
    int coproc_rc, rc = -1;

    r->current = w;
    if (!w->coproc && !(w->coproc = coproc_create (coproc_cb)))
        goto done;
    if (coproc_start (w->coproc, w) < 0)
        goto done;
    if (!coproc_returned (w->coproc, &coproc_rc)) {
        rc = 0;
        goto done;
    }
    if (backlog_flush (w) < 0)
        goto done;
    rc = coproc_rc;
done:
    r->current = NULL;
    return rc;
}

static void handle_cb (struct ev_loop *loop, struct ev_flux *hw, int revents)
{
    void *ptr = (char *)hw - offsetof (struct reactor, handle_w);
    struct reactor *r = ptr;
    struct flux_msg_watcher *w;
    flux_msg_t *msg = NULL;
    int type;

    if (revents & EV_ERROR)
        goto fatal;
    if (!(msg = flux_recv (r->h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
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
    if ((w = find_waiting_msg_watcher (r, msg))) {
        if (flux_requeue (r->h, msg, FLUX_RQ_HEAD) < 0)
            goto fatal;
        zlist_remove (r->msg_waiters, w);
        if (resume_coproc (w) < 0)
            goto fatal;
    /* Message matches a handler.
     * If coproc already running, queue message as backlog.
     * Else if FLUX_O_COPROC, start coproc.
     * If coprocs not enabled, call handler directly.
     */
    } else if ((w = find_msg_watcher (r, msg))) {
        if (w->coproc && coproc_started (w->coproc)) {
            if (backlog_append (w, &msg) < 0) /* msg now property of backlog */
                goto fatal;
        } else if ((flux_flags_get (r->h) & FLUX_O_COPROC)) {
            if (flux_requeue (r->h, msg, FLUX_RQ_HEAD) < 0)
                goto fatal;
            if (start_coproc (w) < 0)
                goto fatal;
        } else {
            w->fn (r->h, w, msg, w->arg);
        }
    /* Message matched nothing.
     * Respond with ENOSYS if it was a request.
     * Else log it if FLUX_O_TRACE
     */
    } else {
        if (type == FLUX_MSGTYPE_REQUEST) {
            if (flux_respond (r->h, msg, ENOSYS, NULL))
                goto done;
        } else if (flux_flags_get (r->h) & FLUX_O_TRACE) {
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
    flux_reactor_stop_error (r->h);
    FLUX_FATAL (r->h);
}

flux_msg_watcher_t *flux_msg_watcher_create (const struct flux_match match,
                                             flux_msg_watcher_f cb, void *arg)
{
    struct flux_msg_watcher *w = xzmalloc (sizeof (*w));
    copy_match (&w->match, match);
    w->fn = cb;
    w->arg = arg;
    return w;
}

void flux_msg_watcher_start (flux_t h, struct flux_msg_watcher *w)
{
    struct reactor *r = reactor_get (h);
    ev_flux_start (r->loop, &r->handle_w);
    if (zlist_push (r->msg_watchers, w) < 0)
        oom ();
    w->h = h;
}

void flux_msg_watcher_stop (flux_t h, struct flux_msg_watcher *w)
{
    struct reactor *r = reactor_get (h);

    zlist_remove (r->msg_waiters, w);
    zlist_remove (r->msg_watchers, w);
    if (zlist_size (r->msg_watchers) == 0)
        ev_flux_stop (r->loop, &r->handle_w);
}

void flux_msg_watcher_destroy (struct flux_msg_watcher *w)
{
    if (w) {
        if (w->match.topic_glob)
            free (w->match.topic_glob);
        if (w->coproc)
            coproc_destroy (w->coproc);
        if (w->backlog) {
            flux_msg_t *msg;
            while ((msg = zlist_pop (w->backlog)))
                flux_msg_destroy (msg);
            zlist_destroy (&w->backlog);
        }
        if (w->wait_match.topic_glob)
            free (w->wait_match.topic_glob);
        if (w->arg_free)
            w->arg_free (w->arg);
        free (w);
    }
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
        flux_msg_watcher_start (h, tab[i].w);
    }
    return 0;
error:
    while (i >= 0) {
        if (tab[i].w) {
            flux_msg_watcher_stop (h, tab[i].w);
            flux_msg_watcher_destroy (tab[i].w);
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
            flux_msg_watcher_stop (h, tab[i].w);
            flux_msg_watcher_destroy (tab[i].w);
            tab[i].w = NULL;
        }
    }
}

/* file descriptors
 */

static void io_cb (struct ev_loop *loop, ev_io *iow, int revents)
{
    void *ptr = (char *)iow - offsetof (struct flux_fd_watcher, w);
    struct flux_fd_watcher *w = ptr;
    w->fn (w->h, w, iow->fd, libev_to_events (revents), w->arg);
}

flux_fd_watcher_t *flux_fd_watcher_create (int fd, int events,
                                           flux_fd_watcher_f cb, void *arg)
{
    struct flux_fd_watcher *w = xzmalloc (sizeof (*w));
    w->fn = cb;
    w->arg = arg;
    ev_io_init (&w->w, io_cb, fd, events_to_libev (events) & ~EV_ERROR);
    return w;
}

void flux_fd_watcher_start (flux_t h, flux_fd_watcher_t *w)
{
    struct reactor *r = reactor_get (h);
    ev_io_start (r->loop, &w->w);
    w->h = h;
}

void flux_fd_watcher_stop (flux_t h, flux_fd_watcher_t *w)
{
    struct reactor *r = reactor_get (h);
    ev_io_stop (r->loop, &w->w);
}

void flux_fd_watcher_destroy (flux_fd_watcher_t *w)
{
    if (w)
        free (w);
}

/* 0MQ sockets
 */

static void zmq_cb (struct ev_loop *loop, ev_zmq *zw, int revents)
{
    void *ptr = (char *)zw - offsetof (struct flux_zmq_watcher, w);
    struct flux_zmq_watcher *w = ptr;
    w->fn (w->h, w, zw->zsock, libev_to_events (revents), w->arg);
}

flux_zmq_watcher_t *flux_zmq_watcher_create (void *zsock, int events,
                                             flux_zmq_watcher_f cb, void *arg)
{
    struct flux_zmq_watcher *w = xzmalloc (sizeof (*w));
    w->fn = cb;
    w->arg = arg;
    ev_zmq_init (&w->w, zmq_cb, zsock, events_to_libev (events) & ~EV_ERROR);
    return w;
}

void flux_zmq_watcher_start (flux_t h, flux_zmq_watcher_t *w)
{
    struct reactor *r = reactor_get (h);
    ev_zmq_start (r->loop, &w->w);
    w->h = h;
}

void flux_zmq_watcher_stop (flux_t h, flux_zmq_watcher_t *w)
{
    struct reactor *r = reactor_get (h);
    ev_zmq_stop (r->loop, &w->w);
}

void flux_zmq_watcher_destroy (flux_zmq_watcher_t *w)
{
    if (w)
        free (w);
}

/* Timer
 */

static void timer_cb (struct ev_loop *loop, ev_timer *tw, int revents)
{
    const size_t off = offsetof (struct flux_timer_watcher, w);
    void *ptr = (char *)tw - off;
    struct flux_timer_watcher *w = ptr;
    w->fn (w->h, w, libev_to_events (revents), w->arg);
}

flux_timer_watcher_t *flux_timer_watcher_create (double after, double repeat,
                                                 flux_timer_watcher_f cb,
                                                 void *arg)
{
    if (after < 0 || repeat < 0) {
        errno = EINVAL;
        return NULL;
    }
    struct flux_timer_watcher *w = xzmalloc (sizeof (*w));
    w->fn = cb;
    w->arg = arg;
    ev_timer_init (&w->w, timer_cb, after, repeat);
    return w;
}

void flux_timer_watcher_start (flux_t h, flux_timer_watcher_t *w)
{
    struct reactor *r = reactor_get (h);
    ev_timer_start (r->loop, &w->w);
    w->h = h;
}

void flux_timer_watcher_stop (flux_t h, flux_timer_watcher_t *w)
{
    struct reactor *r = reactor_get (h);
    ev_timer_stop (r->loop, &w->w);
}

void flux_timer_watcher_destroy (flux_timer_watcher_t *w)
{
    if (w)
        free (w);
}

void flux_timer_watcher_reset (flux_timer_watcher_t *w,
                               double after, double repeat)
{
    ev_timer_set (&w->w, after, repeat);
}
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
