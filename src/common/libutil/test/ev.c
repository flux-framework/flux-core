#include <czmq.h>
#include <poll.h>

#include "src/common/libutil/oom.h"
#include "src/common/libev/ev.h"
#include "src/common/libutil/ev_zmq.h"
#include "src/common/libtap/tap.h"

void timer_arg_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    int *i = w->data;
    if (i && ++(*i) == 100)
        ev_break (loop, EVBREAK_ALL);
}

void timer_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
}

void test_libev_timer (void)
{
    struct ev_loop *loop;
    ev_timer w;
    int i;

    ok ((loop = ev_loop_new (EVFLAG_AUTO)) != NULL,
        "ev_loop_new works");
    ok (ev_run (loop, 0) == 0,
        "ev_run returns 0 with no watchers configured");

    ev_timer_init (&w, timer_cb, 1E-1, 0.);
    ev_timer_start (loop, &w);
    ok (ev_run (loop, 0) == 0,
        "ev_run returns 0 after no-repeat timer fires once");
    ev_timer_stop (loop, &w);

    i = 0;
    ev_timer_init (&w, timer_arg_cb, 1E-1, 0.);
    w.data = &i;
    ev_timer_start (loop, &w);
    ok (ev_run (loop, 0) == 0 && i == 1,
        "passing arbitrary data using w->data works");
    ev_timer_stop (loop, &w);

    i = 0;
    ev_timer_init (&w, timer_arg_cb, 1E-3, 1E-3);
    w.data = &i;
    ev_timer_start (loop, &w);
    ok (ev_run (loop, 0) != 0 && i == 100,
        "ev_break causes ev_run to return nonzero");
    ev_timer_stop (loop, &w);

    ev_loop_destroy (loop);
}

void zero_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    int *i = w->data;
    ssize_t n;
    char buf[1024];

    if ((n = read (w->fd, buf, sizeof (buf))) < 0) {
        fprintf (stderr, "read error on /dev/zero: %s\n", strerror (errno));
        return;
    }
    if (n < sizeof (buf)) {
        fprintf (stderr, "short read on /dev/zero\n");
        return;
    }
    if (i && ++(*i) == 100)
        ev_break (loop, EVBREAK_ALL);
}

void test_libev_io (void)
{
    struct ev_loop *loop;
    ev_io w, w2;
    int i, fd, fd2;

    ok ((loop = ev_loop_new (EVFLAG_AUTO)) != NULL,
        "ev_loop_new works");

    /* handle 100 read events from /dev/zero with two workers */
    fd = open ("/dev/zero", O_RDONLY);
    fd2 = open ("/dev/zero", O_RDONLY);
    ok (fd >= 0 && fd2 >= 0,
        "opened /dev/zero twice");

    i = 0;
    ev_io_init (&w, zero_cb, fd, EV_READ);
    w.data = &i;
    ev_io_init (&w2, zero_cb, fd2, EV_READ);
    w2.data = &i;
    ev_io_start (loop, &w);
    ev_io_start (loop, &w2);
    ok (ev_run (loop, 0) != 0 && i == 100,
        "ev_run ran two /dev/zero readers a total of 100 times");
    ev_io_stop (loop, &w);
    ev_io_stop (loop, &w2);

    close (fd);
    close (fd2);

    ev_loop_destroy (loop);
}

/* test that zmq arcana we built ev_zmq on functions as advertised,
 * mainly the ZMQ_FD and ZMQ_EVENTS socket options that zmq_poll uses.
 */
