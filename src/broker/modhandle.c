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
#include <errno.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

#include "modhandle.h"

#define MODHANDLE_MAGIC    0xfeefbe02
typedef struct {
    int magic;
    void *sock;
    void *zctx;
    uint32_t rank;
    char *uuid;
    flux_t h;
} ctx_t;

static const struct flux_handle_ops mod_handle_ops;

static void modhandle_destroy (void *impl)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    if (ctx->uuid)
        free (ctx->uuid);
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

    if (!(ctx->h = flux_handle_create (ctx, &mod_handle_ops, 0))) {
        modhandle_destroy (ctx);
        return NULL;
    }
    return ctx->h;
}

static int op_pollevents (void *impl)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    uint32_t e;
    size_t esize = sizeof (e);
    int revents = 0;

    if (zmq_getsockopt (ctx->sock, ZMQ_EVENTS, &e, &esize) < 0) {
        revents |= FLUX_POLLERR;
        goto done;
    }
    if (e & ZMQ_POLLIN)
        revents |= FLUX_POLLIN;
    if (e & ZMQ_POLLOUT)
        revents |= FLUX_POLLOUT;
    if (e & ZMQ_POLLERR)
        revents |= FLUX_POLLERR;
done:
    return revents;
}

static int op_pollfd (void *impl)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    int fd;
    size_t fdsize = sizeof (fd);

    if (zmq_getsockopt (ctx->sock, ZMQ_FD, &fd, &fdsize) < 0)
        return -1;
    return fd;
}


static int mod_send (void *impl, const flux_msg_t *msg, int flags)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    flux_msg_t *cpy = NULL;
    int type;
    int rc = -1;

    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
        case FLUX_MSGTYPE_EVENT:
            if (flux_msg_enable_route (cpy) < 0)
                goto done;
            if (flux_msg_push_route (cpy, ctx->uuid) < 0)
                goto done;
            break;
        case FLUX_MSGTYPE_RESPONSE:
            break;
        default:
            errno = EINVAL;
            goto done;
    }
    if (zmsg_send (&cpy, ctx->sock) < 0)
        goto done;
    rc = 0;
done:
    if (cpy)
        flux_msg_destroy (cpy);
    return rc;
}

static flux_msg_t *mod_recv (void *impl, int flags)
{
    ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    zmq_pollitem_t zp = {
        .events = ZMQ_POLLIN, .socket = ctx->sock, .revents = 0, .fd = -1,
    };
    flux_msg_t *msg = NULL;

    if ((flags & FLUX_O_NONBLOCK)) {
        int n;
        if ((n = zmq_poll (&zp, 1, 0L)) < 0)
            goto done; /* likely: EWOULDBLOCK | EAGAIN */
        assert (n == 1);
        assert (zp.revents == ZMQ_POLLIN);
    }
    msg = zmsg_recv (ctx->sock);
done:
    return msg;
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

static const struct flux_handle_ops mod_handle_ops = {
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = mod_send,
    .recv = mod_recv,
    .event_subscribe = mod_event_subscribe,
    .event_unsubscribe = mod_event_unsubscribe,
    .rank = mod_rank,
    .get_zctx = mod_get_zctx,
    .impl_destroy = modhandle_destroy,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
