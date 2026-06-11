/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zmq.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "src/common/libzmqutil/cert.h"
#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libutil/stdlog.h"

#include "parent.h"
#include "children.h"
#include "topology.h"
#include "ovconf.h"

static void diag_logger (const char *buf, int len, void *arg)
{
    struct stdlog_header hdr;
    const char *msg;
    size_t msglen;
    int severity;
    char *s;

    if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0)
        BAIL_OUT ("stdlog_decode failed");
    severity = STDLOG_SEVERITY (hdr.pri);
    if (asprintf (&s,
                  "%s: %.*s",
                  stdlog_severity_to_string (severity),
                  (int)msglen,
                  msg) < 0)
        BAIL_OUT ("asprintf failed");
    diag ("%s", s);
    free (s);
}

struct test_ctx {
    flux_msg_t *parent_msg;
    flux_msg_t *children_msg;
    bool parent_ready;
    bool children_ready;
    struct parent *parent;
    struct children *children;
};

static void parent_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg)
{
    struct test_ctx *ctx = arg;

    ctx->parent_msg = parent_recvmsg (ctx->parent);
    ctx->parent_ready = true;
    diag ("parent_cb: received message");
}

static void children_cb (flux_reactor_t *r,
                         flux_watcher_t *w,
                         int revents,
                         void *arg)
{
    struct test_ctx *ctx = arg;

    ctx->children_msg = children_recvmsg (ctx->children);
    ctx->children_ready = true;
    diag ("children_cb: received message");
}

/* Test basic parent-child message passing with CURVE authentication.
 * This is a unit-level integration test of the parent and children modules.
 */
