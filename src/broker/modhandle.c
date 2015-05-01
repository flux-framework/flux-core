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
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/time.h>
#include <json.h>
#include <argz.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/zdump.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

#include "src/common/libev/ev.h"
#include "src/common/libutil/ev_zmq.h"
#include "src/common/libutil/ev_zlist.h"

#include "modhandle.h"

#define MODHANDLE_MAGIC    0xfeefbe02
typedef struct {
    int magic;

    /* Passed in via modhandle_create.
     */
    void *sock;
    void *zctx;
    uint32_t rank;
    char *uuid;

    struct ev_loop *loop;
    int loop_rc;
    zhash_t *watchers;

    ev_zmq sock_w;

    int timer_seq;

    flux_msg_f msg_cb;
    void *msg_cb_arg;

    zlist_t *putmsg;
    ev_zlist putmsg_w;

    flux_t h;
} ctx_t;

#define HASHKEY_LEN 80

typedef struct {
    ev_timer w;
    FluxTmoutHandler cb;
    void *arg;
    int id;
} ptimer_t;

typedef struct {
    ev_zmq w;
    FluxZsHandler cb;
    void *arg;
} pzs_t;

typedef struct {
    ev_io w;
    FluxFdHandler cb;
    void *arg;
} pfd_t;

static const struct flux_handle_ops mod_handle_ops;

static void main_cb (struct ev_loop *loop, ev_zmq *w, int revents);
static void putmsg_cb (struct ev_loop *loop, ev_zlist *w, int revents);

static void modhandle_destroy (void *impl)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    if (ctx->putmsg) {
        zmsg_t *zmsg;
        while ((zmsg = zlist_pop (ctx->putmsg)))
            zmsg_destroy (&zmsg);
        zlist_destroy (&ctx->putmsg);
    }
    zhash_destroy (&ctx->watchers);
    if (ctx->uuid)
        free (ctx->uuid);
    if (ctx->loop)
        ev_loop_destroy (ctx->loop);
    ctx->magic = ~MODHANDLE_MAGIC;
    free (ctx);
}

flux_t modhandle_create (void *sock, const char *uuid,
                         uint32_t rank, zctx_t *zctx)
{
    ctx_t *ctx = xzmalloc (sizeof (*ctx));
    ctx->magic = MODHANDLE_MAGIC;

    ctx->sock = sock;
    ctx->uuid = xstrdup (uuid);
    ctx->rank = rank;
    ctx->zctx = zctx;

    if (!(ctx->loop = ev_loop_new (EVFLAG_AUTO)))
        err_exit ("ev_loop_new");

    ctx->putmsg = zlist_new ();
    ctx->watchers = zhash_new ();
    if (!ctx->putmsg || !ctx->watchers)
        oom ();

    ev_zmq_init (&ctx->sock_w, main_cb, ctx->sock, EV_READ);
    ctx->sock_w.data = ctx;

    ev_zlist_init (&ctx->putmsg_w, putmsg_cb, ctx->putmsg, EV_READ);
    ctx->putmsg_w.data = ctx;

    if (!(ctx->h = flux_handle_create (ctx, &mod_handle_ops, 0))) {
        modhandle_destroy (ctx);
        return NULL;
    }
    return ctx->h;
}

/* Enable/disable the appropriate watchers to give putmsg priority
 * over the main zsockets.
 */
static void sync_msg_watchers (ctx_t *ctx)
{
    if (ctx->msg_cb == NULL) {
        ev_zmq_stop (ctx->loop, &ctx->sock_w);
        ev_zlist_stop (ctx->loop, &ctx->putmsg_w);
    } else if (zlist_size (ctx->putmsg) == 0) {
        ev_zmq_start (ctx->loop, &ctx->sock_w);
        ev_zlist_stop (ctx->loop, &ctx->putmsg_w);
    } else {
        ev_zmq_stop (ctx->loop, &ctx->sock_w);
        ev_zlist_start (ctx->loop, &ctx->putmsg_w);
    }
}

static int mod_sendmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    int type;
    int rc = -1;

    if (flux_msg_get_type (*zmsg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            if (flux_msg_enable_route (*zmsg) < 0)
                goto done;
            if (flux_msg_push_route (*zmsg, ctx->uuid) < 0)
                goto done;
            rc = zmsg_send (zmsg, ctx->sock);
            break;
        case FLUX_MSGTYPE_RESPONSE:
            rc = zmsg_send (zmsg, ctx->sock);
            break;
        case FLUX_MSGTYPE_EVENT:
            if (flux_msg_enable_route (*zmsg) < 0)
                goto done;
            if (flux_msg_push_route (*zmsg, ctx->uuid) < 0)
                goto done;
            rc = zmsg_send (zmsg, ctx->sock);
            break;
        default:
            errno = EINVAL;
            break;
    }
done:
    return rc;
}

