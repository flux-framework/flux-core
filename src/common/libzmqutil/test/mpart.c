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

#include <flux/core.h>
#include <zmq.h>
#include "mpart.h"

#include "src/common/libtap/tap.h"

static void *zctx;

static void zsocketpair (void **sock, const char *uri)
{
    sock[0] = zmq_socket (zctx, ZMQ_PAIR);
    sock[1] = zmq_socket (zctx, ZMQ_PAIR);
    if (!sock[0]
        || !sock[1]
        || zmq_bind (sock[0], uri) < 0
        || zmq_connect (sock[1], uri) < 0)
        BAIL_OUT ("could not create 0MQ socketpair");
}

void test_mpart (void)
{
    void *sock[2];
    zlist_t *mpart_snd;
    zlist_t *mpart_rcv;

    zsocketpair (sock, "inproc://test_mpart");

    mpart_snd = mpart_create ();
    ok (mpart_snd != NULL,
        "mpart_create works");
    ok (mpart_addstr (mpart_snd, "foo") == 0
        && zlist_size (mpart_snd) == 1,
        "mpart_addstr works");
    ok (mpart_addmem (mpart_snd, "bar", 3) == 0
        && zlist_size (mpart_snd) == 2,
        "mpart_addmem works");
    ok (mpart_addmem (mpart_snd, NULL, 0) == 0
        && zlist_size (mpart_snd) == 3,
        "mpart_addmem buf=NULL size=0 works");
    ok (mpart_send (sock[1], mpart_snd) == 0,
        "mpart_send works");
    mpart_rcv = mpart_recv (sock[0]);
    ok (mpart_rcv != NULL,
        "mpart_recv works");
    ok (zlist_size (mpart_rcv) == 3
        && mpart_streq (mpart_rcv, 0, "foo")
        && mpart_streq (mpart_rcv, 1, "bar")
        && zmq_msg_size (mpart_get (mpart_rcv, 2)) == 0,
        "send and recv messages are identical");

    errno = 42;
    mpart_destroy (mpart_snd);
    mpart_destroy (mpart_rcv);
    ok (errno == 42,
        "mpart_destroy doesn't clobber errno");

    zmq_close (sock[0]);
    zmq_close (sock[1]);
}

void test_mpart_inval (void)
{
    zlist_t *mpart;
    void *sock[2];

    zsocketpair (sock, "inproc://test_mpart_inval");

    if (!(mpart = mpart_create ())
        || mpart_addstr (mpart, "x"))
        BAIL_OUT ("mpart_create failed");

    errno = 0;
    ok (mpart_addmem (NULL, NULL, 0) < 0 && errno == EINVAL,
        "mpart_addmem mpart=NULL fails with EINVAL");

    errno = 0;
    ok (mpart_addstr (NULL, "foo") < 0 && errno == EINVAL,
        "mpart_addstr mpart=NULL fails with EINVAL");
    errno = 0;
    ok (mpart_addstr (mpart, NULL) < 0 && errno == EINVAL,
        "mpart_addstr s=NULL fails with EINVAL");

    errno = 0;
    ok (mpart_recv (NULL) == NULL && errno == ENOTSOCK,
        "mpart_recv sock=NULL fails with ENOTSOCK");

    errno = 0;
    ok (mpart_send (NULL, mpart) < 0 && errno == ENOTSOCK,
        "mpart_send sock=NULL fails with ENOTSOCK");

    errno = 0;
    ok (mpart_send (sock[1], NULL) < 0 && errno == EINVAL,
        "mpart_send mpart=NULL fails with EINVAL");

    ok (mpart_get (NULL, 0) == NULL,
        "mpart_get mpart=NULL returns NULL");
    ok (mpart_streq (NULL, 0, "foo") == false,
        "mpart_streq mpart=NULL returns false");

    mpart_destroy (mpart);

    zmq_close (sock[0]);
    zmq_close (sock[1]);

}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    if (!(zctx = zmq_ctx_new ()))
        BAIL_OUT ("could not create zeromq context");

    test_mpart ();
    test_mpart_inval ();

    zmq_ctx_term (zctx);

    done_testing ();
}

// vi:ts=4 sw=4 expandtab
