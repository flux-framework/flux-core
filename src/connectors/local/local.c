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
#include "src/common/libutil/shortjson.h"

#define CTX_MAGIC   0xf434aaab
typedef struct {
    int magic;
    int fd;
    int fd_nonblock;
    struct flux_msg_iobuf outbuf;
    struct flux_msg_iobuf inbuf;
    flux_t h;
} ctx_t;

static const struct flux_handle_ops handle_ops;

static int set_nonblock (ctx_t *c, int nonblock)
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
    ctx_t *c = impl;
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
    ctx_t *c = impl;
    return c->fd;
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (set_nonblock (c, (flags & FLUX_O_NONBLOCK)) < 0)
        return -1;
    if (flux_msg_sendfd (c->fd, msg, &c->outbuf) < 0)
        return -1;
    return 0;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (set_nonblock (c, (flags & FLUX_O_NONBLOCK)) < 0)
        return NULL;
    return flux_msg_recvfd (c->fd, &c->inbuf);
}

static int op_event_subscribe (void *impl, const char *topic)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    flux_rpc_t *rpc = NULL;
    JSON in = Jnew ();
    int rc = 0;

    Jadd_str (in, "topic", topic);
    if (!(rpc = flux_rpc (c->h, "local.sub", Jtostr (in), FLUX_NODEID_ANY, 0))
                || flux_rpc_get (rpc, NULL, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    Jput (in);
    return rc;
}

static int op_event_unsubscribe (void *impl, const char *topic)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    flux_rpc_t *rpc = NULL;
    JSON in = Jnew ();
    int rc = 0;

    Jadd_str (in, "topic", topic);
    if (!(rpc = flux_rpc (c->h, "local.unsub", Jtostr (in), FLUX_NODEID_ANY, 0))
                || flux_rpc_get (rpc, NULL, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    Jput (in);
    return rc;
}

static void op_fini (void *impl)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    flux_msg_iobuf_clean (&c->outbuf);
    flux_msg_iobuf_clean (&c->inbuf);
    if (c->fd >= 0)
        (void)close (c->fd);
    c->magic = ~CTX_MAGIC;
    free (c);
}

static bool pidcheck (const char *pidfile)
{
    pid_t pid;
    FILE *f = NULL;
    bool running = false;

    if (!(f = fopen (pidfile, "r")))
        goto done;
    if (fscanf (f, "%u", &pid) != 1 || kill (pid, 0) < 0)
        goto done;
    running = true;
done:
    if (f)
        (void)fclose (f);
    return running;
}

static int env_getint (char *name, int dflt)
{
    char *s = getenv (name);
    return s ? strtol (s, NULL, 10) : dflt;
}

/* Path is interpreted as the directory containing the unix domain socket
 * and broker pid.
 */
flux_t connector_init (const char *path, int flags)
{
    ctx_t *c = NULL;
    struct sockaddr_un addr;
    char pidfile[PATH_MAX + 1];
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
    n = snprintf (pidfile, sizeof (pidfile), "%s/broker.pid", path);
    if (n >= sizeof (pidfile)) {
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
        if (count >= env_getint("FLUX_RETRY_COUNT", 5) || !pidcheck (pidfile))
            goto error;
        memset (&addr, 0, sizeof (struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        strncpy (addr.sun_path, sockfile, sizeof (addr.sun_path) - 1);
        if (connect (c->fd, (struct sockaddr *)&addr,
                     sizeof (struct sockaddr_un)) == 0)
            break;
        usleep (100*1000);
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
