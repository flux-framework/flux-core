/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
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

/* loop connector - mainly for testing */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <sys/eventfd.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

#include "src/common/libev/ev.h"
#include "src/common/libutil/ev_zlist.h"
#include "src/common/libutil/ev_zmq.h"

#define CTX_MAGIC   0xf434aaa0
typedef struct {
    int magic;
    int rank;
    flux_t h;

    int pollfd;
    int pollevents;

    zlist_t *queue;
    ev_zlist queue_w;
    struct ev_loop *loop;
    int loop_rc;

    flux_msg_f msg_cb;
    void *msg_cb_arg;

    zhash_t *watchers;
    int timer_seq;
} ctx_t;

#define HASHKEY_LEN 80

typedef struct {
    ev_timer w;
    FluxTmoutHandler cb;
    void *arg;
    int id;
} atimer_t;

typedef struct {
    ev_zmq w;
    FluxZsHandler cb;
    void *arg;
} azs_t;

typedef struct {
    ev_io w;
    FluxFdHandler cb;
    void *arg;
} afd_t;

static void op_reactor_stop (void *impl, int rc);


static const struct flux_handle_ops handle_ops;

const char *fake_uuid = "12345678123456781234567812345678";

static int raise_event (ctx_t *c)
{
    uint64_t val = 1;
    if (write (c->pollfd, &val, sizeof (val)) < 0)
        return -1;
    return 0;
}

static int clear_event (ctx_t *c)
{
    uint64_t val;
    if (read (c->pollfd, &val, sizeof (val)) < 0)
        return -1;
    return 0;
}

static int op_pollevents (void *impl)
{
    ctx_t *c = impl;
    if (clear_event (c) < 0)
        return -1;
    return c->pollevents;
}

static int op_pollfd (void *impl)
{
    ctx_t *c = impl;
    return c->pollfd;
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    int type;
    flux_msg_t *cpy = NULL;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (flux_msg_get_type (cpy, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
        case FLUX_MSGTYPE_EVENT:
            if (flux_msg_enable_route (cpy) < 0)
                goto done;
            if (flux_msg_push_route (cpy, fake_uuid) < 0)
                goto done;
            break;
    }
    if (zlist_append (c->queue, cpy) < 0) {
        errno = ENOMEM;
        goto done;
    }
    if (!(c->pollevents & FLUX_POLLIN)) {
        c->pollevents |= FLUX_POLLIN;
        if (raise_event (c) < 0)
            goto done;
    }
    cpy = NULL; /* c->queue now owns cpy */
    rc = 0;
done:
    if (cpy)
        flux_msg_destroy (cpy);
    return rc;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    flux_msg_t *msg = zlist_pop (c->queue);
    if (!msg) {
        errno = EWOULDBLOCK;
        goto done;
    }
    if ((c->pollevents & FLUX_POLLIN) && zlist_size (c->queue) == 0)
        c->pollevents &= ~FLUX_POLLIN;
done:
    return msg;
}

static int op_requeue (void *impl, const flux_msg_t *msg, int flags)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    int rc = -1;
    flux_msg_t *cpy = NULL;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if ((flags & FLUX_RQ_TAIL))
        rc = zlist_append (c->queue, cpy);
    else
        rc = zlist_push (c->queue, cpy);
    if (rc < 0) {
        flux_msg_destroy (cpy);
        errno = ENOMEM;
        goto done;
    }
    if (!(c->pollevents & FLUX_POLLIN)) {
        c->pollevents |= FLUX_POLLIN;
        if (raise_event (c) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

static void op_purge (void *impl, struct flux_match match)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    zmsg_t *zmsg = zlist_first (c->queue);

    while (zmsg) {
        if (flux_msg_cmp (zmsg, match)) {
            zlist_remove (c->queue, zmsg);
            zmsg_destroy (&zmsg);
        }
        zmsg = zlist_next (c->queue);
    }
    if ((c->pollevents & FLUX_POLLIN) && zlist_size (c->queue) == 0)
        c->pollevents &= ~FLUX_POLLIN;
}

static int op_rank (void *impl)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    return c->rank;
}

static int op_reactor_start (void *impl)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    c->loop_rc = 0;
    ev_run (c->loop, 0);
    return c->loop_rc;
}

static void op_reactor_stop (void *impl, int rc)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    c->loop_rc = rc;
    ev_break (c->loop, EVBREAK_ALL);
}

static void queue_cb (struct ev_loop *loop, ev_zlist *w, int revents)
{
    ctx_t *c = (ctx_t *)((char *)w - offsetof (ctx_t, queue_w));
    assert (c->magic == CTX_MAGIC);

    assert (zlist_size (c->queue) > 0);
    if (c->msg_cb) {
        if (c->msg_cb (c->h, c->msg_cb_arg) < 0)
            op_reactor_stop (c, -1);
    }
}

static int op_reactor_msg_add (void *impl, flux_msg_f cb, void *arg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    int rc = -1;

    if (c->msg_cb != NULL) {
        errno = EBUSY;
        goto done;
    }
    c->msg_cb = cb;
    c->msg_cb_arg = arg;
    rc = 0;
done:
    return rc;
}

static void op_reactor_msg_remove (void *impl)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    c->msg_cb = NULL;
    c->msg_cb_arg = NULL;
}

static void fd_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    afd_t *f = (afd_t *)((char *)w - offsetof (afd_t, w));
    ctx_t *c = w->data;
    assert (c->magic == CTX_MAGIC);

    if (f->cb (c->h, w->fd, etoz (revents), f->arg) < 0)
        op_reactor_stop (c, -1);
}

