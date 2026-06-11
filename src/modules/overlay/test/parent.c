/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <string.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

#include "parent.h"

void test_create_destroy (void)
{
    flux_t *h;
    struct parent *parent;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    parent = parent_create (h, 1);
    ok (parent != NULL,
        "parent_create rank=1 works");
    if (!parent)
        BAIL_OUT ("parent_create failed, cannot continue test");
    ok (parent->h == h,
        "parent has flux handle reference");
    ok (parent->rank == 1,
        "parent rank is 1");
    ok (parent->zsock == NULL,
        "parent zsock is initially NULL");
    ok (parent->uri == NULL,
        "parent uri is initially NULL");
    ok (parent->pubkey == NULL,
        "parent pubkey is initially NULL");
    ok (parent->hello_error == false,
        "parent hello_error is initially false");
    ok (parent->hello_responded == false,
        "parent hello_responded is initially false");
    ok (parent->offline == false,
        "parent offline is initially false");
    ok (parent->goodbye_sent == false,
        "parent goodbye_sent is initially false");

    parent_destroy (parent);
    pass ("parent_destroy doesn't crash");

    flux_close (h);
}

void test_invalid (void)
{
    struct parent *parent;

    lives_ok ({parent_destroy (NULL);},
              "parent_destroy parent=NULL doesn't crash");

    parent = parent_create (NULL, 1);
    ok (parent != NULL,
        "parent_create h=NULL succeeds (no validation)");
    if (!parent)
        BAIL_OUT ("parent_create unexpectedly failed");
    parent_destroy (parent);

    ok (parent_error (NULL) == false,
        "parent_error parent=NULL returns false");
}

void test_set_uri (void)
{
    flux_t *h;
    struct parent *parent;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    if (!(parent = parent_create (h, 1)))
        BAIL_OUT ("parent_create failed, cannot continue test");

    ok (parent_set_uri (parent, "ipc:///tmp/test") == 0,
        "parent_set_uri works");
    ok (parent->uri != NULL && streq (parent->uri, "ipc:///tmp/test"),
        "parent uri was set correctly");

    ok (parent_set_uri (parent, "tcp://localhost:5555") == 0,
        "parent_set_uri can replace uri");
    ok (streq (parent->uri, "tcp://localhost:5555"),
        "parent uri was updated");

    errno = 0;
    ok (parent_set_uri (NULL, "ipc:///tmp/test") < 0 && errno == EINVAL,
        "parent_set_uri parent=NULL fails with EINVAL");

    errno = 0;
    ok (parent_set_uri (parent, NULL) < 0 && errno == EINVAL,
        "parent_set_uri uri=NULL fails with EINVAL");

    parent_destroy (parent);
    flux_close (h);
}

void test_set_pubkey (void)
{
    flux_t *h;
    struct parent *parent;
    const char *key1 = "abcdefghijklmnopqrstuvwxyz0123456789ab";
    const char *key2 = "1234567890abcdefghijklmnopqrstuvwxyz12";

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    if (!(parent = parent_create (h, 1)))
        BAIL_OUT ("parent_create failed, cannot continue test");

    ok (parent_set_pubkey (parent, key1) == 0,
        "parent_set_pubkey works");
    ok (parent->pubkey != NULL && streq (parent->pubkey, key1),
        "parent pubkey was set correctly");

    ok (parent_set_pubkey (parent, key2) == 0,
        "parent_set_pubkey can replace pubkey");
    ok (streq (parent->pubkey, key2),
        "parent pubkey was updated");

    errno = 0;
    ok (parent_set_pubkey (NULL, key1) < 0 && errno == EINVAL,
        "parent_set_pubkey parent=NULL fails with EINVAL");

    errno = 0;
    ok (parent_set_pubkey (parent, NULL) < 0 && errno == EINVAL,
        "parent_set_pubkey pubkey=NULL fails with EINVAL");

    parent_destroy (parent);
    flux_close (h);
}

void test_error (void)
{
    flux_t *h;
    struct parent *parent;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    if (!(parent = parent_create (h, 1)))
        BAIL_OUT ("parent_create failed, cannot continue test");

    ok (parent_error (parent) == false,
        "parent_error returns false initially");

    parent->hello_error = true;
    ok (parent_error (parent) == false,
        "parent_error returns false when hello_error=true but not hello_responded");

    parent->hello_responded = true;
    ok (parent_error (parent) == true,
        "parent_error returns true when hello_error and hello_responded");

    parent->hello_error = false;
    parent->hello_responded = false;
    parent->offline = true;
    ok (parent_error (parent) == true,
        "parent_error returns true when offline=true");

    parent->hello_error = true;
    parent->hello_responded = true;
    ok (parent_error (parent) == true,
        "parent_error returns true when both offline and hello_error set");

    parent_destroy (parent);
    flux_close (h);
}

void test_sendmsg_checks (void)
{
    flux_t *h;
    struct parent *parent;
    flux_msg_t *msg;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    if (!(parent = parent_create (h, 1)))
        BAIL_OUT ("parent_create failed, cannot continue test");

    if (!(msg = flux_request_encode ("test.request", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    errno = 0;
    ok (parent_sendmsg (NULL, msg) < 0 && errno == EHOSTUNREACH,
        "parent_sendmsg parent=NULL fails with EHOSTUNREACH");

    errno = 0;
    ok (parent_sendmsg (parent, msg) < 0 && errno == EHOSTUNREACH,
        "parent_sendmsg without zsock fails with EHOSTUNREACH");

    parent->offline = true;
    errno = 0;
    ok (parent_sendmsg (parent, msg) < 0 && errno == EHOSTUNREACH,
        "parent_sendmsg when offline fails with EHOSTUNREACH");

    parent->offline = false;
    parent->goodbye_sent = true;
    errno = 0;
    ok (parent_sendmsg (parent, msg) < 0 && errno == EHOSTUNREACH,
        "parent_sendmsg when goodbye_sent fails with EHOSTUNREACH");

    flux_msg_decref (msg);
    parent_destroy (parent);
    flux_close (h);
}

void test_recvmsg_checks (void)
{
    flux_t *h;
    struct parent *parent;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    if (!(parent = parent_create (h, 1)))
        BAIL_OUT ("parent_create failed, cannot continue test");

    errno = 0;
    ok (parent_recvmsg (NULL) == NULL && errno == EINVAL,
        "parent_recvmsg parent=NULL fails with EINVAL");

    parent_destroy (parent);
    flux_close (h);
}

void test_connect_disconnect (void)
{
    flux_t *h;
    struct parent *parent;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    if (!(parent = parent_create (h, 1)))
        BAIL_OUT ("parent_create failed, cannot continue test");

    errno = 0;
    ok (parent_connect (NULL, NULL, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "parent_connect parent=NULL fails with EINVAL");

    lives_ok ({parent_disconnect (NULL);},
              "parent_disconnect parent=NULL doesn't crash");

    parent_destroy (parent);
    flux_close (h);
}

void test_watch_checks (void)
{
    flux_t *h;
    struct parent *parent;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");

    if (!(parent = parent_create (h, 1)))
        BAIL_OUT ("parent_create failed, cannot continue test");

    errno = 0;
    ok (parent_watch (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "parent_watch parent=NULL fails with EINVAL");

    parent_destroy (parent);
    flux_close (h);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_create_destroy ();
    test_invalid ();
    test_set_uri ();
    test_set_pubkey ();
    test_error ();
    test_sendmsg_checks ();
    test_recvmsg_checks ();
    test_connect_disconnect ();
    test_watch_checks ();

    done_testing ();
}

// vi: ts=4 sw=4 expandtab
