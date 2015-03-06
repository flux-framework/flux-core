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

#include "handle_impl.h"
#include "message.h"
#include "reactor.h"
#include "tagpool.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/zdump.h"
#include "src/common/libutil/xzmalloc.h"

typedef enum {
    DSP_TYPE_MSG, DSP_TYPE_FD, DSP_TYPE_ZS, DSP_TYPE_TMOUT
} dispatch_type_t;

typedef struct {
    dispatch_type_t type;
    union {
        struct {
            flux_match_t match;
            FluxMsgHandler fn;
            void *arg;
        } msg;
        struct {
            int fd;
            short events;
            FluxFdHandler fn;
            void *arg;
        } fd;
        struct {
            void *zs;
            short events;
            FluxZsHandler fn;
            void *arg;
        } zs;
        struct {
            int timer_id;
            bool oneshot;
            FluxTmoutHandler fn;
            void *arg;
        } tmout;
    };
} dispatch_t;

struct reactor_struct {
    zlist_t *dsp;
    bool timeout_set;
    const struct flux_handle_ops *ops;
    void *impl;
};

static bool reactor_empty (reactor_t r)
{
    return ((zlist_size (r->dsp) == 0) && !r->timeout_set);
}

static dispatch_t *dispatch_create (dispatch_type_t type)
{
    dispatch_t *d = xzmalloc (sizeof (*d));
    d->type = type;
    return d;
}

static void dispatch_destroy (dispatch_t *d)
{
    if (d && d->type == DSP_TYPE_MSG && d->msg.match.topic_glob)
        free (d->msg.match.topic_glob);
    free (d);
}

void flux_reactor_destroy (reactor_t r)
{
    dispatch_t *d;
    if (r) {
        while ((d = zlist_pop (r->dsp)))
            dispatch_destroy (d);
        zlist_destroy (&r->dsp);
        free (r);
    }
}

reactor_t flux_reactor_create (void *impl, const struct flux_handle_ops *ops)
{
    reactor_t r = xzmalloc (sizeof (*r));
    if (!(r->dsp = zlist_new ()))
        oom ();
    r->impl = impl;
    r->ops = ops;
    return r;
}

int flux_handle_event_msg (flux_t h, zmsg_t **zmsg)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int type = 0;
    int rc = 0;
    bool match = false;

    assert (zmsg != NULL);
    assert (*zmsg != NULL);

    if (flux_msg_get_type (*zmsg, &type) < 0)
        goto done;
    d = zlist_first (r->dsp);
    while (d) {
        if (d->type == DSP_TYPE_MSG && flux_msg_cmp (*zmsg, d->msg.match)) {
            if (d->msg.fn)
                rc = d->msg.fn (h, type, zmsg, d->msg.arg);
            match = true;
            break;
        }
        d = zlist_next (r->dsp);
    }
done:
    if (flux_flags_get (h) & FLUX_FLAGS_TRACE) {
        if (!match && *zmsg) {
            char *topic = NULL;
            (void)flux_msg_get_topic (*zmsg, &topic);
            fprintf (stderr, "nomatch: %s '%s'\n", flux_msgtype_string (type),
                     topic ? topic : "");
        }
    }
    return rc;
}

int flux_handle_event_fd (flux_t h, int fd, short events)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int rc = 0;

    d = zlist_first (r->dsp);
    while (d) {
        if (d->type == DSP_TYPE_FD && d->fd.fd == fd
                            && (d->fd.events & events) && d->fd.fn != NULL) {
            rc = d->fd.fn (h, fd, events, d->fd.arg);
            break;
        }
        d = zlist_next (r->dsp);
    }
    return rc;
}

int flux_handle_event_zs (flux_t h, void *zs, short events)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int rc = 0;

    d = zlist_first (r->dsp);
    while (d) {
        if (d->type == DSP_TYPE_ZS && d->zs.zs == zs
                                   && (d->zs.events & events)) {
            rc = d->zs.fn (h, zs, events, d->zs.arg);
            break;
        }
        d = zlist_next (r->dsp);
    }
    return rc;
}

int flux_handle_event_tmout (flux_t h, int timer_id)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int rc = 0;

    d = zlist_first (r->dsp);
    while (d) {
        if (d->type == DSP_TYPE_TMOUT && d->tmout.fn != NULL
                                      && d->tmout.timer_id == timer_id) {
            rc = d->tmout.fn (h, d->tmout.arg);
            break;
        }
        d = zlist_next (r->dsp);
    }
    if (d && d->tmout.oneshot)
        flux_tmouthandler_remove (h, d->tmout.timer_id);
    return rc;
}

