#include <errno.h>
#include <czmq.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>

#include "src/common/libflux/message.h"
#include "src/common/libflux/handle.h"
#include "src/common/libflux/reactor.h"
#include "src/common/libflux/request.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

static int send_request (flux_t h, const char *topic)
{
    int rc = -1;
    flux_msg_t *msg = flux_request_encode (topic, NULL);
    if (!msg || flux_send (h, msg, 0) < 0) {
        fprintf (stderr, "%s: flux_send failed: %s",
                 __FUNCTION__, strerror (errno));
        goto done;
    }
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static int multmatch_count = 0;
static void multmatch1 (flux_t h, flux_msg_watcher_t *w, const flux_msg_t *msg,
                        void *arg)
{
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0 || strcmp (topic, "foo.baz"))
        flux_reactor_stop_error (h);
    flux_msg_watcher_stop (h, w);
    multmatch_count++;
}

static void multmatch2 (flux_t h, flux_msg_watcher_t *w, const flux_msg_t *msg,
                        void *arg)
{
    const char *topic;
    if (flux_msg_get_topic (msg, &topic) < 0 || strcmp (topic, "foo.bar"))
        flux_reactor_stop_error (h);
    flux_msg_watcher_stop (h, w);
    multmatch_count++;
}

static void test_multmatch (flux_t h)
{
    flux_msg_watcher_t *w1, *w2;
    struct flux_match m1 = FLUX_MATCH_ANY;
    struct flux_match m2 = FLUX_MATCH_ANY;

    m1.topic_glob = "foo.*";
    m2.topic_glob = "foo.bar";

    /* test #1: verify multiple match behaves as documented, that is,
     * a message is matched (only) by the most recently added watcher
     */
    ok ((w1 = flux_msg_watcher_create (m1, multmatch1, NULL)) != NULL,
        "multmatch: first added watcher for foo.*");
    ok ((w2 = flux_msg_watcher_create (m2, multmatch2, NULL)) != NULL,
        "multmatch: next added watcher for foo.bar");
    flux_msg_watcher_start (h, w1);
    flux_msg_watcher_start (h, w2);
    ok (send_request (h, "foo.bar") == 0,
        "multmatch: send foo.bar msg");
    ok (send_request (h, "foo.baz") == 0,
        "multmatch: send foo.baz msg");
    ok (flux_reactor_start (h) == 0 && multmatch_count == 2,
        "multmatch: last added watcher handled foo.bar");
    flux_msg_watcher_destroy (w1);
    flux_msg_watcher_destroy (w2);
}

static int msgwatcher_count = 100;
static void msgreader (flux_t h, flux_msg_watcher_t *w, const flux_msg_t *msg,
                       void *arg)
{
    static int count = 0;
    count++;
    if (count == msgwatcher_count)
        flux_msg_watcher_stop (h, w);
}

static void test_msg (flux_t h)
{
    flux_msg_watcher_t *w;
    int i;

    ok ((w = flux_msg_watcher_create (FLUX_MATCH_ANY, msgreader, NULL)) != NULL,
        "msg: created watcher for any message");
    flux_msg_watcher_start (h, w);
    for (i = 0; i < msgwatcher_count; i++) {
        if (send_request (h, "foo") < 0)
            break;
    }
    ok (i == msgwatcher_count,
        "msg: sent %d requests", i);
    ok (flux_reactor_start (h) == 0,
        "msg: reactor ran to completion after %d requests", msgwatcher_count);
    flux_msg_watcher_stop (h, w);
    flux_msg_watcher_destroy (w);
}

static const size_t zmqwriter_msgcount = 1024;

static void zmqwriter (flux_t h, flux_zmq_watcher_t *w, void *sock,
                       int revents, void *arg)
{
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
            flux_zmq_watcher_stop (h, w);
    }
    return;
error:
    flux_reactor_stop_error (h);
}

static void zmqreader (flux_t h, flux_zmq_watcher_t *w, void *sock,
                       int revents, void *arg)
{
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
            flux_zmq_watcher_stop (h, w);
    }
    return;
error:
    flux_reactor_stop_error (h);
}

static void test_zmq (flux_t h)
{
    zctx_t *zctx;
    void *zs[2];
    flux_zmq_watcher_t *r, *w;

    ok ((zctx = zctx_new ()) != NULL,
        "zmq: created zmq context");
    zs[0] = zsocket_new (zctx, ZMQ_PAIR);
    zs[1] = zsocket_new (zctx, ZMQ_PAIR);
    ok (zs[0] && zs[1]
        && zsocket_bind (zs[0], "inproc://test_zmq") == 0
        && zsocket_connect (zs[1], "inproc://test_zmq") == 0,
        "zmq: connected ZMQ_PAIR sockets over inproc");
    r = flux_zmq_watcher_create (zs[0], FLUX_POLLIN, zmqreader, NULL);
    w = flux_zmq_watcher_create (zs[1], FLUX_POLLOUT, zmqwriter, NULL);
    ok (r != NULL && w != NULL,
        "zmq: nonblocking reader and writer created");
    flux_zmq_watcher_start (h, r);
    flux_zmq_watcher_start (h, w);
    ok (flux_reactor_start (h) == 0,
        "zmq: reactor ran to completion after %d messages", zmqwriter_msgcount);
    flux_zmq_watcher_stop (h, r);
    flux_zmq_watcher_stop (h, w);
    flux_zmq_watcher_destroy (r);
    flux_zmq_watcher_destroy (w);

    zsocket_destroy (zctx, zs[0]);
    zsocket_destroy (zctx, zs[1]);
    zctx_destroy (&zctx);
}

static const size_t fdwriter_bufsize = 10*1024*1024;