void test_zmq_events (void)
{
    void *zctx;
    void *zin, *zout;
    int fd;
    char *s = NULL;

    ok ((zctx = zmq_init (1)) != NULL,
        "initialized zmq context");
    ok ((zout = zmq_socket (zctx, ZMQ_PAIR)) != NULL
        && zmq_bind (zout, "inproc://eventloop_test") == 0,
        "PAIR socket bind ok");
    ok ((zin = zmq_socket (zctx, ZMQ_PAIR)) != NULL
        && zmq_connect (zin, "inproc://eventloop_test") == 0,
        "PAIR socket connect ok");
    size_t fd_size = sizeof (fd);
    ok (zmq_getsockopt (zin, ZMQ_FD, &fd, &fd_size) == 0 && fd >= 0,
        "zmq_getsockopt ZMQ_FD returned valid file descriptor");
    /* ZMQ_EVENTS must be called after ZMQ_FD and before each poll()
     * to "reset" event trigger.  For more details see Issue #524.
     */
    uint32_t zevents;
    size_t zevents_size = sizeof (zevents);
    ok (zmq_getsockopt (zin, ZMQ_EVENTS, &zevents, &zevents_size) == 0
        && !(zevents & ZMQ_POLLIN),
        "zmq_getsockopt ZMQ_EVENTS says PAIR socket not ready to recv");
    // this test is somewhat questionable as there may be false positives
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    ok (poll (&pfd, 1, 10) == 0,
        "poll says edge triggered mailbox descriptor is not ready");
    ok (zstr_send (zout, "TEST") == 0,
        "sent a message over PAIR sockets");
    ok (poll (&pfd, 1, 10) == 1
        && (pfd.revents & POLLIN),
        "poll says edge triggered mailbox descriptor is ready");
    ok (zmq_getsockopt (zin, ZMQ_EVENTS, &zevents, &zevents_size) == 0
        && (zevents & ZMQ_POLLIN),
        "zmq_getsockopt ZMQ_EVENTS says PAIR socket ready to recv");
    zmq_pollitem_t zp = { .socket = zin, .events = ZMQ_POLLIN };
    ok (zmq_poll (&zp, 1, 10) == 1 && zp.revents == ZMQ_POLLIN,
        "zmq_poll says PAIR socket ready to recv");
    ok ((s = zstr_recv (zin)) != NULL,
        "received message over PAIR sockets");
    ok (zmq_getsockopt (zin, ZMQ_EVENTS, &zevents, &zevents_size) == 0
        && !(zevents & ZMQ_POLLIN),
        "zmq_getsockopt ZMQ_EVENTS says PAIR socket not ready to recv");
    ok (zmq_poll (&zp, 1, 10) == 0,
        "zmq_poll says PAIR socket not ready to recv");
    if (s)
        free (s);
    zmq_close (zin);
    zmq_close (zout);
    zmq_ctx_destroy (zctx);
}

void zsock_tx_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    static int count = 50; /* send two per invocation */

    if ((revents & EV_WRITE)) {
        if (zstr_send (w->zsock, "PING") < 0)
            fprintf (stderr, "zstr_send: %s", strerror (errno));
        if (zstr_send (w->zsock, "PING") < 0)
            fprintf (stderr, "zstr_send: %s", strerror (errno));
        if (--count == 0)
            ev_zmq_stop (loop, w);
    }
    if ((revents & EV_ERROR))
        ev_break (loop, EVBREAK_ALL);
}

void zsock_rx_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    int *iter = w->data;
    char *s;
    static int count = 100;

    if ((revents & EV_READ)) {
        (*iter)++;
        if (!(s = zstr_recv (w->zsock)))
            fprintf (stderr, "zstr_recv: %s", strerror (errno));
        else
            free (s);
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
    zmq_ctx_destroy (zctx);
}

void list_timer_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    static int i = 100;
    zlist_t *l = w->data;
    if (--i == 0) {
        ev_break (loop, EVBREAK_ALL);
    } else {
        zmsg_t *zmsg;
        if (!(zmsg = zmsg_new ()) || zlist_append (l, zmsg) < 0)
            oom ();
    }
}

int main (int argc, char *argv[])
{
    plan (27);

    test_libev_timer (); // 5
    test_libev_io (); // 3
    test_zmq_events (); // 13
    test_ev_zmq (); // 6

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
