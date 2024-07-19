/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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
#include <stdbool.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"

#include "msg_deque.h"


void check_queue (void)
{
    struct msg_deque *q;
    flux_msg_t *msg1;
    flux_msg_t *msg2;
    flux_msg_t *msg;

    if (!(msg1 = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");
    if (!(msg2 = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");

    q = msg_deque_create (0);
    ok (q != NULL,
        "msg_deque_create works");
    ok (msg_deque_empty (q) == true,
        "msg_deque_empty is true");
    ok (msg_deque_count (q) == 0,
        "msg_deque_count = 0");
    ok (msg_deque_push_back (q, msg1) == 0,
        "msg_deque_push_back msg1 works");
    ok (msg_deque_empty (q) == false,
        "msg_deque_empty is false");
    ok (msg_deque_count (q) == 1,
        "msg_deque_count = 1");
    ok (msg_deque_push_back (q, msg2) == 0,
        "msg_deque_push_back msg2 works");
    ok (msg_deque_empty (q) == false,
        "msg_deque_empty is false");
    ok (msg_deque_count (q) == 2,
        "msg_deque_count = 2");
    ok ((msg = msg_deque_pop_front (q)) == msg1,
        "msg_deque_pop_front popped msg1");
    flux_msg_destroy (msg);
    ok (msg_deque_count (q) == 1,
        "msg_deque_count = 1");
    ok (msg_deque_empty (q) == false,
        "msg_deque_empty is false");
    ok ((msg = msg_deque_pop_front (q)) == msg2,
        "msg_deque_pop_front popped msg2");
    flux_msg_destroy (msg);
    ok (msg_deque_empty (q) == true,
        "msg_deque_empty is true");
    ok (msg_deque_count (q) == 0,
        "msg_deque_count = 0");
    ok (msg_deque_pop_front (q) == NULL,
        "msg_deque_pop_front returned NULL");

    /* Now use push_front and verify messages are popped in opposite order */
    if (!(msg1 = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");
    if (!(msg2 = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");
    ok (msg_deque_empty (q) == true,
        "msg_deque_empty is true");
    ok (msg_deque_push_front (q, msg1) == 0,
        "msg_deque_push_front msg1 works");
    ok (msg_deque_empty (q) == false,
        "msg_deque_empty is false");
    ok (msg_deque_push_front (q, msg2) == 0,
        "msg_deque_push_front msg2 works");
    ok (msg_deque_empty (q) == false,
        "msg_deque_empty is false");
    ok ((msg = msg_deque_pop_front (q)) == msg2,
        "msg_deque_pop_front popped msg2");
    flux_msg_destroy (msg);
    ok (msg_deque_empty (q) == false,
        "msg_deque_empty is false");
    ok ((msg = msg_deque_pop_front (q)) == msg1,
        "msg_deque_pop_front popped msg1");
    flux_msg_destroy (msg);
    ok (msg_deque_empty (q) == true,
        "msg_deque_empty is true");

    msg_deque_destroy (q);
}

void check_poll (void)
{
    struct msg_deque *q;
    flux_msg_t *msg1;
    flux_msg_t *msg2;
    flux_msg_t *msg;
    struct pollfd pfd;

    if (!(msg1 = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    if (!(msg2 = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    ok ((q = msg_deque_create (0)) != NULL,
        "msg_deque_create works");
    ok (msg_deque_pollevents (q) == POLLOUT,
        "msg_deque_pollevents on empty queue returns POLLOUT");
    ok (msg_deque_push_back (q, msg1) == 0,
        "msg_deque_push_back msg1 works");
    ok (msg_deque_pollevents (q) == (POLLOUT | POLLIN),
        "msg_deque_pollevents on non-empty queue returns POLLOUT|POLLIN");
    ok (msg_deque_push_back (q, msg2) == 0,
        "msg_deque_push_back msg2 works");
    ok (msg_deque_pollevents (q) == (POLLOUT | POLLIN),
        "msg_deque_pollevents still returns POLLOUT|POLLIN");
    ok ((msg = msg_deque_pop_front (q)) != NULL,
        "msg_deque_pop_front returns a message");
    flux_msg_decref (msg);
    ok (msg_deque_pollevents (q) == (POLLOUT | POLLIN),
        "msg_deque_pollevents still returns POLLOUT|POLLIN");

    ok ((msg = msg_deque_pop_front (q)) != NULL,
        "msg_deque_pop_front returns a message");
    flux_msg_decref (msg);
    ok (msg_deque_pollevents (q) == POLLOUT,
        "msg_deque_pollevents on empty queue returns POLLOUT");

    /* now test pollfd */
    if (!(msg1 = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    ok ((pfd.fd = msg_deque_pollfd (q)) >= 0,
        "msg_deque_pollfd works");
    pfd.events = POLLIN,
    pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 1 && pfd.revents == POLLIN,
        "msg_deque_pollfd suggests we read pollevents");
    ok (msg_deque_pollevents (q) == POLLOUT,
        "msg_deque_pollevents on empty queue returns POLLOUT");
    pfd.events = POLLIN,
    pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 0,
        "pollfd is no longer ready");
    ok (msg_deque_push_back (q, msg1) == 0,
        "msg_deque_push_back works");
    pfd.events = POLLIN,
    pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 1 && pfd.revents == POLLIN,
        "pollfd suggests we read pollevents");
    ok (msg_deque_pollevents (q) == (POLLOUT | POLLIN),
        "msg_deque_pollevents on non-empty queue returns POLLOUT|POLLIN");
    pfd.events = POLLIN,
    pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 0,
        "pollfd is no longer ready");
    ok (msg_deque_pollevents (q) == (POLLOUT | POLLIN),
        "msg_deque_pollevents still returns POLLOUT|POLLIN");

    msg_deque_destroy (q);
}

void check_single_thread (void)
{
    struct msg_deque *q;
    flux_msg_t *msg1;
    flux_msg_t *msg;

    if (!(msg1 = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");

    q = msg_deque_create (MSG_DEQUE_SINGLE_THREAD);
    ok (q != NULL,
        "msg_deque_create flags=SINGLE_THREAD works");
    flux_msg_incref (msg1);
    ok (msg_deque_push_back (q, msg1) == 0,
        "msg_deque_push_back msg1 works with refcount==2");
    flux_msg_decref (msg1);
    ok ((msg = msg_deque_pop_front (q)) == msg1,
        "msg_deque_pop_front popped msg1");
    flux_msg_destroy (msg);

    msg_deque_destroy (q);
}

void check_inval (void)
{
    struct msg_deque *q;
    flux_msg_t *msg1;
    flux_msg_t *msg;

    if (!(q = msg_deque_create (0)))
        BAIL_OUT ("could not create msg_deque");
    if (!(msg1 = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    errno = 0;
    ok (msg_deque_create (0x1000) == NULL && errno == EINVAL,
        "msg_deque_create flags=0x1000 fails with EINVAL");

    ok (msg_deque_empty (NULL) == true,
        "msg_deque_empty q=NULL is true");
    errno = 42;
    lives_ok ({msg_deque_destroy (NULL);},
        "msg_deque_destroy q=NULL doesn't crash");
    ok (errno == 42,
        "msg_deque_destroy doesn't clobber errno");

    ok (msg_deque_count (NULL) == 0,
        "msg_deque_count q=NULL is 0");

    // msg_deque_push_back
    errno = 0;
    ok (msg_deque_push_back (NULL, msg1) < 0 && errno == EINVAL,
        "msg_deque_push_back q=NULL fails with EINVAL");
    errno = 0;
    ok (msg_deque_push_back (q, NULL) < 0 && errno == EINVAL,
        "msg_deque_push_back msg=NULL fails with EINVAL");
    flux_msg_incref (msg1);
    errno = 0;
    ok (msg_deque_push_back (q, msg1) < 0 && errno == EINVAL,
        "msg_deque_push_back msg with ref=2 fails with EINVAL");
    flux_msg_decref (msg1);

    // msg_deque_push_front
    errno = 0;
    ok (msg_deque_push_front (NULL, msg1) < 0 && errno == EINVAL,
        "msg_deque_push_front q=NULL fails with EINVAL");
    errno = 0;
    ok (msg_deque_push_front (q, NULL) < 0 && errno == EINVAL,
        "msg_deque_push_front msg=NULL fails with EINVAL");
    errno = 0;
    msg = NULL;
    ok (msg_deque_push_front (q, msg) < 0 && errno == EINVAL,
        "msg_deque_push_front *msg=NULL fails with EINVAL");
    flux_msg_incref (msg1);
    errno = 0;
    ok (msg_deque_push_front (q, msg1) < 0 && errno == EINVAL,
        "msg_deque_push_front msg with ref=2 fails with EINVAL");
    flux_msg_decref (msg1);

    ok (msg_deque_pop_front (NULL) == NULL,
        "msg_deque_pop_front q=NULL returns NULL");
    ok (msg_deque_empty (NULL) == true,
        "msg_deque_empty q=NULL returns true");
    errno = 0;
    ok (msg_deque_pollfd (NULL) < 0 && errno == EINVAL,
        "msg_deque_pollfd q=NULL fails with EINVAL");
    errno = 0;
    ok (msg_deque_pollevents (NULL) < 0 && errno == EINVAL,
        "msg_deque_pollevents q=NULL fails with EINVAL");

    msg = msg1;
    ok (msg_deque_push_back (q, msg1) == 0,
        "msg_deque_push_back msg1 works");
    errno = 0;
    ok (msg_deque_push_back (q, msg) < 0 && errno == EINVAL,
        "msg_deque_push_back msg1 again fails with EINVAL");
    // this test ends with msg1 owned by q

    msg_deque_destroy (q);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_queue ();
    check_poll ();
    check_inval ();
    check_single_thread ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