static int op_reactor_fd_add (void *impl, int fd, int events,
                               FluxFdHandler cb, void *arg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    afd_t *f = xzmalloc (sizeof (*f));
    ev_io_init (&f->w, fd_cb, fd, ztoe (events));
    f->w.data = c;
    f->cb = cb;
    f->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "fd:%d:%d", fd, events);
    zhash_update (c->watchers, hashkey, f);
    zhash_freefn (c->watchers, hashkey, free);

    ev_io_start (c->loop, &f->w);

    return 0;
}

static void op_reactor_fd_remove (void *impl, int fd, int events)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "fd:%d:%d", fd, events);
    afd_t *f = zhash_lookup (c->watchers, hashkey);
    if (f) {
        ev_io_stop (c->loop, &f->w);
        zhash_delete (c->watchers, hashkey);
    }
}

static void zs_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    azs_t *z = (azs_t *)((char *)w - offsetof (azs_t, w));
    ctx_t *c = w->data;
    assert (c->magic == CTX_MAGIC);

    int rc = z->cb (c->h, w->zsock, etoz (revents), z->arg);
    if (rc < 0)
        op_reactor_stop (c, -1);
}

static int op_reactor_zs_add (void *impl, void *zs, int events,
                               FluxZsHandler cb, void *arg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    azs_t *z = xzmalloc (sizeof (*z));
    ev_zmq_init (&z->w, zs_cb, zs, ztoe (events));
    z->w.data = c;
    z->cb = cb;
    z->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "zsock:%p:%d", zs, events);
    zhash_update (c->watchers, hashkey, z);
    zhash_freefn (c->watchers, hashkey, free);

    ev_zmq_start (c->loop, &z->w);
    return 0;
}

static void op_reactor_zs_remove (void *impl, void *zs, int events)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "zsock:%p:%d", zs, events);
    azs_t *z = zhash_lookup (c->watchers, hashkey);
    if (z) {
        ev_zmq_stop (c->loop, &z->w);
        zhash_delete (c->watchers, hashkey);
    }
}

static void timer_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    atimer_t *t = (atimer_t *)((char *)w - offsetof (atimer_t, w));
    ctx_t *c = w->data;
    assert (c->magic == CTX_MAGIC);

    if (t->cb (c->h, t->arg) < 0)
         op_reactor_stop (c, -1);
}

static int op_reactor_tmout_add (void *impl, unsigned long msec, bool oneshot,
                                  FluxTmoutHandler cb, void *arg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    double after = (double)msec / 1000.0;
    double repeat = oneshot ? 0 : after;
    char hashkey[HASHKEY_LEN];

    atimer_t *t = xzmalloc (sizeof (*t));
    t->id = c->timer_seq++;
    t->w.data = c;
    ev_timer_init (&t->w, timer_cb, after, repeat);
    t->cb = cb;
    t->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "timer:%d", t->id);
    zhash_update (c->watchers, hashkey, t);
    zhash_freefn (c->watchers, hashkey, free);

    ev_timer_start (c->loop, &t->w);

    return t->id;
}

static void op_reactor_tmout_remove (void *impl, int timer_id)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "timer:%d", timer_id);
    atimer_t *t = zhash_lookup (c->watchers, hashkey);
    if (t) {
        ev_timer_stop (c->loop, &t->w);
        zhash_delete (c->watchers, hashkey);
    }
}

static void op_fini (void *impl)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (c->pollfd >= 0)
        close (c->pollfd);
    if (c->loop)
        ev_loop_destroy (c->loop);
    if (c->queue) {
        zmsg_t *zmsg;
        while ((zmsg = zlist_pop (c->queue)))
            zmsg_destroy (&zmsg);
        zlist_destroy (&c->queue);
    }
    zhash_destroy (&c->watchers);
    c->magic = ~CTX_MAGIC;
    free (c);
}

flux_t connector_init (const char *path, int flags)
{
    ctx_t *c = xzmalloc (sizeof (*c));
    c->magic = CTX_MAGIC;
    c->rank = 0;
    c->pollevents = FLUX_POLLOUT;
    c->pollfd = eventfd (1ULL, EFD_NONBLOCK); /* 1ULL since POLLOUT is set */
    if (c->pollfd < 0)
        goto error;
    if (!(c->loop = ev_loop_new (EVFLAG_AUTO))
                            || !(c->watchers = zhash_new ())
                            || !(c->queue = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    ev_zlist_init (&c->queue_w, queue_cb, c->queue, EV_READ);
    ev_zlist_start (c->loop, &c->queue_w);
    c->h = flux_handle_create (c, &handle_ops, flags);
    return c->h;
error:
    if (c) {
        int saved_errno = errno;
        op_fini (c);
        errno = saved_errno;
    }
    return NULL;
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = op_send,
    .recv = op_recv,
    .requeue = op_requeue,
    .purge = op_purge,
    .event_subscribe = NULL,
    .event_unsubscribe = NULL,
    .rank = op_rank,
    .reactor_stop = op_reactor_stop,
    .reactor_start = op_reactor_start,
    .reactor_fd_add = op_reactor_fd_add,
    .reactor_fd_remove = op_reactor_fd_remove,
    .reactor_zs_add = op_reactor_zs_add,
    .reactor_zs_remove = op_reactor_zs_remove,
    .reactor_tmout_add = op_reactor_tmout_add,
    .reactor_tmout_remove = op_reactor_tmout_remove,
    .reactor_msg_add = op_reactor_msg_add,
    .reactor_msg_remove = op_reactor_msg_remove,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
