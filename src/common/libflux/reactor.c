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
#include "handle_impl.h"
#include "message.h"
#include "tagpool.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/zdump.h"
#include "src/common/libutil/xzmalloc.h"

typedef struct {
    flux_match_t match;
    FluxMsgHandler fn;
    void *arg;
} dispatch_t;

struct reactor_struct {
    zlist_t *dsp;
    const struct flux_handle_ops *ops;
    void *impl;
};

static dispatch_t *dispatch_create (flux_match_t match,
                                    FluxMsgHandler cb, void *arg)
{
    dispatch_t *d = xzmalloc (sizeof (*d));
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

static int msg_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    reactor_t r = flux_get_reactor (h);
    dispatch_t *d;
    int rc = 0;
    bool match = false;

    assert (zmsg != NULL);
    assert (*zmsg != NULL);

    if (flux_flags_get (h) & FLUX_FLAGS_TRACE) {
        zdump_fprint (stderr, *zmsg, flux_msgtype_shortstr (type));
    }

    d = zlist_first (r->dsp);
    while (d) {
        if (flux_msg_cmp (*zmsg, d->match)) {
            rc = d->fn (h, type, zmsg, d->arg);
            match = true;
            break;
        }
        d = zlist_next (r->dsp);
    }
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
    d = dispatch_create (match, cb, arg);
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
