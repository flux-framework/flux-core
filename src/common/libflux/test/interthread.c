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
#include <poll.h>
#include <pthread.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

void recv_count_is (flux_t *h, size_t expected, const char *msg)
{
    size_t n;
    ok (flux_opt_get (h, FLUX_OPT_RECV_QUEUE_COUNT, &n, sizeof (n)) == 0
        && n == expected,
        "%s", msg);
}
void send_count_is (flux_t *h, size_t expected, const char *msg)
{
    size_t n;
    ok (flux_opt_get (h, FLUX_OPT_SEND_QUEUE_COUNT, &n, sizeof (n)) == 0
        && n == expected,
        "%s", msg);
}

void test_basic (void)
{
    const char *uri = "interthread://test1";
    const char *uri2 = "interthread://test2";
    flux_t *h;
    flux_t *h2;
    flux_t *h3;
    flux_t *h4;
    flux_error_t error;
    flux_msg_t *msg;
    flux_msg_t *req;
    flux_msg_t *rep;
    struct flux_msg_cred cred;
    const char *topic;
    const char *payload;

    /* create pair to use in test */
    h = flux_open (uri, 0);
    ok (h != NULL,
        "basic: flux_open %s (1) works", uri);
    h2 = flux_open (uri, 0);
    ok (h2 != NULL,
        "basic: flux_open %s (2) works", uri);

    send_count_is (h, 0, "SEND_QUEUE_COUNT h = 0");
    recv_count_is (h, 0, "RECV_QUEUE_COUNT h = 0");
    send_count_is (h2, 0, "SEND_QUEUE_COUNT h2 = 0");
    recv_count_is (h2, 0, "RECV_QUEUE_COUNT h2 = 0");

    /* cover connecting to a paired channel */
    errno = 0;
    ok (flux_open (uri, 0) == NULL && errno == EADDRINUSE,
        "basic: flux_open %s (3) fails with EADDRINUSE", uri);
    errno = 0;
    error.text[0] = '\0';
    ok (flux_open_ex (uri, 0, &error) == NULL && errno == EADDRINUSE,
        "basic: flux_open_ex %s also fails with EADDRINUSE", uri);
    diag ("%s", error.text);
    like (error.text, "already paired",
        "basic: and error string contains something useful", uri);

    /* create another pair to exercise channel allocation */
    h3 = flux_open (uri2, 0);
    ok (h3 != NULL,
        "basic: flux_open %s (1) works", uri2);
    h4 = flux_open (uri2, 0);
    ok (h4 != NULL,
        "basic: flux_open %s (2) works", uri2);
    flux_close (h4);
    flux_close (h3);

    /* send request h -> h2 */
    if (!(req = flux_request_encode ("foo.bar", "baz")))
        BAIL_OUT ("basic: could not create request");
    ok (flux_send (h, req, 0) == 0,
       "basic: flux_send on first handle works");
    send_count_is (h, 1, "SEND_QUEUE_COUNT h = 1");
    recv_count_is (h2, 1, "RECV_QUEUE_COUNT h2 = 1");
    msg = flux_recv (h2, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "basic: flux_recv on second handle works");
    send_count_is (h, 0, "SEND_QUEUE_COUNT h = 0");
    recv_count_is (h2, 0, "RECV_QUEUE_COUNT h = 0");
    ok (flux_msg_route_count (msg) == 0,
        "basic: request has no route stack");
    ok (flux_request_decode (msg, &topic, &payload) == 0
        && streq (topic, "foo.bar")
        && streq (payload, "baz"),
        "basic: request has expected topic and payload");
    if (!(rep = flux_response_derive (msg, 0)))
        BAIL_OUT ("basic: could not create response");
    ok (flux_msg_get_cred (msg, &cred) == 0
        && cred.userid == getuid ()
        && cred.rolemask == (FLUX_ROLE_OWNER | FLUX_ROLE_LOCAL),
        "basic: message cred has expected values");
    flux_msg_destroy (msg);

    /* send response h2 -> h */
    ok (flux_send (h2, rep, 0) == 0,
       "basic: flux_send on second handle works");
    recv_count_is (h, 1, "RECV_QUEUE_COUNT h = 1");
    send_count_is (h2, 1, "SEND_QUEUE_COUNT h2 = 1");
    msg = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "basic: flux_recv on first handle works");
    recv_count_is (h, 0, "RECV_QUEUE_COUNT h = 0");
    send_count_is (h2, 0, "SEND_QUEUE_COUNT h2 = 0");
    ok (flux_msg_route_count (msg) == 0,
        "basic: response has no route stack");
    ok (flux_response_decode (msg, &topic, &payload) == 0
        && streq (topic, "foo.bar")
        && payload == NULL,
        "basic: response has expected topic and payload");
    flux_msg_destroy (msg);
    flux_msg_destroy (req);
    flux_msg_destroy (rep);

    flux_close (h2);
    flux_close (h);
}

