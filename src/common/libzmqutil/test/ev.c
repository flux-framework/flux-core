/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* ev.c - standalone test of libev ev_zmq watcher (no flux) */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <poll.h>
#include <zmq.h>
#include <stdio.h>

#include "src/common/libzmqutil/ev_zmq.h"
#include "src/common/libtap/tap.h"

static void *zctx;

void zsock_tx_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    static int count = 50; /* send two per invocation */

    if ((revents & EV_WRITE)) {
        if (zmq_send (w->zsock, "PING", 4, 0) < 0)
            fprintf (stderr, "zmq_send: %s", strerror (errno));
        if (zmq_send (w->zsock, "PING", 4, 0) < 0)
            fprintf (stderr, "zmq_send: %s", strerror (errno));
        if (--count == 0)
            ev_zmq_stop (loop, w);
    }
    if ((revents & EV_ERROR))
        ev_break (loop, EVBREAK_ALL);
}

void zsock_rx_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    int *iter = w->data;
    char buf[128];
    static int count = 100;

    if ((revents & EV_READ)) {
        (*iter)++;
        if (zmq_recv (w->zsock, buf, sizeof (buf), 0) < 0)
            fprintf (stderr, "zstr_recv: %s", strerror (errno));
        if (--count == 0)
            ev_zmq_stop (loop, w);
    }
    if ((revents & EV_ERROR))
        ev_break (loop, EVBREAK_ALL);
}


/* send 100 messages over PAIR sockets
 * sender in one event handler, receiver in another
 */
void test_ev_zmq (void)
{
    struct ev_loop *loop;
    void *zctx;
    void *zin, *zout;
    int i;
    ev_zmq win, wout;

    ok ((loop = ev_loop_new (EVFLAG_AUTO)) != NULL,
        "ev_loop_new works");
    ok ((zctx = zmq_init (1)) != NULL,
        "initialized zmq context");
    ok ((zout = zmq_socket (zctx, ZMQ_PAIR)) != NULL
        && zmq_bind (zout, "inproc://eventloop_test") == 0,
        "PAIR socket bind ok");
    ok ((zin = zmq_socket (zctx, ZMQ_PAIR)) != NULL
        && zmq_connect (zin, "inproc://eventloop_test") == 0,
        "PAIR socket connect ok");

    i = 0;
    ev_zmq_init (&win, zsock_rx_cb, zin, EV_READ);
    win.data = &i;
    ev_zmq_init (&wout, zsock_tx_cb, zout, EV_WRITE);

    ev_zmq_start (loop, &win);
    ev_zmq_start (loop, &wout);

    ok (ev_run (loop, 0) == 0,
        "both watchers removed themselves and ev_run exited");
    ev_zmq_stop (loop, &win);
    ev_zmq_stop (loop, &wout);
    cmp_ok (i, "==", 100,
        "ev_zmq handler ran 100 times");

    ev_loop_destroy (loop);

    zmq_close (zin);
    zmq_close (zout);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    if (!(zctx = zmq_ctx_new ()))
        BAIL_OUT ("could not create zeromq context");

    test_ev_zmq ();

    zmq_ctx_term (zctx);

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
