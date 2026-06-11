/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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

#include <flux/core.h>
#include <stdio.h>
#include <string.h>
#include <zmq.h>

#include "src/common/libtap/tap.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/libzmqutil/cert.h"

#include "children.h"
#include "topology.h"
#include "ovconf.h"

void test_create_destroy (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    flux_error_t error;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);

    ctx = children_create (h, topo);
    ok (ctx != NULL,
        "children_create works");
    if (!ctx)
        BAIL_OUT ("children_create failed, cannot continue test");
    ok (ctx->count == 2,
        "children_create for kary:2 size=8 rank=0 has 2 children");
    ok (ctx->topo == topo,
        "children context has topology reference");
    ok (ctx->h == h,
        "children context has flux handle reference");

    children_destroy (ctx);
    pass ("children_destroy doesn't crash");

    topology_decref (topo);
    flux_close (h);
}

void test_invalid (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    struct child *child;
    flux_error_t error;
    bool changed;

    lives_ok ({children_destroy (NULL);},
              "children_destroy ctx=NULL doesn't crash");

    errno = 0;
    ctx = children_create (NULL, NULL);
    ok (ctx == NULL && errno == EINVAL,
        "children_create h=NULL topo=NULL fails with EINVAL");

    ok (children_lookup (NULL, "fakeuuid") == NULL,
        "children_lookup ctx=NULL returns NULL");
    ok (children_lookup_online (NULL, "fakeuuid") == NULL,
        "children_lookup_online ctx=NULL returns NULL");
    ok (children_lookup_byrank (NULL, 1) == NULL,
        "children_lookup_byrank ctx=NULL returns NULL");
    ok (children_lookup_route (NULL, 5) == NULL,
        "children_lookup_route ctx=NULL returns NULL");

    ok (children_get_online_count (NULL) == 0,
        "children_get_online_count ctx=NULL returns 0");

    ok (child_is_online (NULL) == false,
        "child_is_online child=NULL returns false");

    /* Test that children_foreach with NULL ctx doesn't iterate */
    {
        struct children *null_ctx = NULL;
        int count = 0;
        children_foreach (null_ctx, child) {
            count++;
        }
        ok (count == 0,
            "children_foreach ctx=NULL doesn't iterate");
    }

    changed = children_set_status (NULL, NULL, SUBTREE_STATUS_FULL, NULL);
    ok (changed == false,
        "children_set_status ctx=NULL returns false");

    errno = 0;
    ok (children_bind (NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &error) < 0
        && errno == EINVAL,
        "children_bind ctx=NULL fails with EINVAL");

    errno = 0;
    ok (children_watch (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "children_watch ctx=NULL fails with EINVAL");

    errno = 0;
    ok (children_sendmsg (NULL, NULL) < 0 && errno == EINVAL,
        "children_sendmsg ctx=NULL fails with EINVAL");

    errno = 0;
    ok (children_recvmsg (NULL) == NULL && errno == EINVAL,
        "children_recvmsg ctx=NULL fails with EINVAL");

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);
    if (!(ctx = children_create (h, topo)))
        BAIL_OUT ("children_create failed, cannot continue test");

    errno = 0;
    ok (children_sendmsg (ctx, NULL) < 0 && errno == EHOSTUNREACH,
        "children_sendmsg with unbound socket fails with EHOSTUNREACH");

    ok (children_lookup (ctx, NULL) == NULL,
        "children_lookup id=NULL returns NULL");
    ok (children_lookup_online (ctx, NULL) == NULL,
        "children_lookup_online id=NULL returns NULL");

    child = children_lookup_byrank (ctx, 1);
    changed = children_set_status (ctx, NULL, SUBTREE_STATUS_FULL, NULL);
    ok (changed == false,
        "children_set_status child=NULL returns false");

    changed = children_set_status (NULL, child, SUBTREE_STATUS_FULL, NULL);
    ok (changed == false,
        "children_set_status ctx=NULL child=valid returns false");

    children_destroy (ctx);

    if (topology_set_rank (topo, 7) < 0) // no children
        BAIL_OUT ("could not set topology rank to 7");
    errno = 0;
    ctx = children_create (h, topo);
    ok (ctx == NULL && errno == EINVAL,
        "children_create on leaf node fails with EINVAL");

    topology_decref (topo);
    flux_close (h);
}