void test_router (void)
{
    const char *uri = "interthread://test1";
    flux_t *h;
    flux_t *h2;
    flux_msg_t *msg;
    flux_msg_t *req;
    flux_msg_t *rep;

    /* create pair to use in test */
    h = flux_open (uri, 0);
    ok (h != NULL,
        "router: flux_open %s (1) works", uri);
    ok (flux_opt_set (h, FLUX_OPT_ROUTER_NAME, "testrouter", 11) == 0,
        "router: flux_opt_set FLUX_OPT_ROUTER_NAME=testrouter works");
    h2 = flux_open (uri, 0);
    ok (h2 != NULL,
        "router: flux_open %s (2) works", uri);

    /* send request h -> h2 */
    if (!(req = flux_request_encode ("foo.bar", "baz")))
        BAIL_OUT ("router: could not create request");
    ok (flux_send (h, req, 0) == 0,
       "router: flux_send on first handle works");
    msg = flux_recv (h2, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "router: flux_recv on second handle works");
    ok (flux_msg_route_count (msg) == 1
        && streq (flux_msg_route_last (msg), "testrouter"),
        "router: request is from testrouter");
    if (!(rep = flux_response_derive (msg, 0)))
        BAIL_OUT ("router: could not create response");
    flux_msg_destroy (msg);

    /* send response h2 -> h */
    ok (flux_send (h2, rep, 0) == 0,
       "router: flux_send on second handle works");
    msg = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "router: flux_recv on first handle works");
    ok (flux_msg_route_count (msg) == 0,
        "router: response has no route stack");
    flux_msg_destroy (msg);
    flux_msg_destroy (rep);

    /* send request h2 -> h */
    ok (flux_send (h2, req, 0) == 0,
       "router: flux_send on second handle works");
    msg = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "router: flux_recv on first handle works");
    ok (flux_msg_route_count (msg) == 1
        && streq (flux_msg_route_last (msg), "test1"),
        "router: request is from test1");
    if (!(rep = flux_response_derive (msg, 0)))
        BAIL_OUT ("router: could not create response");
    flux_msg_destroy (msg);

    /* send response h -> h2 */
    ok (flux_send (h, rep, 0) == 0,
       "router: flux_send on first handle works");
    msg = flux_recv (h2, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "router: flux_recv on second handle works");
    ok (flux_msg_route_count (msg) == 0,
        "router: response has no route stack");
    flux_msg_destroy (msg);
    flux_msg_destroy (rep);

    flux_msg_destroy (req);

    flux_close (h2);
    flux_close (h);
}

struct test_thread {
    pthread_t t;
    flux_t *h;
    flux_watcher_t *w;
    char uri[64];
    int total;
    int count;
};

static flux_watcher_t *timer;
static int num_active_threads;

void *test_thread (void *arg)
{
    struct test_thread *test = arg;
    flux_t *h;
    flux_msg_t *msg;

    if (!(h = flux_open (test->uri, 0)))
        BAIL_OUT ("%s: flux_open: %s", test->uri, strerror (errno));
    if (!(msg = flux_request_encode ("foo.bar", NULL)))
        BAIL_OUT ("%s: flux_request_encode: %s", test->uri, strerror (errno));
    for (int i = 0; i < test->total; i++) {
        if (flux_send (h, msg, 0) < 0)
            BAIL_OUT ("%s: flux_send: %s", test->uri, strerror (errno));
    }
    flux_msg_destroy (msg);
    flux_close (h);
    return NULL;
}

void timeout (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    diag ("test timed out");
    flux_reactor_stop_error (r);
}

void watcher (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct test_thread *test = arg;
    flux_msg_t *msg;

    if (!(msg = flux_recv (test->h, FLUX_MATCH_ANY, 0))) {
        diag ("%s: flux_recv: %s", test->uri, strerror (errno));
        flux_reactor_stop_error (r);
        return;
    }
    if (flux_request_decode (msg, NULL, NULL) < 0) {
        diag ("%s: flux_request_decode: %s", test->uri, strerror (errno));
        flux_reactor_stop_error (r);
    }
    flux_msg_destroy (msg);
    if (++test->count == test->total) {
        flux_watcher_stop (w);
        if (--num_active_threads == 0)
            flux_watcher_stop (timer);
    }
}

int test_threads_init (struct test_thread *test,
                       size_t count,
                       flux_reactor_t *r,
                       int num_messages)
{
    for (int i = 0; i < count; i++) {
        int e;
        snprintf (test[i].uri, sizeof (test[i].uri), "interthread://%d", i);
        test[i].total = num_messages;
        if (!(test[i].h = flux_open (test[i].uri, 0))) {
            diag ("flux_open %s failed: %s", test[i].uri, strerror (errno));
            return -1;
        }
        if (!(test[i].w = flux_handle_watcher_create (r,
                                                      test[i].h,
                                                      FLUX_POLLIN,
                                                      watcher,
                                                      &test[i]))) {
            diag ("watcher create %s failed: %s", test[i].uri, strerror (errno));
            return -1;
        }
        flux_watcher_start (test[i].w);
        if ((e = pthread_create (&test[i].t, NULL, test_thread, &test[i]))) {
            diag ("pthread_create %s failed: %s", test[i].uri, strerror (e));
            return -1;
        }
    }
    return 0;
}

