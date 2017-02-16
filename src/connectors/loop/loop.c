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
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/msglist.h"

#define CTX_MAGIC   0xf434aaa0
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
    int type;
    flux_msg_t *cpy = NULL;
    uint32_t userid, rolemask;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (flux_msg_get_type (cpy, &type) < 0)
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

static int op_getopt (void *impl, const char *option, void *val, size_t size)
{
    loop_ctx_t *ctx = impl;
    assert (ctx->magic == CTX_MAGIC);
    int rc = -1;

    if (option && !strcmp (option, FLUX_OPT_TESTING_USERID)) {
        if (size != sizeof (ctx->userid)) {
            errno = EINVAL;
            goto done;
        }
        memcpy (val, &ctx->userid, size);
    } else if (option && !strcmp (option, FLUX_OPT_TESTING_ROLEMASK)) {
        if (size != sizeof (ctx->rolemask)) {
            errno = EINVAL;
            goto done;
        }
        memcpy (val, &ctx->rolemask, size);
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
    loop_ctx_t *ctx = impl;
    assert (ctx->magic == CTX_MAGIC);
    size_t val_size;
    int rc = -1;

    if (option && !strcmp (option, FLUX_OPT_TESTING_USERID)) {
        val_size = sizeof (ctx->userid);
        if (size != val_size) {
            errno = EINVAL;
            goto done;
        }
        memcpy (&ctx->userid, val, val_size);
    } else if (option && !strcmp (option, FLUX_OPT_TESTING_ROLEMASK)) {
        val_size = sizeof (ctx->rolemask);
        if (size != val_size) {
            errno = EINVAL;
            goto done;
        }
        memcpy (&ctx->rolemask, val, val_size);
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
    if (flux_attr_fake (c->h, "rank", "0", FLUX_ATTRFLAG_IMMUTABLE) < 0
                || flux_attr_fake (c->h, "size", "1",
                                   FLUX_ATTRFLAG_IMMUTABLE) < 0
                || flux_attr_fake (c->h, "tbon-arity", "2",
                                   FLUX_ATTRFLAG_IMMUTABLE) < 0)
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
    .getopt = op_getopt,
    .setopt = op_setopt,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