void test_lookup (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    struct child *child;
    flux_error_t error;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);

    ctx = children_create (h, topo);
    ok (ctx != NULL,
        "children_create works");
    if (!ctx)
        BAIL_OUT ("children_create failed, cannot continue test");

    ok (ctx->count == 2,
        "children array has 2 entries");
    ok (ctx->children[0].rank == 1,
        "first child has rank 1");
    ok (ctx->children[1].rank == 2,
        "second child has rank 2");

    child = children_lookup_byrank (ctx, 1);
    ok (child != NULL && child->rank == 1,
        "children_lookup_byrank rank=1 works");

    child = children_lookup_byrank (ctx, 2);
    ok (child != NULL && child->rank == 2,
        "children_lookup_byrank rank=2 works");

    child = children_lookup_byrank (ctx, 0);
    ok (child == NULL,
        "children_lookup_byrank rank=0 returns NULL");

    child = children_lookup_byrank (ctx, 99);
    ok (child == NULL,
        "children_lookup_byrank rank=99 returns NULL");

    children_destroy (ctx);
    topology_decref (topo);
    flux_close (h);
}

void test_lookup_route (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    struct child *child;
    flux_error_t error;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);

    if (!(ctx = children_create (h, topo)))
        BAIL_OUT ("children_create failed, cannot continue test");

    child = children_lookup_route (ctx, 3);
    ok (child != NULL && child->rank == 1,
        "children_lookup_route rank=3 routes via rank 1");

    child = children_lookup_route (ctx, 5);
    ok (child != NULL && child->rank == 2,
        "children_lookup_route rank=5 routes via rank 2");

    child = children_lookup_route (ctx, 0);
    ok (child == NULL,
        "children_lookup_route rank=0 (self) returns NULL");

    child = children_lookup_route (ctx, 99);
    ok (child == NULL,
        "children_lookup_route rank=99 (out of range) returns NULL");

    children_destroy (ctx);
    topology_decref (topo);
    flux_close (h);
}

void test_foreach (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    struct child *child;
    flux_error_t error;
    int count = 0;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);

    if (!(ctx = children_create (h, topo)))
        BAIL_OUT ("children_create failed, cannot continue test");

    children_foreach (ctx, child) {
        count++;
    }
    ok (count == 2,
        "children_foreach iterates over 2 children");

    children_destroy (ctx);
    topology_decref (topo);
    flux_close (h);
}

void test_status (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    struct child *child;
    flux_error_t error;
    bool changed;
    bool went_offline;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);

    if (!(ctx = children_create (h, topo)))
        BAIL_OUT ("children_create failed, cannot continue test");

    child = children_lookup_byrank (ctx, 1);
    ok (child != NULL,
        "got child rank 1");

    ok (child->status == SUBTREE_STATUS_OFFLINE,
        "initial status is OFFLINE");
    ok (!child_is_online (child),
        "child_is_online returns false for OFFLINE");

    changed = children_set_status (ctx, child, SUBTREE_STATUS_FULL, NULL);
    ok (changed == true,
        "children_set_status OFFLINE->FULL returns true");
    ok (child->status == SUBTREE_STATUS_FULL,
        "status is now FULL");
    ok (child_is_online (child),
        "child_is_online returns true for FULL");

    changed = children_set_status (ctx, child, SUBTREE_STATUS_FULL, NULL);
    ok (changed == false,
        "children_set_status FULL->FULL returns false");

    went_offline = false;
    changed = children_set_status (ctx,
                                   child,
                                   SUBTREE_STATUS_OFFLINE,
                                   &went_offline);
    ok (changed == true && went_offline == true,
        "children_set_status FULL->OFFLINE returns true, went_offline=true");
    ok (!child_is_online (child),
        "child_is_online returns false for OFFLINE");

    went_offline = false;
    changed = children_set_status (ctx,
                                   child,
                                   SUBTREE_STATUS_LOST,
                                   &went_offline);
    ok (changed == true && went_offline == false,
        "children_set_status OFFLINE->LOST returns true, went_offline=false");

    children_destroy (ctx);
    topology_decref (topo);
    flux_close (h);
}

