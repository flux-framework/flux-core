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
#include <czmq.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>

#include "src/common/libflux/reactor.h"
#include "src/common/libtap/tap.h"

#include "src/common/libzmqutil/reactor.h"

static const size_t zmqwriter_msgcount = 1024;

static void zmqwriter (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    void *sock = zmqutil_watcher_get_zsock (w);
    static int count = 0;
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLOUT) {
        uint8_t blob[64];
        zmsg_t *zmsg = zmsg_new ();
        if (!zmsg || zmsg_addmem (zmsg, blob, sizeof (blob)) < 0) {
            fprintf (stderr, "%s: failed to create message: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (zmsg_send (&zmsg, sock) < 0) {
            fprintf (stderr, "%s: zmsg_send: %s\n",
                     __FUNCTION__, strerror (errno));
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

static void zmqreader (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    void *sock = zmqutil_watcher_get_zsock (w);
    static int count = 0;
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLIN) {
        zmsg_t *zmsg = zmsg_recv (sock);
        if (!zmsg) {
            fprintf (stderr, "%s: zmsg_recv: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        zmsg_destroy (&zmsg);
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
    zsock_t *zs[2];
    flux_watcher_t *r, *w;
    const char *uri = "inproc://test_zmq";

    zsys_set_logstream (stderr);
    zsys_handler_set (NULL);

    zs[0] = zsock_new_pair (NULL);
    zs[1] = zsock_new_pair (NULL);
    ok (zs[0] && zs[1]
        && zsock_bind (zs[0], "%s", uri) == 0
        && zsock_connect (zs[1], "%s", uri) == 0,
        "zmq: connected ZMQ_PAIR sockets over inproc");
    r = zmqutil_watcher_create (reactor, zs[0], FLUX_POLLIN, zmqreader, NULL);
    w = zmqutil_watcher_create (reactor, zs[1], FLUX_POLLOUT, zmqwriter, NULL);
    ok (r != NULL && w != NULL,
        "zmq: nonblocking reader and writer created");
    flux_watcher_start (r);
    flux_watcher_start (w);
    ok (flux_reactor_run  (reactor, 0) == 0,
        "zmq: reactor ran to completion after %d messages", zmqwriter_msgcount);
    flux_watcher_stop (r);
    flux_watcher_stop (w);
    flux_watcher_destroy (r);
    flux_watcher_destroy (w);

    zsock_destroy (&zs[0]);
    zsock_destroy (&zs[1]);

    zsys_shutdown ();
}

int main (int argc, char *argv[])
{
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    ok ((reactor = flux_reactor_create (0)) != NULL,
        "created reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    test_zmq (reactor);

    flux_reactor_destroy (reactor);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

