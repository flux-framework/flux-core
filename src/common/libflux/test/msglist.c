/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* msglist.c - test pollevents/pollfd aspect of flux_msglist
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <poll.h>
#include <flux/core.h>

#include "src/common/libtap/tap.h"


void check_msglist (void)
{
    struct flux_msglist *l;
    flux_msg_t *msg1;
    flux_msg_t *msg2;

    if (!(msg1 = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");
    if (!(msg2 = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");

    ok (flux_msglist_count (NULL) == 0,
        "flux_msglist_count l=NULL is 0");
    l = flux_msglist_create ();
    ok (l != NULL,
        "flux_msglist_create works");
    ok (flux_msglist_count (l) == 0,
        "flux_msglist_count is 0");

    ok (flux_msglist_append (l, msg1) == 0,
        "flux_msglist_append msg1 works");
    ok (flux_msglist_count (l) == 1,
        "flux_msglist_count is 1");
    ok (flux_msglist_first (l) == msg1,
        "flux_msglist_first is msg1");
    ok (flux_msglist_last (l) == msg1,
        "flux_msglist_last is msg1");
    ok (flux_msglist_next (l) == NULL,
        "flux_msglist_next is NULL");

    ok (flux_msglist_append (l, msg2) == 0,
        "flux_msglist_append msg2 works");
    ok (flux_msglist_count (l) == 2,
        "flux_msglist_count is 2");
    ok (flux_msglist_first (l) == msg1,
        "flux_msglist_first is msg1");
    ok (flux_msglist_next (l) == msg2,
        "flux_msglist_next is msg2");
    ok (flux_msglist_last (l) == msg2,
        "flux_msglist_last is msg2");

    ok (flux_msglist_first (l) == msg1,
        "flux_msglist_last is msg1 (assigning curosr to msg1)");
    flux_msglist_delete (l);
    ok (flux_msglist_count (l) == 1,
        "flux_msglist_count is 1 after delete");
    ok (flux_msglist_first (l) == msg2,
        "flux_msglist_first is now msg2");

    flux_msg_decref (msg1);
    flux_msg_decref (msg2);

    flux_msglist_destroy (l);
}


void check_poll (void)
{
    struct flux_msglist *ml;
    int e;
    flux_msg_t *msg;
    const flux_msg_t *tmp;
    struct pollfd pfd;

    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    ok ((ml = flux_msglist_create ()) != NULL,
        "flux_msglist_create works");
    ok ((e = flux_msglist_pollevents (ml)) >= 0 && e == POLLOUT,
        "flux_msglist_pollevents on empty msglist returns POLLOUT");
    ok (flux_msglist_push (ml, msg) == 0,
        "flux_msglist_push works");
    ok ((e = flux_msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "flux_msglist_pollevents on non-empty msglist returns POLLOUT|POLLIN");
    ok (flux_msglist_push (ml, msg) == 0,
        "flux_msglist_push works");
    ok ((e = flux_msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "flux_msglist_pollevents still returns POLLOUT|POLLIN");
    ok ((tmp = flux_msglist_pop (ml)) != NULL,
        "flux_msglist_pop returns a message");
    ok ((e = flux_msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "flux_msglist_pollevents still returns POLLOUT|POLLIN");
    flux_msg_decref (tmp);

    ok ((tmp = flux_msglist_pop (ml)) != NULL,
        "flux_msglist_pop returns a message");
    ok ((e = flux_msglist_pollevents (ml)) >= 0 && e == POLLOUT,
        "flux_msglist_pollevents on empty msglist returns POLLOUT");
    flux_msg_decref (tmp);

    ok ((pfd.fd = flux_msglist_pollfd (ml)) >= 0,
        "flux_msglist_pollfd works");
    pfd.events = POLLIN,
    pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 1 && pfd.revents == POLLIN,
        "flux_msglist_pollfd suggests we read pollevents");
    ok ((e = flux_msglist_pollevents (ml)) >= 0 && e == POLLOUT,
        "flux_msglist_pollevents on empty msglist returns POLLOUT");
    pfd.events = POLLIN,
    pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 0,
        "pollfd is no longer ready");
    ok (flux_msglist_push (ml, msg) == 0,
        "flux_msglist_push works");
    pfd.events = POLLIN,
    pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 1 && pfd.revents == POLLIN,
        "pollfd suggests we read pollevents");
    ok ((e = flux_msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "flux_msglist_pollevents on non-empty msglist returns POLLOUT|POLLIN");
    pfd.events = POLLIN,
    pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 0,
        "pollfd is no longer ready");
    ok ((e = flux_msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "msglist_pollevents still returns POLLOUT|POLLIN");

    flux_msg_decref (msg);
    flux_msglist_destroy (ml);

}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    check_msglist ();
    check_poll ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