void test_status_transitions (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    struct child *child;
    flux_error_t error;
    bool changed;
    bool went_offline;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);

    if (!(ctx = children_create (h, topo)))
        BAIL_OUT ("children_create failed, cannot continue test");

    child = children_lookup_byrank (ctx, 1);
    if (!child)
        BAIL_OUT ("children_lookup_byrank failed");

    /* Test all transitions from OFFLINE */
    ok (child->status == SUBTREE_STATUS_OFFLINE,
        "initial status is OFFLINE");

    changed = children_set_status (ctx, child, SUBTREE_STATUS_FULL, NULL);
    ok (changed && child->status == SUBTREE_STATUS_FULL,
        "OFFLINE -> FULL works");

    /* Test transitions from FULL */
    changed = children_set_status (ctx, child, SUBTREE_STATUS_PARTIAL, NULL);
    ok (changed && child->status == SUBTREE_STATUS_PARTIAL,
        "FULL -> PARTIAL works");

    children_set_status (ctx, child, SUBTREE_STATUS_FULL, NULL);
    changed = children_set_status (ctx, child, SUBTREE_STATUS_DEGRADED, NULL);
    ok (changed && child->status == SUBTREE_STATUS_DEGRADED,
        "FULL -> DEGRADED works");

    children_set_status (ctx, child, SUBTREE_STATUS_FULL, NULL);
    went_offline = false;
    changed = children_set_status (ctx, child, SUBTREE_STATUS_OFFLINE, &went_offline);
    ok (changed && went_offline && child->status == SUBTREE_STATUS_OFFLINE,
        "FULL -> OFFLINE works and sets went_offline=true");

    children_set_status (ctx, child, SUBTREE_STATUS_FULL, NULL);
    went_offline = false;
    changed = children_set_status (ctx, child, SUBTREE_STATUS_LOST, &went_offline);
    ok (changed && went_offline && child->status == SUBTREE_STATUS_LOST,
        "FULL -> LOST works and sets went_offline=true");

    /* Test transitions from PARTIAL */
    children_set_status (ctx, child, SUBTREE_STATUS_PARTIAL, NULL);
    changed = children_set_status (ctx, child, SUBTREE_STATUS_FULL, NULL);
    ok (changed && child->status == SUBTREE_STATUS_FULL,
        "PARTIAL -> FULL works");

    children_set_status (ctx, child, SUBTREE_STATUS_PARTIAL, NULL);
    changed = children_set_status (ctx, child, SUBTREE_STATUS_DEGRADED, NULL);
    ok (changed && child->status == SUBTREE_STATUS_DEGRADED,
        "PARTIAL -> DEGRADED works");

    children_set_status (ctx, child, SUBTREE_STATUS_PARTIAL, NULL);
    went_offline = false;
    changed = children_set_status (ctx, child, SUBTREE_STATUS_OFFLINE, &went_offline);
    ok (changed && went_offline && child->status == SUBTREE_STATUS_OFFLINE,
        "PARTIAL -> OFFLINE works and sets went_offline=true");

    children_set_status (ctx, child, SUBTREE_STATUS_PARTIAL, NULL);
    went_offline = false;
    changed = children_set_status (ctx, child, SUBTREE_STATUS_LOST, &went_offline);
    ok (changed && went_offline && child->status == SUBTREE_STATUS_LOST,
        "PARTIAL -> LOST works and sets went_offline=true");

    /* Test transitions from DEGRADED */
    children_set_status (ctx, child, SUBTREE_STATUS_DEGRADED, NULL);
    changed = children_set_status (ctx, child, SUBTREE_STATUS_FULL, NULL);
    ok (changed && child->status == SUBTREE_STATUS_FULL,
        "DEGRADED -> FULL works");

    children_set_status (ctx, child, SUBTREE_STATUS_DEGRADED, NULL);
    changed = children_set_status (ctx, child, SUBTREE_STATUS_PARTIAL, NULL);
    ok (changed && child->status == SUBTREE_STATUS_PARTIAL,
        "DEGRADED -> PARTIAL works");

    children_set_status (ctx, child, SUBTREE_STATUS_DEGRADED, NULL);
    went_offline = false;
    changed = children_set_status (ctx, child, SUBTREE_STATUS_OFFLINE, &went_offline);
    ok (changed && went_offline && child->status == SUBTREE_STATUS_OFFLINE,
        "DEGRADED -> OFFLINE works and sets went_offline=true");

    children_set_status (ctx, child, SUBTREE_STATUS_DEGRADED, NULL);
    went_offline = false;
    changed = children_set_status (ctx, child, SUBTREE_STATUS_LOST, &went_offline);
    ok (changed && went_offline && child->status == SUBTREE_STATUS_LOST,
        "DEGRADED -> LOST works and sets went_offline=true");

    /* Test transitions from LOST */
    children_set_status (ctx, child, SUBTREE_STATUS_LOST, NULL);
    went_offline = false;
    changed = children_set_status (ctx, child, SUBTREE_STATUS_OFFLINE, &went_offline);
    ok (changed && !went_offline && child->status == SUBTREE_STATUS_OFFLINE,
        "LOST -> OFFLINE works and went_offline=false (already offline)");

    /* Test idempotent transitions (no-ops) */
    children_set_status (ctx, child, SUBTREE_STATUS_OFFLINE, NULL);
    changed = children_set_status (ctx, child, SUBTREE_STATUS_OFFLINE, NULL);
    ok (!changed && child->status == SUBTREE_STATUS_OFFLINE,
        "OFFLINE -> OFFLINE is idempotent (changed=false)");

    children_set_status (ctx, child, SUBTREE_STATUS_LOST, NULL);
    changed = children_set_status (ctx, child, SUBTREE_STATUS_LOST, NULL);
    ok (!changed && child->status == SUBTREE_STATUS_LOST,
        "LOST -> LOST is idempotent (changed=false)");

    children_destroy (ctx);
    topology_decref (topo);
    flux_close (h);
}

