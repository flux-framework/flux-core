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
#include "handle_impl.h"
#include "message.h"
#include "response.h"
#include "tagpool.h"
#include "reactor_impl.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/coproc.h"

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

struct msg_watcher_compat {
    FluxMsgHandler fn;
    void *arg;
};

struct reactor {
    zlist_t *msg_watchers; /* all msg_watchers are on this list */
    zlist_t *msg_waiters;  /* waiting coproc watchers are also on this list */
    const struct flux_handle_ops *ops;
    void *impl;
    struct flux_msg_watcher *current;
};

static bool match_match (const struct flux_match m1,
                         const struct flux_match m2)
{
    if (m1.typemask != m2.typemask)
        return false;
    if (m1.matchtag != m2.matchtag)
        return false;
    if (m1.bsize != m2.bsize)
        return false;
    if (m1.topic_glob == NULL && m2.topic_glob != NULL)
        return false;
    if (m1.topic_glob != NULL && m2.topic_glob == NULL)
        return false;
    if (m1.topic_glob != NULL && m2.topic_glob != NULL
                              && strcmp (m1.topic_glob, m2.topic_glob) != 0)
        return false;
    return true;
}

static void copy_match (struct flux_match *dst,
                        const struct flux_match src)
{
    if (dst->topic_glob)
        free (dst->topic_glob);
    *dst = src;
    dst->topic_glob = src.topic_glob ? xstrdup (src.topic_glob) : NULL;
}

static struct flux_msg_watcher *msg_watcher_create (flux_t h,
                                          const struct flux_match match,
                                          flux_msg_watcher_f cb, void *arg)
{
    struct flux_msg_watcher *d = xzmalloc (sizeof (*d));
    d->h = h;
    copy_match (&d->match, match);
    d->fn = cb;
    d->arg = arg;
    return d;
}

static void msg_watcher_destroy (struct flux_msg_watcher *d)
{
    if (d) {
        if (d->match.topic_glob)
            free (d->match.topic_glob);
        if (d->coproc)
            coproc_destroy (d->coproc);
        if (d->backlog) {
            zmsg_t *zmsg;
            while ((zmsg = zlist_pop (d->backlog)))
                zmsg_destroy (&zmsg);
            zlist_destroy (&d->backlog);
        }
        if (d->wait_match.topic_glob)
            free (d->wait_match.topic_glob);
        if (d->arg_free)
            d->arg_free (d->arg);
        free (d);
    }
}

void reactor_destroy (struct reactor *r)
{
    struct flux_msg_watcher *d;
    if (r) {
        while ((d = zlist_pop (r->msg_watchers)))
            msg_watcher_destroy (d);
        zlist_destroy (&r->msg_watchers);
        zlist_destroy (&r->msg_waiters);
        free (r);
    }
}

struct reactor *reactor_create (void *impl, const struct flux_handle_ops *ops)
{
    struct reactor *r = xzmalloc (sizeof (*r));
    if (!(r->msg_watchers = zlist_new ()))
        oom ();
    if (!(r->msg_waiters = zlist_new ()))
        oom ();
    r->impl = impl;
    r->ops = ops;
    return r;
}

static struct flux_msg_watcher *find_msg_watcher (struct reactor *r,
                                                  zmsg_t *zmsg)
{
    struct flux_msg_watcher *d = zlist_first (r->msg_watchers);
    while (d) {
        if (flux_msg_cmp (zmsg, d->match))
            break;
        d = zlist_next (r->msg_watchers);
    }
    return d;
}

static struct flux_msg_watcher *find_waiting_msg_watcher (struct reactor *r,
                                                          zmsg_t *zmsg)
{
    struct flux_msg_watcher *d = zlist_first (r->msg_waiters);
    while (d) {
        if (flux_msg_cmp (zmsg, d->wait_match))
            break;
        d = zlist_next (r->msg_waiters);
    }
    return d;
}

