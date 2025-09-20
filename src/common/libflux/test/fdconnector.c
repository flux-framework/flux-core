/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"
#include "ccan/array_size/array_size.h"

static int fdcount (void)
{
    int fd, fdlimit = sysconf (_SC_OPEN_MAX);
    int count = 0;
    for (fd = 0; fd < fdlimit; fd++) {
        if (fcntl (fd, F_GETFD) != -1) {
            count++;
        }
    }
    return count;
}

void test_basic (void)
{
    int sock[2];
    char uri[2][32];
    flux_t *h[2];
    flux_msg_t *msg;
    flux_msg_t *req;
    flux_msg_t *rep;
    const char *topic;
    const char *payload;

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, sock) < 0)
        BAIL_OUT ("could not create socketpair");

    /* send auth byte in both directions to prevent flux_open() hang */
    char c = 0;
    if (write (sock[0], &c, 1) < 0 || write (sock[1], &c, 1) < 0)
        BAIL_OUT ("could not write auth bytes");

    /* create pair to use in test */
    for (int i = 0; i < 2; i++) {
        snprintf (uri[i], sizeof (uri[0]), "fd://%d", sock[i]);
        h[i] = flux_open (uri[i], 0);
        ok (h[i] != NULL,
            "basic: flux_open %s works", uri[i]);
    }

    /* send request h[0] -> h[1] */
    if (!(req = flux_request_encode ("foo.bar", "baz")))
        BAIL_OUT ("basic: could not create request");
    ok (flux_send (h[0], req, 0) == 0,
       "basic: flux_send on first handle works");
    msg = flux_recv (h[1], FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "basic: flux_recv on second handle works");
    ok (flux_msg_route_count (msg) == 0,
        "basic: request has no route stack");
    ok (flux_request_decode (msg, &topic, &payload) == 0
        && streq (topic, "foo.bar")
        && streq (payload, "baz"),
        "basic: request has expected topic and payload");
    if (!(rep = flux_response_derive (msg, 0)))
        BAIL_OUT ("basic: could not create response");
    flux_msg_destroy (msg);

    /* send response h[1] -> h[0] */
    ok (flux_send (h[1], rep, 0) == 0,
       "basic: flux_send on second handle works");
    msg = flux_recv (h[0], FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "basic: flux_recv on first handle works");
    ok (flux_msg_route_count (msg) == 0,
        "basic: response has no route stack");
    ok (flux_response_decode (msg, &topic, &payload) == 0
        && streq (topic, "foo.bar")
        && payload == NULL,
        "basic: response has expected topic and payload");
    flux_msg_destroy (msg);
    flux_msg_destroy (req);
    flux_msg_destroy (rep);

    flux_close (h[1]);
    flux_close (h[0]);
}

void test_poll (void)
{
    int sock[2];
    char uri[2][32];
    flux_t *h[2];
    flux_msg_t *msg;
    flux_msg_t *msg2;
    struct pollfd pfd;
    int rc;

    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, sock) < 0)
        BAIL_OUT ("could not create socketpair");

    /* send auth byte in both directions to prevent flux_open() hang */
    char c = 0;
    if (write (sock[0], &c, 1) < 0 || write (sock[1], &c, 1) < 0)
        BAIL_OUT ("could not write auth bytes");

    /* create pair to use in test */
    for (int i = 0; i < 2; i++) {
        snprintf (uri[i], sizeof (uri[0]), "fd://%d", sock[i]);
        h[i] = flux_open (uri[i], 0);
        ok (h[i] != NULL,
            "basic: flux_open %s works", uri[i]);
    }

    ok ((flux_pollfd (h[1])) >= 0,
        "flux_pollfd works");
    ok (flux_pollevents (h[1]) == FLUX_POLLOUT,
        "flux_pollevents initially returns POLLOUT");

    pfd.fd = flux_pollfd (h[1]);
    pfd.events = POLLIN;
    pfd.revents = 0;
    rc = poll (&pfd, 1, 0);
    if (rc < 0)
        diag ("poll: %s", strerror (errno));
    if (rc == 1) {
        int revents = flux_pollevents (h[1]);
        diag ("pollfd is ready, pollevents = 0x%x", revents);
    }
    ok (rc == 0,
        "pollfd is not ready, as required by the next test");

    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    ok (flux_send (h[0], msg, 0) == 0,
        "flux_send  works");

    pfd.fd = flux_pollfd (h[1]);
    pfd.events = POLLIN;
    pfd.revents = 0;
    rc = poll (&pfd, 1, 1000); // timeout is in units of ms
    ok (rc == 1,
        "pollfd became ready");
    ok (flux_pollevents (h[1]) == (FLUX_POLLOUT | FLUX_POLLIN),
        "flux_pollevents returns POLLOUT|POLLIN");
    ok ((msg2 = flux_recv (h[1], FLUX_MATCH_ANY, 0)) != NULL,
        "flux_recv works");
    flux_msg_decref (msg2);

    // N.B. we don't own the pollfd so no close here
    flux_msg_destroy (msg);
    flux_close (h[0]);
    flux_close (h[1]);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    int start_fdcount = fdcount ();

    test_basic ();
    test_poll ();

    int end_fdcount = fdcount ();

    ok (start_fdcount == end_fdcount,
        "no file descriptors leaked");
    if (start_fdcount != end_fdcount)
        diag ("test leaked %d file descriptors", end_fdcount - start_fdcount);

    done_testing ();
    return 0;
}

// vi: ts=4 sw=4 expandtab
