/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* connector_interthread.c - bidirectional, inter-thread message channel */

/* Notes:
 * - Each channel has a unique name and 1-2 attached flux_t handles.
 * - Channels can be safely opened and closed from multiple threads.
 * - The channel is created on first open and destroyed on last close.
 * - There are no active/passive roles.
 * - Writing is always non-blocking.
 * - Reading can be either blocking or non-blocking.
 * - Neither reading nor writing are affected if the other end disconnects.
 * - Reconnect is allowed (by happenstance, not for any particular use case)
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/aux.h"
#include "ccan/list/list.h"
#include "ccan/str/str.h"

#include "message_private.h" // for access to msg->aux
#include "msg_deque.h"

struct channel {
    char *name;
    struct msg_deque *pair[2];
    int refcount; // max of 2
    struct list_node list;
};

struct interthread_ctx {
    flux_t *h;
    struct flux_msg_cred cred;
    char *router;
    struct channel *chan;
    struct msg_deque *send; // refers to ctx->chan->pair[x]
    struct msg_deque *recv; // refers to ctx->chan->pair[y]
};

/* Global state.
 * Threads accessing a channel must share this global in order to "connect".
 */
static struct list_head channels = LIST_HEAD_INIT (channels);
static pthread_mutex_t channels_lock = PTHREAD_MUTEX_INITIALIZER;

static const struct flux_handle_ops handle_ops;

static void channel_destroy (struct channel *chan)
{
    if (chan) {
        int saved_errno = errno;
        msg_deque_destroy (chan->pair[0]);
        msg_deque_destroy (chan->pair[1]);
        free (chan->name);
        free (chan);
        errno = saved_errno;
    }
}

static struct channel *channel_create (const char *name)
{
    struct channel *chan;

    if (!(chan = calloc (1, sizeof (*chan)))
        || !(chan->name = strdup (name))
        || !(chan->pair[0] = msg_deque_create (0))
        || !(chan->pair[1] = msg_deque_create (0)))
        goto error;
    list_node_init (&chan->list);
    return chan;
error:
    channel_destroy (chan);
    return NULL;
}

/* Add new channel to global list under lock and take a reference.
 */
static void channel_add_safe (struct channel *chan)
{
    pthread_mutex_lock (&channels_lock);
    list_add (&channels, &chan->list);
    chan->refcount++;
    pthread_mutex_unlock (&channels_lock);
}

/* Drop a reference on channel under lock.
 * If the refcount becomes zero, remove it from the global list and return true.
 */
static bool channel_remove_safe (struct channel *chan)
{
    bool result = false;
    if (chan) {
        pthread_mutex_lock (&channels_lock);
        if (--chan->refcount == 0) {
            list_del (&chan->list);
            result = true;
        }
        pthread_mutex_unlock (&channels_lock);
    }
    return result;
}

/* Look up a channel by name under lock, for pairing.
 * If found but already paired (refcount == 2), fail with EADDRINUSE.
 * If not found, fail with ENOENT.
 * Otherwise take a reference under lock and return the channel.
 */
static struct channel *channel_pair_safe (const char *name)
{
    struct channel *result = NULL;
    struct channel *chan = NULL;

    pthread_mutex_lock (&channels_lock);
    list_for_each (&channels, chan, list) {
        if (streq (name, chan->name)) {
            if (chan->refcount > 1) {
                pthread_mutex_unlock (&channels_lock);
                errno = EADDRINUSE;
                return NULL;
            }
            chan->refcount++;
            result = chan;
            break;
        }
    }
    pthread_mutex_unlock (&channels_lock);
    if (!result)
        errno = ENOENT;
    return result;
}

/**
 ** Connector interfaces follow
 **/

static int op_pollevents (void *impl)
{
    struct interthread_ctx *ctx = impl;
    int e, revents = 0;

    e = msg_deque_pollevents (ctx->recv);
    if (e & POLLIN)
        revents |= FLUX_POLLIN;
    if (e & POLLOUT)
        revents |= FLUX_POLLOUT;
    if (e & POLLERR)
        revents |= FLUX_POLLERR;
    return revents;
}

static int op_pollfd (void *impl)
{
    struct interthread_ctx *ctx = impl;
    return msg_deque_pollfd (ctx->recv);
}

static int router_process (flux_msg_t *msg, const char *name)
{
    int type;
    if (flux_msg_get_type (msg, &type) < 0)
        return -1;
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            if (flux_msg_route_delete_last (msg) < 0)
                return -1;
            break;
        case FLUX_MSGTYPE_REQUEST:
        case FLUX_MSGTYPE_EVENT:
            flux_msg_route_enable (msg);
            if (flux_msg_route_push (msg, name) < 0)
                return -1;
            break;
        default:
            break;
    }
    return 0;
}

