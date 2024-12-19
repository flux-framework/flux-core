/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <zmq.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

#include "zwatcher.h"

static const size_t zmqwriter_msgcount = 1024;
static void *zctx;

void watcher_is (flux_watcher_t *w,
                 bool exp_active,
                 bool exp_referenced,
                 const char *what)
{
    bool is_active = flux_watcher_is_active (w);
    bool is_referenced = flux_watcher_is_referenced (w);

    ok (is_active == exp_active && is_referenced == exp_referenced,
        "%sact%sref after %s",
        exp_active ? "+" : "-",
        exp_referenced ? "+" : "-",
        what);
    if (is_active != exp_active)
        diag ("unexpectedly %sact", is_active ? "+" : "-");
    if (is_referenced != exp_referenced)
        diag ("unexpectedly %sref", is_referenced ? "+" : "-");
}

static void zmqwriter (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg)
{
    void *sock = zmqutil_watcher_get_zsock (w);
    static int count = 0;
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLOUT) {
        uint8_t blob[64] = { 0 };
        if (zmq_send (sock, blob, sizeof (blob), 0) < 0) {
            diag ("zmq_send: %s", strerror (errno));
            goto error;
        }
        count++;
        if (count == zmqwriter_msgcount)
            flux_watcher_stop (w);
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static void zmqreader (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg)
{
    void *sock = zmqutil_watcher_get_zsock (w);
    static int count = 0;
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLIN) {
        char buf[64];
        int rc;
        if ((rc = zmq_recv (sock, buf, sizeof (buf), 0)) < 0) {
            diag ("zmq_recv: %s", strerror (errno));
            goto error;
        }
        if (rc != 64) {
            diag ("zmq_reciv: got %d bytes, expected 64", rc);
            goto error;
        }
        count++;
        if (count == zmqwriter_msgcount)
            flux_watcher_stop (w);
    }
    return;
error:
    flux_reactor_stop_error (r);
}

static void test_zmq (flux_reactor_t *reactor)
{
    void *zs[2];
    flux_watcher_t *r, *w;
    const char *uri = "inproc://test_zmq";

    zs[0] = zmq_socket (zctx, ZMQ_PAIR);
    zs[1] = zmq_socket (zctx, ZMQ_PAIR);
    ok (zs[0] && zs[1]
        && zmq_bind (zs[0], uri) == 0
        && zmq_connect (zs[1], uri) == 0,
        "zmq: connected ZMQ_PAIR sockets over inproc");
    r = zmqutil_watcher_create (reactor, zs[0], FLUX_POLLIN, zmqreader, NULL);
    w = zmqutil_watcher_create (reactor, zs[1], FLUX_POLLOUT, zmqwriter, NULL);
    ok (r != NULL && w != NULL,
        "zmq: nonblocking reader and writer created");

    flux_watcher_start (w);
    watcher_is (w, true, true, "start");
    flux_watcher_unref (w);
    watcher_is (w, true, false, "unref");
    flux_watcher_ref (w);
    watcher_is (w, true, true, "ref");
    flux_watcher_stop (w);
    watcher_is (w, false, true, "stop");

    flux_watcher_start (r);
    flux_watcher_start (w);
    ok (flux_reactor_run  (reactor, 0) == 0,
        "zmq: reactor ran to completion after %d messages", zmqwriter_msgcount);
    flux_watcher_stop (r);
    ok (flux_watcher_is_active (r) == false,
        "flux_watcher_is_active() returns false after stop");
    ok (flux_watcher_is_referenced (w),
        "flux_watcher_is_referenced() returns true");
    flux_watcher_unref (w);
    ok (!flux_watcher_is_referenced (w),
        "flux_watcher_is_referenced() returns false after unref");
    flux_watcher_stop (w);
    flux_watcher_destroy (r);
    flux_watcher_destroy (w);

    zmq_close (zs[0]);
    zmq_close (zs[1]);
}

int main (int argc, char *argv[])
{
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    if (!(zctx = zmq_ctx_new ()))
        BAIL_OUT ("cannot create zmq context");

    ok ((reactor = flux_reactor_create (0)) != NULL,
        "created reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    test_zmq (reactor);

    flux_reactor_destroy (reactor);

    zmq_ctx_term (zctx);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