static zmsg_t *mod_recvmsg_main (ctx_t *ctx, bool nonblock)
{
    zmq_pollitem_t items[] = {
        {  .events = ZMQ_POLLIN, .socket = ctx->sock},
    };
    int nitems = sizeof (items) / sizeof (items[0]);
    int rc, i;
    zmsg_t *zmsg = NULL;

    do {
        rc = zmq_poll (items, nitems, nonblock ? 0 : -1);
        if (rc < 0)
            goto done;
        if (rc > 0) {
            for (i = 0; i < nitems; i++) {
                if (items[i].revents & ZMQ_POLLIN) {
                    zmsg = zmsg_recv (items[i].socket);
                    goto done;
                }
            }
        }
    } while (!nonblock);
done:
    return zmsg;
}

static zmsg_t *mod_recvmsg_putmsg (ctx_t *ctx)
{
    zmsg_t *zmsg = zlist_pop (ctx->putmsg);
    if (zmsg) {
        if (zlist_size (ctx->putmsg) == 0)
            sync_msg_watchers (ctx);
    }
    return zmsg;
}

static zmsg_t *mod_recvmsg (void *impl, bool nonblock)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    zmsg_t *zmsg = NULL;

    if (!(zmsg = mod_recvmsg_putmsg (ctx)))
        zmsg = mod_recvmsg_main (ctx, nonblock);
    return zmsg;
}

static int mod_putmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    int oldcount = zlist_size (ctx->putmsg);

    if (zlist_append (ctx->putmsg, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    if (oldcount == 0)
        sync_msg_watchers (ctx);
    return 0;
}

static int mod_pushmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    int oldcount = zlist_size (ctx->putmsg);

    if (zlist_push (ctx->putmsg, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    if (oldcount == 0)
        sync_msg_watchers (ctx);
    return 0;
}

static void mod_purge (void *impl, flux_match_t match)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    zmsg_t *zmsg = zlist_first (ctx->putmsg);

    while (zmsg) {
        if (flux_msg_cmp (zmsg, match)) {
            zlist_remove (ctx->putmsg, zmsg);
            zmsg_destroy (&zmsg);
        }
        zmsg = zlist_next (ctx->putmsg);
    }
}

static int mod_event_subscribe (void *impl, const char *topic)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    JSON in = Jnew ();
    int rc = -1;

    Jadd_str (in, "topic", topic);
    if (flux_json_rpc (ctx->h, ctx->rank, "cmb.sub", in, NULL) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    return rc;
}

static int mod_event_unsubscribe (void *impl, const char *topic)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    JSON in = Jnew ();
    int rc = -1;

    Jadd_str (in, "topic", topic);
    if (flux_json_rpc (ctx->h, ctx->rank, "cmb.unsub", in, NULL) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    return rc;
}

static int mod_rank (void *impl)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    return ctx->rank;
}

static zctx_t *mod_get_zctx (void *impl)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    return ctx->zctx;
}

static int mod_reactor_start (void *impl)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);

    ctx->loop_rc = 0;
    ev_run (ctx->loop, 0);
    return ctx->loop_rc;
};

static void mod_reactor_stop (void *impl, int rc)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);

    ctx->loop_rc = rc;
    ev_break (ctx->loop, EVBREAK_ALL);
}

static int mod_reactor_msg_add (void *impl, flux_msg_f cb, void *arg)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    int rc = -1;

    if (ctx->msg_cb != NULL) {
        errno = EBUSY;
        goto done;
    }
    ctx->msg_cb = cb;
    ctx->msg_cb_arg = arg;
    sync_msg_watchers (ctx);
    rc = 0;
done:
    return rc;
}

static void mod_reactor_msg_remove (void *impl)
{
    ctx_t *ctx = impl;
    ctx->msg_cb = NULL;
    ctx->msg_cb_arg = NULL;
    sync_msg_watchers (ctx);
}

static void fd_cb (struct ev_loop *looop, ev_io *w, int revents)
{
    pfd_t *f = (pfd_t *)((char *)w - offsetof (pfd_t, w));
    ctx_t *ctx = w->data;
    assert (ctx->magic == MODHANDLE_MAGIC);

    if (f->cb (ctx->h, w->fd, etoz (revents), f->arg) < 0)
        mod_reactor_stop (ctx, -1);
}

static int mod_reactor_fd_add (void *impl, int fd, int events,
                                  FluxFdHandler cb, void *arg)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    char hashkey[HASHKEY_LEN];

    pfd_t *f = xzmalloc (sizeof (*f));
    ev_io_init (&f->w, fd_cb, fd, ztoe (events));
    f->w.data = ctx;
    f->cb = cb;
    f->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "fd:%d:%d", fd, events);
    zhash_update (ctx->watchers, hashkey, f);
    zhash_freefn (ctx->watchers, hashkey, free);

    ev_io_start (ctx->loop, &f->w);

    return 0;
}

static void mod_reactor_fd_remove (void *impl, int fd, int events)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "fd:%d:%d", fd, events);
    pfd_t *f = zhash_lookup (ctx->watchers, hashkey);
    if (f) {
        ev_io_stop (ctx->loop, &f->w);
        zhash_delete (ctx->watchers, hashkey);
    }
}

