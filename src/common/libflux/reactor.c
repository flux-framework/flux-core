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

#include "handle.h"
#include "reactor.h"
#include "request.h"
#include "handle_impl.h"
#include "message.h"
#include "tagpool.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/zdump.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/coproc.h"

typedef struct {
    flux_t h;
    flux_match_t match;
    FluxMsgHandler fn;
    void *arg;
    coproc_t coproc;
    zlist_t *backlog;
    flux_match_t wait_match;
} dispatch_t;

struct reactor_struct {
    zlist_t *dsp;
    zlist_t *waiters;
    const struct flux_handle_ops *ops;
    void *impl;
    dispatch_t *current;
};

static void copy_match (flux_match_t *dst, flux_match_t src)
{
    if (dst->topic_glob)
        free (dst->topic_glob);
    *dst = src;
    dst->topic_glob = src.topic_glob ? xstrdup (src.topic_glob) : NULL;
}

static dispatch_t *dispatch_create (flux_t h, flux_match_t match,
                                    FluxMsgHandler cb, void *arg)
{
    dispatch_t *d = xzmalloc (sizeof (*d));
    d->h = h;
    d->match = match;
    d->fn = cb;
    d->arg = arg;
    return d;
}

static void dispatch_destroy (dispatch_t *d)
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
        free (d);
    }
}

void flux_reactor_destroy (reactor_t r)
{
    dispatch_t *d;
    if (r) {
        while ((d = zlist_pop (r->dsp)))
            dispatch_destroy (d);
        zlist_destroy (&r->dsp);
        zlist_destroy (&r->waiters); /* any items were also on r->dsp */
        free (r);
    }
}

reactor_t flux_reactor_create (void *impl, const struct flux_handle_ops *ops)
{
    reactor_t r = xzmalloc (sizeof (*r));
    if (!(r->dsp = zlist_new ()))
        oom ();
    if (!(r->waiters = zlist_new ()))
        oom ();
    r->impl = impl;
    r->ops = ops;
    return r;
}

static dispatch_t *find_dispatch (reactor_t r, zmsg_t *zmsg, bool waitlist)
{
    zlist_t *l = waitlist ? r->waiters : r->dsp;
    dispatch_t *d = zlist_first (l);
    while (d) {
        if (flux_msg_cmp (zmsg, waitlist ? d->wait_match : d->match))
            break;
        d = zlist_next (l);
    }
    return d;
}

