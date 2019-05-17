/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* loop connector - mainly for testing */

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/msglist.h"

#define CTX_MAGIC 0xf434aaa0
typedef struct {
    int magic;
    flux_t *h;

    int pollfd;
    int pollevents;

    uint32_t userid;
    uint32_t rolemask;

    msglist_t *queue;
} loop_ctx_t;

static const struct flux_handle_ops handle_ops;

const char *fake_uuid = "12345678123456781234567812345678";

static int op_pollevents (void *impl)
{
    loop_ctx_t *c = impl;
    int e, revents = 0;

    if ((e = msglist_pollevents (c->queue)) < 0)
        return e;
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
    loop_ctx_t *c = impl;
    return msglist_pollfd (c->queue);
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    loop_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    flux_msg_t *cpy = NULL;
    uint32_t userid, rolemask;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (flux_msg_get_userid (cpy, &userid) < 0)
        goto done;
    if (flux_msg_get_rolemask (cpy, &rolemask) < 0)
        goto done;
    if (userid == FLUX_USERID_UNKNOWN)
        userid = c->userid;
    if (rolemask == FLUX_ROLE_NONE)
        rolemask = c->rolemask;
    if (flux_msg_set_userid (cpy, userid) < 0)
        goto done;
    if (flux_msg_set_rolemask (cpy, rolemask) < 0)
        goto done;
    if (msglist_append (c->queue, cpy) < 0)
        goto done;
    cpy = NULL; /* c->queue now owns cpy */
    rc = 0;
done:
    if (cpy)
        flux_msg_destroy (cpy);
    return rc;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    loop_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    flux_msg_t *msg = msglist_pop (c->queue);
    if (!msg)
        errno = EWOULDBLOCK;
    return msg;
}

static void op_fini (void *impl)
{
    loop_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (c->pollfd >= 0)
        close (c->pollfd);
    msglist_destroy (c->queue);
    c->magic = ~CTX_MAGIC;
    free (c);
}

flux_t *connector_init (const char *path, int flags)
{
    loop_ctx_t *c = malloc (sizeof (*c));
    if (!c) {
        errno = ENOMEM;
        goto error;
    }
    memset (c, 0, sizeof (*c));
    c->magic = CTX_MAGIC;
    if (!(c->queue = msglist_create ((msglist_free_f)flux_msg_destroy)))
        goto error;
    if (!(c->h = flux_handle_create (c, &handle_ops, flags)))
        goto error;
    /* Fake out size, rank, tbon-arity attributes for testing.
     */
    if (flux_attr_set_cacheonly (c->h, "rank", "0") < 0
        || flux_attr_set_cacheonly (c->h, "size", "1") < 0
        || flux_attr_set_cacheonly (c->h, "tbon-arity", "2") < 0)
        goto error;
    c->userid = geteuid ();
    c->rolemask = FLUX_ROLE_OWNER;
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
    .getopt = NULL,
    .setopt = NULL,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
