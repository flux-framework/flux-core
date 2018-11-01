/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>
#include <czmq.h>

#include "src/common/libflux/handle.h"
#include "src/common/libflux/reactor.h"
#include "src/common/libflux/msg_handler.h"
#include "src/common/libflux/message.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"

#include "reactor.h"


#define HASHKEY_LEN 80

struct ctx {
    zhash_t *watchers;
    int timer_seq;
};

struct msg_compat {
    flux_msg_handler_t *mh;
    FluxMsgHandler fn;
    void *arg;
};

struct fd_compat {
    flux_t *h;
    flux_watcher_t *w;
    FluxFdHandler fn;
    void *arg;
};

struct timer_compat {
    flux_t *h;
    flux_watcher_t *w;
    FluxTmoutHandler fn;
    void *arg;
    int id;
    bool oneshot;
};


static void freectx (void *arg)
{
    struct ctx *ctx = arg;

    zhash_destroy (&ctx->watchers);
    free (ctx);
}

static struct ctx *getctx (flux_t *h)
{
    struct ctx *ctx = flux_aux_get (h, "reactor_compat");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->watchers = zhash_new ();
        if (!ctx->watchers)
            oom ();
        flux_aux_set (h, "reactor_compat", ctx, freectx);
    }
    return ctx;
}

static int events_to_libzmq (int events)
{
    int e = 0;
    if (events & FLUX_POLLIN)
        e |= ZMQ_POLLIN;
    if (events & FLUX_POLLOUT)
        e |= ZMQ_POLLOUT;
    if (events & FLUX_POLLERR)
        e |= ZMQ_POLLERR;
    return e;
}

static int libzmq_to_events (int events)
{
    int e = 0;
    if (events & ZMQ_POLLIN)
        e |= FLUX_POLLIN;
    if (events & ZMQ_POLLOUT)
        e |= FLUX_POLLOUT;
    if (events & ZMQ_POLLERR)
        e |= FLUX_POLLERR;
    return e;
}

/* message
 */
void msg_compat_cb (flux_t *h, flux_msg_handler_t *mh,
                    const flux_msg_t *msg, void *arg)
{
    struct msg_compat *compat = arg;
    flux_msg_t *cpy = NULL;
    int type;

    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (compat->fn (h, type, &cpy, compat->arg) < 0)
        flux_reactor_stop_error (flux_get_reactor (h));
done:
    flux_msg_destroy (cpy);
}

static void msg_compat_free (struct msg_compat *c)
{
    if (c) {
        flux_msg_handler_destroy (c->mh);
        free (c);
    }
}

static int msghandler_add (flux_t *h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg)
{
    struct ctx *ctx = getctx (h);
    struct flux_match match = {
        .typemask = typemask,
        .topic_glob = (char *)pattern,
        .matchtag = FLUX_MATCHTAG_NONE,
    };
    char hashkey[HASHKEY_LEN];
    struct msg_compat *c = xzmalloc (sizeof (*c));

    c->fn = cb;
    c->arg = arg;
    if (!(c->mh = flux_msg_handler_create (h, match, msg_compat_cb, c))) {
        free (c);
        return -1;
    }
    flux_msg_handler_start (c->mh);
    snprintf (hashkey, sizeof (hashkey), "msg:%d:%s", typemask, pattern);
    zhash_update (ctx->watchers, hashkey, c);
    zhash_freefn (ctx->watchers, hashkey, (zhash_free_fn *)msg_compat_free);
    return 0;
}

int flux_msghandler_add (flux_t *h, int typemask, const char *pattern,
                         FluxMsgHandler cb, void *arg)
{
    return msghandler_add (h, typemask, pattern, cb, arg);
}

void flux_msghandler_remove (flux_t *h, int typemask, const char *pattern)
{
    struct ctx *ctx = getctx (h);
    struct msg_compat *c;
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "msg:%d:%s", typemask, pattern);
    if ((c = zhash_lookup (ctx->watchers, hashkey))) {
        flux_msg_handler_stop (c->mh);
        zhash_delete (ctx->watchers, hashkey);
    }
}

/* fd
 */

static void fd_compat_free (struct fd_compat *c)
{
    if (c) {
        flux_watcher_destroy (c->w);
        free (c);
    }
}

static void fd_compat_cb (flux_reactor_t *r, flux_watcher_t *w, int revents,
                          void *arg)
{
    struct fd_compat *c = arg;
    int fd = flux_fd_watcher_get_fd (w);
    if (c->fn (c->h, fd, events_to_libzmq (revents), c->arg) < 0)
        flux_reactor_stop_error (r);
}


int flux_fdhandler_add (flux_t *h, int fd, short events,
                        FluxFdHandler cb, void *arg)
{
    struct ctx *ctx = getctx (h);
    struct fd_compat *c = xzmalloc (sizeof (*c));
    char hashkey[HASHKEY_LEN];

    c->h = h;
    c->fn = cb;
    c->arg = arg;
    c->w = flux_fd_watcher_create (flux_get_reactor (h), fd,
                                   libzmq_to_events (events), fd_compat_cb,c);
    if (!c->w) {
        free (c);
        return -1;
    }
    flux_watcher_start (c->w);
    snprintf (hashkey, sizeof (hashkey), "fd:%d:%hd", fd, events);
    zhash_update (ctx->watchers, hashkey, c);
    zhash_freefn (ctx->watchers, hashkey, (zhash_free_fn *)fd_compat_free);
    return 0;
}

void flux_fdhandler_remove (flux_t *h, int fd, short events)
{
    struct ctx *ctx = getctx (h);
    struct fd_compat *c;
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "fd:%d:%hd", fd, events);
    if ((c = zhash_lookup (ctx->watchers, hashkey))) {
        flux_watcher_stop (c->w);
        zhash_delete (ctx->watchers, hashkey);
    }
}

/* Timer
 */

static void timer_compat_free (struct timer_compat *c)
{
    if (c) {
        flux_watcher_destroy (c->w);
        free (c);
    }
}

static void timer_compat_cb (flux_reactor_t *r, flux_watcher_t *w,
                             int revents, void *arg)
{
    struct timer_compat *c = arg;
    if (c->fn (c->h, c->arg) < 0)
        flux_reactor_stop_error (r);
}

int flux_tmouthandler_add (flux_t *h, unsigned long msec, bool oneshot,
                           FluxTmoutHandler cb, void *arg)
{
    struct ctx *ctx = getctx (h);
    struct timer_compat *c = xzmalloc (sizeof (*c));
    char hashkey[HASHKEY_LEN];
    double after = 1E-3 * msec;
    double rpt = oneshot ? 0 : after;

    c->h = h;
    c->fn = cb;
    c->arg = arg;
    c->oneshot = oneshot;
    c->id = ctx->timer_seq++;
    c->w = flux_timer_watcher_create (flux_get_reactor (h),
                                      after, rpt, timer_compat_cb, c);
    if (!c->w) {
        free (c);
        return -1;
    }
    flux_watcher_start (c->w);
    snprintf (hashkey, sizeof (hashkey), "timer:%d", c->id);
    zhash_update (ctx->watchers, hashkey, c);
    zhash_freefn (ctx->watchers, hashkey, (zhash_free_fn *)timer_compat_free);
    return c->id;
}

void flux_tmouthandler_remove (flux_t *h, int timer_id)
{
    struct ctx *ctx = getctx (h);
    struct timer_compat *c;
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "timer:%d", timer_id);
    if ((c = zhash_lookup (ctx->watchers, hashkey))) {
        flux_watcher_stop (c->w);
        zhash_delete (ctx->watchers, hashkey);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
