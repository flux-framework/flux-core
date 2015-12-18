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

struct dispatch {
    flux_t h;
    zlist_t *handlers; /* all handlers are on this list */
    zlist_t *waiters;  /* waiting coproc watchers are also on this list */
    flux_msg_handler_t *current;
    flux_watcher_t *w;
    int usecount;
};

#define HANDLER_MAGIC 0x44433322
struct flux_msg_handler {
    int magic;
    struct dispatch *d;
    struct flux_match match;
    flux_msg_handler_f fn;
    void *arg;
    flux_free_f arg_free;

    /* coproc */
    coproc_t *coproc;
    zlist_t *backlog;
    struct flux_match wait_match;
};

static void handle_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg);

static void dispatch_usecount_decr (struct dispatch *d)
{
    if (d && --d->usecount == 0) {
        flux_watcher_destroy (d->w);
        zlist_destroy (&d->handlers);
        zlist_destroy (&d->waiters);
        free (d);
    }
}

static void dispatch_usecount_incr (struct dispatch *d)
{
    d->usecount++;
}


static void dispatch_destroy (void *arg)
{
    struct dispatch *d = arg;
    dispatch_usecount_decr (d);
}

static struct dispatch *dispatch_get (flux_t h)
{
    struct dispatch *d = flux_aux_get (h, "flux::dispatch");
    if (!d) {
        flux_reactor_t *r = flux_get_reactor (h);
        d = xzmalloc (sizeof (*d));
        d->handlers = zlist_new ();
        d->waiters = zlist_new ();
        if (!d->handlers || !d->waiters)
            oom ();
        d->h = h;
        d->w = flux_handle_watcher_create (r, h, FLUX_POLLIN, handle_cb, d);
        if (!d->w)
            oom ();
        d->usecount = 1;
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

static int backlog_append (flux_msg_handler_t *w, const flux_msg_t *msg)
{
    flux_msg_t *cpy;
    int rc = -1;

    if (!w->backlog && !(w->backlog = zlist_new ())) {
        errno = ENOMEM;
        goto done;
    }
    if (!(cpy = flux_msg_copy (msg, true))) {
        errno = ENOMEM;
        goto done;
    }
    if (zlist_append (w->backlog, cpy) < 0) {
        flux_msg_destroy (cpy);
        errno = ENOMEM;
    }
    rc = 0;
done:
    return rc;
}

static int backlog_flush (flux_msg_handler_t *w)
{
    int errnum = 0;
    int rc = 0;

    if (w->backlog) {
        flux_msg_t *msg;
        while ((msg = zlist_pop (w->backlog))) {
            if (flux_requeue (w->d->h, msg, FLUX_RQ_TAIL) < 0) {
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
    flux_msg_handler_t *w = d->current;
    copy_match (&w->wait_match, match);
    if (zlist_append (d->waiters, w) < 0)
        oom ();
    if (coproc_yield (w->coproc) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

static int coproc_cb (coproc_t *c, void *arg)
{
    flux_msg_handler_t *w = arg;
    flux_msg_t *msg;
    int type;
    int rc = -1;
    if (!(msg = flux_recv (w->d->h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            rc = 0;
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    w->fn (w->d->h, w, msg, w->arg);
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int resume_coproc (flux_msg_handler_t *w)
{
    struct dispatch *d = w->d;
    int coproc_rc, rc = -1;

    d->current = w;
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
    d->current = NULL;
    return rc;
}

static int start_coproc (flux_msg_handler_t *w)
{
    struct dispatch *d = w->d;
    int coproc_rc, rc = -1;

    d->current = w;
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
    d->current = NULL;
    return rc;
}

/* Return value of dispatch_message[_coproc] is:
 * -1 error, 0 nomatch, or 1 match
 */

static int dispatch_message_coproc (struct dispatch *d,
                                    const flux_msg_t *msg, int type)
{
    flux_msg_handler_t *w;
    bool match = false;
    int rc = -1;

    /* Message matches a coproc that yielded.
     * Resume, arranging for msg to be returned next by flux_recv().
     */
    w = zlist_first (d->waiters);
    while (w) {
        if (flux_msg_cmp (msg, w->wait_match)) {
            if (flux_requeue (d->h, msg, FLUX_RQ_HEAD) < 0)
                goto done;
            zlist_remove (d->waiters, w);
            if (resume_coproc (w) < 0)
                goto done;
            match = true;
            if (type != FLUX_MSGTYPE_EVENT)
                break;
        }
        w = zlist_next (d->waiters);
    }
    /* Message matches a handler.
     * If coproc already running, queue message as backlog.
     * Else start coproc.
     */
    if (!match || type == FLUX_MSGTYPE_EVENT) {
        w = zlist_first (d->handlers);
        while (w) {
            if (flux_msg_cmp (msg, w->match)) {
                if (w->coproc && coproc_started (w->coproc)) {
                    if (backlog_append (w, msg) < 0)
                        goto done;
                } else {
                    if (flux_requeue (d->h, msg, FLUX_RQ_HEAD) < 0)
                        goto done;
                    if (start_coproc (w) < 0)
                        goto done;
                }
                match = true;
                if (type != FLUX_MSGTYPE_EVENT)
                    break;
            }
            w = zlist_next (d->handlers);
        }
    }
    rc = match ? 1 : 0;
done:
    return rc;
}

static int dispatch_message (struct dispatch *d,
                             const flux_msg_t *msg, int type)
{
    flux_msg_handler_t *w;
    bool match = false;
    int rc = -1;

    w = zlist_first (d->handlers);
    while (w) {
        if (flux_msg_cmp (msg, w->match)) {
            w->fn (d->h, w, msg, w->arg);
            match = true;
            if (type != FLUX_MSGTYPE_EVENT)
                break;
        }
        w = zlist_next (d->handlers);
    }
    rc = match ? 1 : 0;
    return rc;
}

static void handle_cb (flux_reactor_t *r, flux_watcher_t *hw,
                       int revents, void *arg)
{
    struct dispatch *d = arg;
    flux_msg_t *msg = NULL;
    int rc = -1;
    int type, match;

    if (revents & FLUX_POLLERR)
        goto done;
    if (!(msg = flux_recv (d->h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
        if (errno == EAGAIN && errno == EWOULDBLOCK)
            rc = 0; /* ignore spurious wakeup */
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0) {
        rc = 0; /* ignore mangled message */
        goto done;
    }
    if ((flux_flags_get (d->h) & FLUX_O_COPROC))
        match = dispatch_message_coproc (d, msg, type);
    else
        match = dispatch_message (d, msg, type);
    if (match < 0)
        goto done;
    /* Message matched nothing.
     * Respond with ENOSYS if it was a request.
     * Else log it if FLUX_O_TRACE
     */
    if (match == 0) {
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
    rc = 0;
done:
    if (rc < 0) {
        flux_reactor_stop_error (r);
        FLUX_FATAL (d->h);
    }
    flux_msg_destroy (msg);
}

void flux_msg_handler_start (flux_msg_handler_t *w)
{
    struct dispatch *d = w->d;

    assert (w->magic == HANDLER_MAGIC);
    flux_watcher_start (d->w);
    if (zlist_push (d->handlers, w) < 0)
        oom ();
}

void flux_msg_handler_stop (flux_msg_handler_t *w)
{
    struct dispatch *d = w->d;

    assert (w->magic == HANDLER_MAGIC);
    zlist_remove (d->waiters, w);
    zlist_remove (d->handlers, w);
    if (zlist_size (d->handlers) == 0)
        flux_watcher_stop (d->w);
}

void flux_msg_handler_destroy (flux_msg_handler_t *w)
{
    if (w) {
        assert (w->magic == HANDLER_MAGIC);
        flux_msg_handler_stop (w);
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
        w->magic = ~HANDLER_MAGIC;
        dispatch_usecount_decr (w->d);
        free (w);
    }
}

flux_msg_handler_t *flux_msg_handler_create (flux_t h,
                                             const struct flux_match match,
                                             flux_msg_handler_f cb, void *arg)
{
    struct dispatch *d = dispatch_get (h);
    flux_msg_handler_t *w = xzmalloc (sizeof (*w));

    w->magic = HANDLER_MAGIC;
    copy_match (&w->match, match);
    w->fn = cb;
    w->arg = arg;
    w->d = d;
    dispatch_usecount_incr (d);

    return w;
}

int flux_msg_handler_addvec (flux_t h, struct flux_msg_handler_spec tab[],
                             void *arg)
{
    int i;
    struct flux_match match = FLUX_MATCH_ANY;

    for (i = 0; ; i++) {
        if (!tab[i].typemask && !tab[i].topic_glob && !tab[i].cb)
            break; /* FLUX_MSGHANDLER_TABLE_END */
        match.typemask = tab[i].typemask;
        match.topic_glob = tab[i].topic_glob;
        tab[i].w = flux_msg_handler_create (h, match, tab[i].cb, arg);
        if (!tab[i].w)
            goto error;
        flux_msg_handler_start (tab[i].w);
    }
    return 0;
error:
    while (i >= 0) {
        if (tab[i].w) {
            flux_msg_handler_stop (tab[i].w);
            flux_msg_handler_destroy (tab[i].w);
            tab[i].w = NULL;
        }
        i--;
    }
    return -1;
}

void flux_msg_handler_delvec (struct flux_msg_handler_spec tab[])
{
    int i;

    for (i = 0; ; i++) {
        if (!tab[i].typemask && !tab[i].topic_glob && !tab[i].cb)
            break; /* FLUX_MSGHANDLER_TABLE_END */
        if (tab[i].w) {
            flux_msg_handler_stop (tab[i].w);
            flux_msg_handler_destroy (tab[i].w);
            tab[i].w = NULL;
        }
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
