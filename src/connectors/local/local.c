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
#include <stdbool.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

#define CTX_MAGIC   0xf434aaab
typedef struct {
    int magic;
    int fd;
    int fd_nonblock;
    struct flux_msg_iobuf outbuf;
    struct flux_msg_iobuf inbuf;
    flux_t *h;
} local_ctx_t;

static const struct flux_handle_ops handle_ops;

static int set_nonblock (local_ctx_t *c, int nonblock)
{
    int flags;
    if (c->fd_nonblock != nonblock) {
        if ((flags = fcntl (c->fd, F_GETFL)) < 0)
            return -1;
        if (fcntl (c->fd, F_SETFL, nonblock ? flags | O_NONBLOCK
                                            : flags & ~O_NONBLOCK) < 0)
            return -1;
        c->fd_nonblock = nonblock;
    }
    return 0;
}

static int op_pollevents (void *impl)
{
    local_ctx_t *c = impl;
    struct pollfd pfd = {
        .fd = c->fd,
        .events = POLLIN | POLLOUT | POLLERR | POLLHUP,
        .revents = 0,
    };
    int revents = 0;
    switch (poll (&pfd, 1, 0)) {
        case 1:
            if (pfd.revents & POLLIN)
                revents |= FLUX_POLLIN;
            if (pfd.revents & POLLOUT)
                revents |= FLUX_POLLOUT;
            if ((pfd.revents & POLLERR) || (pfd.revents & POLLHUP))
                revents |= FLUX_POLLERR;
            break;
        case 0:
            break;
        default: /* -1 */
            revents |= FLUX_POLLERR;
            break;
    }
    return revents;
}

static int op_pollfd (void *impl)
{
    local_ctx_t *c = impl;
    return c->fd;
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    local_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (set_nonblock (c, (flags & FLUX_O_NONBLOCK)) < 0)
        return -1;
    if (flux_msg_sendfd (c->fd, msg, &c->outbuf) < 0)
        return -1;
    return 0;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    local_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (set_nonblock (c, (flags & FLUX_O_NONBLOCK)) < 0)
        return NULL;
    return flux_msg_recvfd (c->fd, &c->inbuf);
}

static int op_event (void *impl, const char *topic, const char *msg_topic)
{
    local_ctx_t *c = impl;
    flux_rpc_t *rpc;
    int rc = -1;

    assert (c->magic == CTX_MAGIC);

    if (!(rpc = flux_rpcf (c->h, msg_topic, FLUX_NODEID_ANY, 0,
                           "{s:s}", "topic", topic)))
        goto done;
    if (flux_rpc_get (rpc, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}

static int op_event_subscribe (void *impl, const char *topic)
{
    return op_event (impl, topic, "local.sub");
}

static int op_event_unsubscribe (void *impl, const char *topic)
{
    return op_event (impl, topic, "local.unsub");
}

static void op_fini (void *impl)
{
    local_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    flux_msg_iobuf_clean (&c->outbuf);
    flux_msg_iobuf_clean (&c->inbuf);
    if (c->fd >= 0)
        (void)close (c->fd);
    c->magic = ~CTX_MAGIC;
    free (c);
}

static int env_getint (char *name, int dflt)
{
    char *s = getenv (name);
    return s ? strtol (s, NULL, 10) : dflt;
}

/* Path is interpreted as the directory containing the unix domain socket.
 */
flux_t *connector_init (const char *path, int flags)
{
    local_ctx_t *c = NULL;
    struct sockaddr_un addr;
    char sockfile[PATH_MAX + 1];
    int n, count;

    if (!path) {
        errno = EINVAL;
        goto error;
    }
    n = snprintf (sockfile, sizeof (sockfile), "%s/local", path);
    if (n >= sizeof (sockfile)) {
        errno = EINVAL;
        goto error;
    }
    if (!(c = malloc (sizeof (*c)))) {
        errno = ENOMEM;
        goto error;
    }
    memset (c, 0, sizeof (*c));
    c->magic = CTX_MAGIC;

    c->fd = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (c->fd < 0)
        goto error;
    c->fd_nonblock = -1;
    for (count=0;;count++) {
        if (count >= env_getint("FLUX_RETRY_COUNT", 5))
            goto error;
        memset (&addr, 0, sizeof (struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        strncpy (addr.sun_path, sockfile, sizeof (addr.sun_path) - 1);
        if (connect (c->fd, (struct sockaddr *)&addr,
                     sizeof (struct sockaddr_un)) == 0)
            break;
        usleep (100*1000);
    }
    /* read 1 byte indicating success or failure of auth */
    unsigned char e;
    int rc;
    rc = read (c->fd, &e, 1);
    if (rc < 0)
        goto error;
    if (rc == 0) {
        errno = ECONNRESET;
        goto error;
    }
    if (e != 0) {
        errno = e;
        goto error;
    }
    flux_msg_iobuf_init (&c->outbuf);
    flux_msg_iobuf_init (&c->inbuf);
    if (!(c->h = flux_handle_create (c, &handle_ops, flags)))
        goto error;
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
    .event_subscribe = op_event_subscribe,
    .event_unsubscribe = op_event_unsubscribe,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