void test_parent_child_messaging (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *children_ctx;
    struct parent *parent_ctx;
    struct cert *server_cert;
    struct cert *client_cert;
    void *zctx;
    char *bind_uri = NULL;
    const char *server_pubkey;
    const char *client_pubkey;
    char client_uuid[37];
    flux_error_t error;
    flux_msg_t *msg;
    flux_reactor_t *reactor;
    struct test_ctx test_ctx = {0};
    struct ovconf config = {
        .torpid_min = 5.0,
        .torpid_max = 30.0,
        .tcp_user_timeout = 0,
        .connect_timeout = 30.0,
        .zmqdebug = 1,  // Enable ZMQ debug
        .zmq_io_threads = 1,
        .enable_ipv6 = 0,
        .child_rcvhwm = 0,
        .handlers = NULL,
    };

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    /* Redirect flux logs to diag for test output */
    flux_log_set_redirect (h, diag_logger, NULL);

    if (!(reactor = flux_get_reactor (h)))
        BAIL_OUT ("flux_get_reactor failed");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);
    if (!(zctx = zmq_ctx_new ()))
        BAIL_OUT ("zmq_ctx_new failed");
    if (!(server_cert = cert_create ()))
        BAIL_OUT ("server cert_create failed");
    if (!(client_cert = cert_create ()))
        BAIL_OUT ("client cert_create failed");

    /* Set up children (server) side */
    children_ctx = children_create (h, topo);
    ok (children_ctx != NULL,
        "children_create succeeds");
    if (!children_ctx)
        BAIL_OUT ("cannot continue without children_ctx");

    ok (children_bind (children_ctx,
                       zctx,
                       server_cert,
                       "ipc://*",
                       NULL,
                       &config,
                       &bind_uri,
                       NULL,
                       &error) == 0,
        "children_bind succeeds");
    if (!bind_uri)
        BAIL_OUT ("cannot continue without bind_uri");

    diag ("children bound to %s", bind_uri);

    test_ctx.children = children_ctx;

    /* Set up watcher for children socket */
    ok (children_watch (children_ctx, children_cb, &test_ctx) == 0,
        "children_watch succeeds");
    if (!children_ctx->bind_w)
        BAIL_OUT ("cannot continue without bind_w");
    flux_watcher_start (children_ctx->bind_w);

    /* Authorize the client */
    if (!(client_pubkey = cert_public_txt (client_cert)))
        BAIL_OUT ("cert_public_txt failed");
    ok (children_authorize (children_ctx, "test-child", client_pubkey) == 0,
        "children_authorize succeeds");

    /* Set up parent (client) side */
    parent_ctx = parent_create (h, 1);
    ok (parent_ctx != NULL,
        "parent_create succeeds");
    if (!parent_ctx)
        BAIL_OUT ("cannot continue without parent_ctx");

    if (!(server_pubkey = cert_public_txt (server_cert)))
        BAIL_OUT ("cert_public_txt failed");
    ok (parent_set_uri (parent_ctx, bind_uri) == 0,
        "parent_set_uri succeeds");
    ok (parent_set_pubkey (parent_ctx, server_pubkey) == 0,
        "parent_set_pubkey succeeds");

    snprintf (client_uuid, sizeof (client_uuid), "test-child-uuid");
    ok (parent_connect (parent_ctx, zctx, client_cert, client_uuid, &config) == 0,
        "parent_connect succeeds");

    diag ("parent connected to %s", bind_uri);

    test_ctx.parent = parent_ctx;

    /* Set up watcher for parent socket */
    ok (parent_watch (parent_ctx, parent_cb, &test_ctx) == 0,
        "parent_watch succeeds");
    if (!parent_ctx->w)
        BAIL_OUT ("cannot continue without parent watcher");
    flux_watcher_start (parent_ctx->w);

    /* Test 1: Send from parent (child) to children (parent) */
    diag ("attempting to send message from parent to children");
    if (!(msg = flux_request_encode ("test.hello", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    ok (parent_sendmsg (parent_ctx, msg) == 0,
        "parent_sendmsg succeeds");
    flux_msg_decref (msg);

    /* Wait for children watcher to receive message */
    diag ("running reactor to receive message on children side");
    while (!test_ctx.children_ready && flux_reactor_run (reactor, FLUX_REACTOR_ONCE) >= 0)
        ;

    ok (test_ctx.children_ready && test_ctx.children_msg != NULL,
        "children_recvmsg received message from parent");

    /* Test 3: Verify received message has correct topic */
    const char *topic = NULL;
    ok (test_ctx.children_msg != NULL
        && flux_msg_get_topic (test_ctx.children_msg, &topic) == 0
        && streq (topic, "test.hello"),
        "received message has correct topic");

    /* Test 4: Verify received message has correct routing ID */
    const char *id = test_ctx.children_msg ? flux_msg_route_last (test_ctx.children_msg) : NULL;
    ok (id != NULL && streq (id, client_uuid),
        "received message has correct routing ID");

    /* Test 5: Send reply from children (parent) back to parent (child) */
    flux_msg_t *reply = NULL;
    if (test_ctx.children_msg)
        reply = flux_response_derive (test_ctx.children_msg, 0);
    ok (reply != NULL && children_sendmsg (children_ctx, reply) == 0,
        "children_sendmsg reply succeeds");
    flux_msg_decref (reply);

    /* Wait for parent watcher to receive reply */
    if (reply) {
        diag ("running reactor to receive reply on parent side");
        while (!test_ctx.parent_ready && flux_reactor_run (reactor, FLUX_REACTOR_ONCE) >= 0)
            ;
    }

    /* Test 6: Verify parent received reply */
    ok (test_ctx.parent_ready && test_ctx.parent_msg != NULL,
        "parent_recvmsg received reply from children");

    /* Test 7: Verify reply has correct topic */
    topic = NULL;
    ok (test_ctx.parent_msg != NULL
        && flux_msg_get_topic (test_ctx.parent_msg, &topic) == 0
        && streq (topic, "test.hello"),
        "reply has correct topic");

    flux_msg_decref (test_ctx.parent_msg);
    flux_msg_decref (test_ctx.children_msg);
    parent_destroy (parent_ctx);
    children_destroy (children_ctx);
    free (bind_uri);
    cert_destroy (server_cert);
    cert_destroy (client_cert);
    topology_decref (topo);
    zmq_ctx_term (zctx);
    flux_close (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_parent_child_messaging ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