int test_threads_join (struct test_thread *test, size_t count)
{
    for (int i = 0; i < count; i++) {
        void *result;
        int e;
        if ((e = pthread_join (test[i].t, &result))) {
            diag ("pthread_join %s failed: %s", test[i].uri, strerror (e));
            return -1;
        }
        flux_watcher_destroy (test[i].w);
        flux_close (test[i].h);
    }
    return 0;
}

void test_threads (void)
{
    const double timeout_s = 30.;
    const int num_messages = 32;
    const int num_threads = 16;
    struct test_thread test[num_threads];
    flux_reactor_t *r;

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("could not create reactor");

    num_active_threads = num_threads;
    if (!(timer = flux_timer_watcher_create (r, timeout_s, 0, timeout, NULL)))
        BAIL_OUT ("could not create timer watcher");
    flux_watcher_start (timer);

    memset (test, 0, sizeof (test));

    ok (test_threads_init (test, num_threads, r, num_messages) == 0,
        "started %d threads that will each send %d messages",
        num_threads,
        num_messages);
    ok (flux_reactor_run (r, 0) == 0,
        "all messages received with no errors");
    ok (test_threads_join (test, num_threads) == 0,
        "finalized test threads");

    flux_watcher_destroy (timer);
    flux_reactor_destroy (r);
}

void test_poll (void)
{
    const char *uri = "interthread://polltest";
    flux_t *h1;
    flux_t *h2;
    flux_msg_t *msg;
    flux_msg_t *msg2;
    struct pollfd pfd;

    // with NOREQUEUE, pollfd/pollevents come directly from connector
    if (!(h1 = flux_open (uri, FLUX_O_NOREQUEUE)))
        BAIL_OUT ("%s: flux_open: %s", uri, strerror (errno));
    if (!(h2 = flux_open (uri, FLUX_O_NOREQUEUE)))
        BAIL_OUT ("%s: flux_open: %s", uri, strerror (errno));
    diag ("poll: opened h1 and h2");

    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    // enqueue 2 messages
    ok (flux_pollevents (h2) == FLUX_POLLOUT,
        "flux_pollevents h2 returns POLLOUT");
    ok (flux_send (h1, msg, 0) == 0,
        "flux_send h1 works");
    ok (flux_send (h1, msg, 0) == 0,
        "flux_send h1 works");
    ok (flux_pollevents (h2) == (FLUX_POLLOUT | FLUX_POLLIN),
        "flux_pollevents h2 returns POLLOUT|POLLIN");

    // read 1 message
    ok ((msg2 = flux_recv (h2, FLUX_MATCH_ANY, 0)) != NULL,
        "flux_recv h2 works");
    ok (flux_pollevents (h2) == (FLUX_POLLOUT | FLUX_POLLIN),
        "flux_pollevents h2 returns POLLOUT|POLLIN");
    flux_msg_decref (msg2);

    // read 2nd message
    ok ((msg2 = flux_recv (h2, FLUX_MATCH_ANY, 0)) != NULL,
        "flux_recv h2 works");
    ok (flux_pollevents (h2) == FLUX_POLLOUT,
        "flux_pollevents h2 returns POLLOUT");
    flux_msg_destroy (msg2);

    // get pollfd set up with no messages pending
    ok ((pfd.fd = flux_pollfd (h2)) >= 0,
        "flux_pollfd works");
    pfd.events = POLLIN; // poll fd becomes "readable" when pollevents should
    pfd.revents = 0;     //   be checked
    ok (poll (&pfd, 1, 0) == 1 && pfd.revents == POLLIN,
        "flux_pollfd suggests we check pollevents");
    ok (flux_pollevents (h2) == FLUX_POLLOUT,
        "flux_pollevents returns POLLOUT only");
    pfd.events = POLLIN;
    pfd.revents = 0;
    ok (poll (&pfd, 1, 0) == 0, // because edge triggered
        "flux_pollfd says not ready, now that we've checked pollevents");

    // enqueue 2 messages
    ok (flux_send (h1, msg, 0) == 0,
        "flux_send h1 works");
    ok (flux_send (h1, msg, 0) == 0,
        "flux_send h1 works");
    pfd.events = POLLIN,
    pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 1 && pfd.revents == POLLIN,
        "pollfd suggests we read pollevents");
    ok (flux_pollevents (h2) == (FLUX_POLLOUT | FLUX_POLLIN),
        "flux_pollevents returns POLLOUT|POLLIN");
    ok (poll (&pfd, 1, 0) == 0,
        "flux_pollfd says not ready, now that we've checked pollevents");

    // N.B. we don't own the pollfd so no close here
    flux_msg_destroy (msg);
    flux_close (h1);
    flux_close (h2);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_router ();
    test_threads ();
    test_poll ();

    done_testing ();
    return 0;
}

// vi: ts=4 sw=4 expandtab