void test_online_count (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    struct child *child;
    flux_error_t error;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);

    if (!(ctx = children_create (h, topo)))
        BAIL_OUT ("children_create failed, cannot continue test");

    ok (children_get_online_count (ctx) == 0,
        "children_get_online_count initially returns 0");

    child = children_lookup_byrank (ctx, 1);
    children_set_status (ctx, child, SUBTREE_STATUS_FULL, NULL);

    ok (children_get_online_count (ctx) == 1,
        "children_get_online_count returns 1 after one child online");

    child = children_lookup_byrank (ctx, 2);
    children_set_status (ctx, child, SUBTREE_STATUS_PARTIAL, NULL);

    ok (children_get_online_count (ctx) == 2,
        "children_get_online_count returns 2 after both children online");

    children_set_status (ctx, child, SUBTREE_STATUS_OFFLINE, NULL);

    ok (children_get_online_count (ctx) == 1,
        "children_get_online_count returns 1 after one goes offline");

    children_destroy (ctx);
    topology_decref (topo);
    flux_close (h);
}

void test_flat_topology (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    struct child *child;
    flux_error_t error;
    int count;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create (NULL, 16, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);

    ctx = children_create (h, topo);
    ok (ctx != NULL && ctx->count == 15,
        "children_create for flat size=16 has 15 children");
    if (!ctx)
        BAIL_OUT ("children_create failed, cannot continue test");

    count = 0;
    children_foreach (ctx, child) {
        ok (child->rank == count + 1,
            "child %d has rank %d", count, count + 1);
        count++;
    }

    child = children_lookup_route (ctx, 7);
    ok (child != NULL && child->rank == 7,
        "children_lookup_route rank=7 in flat topo routes directly to rank 7");

    children_destroy (ctx);
    topology_decref (topo);
    flux_close (h);
}

