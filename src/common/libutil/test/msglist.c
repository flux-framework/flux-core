/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <alloca.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/msglist.h"
#include "src/common/libutil/xzmalloc.h"

int main (int argc, char *argv[])
{
    msglist_t *ml;
    int e;
    char *msg;
    struct pollfd pfd;

    plan (19);

    ok ((ml = msglist_create (free)) != NULL, "msglist_create works");
    ok ((e = msglist_pollevents (ml)) >= 0 && e == POLLOUT,
        "msglist_pollevents on empty msglist returns POLLOUT");
    ok (msglist_append (ml, xstrdup ("foo")) == 0,
        "msglist_append 'foo' works");
    ok ((e = msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "msglist_pollevents on non-empty msglist returns POLLOUT | POLLIN");
    ok (msglist_push (ml, xstrdup ("bar")) == 0, "msglist_push 'bar' works");
    ok ((e = msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "msglist_pollevents still returns POLLOUT | POLLIN");
    ok ((msg = msglist_pop (ml)) != NULL && !strcmp (msg, "bar"),
        "msglist_pop returns 'bar'");
    ok ((e = msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "msglist_pollevents still returns POLLOUT | POLLIN");
    free (msg);

    ok ((msg = msglist_pop (ml)) != NULL && !strcmp (msg, "foo"),
        "msglist_pop returns 'foo'");
    ok ((e = msglist_pollevents (ml)) >= 0 && e == POLLOUT,
        "msglist_pollevents on empty msglist returns POLLOUT");
    // cppcheck-suppress doubleFree
    free (msg);

    ok ((pfd.fd = msglist_pollfd (ml)) >= 0, "msglist_pollfd works");
    pfd.events = POLLIN, pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 1 && pfd.revents == POLLIN,
        "pollfd suggests we read pollevents");
    ok ((e = msglist_pollevents (ml)) >= 0 && e == POLLOUT,
        "msglist_pollevents on empty msglist returns POLLOUT");
    pfd.events = POLLIN, pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 0, "pollfd is no longer ready");
    ok (msglist_push (ml, xstrdup ("foo")) == 0, "msglist_push 'foo' works");
    pfd.events = POLLIN, pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 1 && pfd.revents == POLLIN,
        "pollfd suggests we read pollevents");
    ok ((e = msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "msglist_pollevents on non-empty msglist returns POLLOUT | POLLIN");
    pfd.events = POLLIN, pfd.revents = 0,
    ok (poll (&pfd, 1, 0) == 0, "pollfd is no longer ready");
    ok ((e = msglist_pollevents (ml)) >= 0 && e == (POLLOUT | POLLIN),
        "msglist_pollevents still returns POLLOUT | POLLIN");

    msglist_destroy (ml);

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
