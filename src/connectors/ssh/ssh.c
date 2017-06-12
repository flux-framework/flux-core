/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
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
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/popen2.h"

static const char *default_ssh_cmd = "/usr/bin/rsh";

#define CTX_MAGIC   0xe534babb
typedef struct {
    int magic;
    int fd;
    int fd_nonblock;
    struct flux_msg_iobuf outbuf;
    struct flux_msg_iobuf inbuf;
    const char *ssh_cmd;
    char *ssh_argz;
    size_t ssh_argz_len;
    char **ssh_argv;
    int ssh_argc;
    struct popen2_child *p;
    flux_t *h;
} ssh_ctx_t;

static const struct flux_handle_ops handle_ops;

static int set_nonblock (ssh_ctx_t *c, int nonblock)
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
    ssh_ctx_t *c = impl;
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
    ssh_ctx_t *c = impl;
    return c->fd;
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    ssh_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (set_nonblock (c, (flags & FLUX_O_NONBLOCK)) < 0)
        return -1;
    if (flux_msg_sendfd (c->fd, msg, &c->outbuf) < 0)
        return -1;
    return 0;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    ssh_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (set_nonblock (c, (flags & FLUX_O_NONBLOCK)) < 0)
        return NULL;
    return flux_msg_recvfd (c->fd, &c->inbuf);
}