void test_is_bound (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    flux_error_t error;

    ok (children_is_bound (NULL) == false,
        "children_is_bound ctx=NULL returns false");

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);
    if (!(ctx = children_create (h, topo)))
        BAIL_OUT ("children_create failed, cannot continue test");

    ok (children_is_bound (ctx) == false,
        "children_is_bound returns false before bind");
    ok (ctx->bind_zsock == NULL,
        "bind_zsock is NULL before bind");

    children_destroy (ctx);
    topology_decref (topo);
    flux_close (h);
}

void test_authorize (void)
{
    ok (children_authorize (NULL, "test", "pubkey") < 0 && errno == EINVAL,
        "children_authorize ctx=NULL fails with EINVAL");
}

void test_bind_watch (void)
{
    flux_t *h;
    struct topology *topo;
    struct children *ctx;
    struct cert *cert;
    void *zctx;
    char *uri_out = NULL;
    flux_error_t error;
    struct ovconf config = {
        .torpid_min = 5.0,
        .torpid_max = 30.0,
        .tcp_user_timeout = 0,
        .connect_timeout = 30.0,
        .zmqdebug = 0,
        .zmq_io_threads = 1,
        .enable_ipv6 = 0,
        .child_rcvhwm = 0,
        .handlers = NULL,
    };

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (!(topo = topology_create ("kary:2", 8, NULL, &error)))
        BAIL_OUT ("topology_create failed: %s", error.text);
    if (!(ctx = children_create (h, topo)))
        BAIL_OUT ("children_create failed, cannot continue test");
    if (!(zctx = zmq_ctx_new ()))
        BAIL_OUT ("zmq_ctx_new failed");
    if (!(cert = cert_create ()))
        BAIL_OUT ("cert_create failed");

    ok (children_is_bound (ctx) == false,
        "children_is_bound returns false before bind");

    ok (children_bind (ctx,
                       zctx,
                       cert,
                       "ipc://*",
                       NULL,
                       &config,
                       &uri_out,
                       NULL,
                       &error) == 0,
        "children_bind to ipc://* works");

    ok (uri_out != NULL,
        "children_bind returned a concrete URI");
    ok (strstarts (uri_out, "ipc://"),
        "returned URI starts with ipc://");

    ok (children_is_bound (ctx) == true,
        "children_is_bound returns true after bind");

    ok (ctx->bind_zsock != NULL,
        "bind_zsock is set after bind");
    ok (ctx->zap != NULL,
        "zap server is created after bind");

    ok (children_watch (ctx, NULL, NULL) == 0,
        "children_watch succeeds after bind");

    ok (ctx->bind_w != NULL,
        "bind watcher is created after watch");

    /* Test sendmsg with no connected peers (should fail with EHOSTUNREACH) */
    {
        flux_msg_t *msg;
        if (!(msg = flux_request_encode ("test", NULL)))
            BAIL_OUT ("flux_request_encode failed");
        flux_msg_route_enable (msg);
        if (flux_msg_route_push (msg, "nonexistent-uuid") < 0)
            BAIL_OUT ("flux_msg_route_push failed");

        errno = 0;
        ok (children_sendmsg (ctx, msg) < 0 && errno == EHOSTUNREACH,
            "children_sendmsg with no connected peers fails with EHOSTUNREACH");

        flux_msg_decref (msg);
    }

    /* Note: Full send/recv with actual connected peers involves complex
     * CURVE+ZAP authentication and is tested in test/overlay.c integration tests.
     */

    free (uri_out);
    cert_destroy (cert);
    children_destroy (ctx);
    topology_decref (topo);
    zmq_ctx_term (zctx);
    flux_close (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_create_destroy ();
    test_invalid ();
    test_lookup ();
    test_lookup_route ();
    test_foreach ();
    test_status ();
    test_status_transitions ();
    test_online_count ();
    test_flat_topology ();
    test_is_bound ();
    test_authorize ();
    test_bind_watch ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
