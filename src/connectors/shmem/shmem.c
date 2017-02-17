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
#if HAVE_CALIPER
#include <caliper/cali.h>
#endif
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

#define MODHANDLE_MAGIC    0xfeefbe02
typedef struct {
    int magic;
    void *sock;
    char *uuid;
    char *uri;
    flux_t *h;
    zctx_t *zctx;
} shmem_ctx_t;

static const struct flux_handle_ops handle_ops;

static int connect_socket (shmem_ctx_t *ctx);

static int op_pollevents (void *impl)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    uint32_t e;
    size_t esize = sizeof (e);
    int revents = 0;

    if (connect_socket (ctx) < 0)
        goto done;
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
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    int fd = -1;
    size_t fdsize = sizeof (fd);

    if (connect_socket (ctx) < 0)
        goto done;
    if (zmq_getsockopt (ctx->sock, ZMQ_FD, &fd, &fdsize) < 0)
        goto done;
done:
    return fd;
}


static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    flux_msg_t *cpy = NULL;
    int type;
    int rc = -1;

    if (connect_socket (ctx) < 0)
        goto done;
    if (flux_msg_get_type (msg, &type) < 0)
        goto done;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
        case FLUX_MSGTYPE_EVENT:
            if (!(cpy = flux_msg_copy (msg, true)))
                goto done;
            if (flux_msg_enable_route (cpy) < 0)
                goto done;
            if (flux_msg_push_route (cpy, ctx->uuid) < 0)
                goto done;
            if (flux_msg_sendzsock (ctx->sock, cpy) < 0)
                goto done;
            break;
        case FLUX_MSGTYPE_RESPONSE:
        case FLUX_MSGTYPE_KEEPALIVE:
            if (flux_msg_sendzsock (ctx->sock, msg) < 0)
                goto done;
            break;
        default:
            errno = EINVAL;
            goto done;
    }
    rc = 0;
done:
    flux_msg_destroy (cpy);
    return rc;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    zmq_pollitem_t zp = {
        .events = ZMQ_POLLIN, .socket = ctx->sock, .revents = 0, .fd = -1,
    };
    flux_msg_t *msg = NULL;

    if (connect_socket (ctx) < 0)
        goto done;
    if ((flags & FLUX_O_NONBLOCK)) {
        int n;
        if ((n = zmq_poll (&zp, 1, 0L)) <= 0) {
            if (n == 0)
                errno = EWOULDBLOCK;
            goto done;
        }
    }
    msg = flux_msg_recvzsock (ctx->sock);
done:
    return msg;
}

static int op_event_subscribe (void *impl, const char *topic)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    json_object *in = Jnew ();
    flux_rpc_t *rpc = NULL;
    int rc = -1;

    if (connect_socket (ctx) < 0)
        goto done;
    Jadd_str (in, "topic", topic);
    if (!(rpc = flux_rpc (ctx->h, "cmb.sub", Jtostr (in), FLUX_NODEID_ANY, 0))
                || flux_rpc_get (rpc, NULL) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    flux_rpc_destroy (rpc);
    return rc;
}

static int op_event_unsubscribe (void *impl, const char *topic)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    json_object *in = Jnew ();
    flux_rpc_t *rpc = NULL;
    int rc = -1;

    if (connect_socket (ctx) < 0)
        goto done;
    Jadd_str (in, "topic", topic);
    if (!(rpc = flux_rpc (ctx->h, "cmb.unsub", Jtostr (in), FLUX_NODEID_ANY, 0))
                || flux_rpc_get (rpc, NULL) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    flux_rpc_destroy (rpc);
    return rc;
}

static int op_getopt (void *impl, const char *option, void *val, size_t size)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    int rc = -1;

    if (option && !strcmp (option, FLUX_OPT_ZEROMQ_CONTEXT)) {
        if (size != sizeof (ctx->zctx)) {
            errno = EINVAL;
            goto done;
        }
        memcpy (val, &ctx->zctx, size);
    } else {
        errno = EINVAL;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static int op_setopt (void *impl, const char *option,
                      const void *val, size_t size)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    size_t val_size;
    int rc = -1;

    if (option && !strcmp (option, FLUX_OPT_ZEROMQ_CONTEXT)) {
        val_size = sizeof (ctx->zctx);
        if (size != val_size) {
            errno = EINVAL;
            goto done;
        }
        memcpy (&ctx->zctx, &val, val_size);
        if (connect_socket (ctx) < 0)
            goto done;
    } else {
        errno = EINVAL;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static void op_fini (void *impl)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    if (ctx->sock)
        zsocket_destroy (ctx->zctx, ctx->sock);
    if (ctx->uuid)
        free (ctx->uuid);
    if (ctx->uri)
        free (ctx->uri);
    ctx->magic = ~MODHANDLE_MAGIC;
    free (ctx);
}

/* We have to defer connection until the zctx is available to us.
 * This function is idempotent.
 */
static int connect_socket (shmem_ctx_t *ctx)
{
    if (!ctx->sock) {
        if (!ctx->zctx) {
            errno = EINVAL;
            return -1;
        }
        if (!(ctx->sock = zsocket_new (ctx->zctx, ZMQ_PAIR)))
            return -1;
        zsocket_set_hwm (ctx->sock, 0);
        if (zsocket_connect (ctx->sock, "%s", ctx->uri) < 0) {
            zsocket_destroy (ctx->zctx, ctx->sock);
            ctx->sock = NULL;
            return -1;
        }
    }
    return 0;
}

flux_t *connector_init (const char *path, int flags)
{
#if HAVE_CALIPER
    cali_id_t uuid   = cali_create_attribute ("flux.uuid",
                                              CALI_TYPE_STRING,
                                              CALI_ATTR_SKIP_EVENTS);
    size_t length = strlen(path);
    cali_push_snapshot ( CALI_SCOPE_PROCESS | CALI_SCOPE_THREAD,
                         1, &uuid, (const void **)&path, &length);
#endif

    shmem_ctx_t *ctx = NULL;
    if (!path) {
        errno = EINVAL;
        goto error;
    }
    if (!(ctx = malloc (sizeof (*ctx)))) {
        errno = ENOMEM;
        goto error;
    }
    memset (ctx, 0, sizeof (*ctx));
    ctx->magic = MODHANDLE_MAGIC;
    if (!(ctx->uuid = strdup (path))) {
        errno = ENOMEM;
        goto error;
    }
    if (asprintf (&ctx->uri, "inproc://%s", ctx->uuid) < 0) {
        errno = ENOMEM;
        goto error;
    }
    if (!(ctx->h = flux_handle_create (ctx, &handle_ops, flags)))
        goto error;
    return ctx->h;
error:
    if (ctx) {
        int saved_errno = errno;
        op_fini (ctx);
        errno = saved_errno;
    }
    return NULL;
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = op_send,
    .recv = op_recv,
    .getopt = op_getopt,
    .setopt = op_setopt,
    .event_subscribe = op_event_subscribe,
    .event_unsubscribe = op_event_unsubscribe,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