static int op_send_new (void *impl, flux_msg_t **msg, int flags)
{
    struct interthread_ctx *ctx = impl;
    struct flux_msg_cred cred;

    if (flux_msg_get_cred (*msg, &cred) < 0)
        return -1;
    if (cred.userid == FLUX_USERID_UNKNOWN
        && cred.rolemask == FLUX_ROLE_NONE) {
        if (flux_msg_set_cred (*msg, ctx->cred) < 0)
            return -1;
    }
    if (ctx->router) {
        if (router_process (*msg, ctx->router) < 0)
            return -1;
    }
    /* The aux container doesn't survive transit of a TCP channel
     * so it shouldn't survive transit of this kind either.
     */
    aux_destroy (&(*msg)->aux);
    if (msg_deque_push_back (ctx->send, *msg) < 0)
        return -1;
    *msg = NULL;
    return 0;
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    flux_msg_t *cpy;

    if (!(cpy = flux_msg_copy (msg, true))
        || op_send_new (impl, &cpy, flags)) {
        flux_msg_destroy (cpy);
        return -1;
    }
    return 0;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    struct interthread_ctx *ctx = impl;
    flux_msg_t *msg;

    do {
        msg = msg_deque_pop_front (ctx->recv);
        if (!msg) {
            if ((flags & FLUX_O_NONBLOCK)) {
                errno = EWOULDBLOCK;
                return NULL;
            }
            struct pollfd pfd = {
                .fd = msg_deque_pollfd (ctx->recv),
                .events = POLLIN,
                .revents = 0,
            };
            if (poll (&pfd, 1, -1) < 0)
                return NULL;
        }
    } while (!msg);
    if (ctx->router) {
        if (router_process (msg, ctx->chan->name) < 0) {
            flux_msg_destroy (msg);
            return NULL;
        }
    }
    return msg;
}

static int op_getopt (void *impl, const char *option, void *val, size_t size)
{
    struct interthread_ctx *ctx = impl;

    if (streq (option, FLUX_OPT_RECV_QUEUE_COUNT)) {
        size_t count = msg_deque_count (ctx->recv);
        if (size != sizeof (count) || !val)
            goto error;
        memcpy (val, &count, size);
    }
    else if (streq (option, FLUX_OPT_SEND_QUEUE_COUNT)) {
        size_t count = msg_deque_count (ctx->send);
        if (size != sizeof (count) || !val)
            goto error;
        memcpy (val, &count, size);
    }
    else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static int op_setopt (void *impl,
                      const char *option,
                      const void *val,
                      size_t size)
{
    struct interthread_ctx *ctx = impl;

    if (streq (option, FLUX_OPT_ROUTER_NAME)) {
        free (ctx->router);
        if (!(ctx->router = strndup (val, size)))
            return -1;
    }
    else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static void op_fini (void *impl)
{
    struct interthread_ctx *ctx = impl;
    if (ctx) {
        int saved_errno = errno;
        if (channel_remove_safe (ctx->chan))
            channel_destroy (ctx->chan);
        free (ctx->router);
        free (ctx);
        errno = saved_errno;
    }
}

flux_t *connector_interthread_init (const char *path,
                                    int flags,
                                    flux_error_t *errp)
{
    struct interthread_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        goto error_seterrp;
    ctx->cred.userid = getuid ();
    ctx->cred.rolemask = FLUX_ROLE_OWNER | FLUX_ROLE_LOCAL;
    if (!(ctx->chan = channel_pair_safe (path))) {
        if (errno == EADDRINUSE) {
            errprintf (errp, "interthread channel %s is already paired", path);
            goto error;
        }
        else if (errno != ENOENT)
            goto error_seterrp;
        if (!(ctx->chan = channel_create (path)))
            goto error_seterrp;
        channel_add_safe (ctx->chan);
        ctx->send = ctx->chan->pair[0];
        ctx->recv = ctx->chan->pair[1];
    }
    else {
        ctx->send = ctx->chan->pair[1];
        ctx->recv = ctx->chan->pair[0];
    }
    if (!(ctx->h = flux_handle_create (ctx, &handle_ops, flags)))
        goto error_seterrp;
    return ctx->h;
error_seterrp:
    errprintf (errp, "%s", strerror (errno));
error:
    op_fini (ctx);
    return NULL;
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = op_send,
    .send_new = op_send_new,
    .recv = op_recv,
    .setopt = op_setopt,
    .getopt = op_getopt,
    .impl_destroy = op_fini,
};

// vi:ts=4 sw=4 expandtab
