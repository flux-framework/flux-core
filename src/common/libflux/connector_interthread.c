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
#include <sys/poll.h>
#include <pthread.h>

#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "ccan/list/list.h"
#include "ccan/str/str.h"

struct msglist_safe {
    struct flux_msglist *queue;
    pthread_mutex_t lock;
};

struct channel {
    char *name;
    struct msglist_safe *pair[2];
    int refcount; // max of 2
    struct list_node list;
};

struct interthread_ctx {
    flux_t *h;
    struct flux_msg_cred cred;
    char *router;
    struct channel *chan;
    struct msglist_safe *send; // refers to ctx->chan->pair[x]
    struct msglist_safe *recv; // refers to ctx->chan->pair[y]
};

/* Global state.
 * Threads accessing a channel must share this global in order to "connect".
 */
static struct list_head channels = LIST_HEAD_INIT (channels);
static pthread_mutex_t channels_lock = PTHREAD_MUTEX_INITIALIZER;

static const struct flux_handle_ops handle_ops;


static void msglist_safe_destroy (struct msglist_safe *ml)
{
    if (ml) {
        int saved_errno = errno;
        pthread_mutex_destroy (&ml->lock);
        flux_msglist_destroy (ml->queue);
        free (ml);
        errno = saved_errno;
    };
}

static struct msglist_safe *msglist_safe_create (void)
{
    struct msglist_safe *ml;

    if (!(ml = calloc (1, sizeof (*ml))))
        return NULL;
    pthread_mutex_init (&ml->lock, NULL);
    if (!(ml->queue = flux_msglist_create ())) {
        msglist_safe_destroy (ml);
        return NULL;
    }
    return ml;
}

static int msglist_safe_append (struct msglist_safe *ml, const flux_msg_t *msg)
{
    pthread_mutex_lock (&ml->lock);
    int rc = flux_msglist_append (ml->queue, msg);
    pthread_mutex_unlock (&ml->lock);
    return rc;
}

static flux_msg_t *msglist_safe_pop (struct msglist_safe *ml)
{

    pthread_mutex_lock (&ml->lock);
    flux_msg_t *msg = (flux_msg_t *)flux_msglist_pop (ml->queue);
    pthread_mutex_unlock (&ml->lock);
    return msg;
}

static int msglist_safe_pollfd (struct msglist_safe *ml)
{
    pthread_mutex_lock (&ml->lock);
    int rc = flux_msglist_pollfd (ml->queue);
    pthread_mutex_unlock (&ml->lock);
    return rc;
}

static int msglist_safe_pollevents (struct msglist_safe *ml)
{
    pthread_mutex_lock (&ml->lock);
    int rc = flux_msglist_pollevents (ml->queue);
    pthread_mutex_unlock (&ml->lock);
    return rc;
}

static void channel_destroy (struct channel *chan)
{
    if (chan) {
        int saved_errno = errno;
        msglist_safe_destroy (chan->pair[0]);
        msglist_safe_destroy (chan->pair[1]);
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
        || !(chan->pair[0] = msglist_safe_create ())
        || !(chan->pair[1] = msglist_safe_create ()))
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

    e = msglist_safe_pollevents (ctx->recv);
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
    return msglist_safe_pollfd (ctx->recv);
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

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct interthread_ctx *ctx = impl;
    flux_msg_t *cpy;
    struct flux_msg_cred cred;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (flux_msg_get_cred (cpy, &cred) < 0)
        goto done;
    if (cred.userid == FLUX_USERID_UNKNOWN
        && cred.rolemask == FLUX_ROLE_NONE) {
        if (flux_msg_set_cred (cpy, ctx->cred) < 0)
            goto done;
    }
    if (ctx->router) {
        if (router_process (cpy, ctx->router) < 0)
            goto done;
    }
    rc = msglist_safe_append (ctx->send, cpy);
done:
    flux_msg_destroy (cpy);
    return rc;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    struct interthread_ctx *ctx = impl;
    flux_msg_t *msg;

    do {
        msg = msglist_safe_pop (ctx->recv);
        if (!msg) {
            if ((flags & FLUX_O_NONBLOCK)) {
                errno = EWOULDBLOCK;
                return NULL;
            }
            struct pollfd pfd = {
                .fd = msglist_safe_pollfd (ctx->recv),
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
    .recv = op_recv,
    .setopt = op_setopt,
    .impl_destroy = op_fini,
};

// vi:ts=4 sw=4 expandtab