static int backlog_append (struct flux_msg_watcher *d, zmsg_t **zmsg)
{
    if (!d->backlog && !(d->backlog = zlist_new ()))
        oom ();
    if (zlist_append (d->backlog, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    return 0;
}

static int backlog_flush (struct flux_msg_watcher *d)
{
    int errnum = 0;
    int rc = 0;

    if (d->backlog) {
        zmsg_t *zmsg;
        while ((zmsg = zlist_pop (d->backlog))) {
            if (flux_requeue (d->h, zmsg, FLUX_RQ_TAIL) < 0) {
                if (errnum < errno) {
                    errnum = errno;
                    rc = -1;
                }
                zmsg_destroy (&zmsg);
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
    struct flux_msg_watcher *d = r->current;
    copy_match (&d->wait_match, match);
    if (zlist_append (r->msg_waiters, d) < 0)
        oom ();
    if (coproc_yield (d->coproc) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int coproc_cb (coproc_t c, void *arg)
{
    struct flux_msg_watcher *d = arg;
    flux_msg_t *msg;
    struct flux_match match = FLUX_MATCH_ANY;
    int type;
    int rc = -1;
    if (!(msg = flux_recv (d->h, match, FLUX_O_NONBLOCK))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            rc = 0;
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    d->fn (d->h, d, msg, d->arg);
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int resume_coproc (struct flux_msg_watcher *d)
{
    struct reactor *r = reactor_get (d->h);
    int coproc_rc, rc = -1;

    r->current = d;
    if (coproc_resume (d->coproc) < 0)
        goto done;
    if (!coproc_returned (d->coproc, &coproc_rc)) {
        rc = 0;
        goto done;
    }
    if (backlog_flush (d) < 0)
        goto done;
    rc = coproc_rc;
done:
    r->current = NULL;
    return rc;
}

static int start_coproc (struct flux_msg_watcher *d)
{
    struct reactor *r = reactor_get (d->h);
    int coproc_rc, rc = -1;

    r->current = d;
    if (!d->coproc && !(d->coproc = coproc_create (coproc_cb)))
        goto done;
    if (coproc_start (d->coproc, d) < 0)
        goto done;
    if (!coproc_returned (d->coproc, &coproc_rc)) {
        rc = 0;
        goto done;
    }
    if (backlog_flush (d) < 0)
        goto done;
    rc = coproc_rc;
done:
    r->current = NULL;
    return rc;
}

static int message_cb (flux_t h, void *arg)
{
    struct reactor *r = reactor_get (h);
    struct flux_msg_watcher *d;
    struct flux_match match = FLUX_MATCH_ANY;
    int rc = -1;
    flux_msg_t *msg = NULL;
    int type;

    if (!(msg = flux_recv (h, match, FLUX_O_NONBLOCK))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            rc = 0;
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    /* Message matches a coproc that yielded.
     * Resume, arranging for msg to be returned next by flux_recv().
     */
    if ((d = find_waiting_msg_watcher (r, msg))) {
        if (flux_requeue (h, msg, FLUX_RQ_HEAD) < 0)
            goto done;
        zlist_remove (r->msg_waiters, d);
        rc = resume_coproc (d);
    /* Message matches a handler.
     * If coproc already running, queue message as backlog.
     * Else if FLUX_O_COPROC, start coproc.
     * If coprocs not enabled, call handler directly.
     */
    } else if ((d = find_msg_watcher (r, msg))) {
        if (d->coproc && coproc_started (d->coproc)) {
            if (backlog_append (d, &msg) < 0) /* msg now property of backlog */
                goto done;
            rc = 0;
        } else if ((flux_flags_get (h) & FLUX_O_COPROC)) {
            if (flux_requeue (h, msg, FLUX_RQ_HEAD) < 0)
                goto done;
            rc = start_coproc (d);
        } else {
            d->fn (h, d, msg, d->arg);
            rc = 0;
        }
    /* Message matched nothing.
     * Respond with ENOSYS if it was a request.
     * Else log it if FLUX_O_TRACE
     */
    } else {
        if (type == FLUX_MSGTYPE_REQUEST) {
            if (flux_respond (h, msg, ENOSYS, NULL))
                goto done;
        } else if (flux_flags_get (h) & FLUX_O_TRACE) {
            const char *topic = NULL;
            (void)flux_msg_get_topic (msg, &topic);
            fprintf (stderr, "nomatch: %s '%s'\n", flux_msg_typestr (type),
                     topic ? topic : "");
        }
        rc = 0;
    }
done:
    flux_msg_destroy (msg);
    return rc;
}

int flux_msg_watcher_add (flux_t h, struct flux_match match,
                          flux_msg_watcher_f cb, void *arg,
                          struct flux_msg_watcher **wp)
{
    struct reactor *r = reactor_get (h);
    struct flux_msg_watcher *w;
    int oldsize = zlist_size (r->msg_watchers);
    int rc = -1;

    if (!cb || match.typemask == 0) {
        errno = EINVAL;
        goto done;
    }
    w = msg_watcher_create (h, match, cb, arg);
    if (oldsize == 0 && r->ops->reactor_msg_add) {
        if (r->ops->reactor_msg_add (r->impl, message_cb, r) < 0) {
            msg_watcher_destroy (w);
            goto done;
        }
    }
    if (zlist_push (r->msg_watchers, w) < 0)
        oom ();
    if (wp)
        *wp = w;
    rc = 0;
done:
    return rc;
}

int flux_msg_watcher_addvec (flux_t h, struct flux_msghandler tab[], void *arg)
{
    int i;
    struct flux_match match = FLUX_MATCH_ANY;

    for (i = 0; ; i++) {
        if (!tab[i].typemask && !tab[i].topic_glob && !tab[i].cb)
            break;
        match.typemask = tab[i].typemask;
        match.topic_glob = tab[i].topic_glob;
        if (flux_msg_watcher_add (h, match, tab[i].cb, arg, NULL) < 0)
            return -1;
    }
    return 0;
}

void flux_msg_watcher_cancel (struct flux_msg_watcher *w)
{
    struct reactor *r = reactor_get (w->h);

    zlist_remove (r->msg_waiters, w);
    zlist_remove (r->msg_watchers, w);
    msg_watcher_destroy (w);
    if (zlist_size (r->msg_watchers) == 0 && r->ops->reactor_msg_remove)
        r->ops->reactor_msg_remove (r->impl);
}

static void reactor_stop_error (flux_t h)
{
    struct reactor *r = reactor_get (h);
    if (r->ops->reactor_stop)
        r->ops->reactor_stop (r->impl, -1);
}

void msg_compat_cb (flux_t h, struct flux_msg_watcher *w,
                    const flux_msg_t *msg, void *arg)
{
    struct msg_watcher_compat *compat = arg;
    flux_msg_t *cpy = NULL;
    int type;

    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (compat->fn (h, type, &cpy, compat->arg) != 0)
        reactor_stop_error (h);
done:
    flux_msg_destroy (cpy);
}

int flux_msghandler_add (flux_t h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg)
{
    struct flux_msg_watcher *d = NULL;
    struct msg_watcher_compat *compat = xzmalloc (sizeof (*compat));
    struct flux_match match = {
        .typemask = typemask,
        .topic_glob = (char *)pattern,
        .matchtag = FLUX_MATCHTAG_NONE,
        .bsize = 1,
    };

    compat->fn = cb;
    compat->arg = arg;
    if (flux_msg_watcher_add (h, match, msg_compat_cb, compat, &d) < 0) {
        free (compat);
        return -1;
    }
    d->arg_free = free; // free compat on destroy
    return 0;
}

int flux_msghandler_addvec (flux_t h, msghandler_t *handlers, int len,
                            void *arg)
{
    int i;

    for (i = 0; i < len; i++)
        if (flux_msghandler_add (h, handlers[i].typemask, handlers[i].pattern,
                                    handlers[i].cb, arg) < 0)
            return -1;
    return 0;
}

void flux_msghandler_remove (flux_t h, int typemask, const char *pattern)
{
    struct reactor *r = reactor_get (h);
    struct flux_msg_watcher *d;
    struct flux_match match = {
        .typemask = typemask,
        .matchtag = FLUX_MATCHTAG_NONE,
        .bsize = 1,
        .topic_glob = (char*)pattern,
    };

    d = zlist_first (r->msg_watchers);
    while (d) {
        if (match_match (d->match, match)) {
            flux_msg_watcher_cancel (d);
            break;
        }
        d = zlist_next (r->msg_watchers);
    }
}

int flux_fdhandler_add (flux_t h, int fd, short events,
                        FluxFdHandler cb, void *arg)
{
    struct reactor *r = reactor_get (h);
    int rc = -1;

    if (fd < 0 || events == 0 || !cb) {
        errno = EINVAL;
        goto done;
    }
    if (!r->ops->reactor_fd_add) {
        errno = ENOSYS;
        goto done;
    }
    rc = r->ops->reactor_fd_add (r->impl, fd, events, cb, arg);
done:
    return rc;
}

void flux_fdhandler_remove (flux_t h, int fd, short events)
{
    struct reactor *r = reactor_get (h);

    if (r->ops->reactor_fd_remove)
        r->ops->reactor_fd_remove (r->impl, fd, events);
}

int flux_zshandler_add (flux_t h, void *zs, short events,
                        FluxZsHandler cb, void *arg)
{
    struct reactor *r = reactor_get (h);
    int rc = -1;

    if (!zs || events == 0 || !cb) {
        errno = EINVAL;
        goto done;
    }
    if (!r->ops->reactor_zs_add) {
        errno = ENOSYS;
        goto done;
    }
    rc = r->ops->reactor_zs_add (r->impl, zs, events, cb, arg);
done:
    return rc;
}

void flux_zshandler_remove (flux_t h, void *zs, short events)
{
    struct reactor *r = reactor_get (h);

    if (r->ops->reactor_zs_remove)
        r->ops->reactor_zs_remove (r->impl, zs, events);
}

int flux_tmouthandler_add (flux_t h, unsigned long msec, bool oneshot,
                           FluxTmoutHandler cb, void *arg)
{
    struct reactor *r = reactor_get (h);
    int rc = -1;

    if (!cb) {
        errno = EINVAL;
        goto done;
    }
    if (!r->ops->reactor_tmout_add) {
        errno = ENOSYS;
        goto done;
    }
    rc = r->ops->reactor_tmout_add (r->impl, msec, oneshot, cb, arg);
done:
    return rc;
}

void flux_tmouthandler_remove (flux_t h, int timer_id)
{
    struct reactor *r = reactor_get (h);

    if (r->ops->reactor_tmout_remove)
        r->ops->reactor_tmout_remove (r->impl, timer_id);
}

int flux_reactor_start (flux_t h)
{
    struct reactor *r = reactor_get (h);
    if (!r->ops->reactor_start) {
        errno = ENOSYS;
        return -1;
    }
    return r->ops->reactor_start (r->impl);
}

void flux_reactor_stop (flux_t h)
{
    struct reactor *r = reactor_get (h);
    if (r->ops->reactor_stop)
        r->ops->reactor_stop (r->impl, 0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