int flux_msghandler_add (flux_t h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int rc = -1;

    if (typemask == 0 || !pattern || !cb) {
        errno = EINVAL;
        goto done;
    }
    d = dispatch_create (DSP_TYPE_MSG);
    d->msg.match.typemask = typemask;
    d->msg.match.matchtag = FLUX_MATCHTAG_NONE;
    d->msg.match.bsize = 0;
    d->msg.match.topic_glob = xstrdup (pattern);
    d->msg.fn = cb;
    d->msg.arg = arg;
    if (zlist_push (r->dsp, d) < 0)
        oom ();
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
        if (d->type == DSP_TYPE_MSG && d->msg.match.typemask == typemask
                             && !strcmp (d->msg.match.topic_glob, pattern)) {
            zlist_remove (r->dsp, d);
            dispatch_destroy (d);
            if (reactor_empty (r) && r->ops->reactor_stop)
                r->ops->reactor_stop (r->impl, 0);
            break;
        }
        d = zlist_next (r->dsp);
    }
}

int flux_fdhandler_add (flux_t h, int fd, short events,
                        FluxFdHandler cb, void *arg)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int rc = -1;

    if (fd < 0 || events == 0 || !cb) {
        errno = EINVAL;
        goto done;
    }
    if (!r->ops->reactor_fd_add) {
        errno = ENOSYS;
        goto done;
    }
    if (r->ops->reactor_fd_add (r->impl, fd, events) < 0)
        goto done;

    d = dispatch_create (DSP_TYPE_FD);
    d->fd.fd = fd;
    d->fd.events = events;
    d->fd.fn = cb;
    d->fd.arg = arg;
    if (zlist_append (r->dsp, d) < 0)
        oom ();
    rc = 0;
done:
    return rc;
}

void flux_fdhandler_remove (flux_t h, int fd, short events)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;

    if (!r->ops->reactor_fd_remove)
        return;
    d = zlist_first (r->dsp);
    while (d) {
        if (d->type == DSP_TYPE_FD && d->fd.fd == fd
                                   && d->fd.events == events) {
            zlist_remove (r->dsp, d);
            dispatch_destroy (d);
            if (reactor_empty (r) && r->ops->reactor_stop)
                r->ops->reactor_stop (r->impl, 0);
            break;
        }
        d = zlist_next (r->dsp);
    }
    r->ops->reactor_fd_remove (r->impl, fd, events);
}

int flux_zshandler_add (flux_t h, void *zs, short events,
                        FluxZsHandler cb, void *arg)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;

    if (!r->ops->reactor_zs_add) {
        errno = ENOSYS;
        return -1;
    }
    if (r->ops->reactor_zs_add (r->impl, zs, events) < 0)
        return -1;
    d = dispatch_create (DSP_TYPE_ZS);
    d->zs.zs = zs;
    d->zs.events = events;
    d->zs.fn = cb;
    d->zs.arg = arg;
    if (zlist_append (r->dsp, d) < 0)
        oom ();
    return 0;
}

void flux_zshandler_remove (flux_t h, void *zs, short events)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;

    if (!r->ops->reactor_zs_remove)
        return;
    d = zlist_first (r->dsp);
    while (d) {
        if (d->type == DSP_TYPE_ZS && d->zs.zs == zs
                                   && d->zs.events == events) {
            zlist_remove (r->dsp, d);
            dispatch_destroy (d);
            if (reactor_empty (r) && r->ops->reactor_stop)
                r->ops->reactor_stop (r->impl, 0);
            break;
        }
        d = zlist_next (r->dsp);
    }
    r->ops->reactor_zs_remove (r->impl, zs, events);
}

int flux_tmouthandler_add (flux_t h, unsigned long msec, bool oneshot,
                           FluxTmoutHandler cb, void *arg)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int id;

    if (!r->ops->reactor_tmout_add) {
        errno = ENOSYS;
        return -1;
    }
    if ((id = r->ops->reactor_tmout_add (r->impl, msec, oneshot)) < 0)
        return -1;
    d = dispatch_create (DSP_TYPE_TMOUT);
    d->tmout.fn = cb;
    d->tmout.arg = arg;
    d->tmout.timer_id = id;
    d->tmout.oneshot = oneshot;
    if (zlist_append (r->dsp, d) < 0)
        oom ();
    return id;
}

void flux_tmouthandler_remove (flux_t h, int timer_id)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;

    if (!r->ops->reactor_tmout_remove)
        return;
    d = zlist_first (r->dsp);
    while (d) {
        if (d->type == DSP_TYPE_TMOUT && d->tmout.timer_id == timer_id) {
            zlist_remove (r->dsp, d);
            dispatch_destroy (d);
            if (reactor_empty (r) && r->ops->reactor_stop)
                r->ops->reactor_stop (r->impl, 0);
            break;
        }
        d = zlist_next (r->dsp);
    }
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
