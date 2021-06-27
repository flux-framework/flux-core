/************************************************************  \
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <limits.h>
#include <pthread.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/librouter/usock_service.h"

struct server_context {
    char sockpath[1024];
    pthread_t t;
    flux_t *h;
    flux_reactor_t *r;
    flux_msg_handler_t **handlers;
};

void tmpdir_destroy (const char *path)
{
    diag ("rm -r %s", path);
    if (unlink_recursive (path) < 0)
        BAIL_OUT ("unlink_recursive failed");
}

void tmpdir_create (char *buf, int size)
{
    const char *tmpdir = getenv ("TMPDIR");

    if (snprintf (buf,
                  size,
                  "%s/usock.XXXXXXX",
                  tmpdir ? tmpdir : "/tmp") >= size)
        BAIL_OUT ("tmpdir_create buffer overflow");
    if (!mkdtemp (buf))
        BAIL_OUT ("mkdtemp %s: %s", buf, strerror (errno));
    diag ("mkdir %s", buf);
}

void hello_cb (flux_t *h,
               flux_msg_handler_t *mh,
               const flux_msg_t *msg,
               void *arg)
{
    diag ("hello");
    if (flux_respond (h, msg, NULL) < 0)
        BAIL_OUT ("flux_respond failed");
}

void disconnect_cb (flux_t *h,
                    flux_msg_handler_t *mh,
                    const flux_msg_t *msg,
                    void *arg)
{
    const char *uuid;
    if ((uuid = flux_msg_get_route_first (msg)))
        diag ("disconnect from %.5s", uuid);
    flux_reactor_stop (flux_get_reactor (h));
}

void *server_thread (void *arg)
{
    struct server_context *ctx = arg;

    flux_reactor_run (ctx->r, 0);
    return NULL;
}

const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "hello", hello_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "disconnect", disconnect_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void server_create (struct server_context *ctx, const char *tmpdir)
{
    if (!(ctx->r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");
    snprintf (ctx->sockpath, sizeof (ctx->sockpath), "%s/sock", tmpdir);
    ctx->h = usock_service_create (ctx->r, ctx->sockpath, true);
    ok (ctx->h != NULL,
        "usock_service_create listening on %s", ctx->sockpath);
    ok (flux_msg_handler_addvec (ctx->h, htab, &ctx, &ctx->handlers) == 0,
        "successfully registered msg handlers");
    ok (pthread_create (&ctx->t, NULL, server_thread, ctx) == 0,
        "started server thread");
}

void server_destroy (struct server_context *ctx)
{
    ok (pthread_join (ctx->t, NULL) == 0,
        "joined with server thread");
    flux_msg_handler_delvec (ctx->handlers);
    flux_close (ctx->h);
    flux_reactor_destroy (ctx->r);
}

void simple_check (const char *tmpdir)
{
    char uri[2048];
    struct server_context ctx;
    flux_t *h;
    flux_future_t *f;

    memset (&ctx, 0, sizeof (ctx));
    server_create (&ctx, tmpdir);

    snprintf (uri, sizeof (uri), "local://%s", ctx.sockpath);
    h = flux_open (uri, 0);
    ok (h != NULL,
        "client connected to server");

    if (!(f = flux_rpc (h, "hello", NULL, 0, 0)))
        BAIL_OUT ("error sending hello RPC");
    ok (flux_rpc_get (f, NULL) == 0,
        "got response to HELLO rpc");
    flux_future_destroy (f);

    flux_close (h); // triggers disconnect

    server_destroy (&ctx);
}

int main (int argc, char *argv[])
{
    char tmpdir[PATH_MAX + 1];

    plan (NO_PLAN);
    tmpdir_create (tmpdir, sizeof (tmpdir));

    simple_check (tmpdir);

    tmpdir_destroy (tmpdir);
    done_testing ();
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
