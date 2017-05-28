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
#include <argz.h>
#if HAVE_CALIPER
#include <caliper/cali.h>
#endif
#include <flux/core.h>

#include "src/common/libutil/log.h"

#define MODHANDLE_MAGIC    0xfeefbe02
typedef struct {
    int magic;
    zsock_t *sock;
    char *uuid;
    flux_t *h;
    char *argz;
    size_t argz_len;
    uint32_t testing_userid;
    uint32_t testing_rolemask;
} shmem_ctx_t;

static const struct flux_handle_ops handle_ops;

static int op_pollevents (void *impl)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    uint32_t e;
    int revents = 0;

    e = zsock_events (ctx->sock);
    if (e & ZMQ_POLLIN)
        revents |= FLUX_POLLIN;
    if (e & ZMQ_POLLOUT)
        revents |= FLUX_POLLOUT;
    if (e & ZMQ_POLLERR)
        revents |= FLUX_POLLERR;

    return revents;
}

static int op_pollfd (void *impl)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);

    return zsock_fd (ctx->sock);
}

static int send_testing (shmem_ctx_t *ctx, const flux_msg_t *msg)
{
    flux_msg_t *cpy;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (flux_msg_set_userid (cpy, ctx->testing_userid) < 0)
        goto done;
    if (flux_msg_set_rolemask (cpy, ctx->testing_rolemask) < 0)
        goto done;
    rc = flux_msg_sendzsock (ctx->sock, cpy);
done:
    flux_msg_destroy (cpy);
    return rc;
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    int rc;

    if (ctx->testing_userid != FLUX_USERID_UNKNOWN
            || ctx->testing_rolemask != FLUX_ROLE_NONE)
        rc = send_testing (ctx, msg);
    else
        rc = flux_msg_sendzsock (ctx->sock, msg);
    return rc;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    zmq_pollitem_t zp = {
        .events = ZMQ_POLLIN,
        .socket = zsock_resolve (ctx->sock),
        .revents = 0,
        .fd = -1,
    };
    flux_msg_t *msg = NULL;

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
    flux_rpc_t *rpc = NULL;
    int rc = -1;

    if (!(rpc = flux_rpcf (ctx->h, "cmb.sub", FLUX_NODEID_ANY, 0,
                           "{ s:s }", "topic", topic))
                || flux_rpc_get (rpc, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}

static int op_event_unsubscribe (void *impl, const char *topic)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    flux_rpc_t *rpc = NULL;
    int rc = -1;

    if (!(rpc = flux_rpcf (ctx->h, "cmb.unsub", FLUX_NODEID_ANY, 0,
                           "{ s:s }", "topic", topic))
                || flux_rpc_get (rpc, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}


static int op_setopt (void *impl, const char *option,
                      const void *val, size_t size)
{
    shmem_ctx_t *ctx = impl;
    assert (ctx->magic == MODHANDLE_MAGIC);
    size_t val_size;
    int rc = -1;

    if (option && !strcmp (option, FLUX_OPT_TESTING_USERID)) {
        val_size = sizeof (ctx->testing_userid);
        if (size != val_size) {
            errno = EINVAL;
            goto done;
        }
        memcpy (&ctx->testing_userid, val, val_size);
    } else if (option && !strcmp (option, FLUX_OPT_TESTING_ROLEMASK)) {
        val_size = sizeof (ctx->testing_rolemask);
        if (size != val_size) {
            errno = EINVAL;
            goto done;
        }
        memcpy (&ctx->testing_rolemask, val, val_size);
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
    zsock_destroy (&ctx->sock);
    free (ctx->argz);
    ctx->magic = ~MODHANDLE_MAGIC;
    free (ctx);
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
    char *item;
    int e;
    int bind_socket = 0; // if set, call bind on socket, else connect

    if (!path) {
        errno = EINVAL;
        goto error;
    }
    if (!(ctx = calloc (1, sizeof (*ctx)))) {
        errno = ENOMEM;
        goto error;
    }
    ctx->magic = MODHANDLE_MAGIC;
    ctx->testing_userid = FLUX_USERID_UNKNOWN;
    ctx->testing_rolemask = FLUX_ROLE_NONE;
    if ((e = argz_create_sep (path, '&', &ctx->argz, &ctx->argz_len)) != 0) {
        errno = e;
        goto error;
    }
    ctx->uuid = item = argz_next (ctx->argz, ctx->argz_len, NULL);
    if (!ctx->uuid) {
        errno = EINVAL;
        goto error;
    }
    while ((item = argz_next (ctx->argz, ctx->argz_len, item))) {
        if (!strcmp (item, "bind"))
            bind_socket = 1;
        else if (!strcmp (item, "connect"))
            bind_socket = 0;
        else {
            errno = EINVAL;
            goto error;
        }
    }
    if (!(ctx->sock = zsock_new_pair (NULL)))
        goto error;
    zsock_set_unbounded (ctx->sock);
    if (bind_socket) {
        if (zsock_bind (ctx->sock, "inproc://%s", ctx->uuid) < 0)
            goto error;
    }
    else {
        if (zsock_connect (ctx->sock, "inproc://%s", ctx->uuid) < 0)
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
    .getopt = NULL,
    .setopt = op_setopt,
    .event_subscribe = op_event_subscribe,
    .event_unsubscribe = op_event_unsubscribe,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