static int backlog_append (dispatch_t *d, zmsg_t **zmsg)
{
    if (!d->backlog && !(d->backlog = zlist_new ()))
        oom ();
    if (zlist_append (d->backlog, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    return 0;
}

static int backlog_flush (dispatch_t *d)
{
    if (d->backlog)
        return flux_putmsg_list (d->h, d->backlog);
    return 0;
}

int flux_sleep_on (flux_t h, flux_match_t match)
{
    reactor_t r = flux_get_reactor (h);
    int rc = -1;

    if (!r->current || !r->current->coproc) {
        errno = EINVAL;
        goto done;
    }
    dispatch_t *d = r->current;
    copy_match (&d->wait_match, match);
    if (zlist_append (r->waiters, d) < 0)
        oom ();
    if (coproc_yield (d->coproc) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int coproc_cb (coproc_t c, void *arg)
{
    dispatch_t *d = arg;
    zmsg_t *zmsg;
    int type;
    int rc = -1;
    if (!(zmsg = flux_recvmsg (d->h, true))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            rc = 0;
        goto done;
    }
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    rc = d->fn (d->h, type, &zmsg, d->arg);
done:
    zmsg_destroy (&zmsg);
    return rc;
}

static int resume_coproc (dispatch_t *d)
{
    reactor_t r = flux_get_reactor (d->h);
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

static int start_coproc (dispatch_t *d)
{
    reactor_t r = flux_get_reactor (d->h);
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

static int msg_cb (flux_t h, void *arg)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int rc = -1;
    zmsg_t *zmsg = NULL;
    int type;

    if (!(zmsg = flux_recvmsg (h, true))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            rc = 0;
        goto done;
    }
    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    /* Message matches a coproc that yielded.
     * Resume, arranging for zmsg to be returned next by flux_recvmsg().
     */
    if ((d = find_dispatch (r, zmsg, true))) {
        if (flux_pushmsg (h, &zmsg) < 0)
            goto done;
        zlist_remove (r->waiters, d);
        rc = resume_coproc (d);
    /* Message matches a handler.
     * If coproc already running, queue message as backlog.
     * Else if FLUX_O_COPROC, start coproc.
     * If coprocs not enabled, call handler directly.
     */
    } else if ((d = find_dispatch (r, zmsg, false))) {
        if (d->coproc && coproc_started (d->coproc)) {
            if (backlog_append (d, &zmsg) < 0)
                goto done;
            rc = 0;
        } else if ((flux_flags_get (h) & FLUX_O_COPROC)) {
            if (flux_pushmsg (h, &zmsg) < 0)
                goto done;
            rc = start_coproc (d);
        } else {
            rc = d->fn (h, type, &zmsg, d->arg);
        }
    /* Message matched nothing.
     * Respond with ENOSYS if it was a request.
     * Else log it if FLUX_O_TRACE
     */
    } else {
        if (type == FLUX_MSGTYPE_REQUEST) {
            if (flux_err_respond (h, ENOSYS, &zmsg) < 0)
                goto done;
        } else if (flux_flags_get (h) & FLUX_O_TRACE) {
            char *topic = NULL;
            (void)flux_msg_get_topic (zmsg, &topic);
            fprintf (stderr, "nomatch: %s '%s'\n", flux_msgtype_string (type),
                     topic ? topic : "");
            if (topic)
                free (topic);
        }
        rc = 0;
    }
done:
    zmsg_destroy (&zmsg);
    return rc;
}

int flux_msghandler_add (flux_t h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int oldsize = zlist_size (r->dsp);
    int rc = -1;

    if (typemask == 0 || !pattern || !cb) {
        errno = EINVAL;
        goto done;
    }

    flux_match_t match = {
        .typemask = typemask,
        .topic_glob = pattern ? xstrdup (pattern) : NULL,
        .matchtag = FLUX_MATCHTAG_NONE,
        .bsize = 1,
    };
    d = dispatch_create (h, match, cb, arg);
    if (zlist_push (r->dsp, d) < 0)
        oom ();
    if (oldsize == 0 && r->ops->reactor_msg_add)
        rc = r->ops->reactor_msg_add (r->impl, msg_cb, r);
    rc = 0;
done:
    return rc;
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
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;

    d = zlist_first (r->dsp);
    while (d) {
        if (d->match.typemask == typemask && !strcmp (d->match.topic_glob,
                                                        pattern)) {
            zlist_remove (r->dsp, d);
            dispatch_destroy (d);
            if (zlist_size (r->dsp) == 0 && r->ops->reactor_msg_remove)
                r->ops->reactor_msg_remove (r->impl);
            break;
        }
        d = zlist_next (r->dsp);
    }
}

int flux_fdhandler_add (flux_t h, int fd, short events,
                        FluxFdHandler cb, void *arg)
{
    reactor_t r = flux_get_reactor (h);
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
    reactor_t r = flux_get_reactor (h);

    if (r->ops->reactor_fd_remove)
        r->ops->reactor_fd_remove (r->impl, fd, events);
}

int flux_zshandler_add (flux_t h, void *zs, short events,
                        FluxZsHandler cb, void *arg)
{
    reactor_t r = flux_get_reactor (h);
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
    reactor_t r = flux_get_reactor (h);

    if (r->ops->reactor_zs_remove)
        r->ops->reactor_zs_remove (r->impl, zs, events);
}

int flux_tmouthandler_add (flux_t h, unsigned long msec, bool oneshot,
                           FluxTmoutHandler cb, void *arg)
{
    reactor_t r = flux_get_reactor (h);
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
    reactor_t r = flux_get_reactor (h);

    if (r->ops->reactor_tmout_remove)
        r->ops->reactor_tmout_remove (r->impl, timer_id);
}

int flux_reactor_start (flux_t h)
{
    reactor_t r = flux_get_reactor (h);
    if (!r->ops->reactor_start) {
        errno = ENOSYS;
        return -1;
    }
    return r->ops->reactor_start (r->impl);
}

void flux_reactor_stop (flux_t h)
{
    reactor_t r = flux_get_reactor (h);
    if (r->ops->reactor_stop)
        r->ops->reactor_stop (r->impl, 0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