static void zs_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    pzs_t *z = (pzs_t *)((char *)w - offsetof (pzs_t, w));
    ctx_t *ctx = z->w.data;
    assert (ctx->magic == MODHANDLE_MAGIC);

    if (z->cb (ctx->h, w->zsock, etoz (revents), z->arg) < 0)
        mod_reactor_stop (ctx, -1);
}

static int mod_reactor_zs_add (void *impl, void *zs, int events,
                                  FluxZsHandler cb, void *arg)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    char hashkey[HASHKEY_LEN];

    pzs_t *z = xzmalloc (sizeof (*z));
    ev_zmq_init (&z->w, zs_cb, zs, ztoe (events));
    z->w.data = ctx;
    z->cb = cb;
    z->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "zsock:%p:%d", zs, events);
    zhash_update (ctx->watchers, hashkey, z);
    zhash_freefn (ctx->watchers, hashkey, free);

    ev_zmq_start (ctx->loop, &z->w);

    return 0;
}

static void mod_reactor_zs_remove (void *impl, void *zs, int events)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "zsock:%p:%d", zs, events);
    pzs_t *z = zhash_lookup (ctx->watchers, hashkey);
    if (z) {
        ev_zmq_stop (ctx->loop, &z->w);
        zhash_delete (ctx->watchers, hashkey);
    }
}

static void timer_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    ptimer_t *t = (ptimer_t *)((char *)w - offsetof (ptimer_t, w));
    ctx_t *ctx = w->data;
    assert (ctx->magic == MODHANDLE_MAGIC);

    if (t->cb (ctx->h, t->arg) < 0)
        mod_reactor_stop (ctx, -1);
}

static int mod_reactor_tmout_add (void *impl, unsigned long msec,
                                     bool oneshot,
                                     FluxTmoutHandler cb, void *arg)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    double after = (double)msec / 1000.0;
    double repeat = oneshot ? 0 : after;
    char hashkey[HASHKEY_LEN];

    ptimer_t *t = xzmalloc (sizeof (*t));
    t->id = ctx->timer_seq++;
    t->w.data = ctx;
    ev_timer_init (&t->w, timer_cb, after, repeat);
    t->cb = cb;
    t->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "timer:%d", t->id);
    zhash_update (ctx->watchers, hashkey, t);
    zhash_freefn (ctx->watchers, hashkey, free);

    ev_timer_start (ctx->loop, &t->w);

    return t->id;
}

static void mod_reactor_tmout_remove (void *impl, int timer_id)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "timer:%d", timer_id);
    ptimer_t *t = zhash_lookup (ctx->watchers, hashkey);
    if (t) {
        ev_timer_stop (ctx->loop, &t->w);
        zhash_delete (ctx->watchers, hashkey);
    }
}

static void main_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    ctx_t *ctx = w->data;
    assert (ctx->magic == MODHANDLE_MAGIC);

    assert (zlist_size (ctx->putmsg) == 0);

    if ((revents & EV_ERROR)) {
        mod_reactor_stop (ctx, -1);
    } else if ((revents & EV_READ)) {
        if (ctx->msg_cb) {
            if (ctx->msg_cb (ctx->h, ctx->msg_cb_arg) < 0)
                mod_reactor_stop (ctx, -1);
        }
    }
}

static void putmsg_cb (struct ev_loop *loop, ev_zlist *w, int revents)
{
    ctx_t *ctx = w->data;
    assert (ctx->magic == MODHANDLE_MAGIC);

    assert (zlist_size (ctx->putmsg) > 0);
    if (ctx->msg_cb) {
        if (ctx->msg_cb (ctx->h, ctx->msg_cb_arg) < 0)
            mod_reactor_stop (ctx, -1);
    }
}

static const struct flux_handle_ops mod_handle_ops = {
    .sendmsg = mod_sendmsg,
    .recvmsg = mod_recvmsg,
    .putmsg = mod_putmsg,
    .pushmsg = mod_pushmsg,
    .purge = mod_purge,
    .event_subscribe = mod_event_subscribe,
    .event_unsubscribe = mod_event_unsubscribe,
    .rank = mod_rank,
    .get_zctx = mod_get_zctx,
    .reactor_stop = mod_reactor_stop,
    .reactor_start = mod_reactor_start,
    .reactor_fd_add = mod_reactor_fd_add,
    .reactor_fd_remove = mod_reactor_fd_remove,
    .reactor_zs_add = mod_reactor_zs_add,
    .reactor_zs_remove = mod_reactor_zs_remove,
    .reactor_tmout_add = mod_reactor_tmout_add,
    .reactor_tmout_remove = mod_reactor_tmout_remove,
    .reactor_msg_add = mod_reactor_msg_add,
    .reactor_msg_remove = mod_reactor_msg_remove,
    .impl_destroy = modhandle_destroy,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
