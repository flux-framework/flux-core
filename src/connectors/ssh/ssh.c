/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

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
#include <argz.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/popen2.h"
#include "src/common/librouter/usock.h"

struct ssh_connector {
    struct usock_client *uclient;
    const char *ssh_cmd;
    char *ssh_argz;
    size_t ssh_argz_len;
    char **ssh_argv;
    int ssh_argc;
    struct popen2_child *p;
    flux_t *h;
};

static const struct flux_handle_ops handle_ops;

static int op_pollevents (void *impl)
{
    struct ssh_connector *ctx = impl;

    return usock_client_pollevents (ctx->uclient);
}

static int op_pollfd (void *impl)
{
    struct ssh_connector *ctx = impl;

    return usock_client_pollfd (ctx->uclient);
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct ssh_connector *ctx = impl;

    return usock_client_send (ctx->uclient, msg, flags);
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    struct ssh_connector *ctx = impl;

    return usock_client_recv (ctx->uclient, flags);
}

static int op_event_subscribe (void *impl, const char *topic)
{
    struct ssh_connector *ctx = impl;
    flux_future_t *f;
    int rc = 0;

    if (!(f = flux_rpc_pack (ctx->h, "local.sub", FLUX_NODEID_ANY, 0,
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
    struct ssh_connector *ctx = impl;
    flux_future_t *f;
    int rc = 0;

    if (!(f = flux_rpc_pack (ctx->h, "local.unsub", FLUX_NODEID_ANY, 0,
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
    struct ssh_connector *ctx = impl;

    if (ctx) {
        int saved_errno = errno;
        usock_client_destroy (ctx->uclient);
        free (ctx->ssh_argz);
        free (ctx->ssh_argv);
        pclose2 (ctx->p);
        free (ctx);
        errno = saved_errno;
    }
}

static int parse_ssh_port (struct ssh_connector *c, const char *path)
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

static int parse_ssh_user_at_host (struct ssh_connector *c, const char *path)
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

static int parse_ssh_rcmd (struct ssh_connector *c, const char *path)
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
     || argz_add (&proxy_argz, &proxy_argz_len, "relay") != 0) {
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

/* Path is interpreted as
 *   [user@]hostname[:port][/unix-path][?key=val[&key=val]...]
 */
flux_t *connector_init (const char *path, int flags)
{
    struct ssh_connector *ctx = NULL;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->ssh_cmd = getenv ("FLUX_SSH")))
        ctx->ssh_cmd = PATH_SSH;
    if (argz_add (&ctx->ssh_argz, &ctx->ssh_argz_len, ctx->ssh_cmd) != 0)
        goto error;
    if (parse_ssh_port (ctx, path) < 0) /* [-p port] */
        goto error;
    if (parse_ssh_user_at_host (ctx, path) < 0) /* [user@]host */
        goto error;
    if (parse_ssh_rcmd (ctx, path) < 0) /* flux-relay path [args...] */
        goto error;
    ctx->ssh_argc = argz_count (ctx->ssh_argz, ctx->ssh_argz_len) + 1;
    if (!(ctx->ssh_argv = calloc (sizeof (char *), ctx->ssh_argc)))
        goto error;
    argz_extract (ctx->ssh_argz, ctx->ssh_argz_len, ctx->ssh_argv);
    if (!(ctx->p = popen2 (ctx->ssh_cmd, ctx->ssh_argv))) {
        /* If popen fails because ssh cannot be found, flux_open()
         * will just fail with errno = ENOENT, which is not all that helpful.
         * Emit a hint on stderr even though this is perhaps not ideal.
         */
        fprintf (stderr, "ssh-connector: %s: %s\n",
                 ctx->ssh_cmd, strerror (errno));
        fprintf (stderr, "Hint: set FLUX_SSH in environment to override\n");
        goto error;
    }
    if (!(ctx->uclient = usock_client_create (popen2_get_fd (ctx->p))))
        goto error;
    if (!(ctx->h = flux_handle_create (ctx, &handle_ops, flags)))
        goto error;
    return ctx->h;
error:
    if (ctx) {
        if (ctx->h)
            flux_handle_destroy (ctx->h); /* calls op_fini */
        else
            op_fini (ctx);
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