static int op_event_subscribe (void *impl, const char *topic)
{
    ssh_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    flux_future_t *f;
    int rc = 0;

    if (!(f = flux_rpcf (c->h, "local.sub", FLUX_NODEID_ANY, 0,
                           "{ s:s }", "topic", topic)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int op_event_unsubscribe (void *impl, const char *topic)
{
    ssh_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    flux_future_t *f;
    int rc = 0;

    if (!(f = flux_rpcf (c->h, "local.unsub", FLUX_NODEID_ANY, 0,
                           "{ s:s }", "topic", topic)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static void op_fini (void *impl)
{
    ssh_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    flux_msg_iobuf_clean (&c->outbuf);
    flux_msg_iobuf_clean (&c->inbuf);
    if (c->fd >= 0)
        (void)close (c->fd);
    if (c->ssh_argz)
        free (c->ssh_argz);
    if (c->ssh_argv)
        free (c->ssh_argv);
    if (c->p)
        pclose2 (c->p);
    c->magic = ~CTX_MAGIC;
    free (c);
}

static int parse_ssh_port (ssh_ctx_t *c, const char *path)
{
    char *p, *cpy = NULL;
    int rc = -1;

    if ((p = strchr (path, ':'))) {
        if (!(cpy = strdup (p + 1))) {
            errno = ENOMEM;
            goto done;
        }
        if ((p = strchr (cpy, '/')) || (p = strchr (cpy, '?')))
            *p = '\0';
        if (argz_add (&c->ssh_argz, &c->ssh_argz_len, "-p") != 0
         || argz_add (&c->ssh_argz, &c->ssh_argz_len, cpy) != 0) {
            errno = ENOMEM;
            goto done;
        }
    }
    rc = 0;
done:
    if (cpy)
        free (cpy);
    return rc;
}

static int parse_ssh_user_at_host (ssh_ctx_t *c, const char *path)
{
    char *p, *cpy = NULL;
    int rc = -1;

    if (!(cpy = strdup (path))) {
        errno = ENOMEM;
        goto done;
    }
    if ((p = strchr (cpy, ':')) || (p = strchr (cpy, '/'))
                                || (p = strchr (cpy, '?')))
        *p = '\0';
    if (argz_add (&c->ssh_argz, &c->ssh_argz_len, cpy) != 0) {
        errno = ENOMEM;
        goto done;
    }
    rc = 0;
done:
    if (cpy)
        free (cpy);
    return rc;
}

static int parse_extra_args (char **argz, size_t *argz_len, const char *s)
{
    char *cpy = NULL;
    char *tok, *a1, *saveptr = NULL;

    if (!(cpy = strdup (s)))
        goto nomem;
    a1 = cpy;
    while ((tok = strtok_r (a1, "?&", &saveptr))) {
        char *arg;
        error_t e;
        if (asprintf (&arg, "--%s", tok) < 0)
            goto nomem;
        e = argz_add (argz, argz_len, arg);
        free (arg);
        if (e != 0)
            goto nomem;
        a1 = NULL;
    }
    free (cpy);
    return 0;
nomem:
    if (cpy)
        free (cpy);
    errno = ENOMEM;
    return -1;
}

static char *which (const char *prog, char *buf, size_t size)
{
    char *path = getenv ("PATH");
    char *cpy = path ? strdup (path) : NULL;
    char *dir, *saveptr = NULL, *a1 = cpy;
    struct stat sb;
    char *result = NULL;

    if (cpy) {
        while ((dir = strtok_r (a1, ":", &saveptr))) {
            snprintf (buf, size, "%s/%s", dir, prog);
            if (stat (buf, &sb) == 0 && S_ISREG (sb.st_mode)
                                     && access (buf, X_OK) == 0) {
                result = buf;
                break;
            }
            a1 = NULL;
        }
    }
    if (cpy)
        free (cpy);
    return result;
}

static int parse_ssh_rcmd (ssh_ctx_t *c, const char *path)
{
    char *cpy = NULL, *local = NULL, *extra = NULL;
    char *proxy_argz = NULL;
    size_t proxy_argz_len = 0;
    const char *flux_cmd = getenv ("FLUX_SSH_RCMD");
    char pathbuf[PATH_MAX + 1];
    int rc = -1;

    if (flux_cmd && !*flux_cmd) {
        errno = EINVAL;
        goto done;
    }
    if (!flux_cmd)
        flux_cmd = which ("flux", pathbuf, sizeof (pathbuf));
    if (!flux_cmd)
        flux_cmd = "flux";
    if (argz_add (&proxy_argz, &proxy_argz_len, flux_cmd) != 0
     || argz_add (&proxy_argz, &proxy_argz_len, "proxy") != 0
     || argz_add (&proxy_argz, &proxy_argz_len, "--stdio") != 0) {
        errno = ENOMEM;
        goto done;
    }
    if (!(cpy = strdup (path))) {
        errno = ENOMEM;
        goto done;
    }
    if (!(local = strchr (cpy, '/'))) {
        errno = EINVAL;
        goto done;
    }
    if ((extra = strchr (local, '?')))
        *extra++ = '\0';
    if (extra) {
        if (parse_extra_args (&proxy_argz, &proxy_argz_len, extra) < 0)
            goto done;
    }
    if (argz_add (&proxy_argz, &proxy_argz_len, local) != 0) {
        errno = ENOMEM;
        goto done;
    }
    argz_stringify (proxy_argz, proxy_argz_len, ' ');
    if (argz_add (&c->ssh_argz, &c->ssh_argz_len, proxy_argz) != 0) {
        errno = ENOMEM;
        goto done;
    }
    rc = 0;
done:
    if (cpy)
        free (cpy);
    if (proxy_argz)
        free (proxy_argz);
    return rc;
}

static int test_broker_connection (ssh_ctx_t *c)
{
    flux_msg_t *in = NULL;
    flux_msg_t *out = NULL;
    struct flux_match match = FLUX_MATCH_RESPONSE;
    int rc = -1;

    if (!(in = flux_request_encode ("cmb.ping", "{}")))
        goto done;
    if (flux_send (c->h, in, 0) < 0)
        goto done;
    match.topic_glob = "cmb.ping";
    if (!(out = flux_recv (c->h, match, 0)))
        goto done;
    if (flux_response_decode (out, NULL, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (in);
    flux_msg_destroy (out);
    return rc;
}

/* Path is interpreted as
 *   [user@]hostname[:port][/unix-path][?key=val[&key=val]...]
 */
flux_t *connector_init (const char *path, int flags)
{
    ssh_ctx_t *c = NULL;

    if (!(c = malloc (sizeof (*c)))) {
        errno = ENOMEM;
        goto error;
    }
    memset (c, 0, sizeof (*c));
    c->magic = CTX_MAGIC;

    if (!(c->ssh_cmd = getenv ("FLUX_SSH")))
        c->ssh_cmd = default_ssh_cmd;
    if (argz_add (&c->ssh_argz, &c->ssh_argz_len, c->ssh_cmd) != 0)
        goto error;
    if (parse_ssh_port (c, path) < 0) /* [-p port] */
        goto error;
    if (parse_ssh_user_at_host (c, path) < 0) /* [user@]host */
        goto error;
    if (parse_ssh_rcmd (c, path) < 0) /* flux-proxy --stdio JOB [args...] */
        goto error;
    c->ssh_argc = argz_count (c->ssh_argz, c->ssh_argz_len) + 1;
    if (!(c->ssh_argv = malloc (sizeof (char *) * c->ssh_argc))) {
        errno = ENOMEM;
        goto error;
    }
    argz_extract (c->ssh_argz, c->ssh_argz_len, c->ssh_argv);
    if (!(c->p = popen2 (c->ssh_cmd, c->ssh_argv)))
        goto error;
    c->fd = popen2_get_fd (c->p);
    c->fd_nonblock = -1;
    flux_msg_iobuf_init (&c->outbuf);
    flux_msg_iobuf_init (&c->inbuf);
    if (!(c->h = flux_handle_create (c, &handle_ops, flags)))
        goto error;
    if (test_broker_connection (c) < 0)
        goto error;
    return c->h;
error:
    if (c) {
        int saved_errno = errno;
        if (c->h)
            flux_handle_destroy (c->h); /* calls op_fini */
        else
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