static void fdwriter (flux_t h, flux_fd_watcher_t *w, int fd,
                      int revents, void *arg)
{
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLOUT) {
        if ((n = write (fd, buf + count, fdwriter_bufsize - count)) < 0
                                && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: write failed: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_fd_watcher_stop (h, w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (h);
}
static void fdreader (flux_t h, flux_fd_watcher_t *w, int fd,
                      int revents, void *arg)
{
    static char *buf = NULL;
    static int count = 0;
    int n;

    if (!buf)
        buf = xzmalloc (fdwriter_bufsize);
    if (revents & FLUX_POLLERR) {
        fprintf (stderr, "%s: FLUX_POLLERR is set\n", __FUNCTION__);
        goto error;
    }
    if (revents & FLUX_POLLIN) {
        if ((n = read (fd, buf + count, fdwriter_bufsize - count)) < 0
                            && errno != EWOULDBLOCK && errno != EAGAIN) {
            fprintf (stderr, "%s: read failed: %s\n",
                     __FUNCTION__, strerror (errno));
            goto error;
        }
        if (n > 0) {
            count += n;
            if (count == fdwriter_bufsize) {
                flux_fd_watcher_stop (h, w);
                free (buf);
            }
        }
    }
    return;
error:
    flux_reactor_stop_error (h);
}

static int set_nonblock (int fd)
{
    int flags = fcntl (fd, F_GETFL, NULL);
    if (flags < 0 || fcntl (fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        fprintf (stderr, "fcntl: %s\n", strerror (errno));
        return -1;
    }
    return 0;
}

static void test_fd (flux_t h)
{
    int fd[2];
    flux_fd_watcher_t *r, *w;

    ok (socketpair (PF_LOCAL, SOCK_STREAM, 0, fd) == 0
        && set_nonblock (fd[0]) == 0 && set_nonblock (fd[1]) == 0,
        "fd: successfully created non-blocking socketpair");
    r = flux_fd_watcher_create (fd[0], FLUX_POLLIN, fdreader, NULL);
    w = flux_fd_watcher_create (fd[1], FLUX_POLLOUT, fdwriter, NULL);
    ok (r != NULL && w != NULL,
        "fd: reader and writer created");
    flux_fd_watcher_start (h, r);
    flux_fd_watcher_start (h, w);
    ok (flux_reactor_start (h) == 0,
        "fd: reactor ran to completion after %lu bytes", fdwriter_bufsize);
    flux_fd_watcher_stop (h, r);
    flux_fd_watcher_stop (h, w);
    flux_fd_watcher_destroy (r);
    flux_fd_watcher_destroy (w);
    close (fd[0]);
    close (fd[1]);
}

static int repeat_countdown = 10;
static void repeat (flux_t h, flux_timer_watcher_t *w, int revents, void *arg)
{
    repeat_countdown--;
    if (repeat_countdown == 0)
        flux_timer_watcher_stop (h, w);
}

static bool oneshot_ran = false;
static int oneshot_errno = 0;
static void oneshot (flux_t h, flux_timer_watcher_t *w, int revents, void *arg)
{
    oneshot_ran = true;
    if (oneshot_errno != 0) {
        errno = oneshot_errno;
        flux_reactor_stop_error (h);
    }
}

static void test_timer (flux_t h)
{
    flux_timer_watcher_t *w;

    errno = 0;
    ok (!flux_timer_watcher_create (-1, 0, oneshot, NULL) && errno == EINVAL,
        "timer: creating negative timeout fails with EINVAL");
    ok (!flux_timer_watcher_create (0, -1, oneshot, NULL) && errno == EINVAL,
        "timer: creating negative repeat fails with EINVAL");
    ok ((w = flux_timer_watcher_create (0, 0, oneshot, NULL)) != NULL,
        "timer: creating zero timeout works");
    flux_timer_watcher_start (h, w);
    ok (flux_reactor_start (h) == 0,
        "timer: reactor ran to completion (single oneshot)");
    ok (oneshot_ran == true,
        "timer: oneshot was executed");
    oneshot_ran = false;
    ok (flux_reactor_start (h) == 0,
        "timer: reactor ran to completion (expired oneshot)");
    ok (oneshot_ran == false,
        "timer: expired oneshot was not re-executed");

    errno = 0;
    oneshot_errno = ESRCH;
    flux_timer_watcher_start (h, w);
    ok (flux_reactor_start (h) < 0 && errno == ESRCH,
        "general: reactor stop_error worked with errno passthru");
    flux_timer_watcher_stop (h, w);
    flux_timer_watcher_destroy (w);

    ok ((w = flux_timer_watcher_create (0.01, 0.01, repeat, NULL)) != NULL,
        "timer: creating 1ms timeout with 1ms repeat works");
    flux_timer_watcher_start (h, w);
    ok (flux_reactor_start (h) == 0,
        "timer: reactor ran to completion (single repeat)");
    ok (repeat_countdown == 0,
        "timer: repeat timer stopped itself after countdown");
    flux_timer_watcher_stop (h, w);
    flux_timer_watcher_destroy (w);
}

static void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

int main (int argc, char *argv[])
{
    flux_t h;

    plan (3+11+3+4+3+5);

    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    flux_fatal_set (h, fatal_err, NULL);

    ok (flux_reactor_start (h) == 0,
        "general: reactor ran to completion (no watchers)");
    errno = 0;
    ok (flux_sleep_on (h, FLUX_MATCH_ANY) < 0 && errno == EINVAL,
        "general: flux_sleep_on outside coproc fails with EINVAL");

    test_timer (h); // 11
    test_fd (h); // 3
    test_zmq (h); // 4
    test_msg (h); // 3
    test_multmatch (h); // 5

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

