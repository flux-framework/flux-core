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
#include <errno.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"

/* Destructor for malloc'ed string.
 * Set flag so we know this was called when aux was destroyed.
 */
static bool aux_destroyed = false;
static void aux_free (void *arg)
{
    free (arg);
    aux_destroyed = true;
}

static int comms_err (flux_t *h, void *arg)
{
    BAIL_OUT ("fatal comms error: %s", strerror (errno));
    return -1;
}

void test_handle_invalid_args (flux_t *h)
{
    flux_msg_t *msg;

    errno = 0;
    ok (flux_aux_set (NULL, "foo", "bar", NULL) < 0 && errno == EINVAL,
        "flux_aux_set h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_aux_get (NULL, "foo") == NULL && errno == EINVAL,
        "flux_aux_get h=NULL fails with EINVAL");

    errno = 0;
    ok (flux_open (NULL, 0x100000) == NULL && errno == EINVAL,
        "flux_open flags=BOGUS fails with EINVAL");

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        BAIL_OUT ("failed to create message");
    errno = 0;
    ok (flux_send (NULL, msg, 0) < 0 && errno == EINVAL,
       "flux_send h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_send (h, NULL, 0) < 0 && errno == EINVAL,
       "flux_send msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_send (h, msg, 0x100000) < 0 && errno == EINVAL,
       "flux_send flags=BOGUS fails with EINVAL");
    errno = 0;
    ok (flux_recv (NULL, FLUX_MATCH_ANY, 0) == NULL && errno == EINVAL,
       "flux_recv h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_recv (h, FLUX_MATCH_ANY, 0x1000000) == NULL && errno == EINVAL,
       "flux_recv flags=BOGUS fails with EINVAL");
    flux_msg_destroy (msg);
}

static void test_flux_open_ex ()
{
    flux_error_t error;

    ok (flux_open_ex ("foo://foo", 0, &error) == NULL,
        "flux_open_ex with invalid connector name fails");
    is ("Unable to find connector name 'foo'", error.text,
        "flux_open_ex returns expected error in error.text");

    ok (flux_open_ex (NULL, 0x1000000, &error) == NULL,
        "flux_open_ex with invalid flags fails");
    is ("invalid flags specified", error.text,
        "flux_open_ex returns expected error in error.text");

    lives_ok ({flux_open_ex ("foo://foo", 0, NULL);},
        "flux_open_ex doesn't crash if error parameter is NULL");
}

int main (int argc, char *argv[])
{
    flux_t *h;
    char *s;
    flux_msg_t *msg;
    const char *topic;
    uint32_t matchtag;

    plan (NO_PLAN);

    if (!(h = loopback_create (0)))
        BAIL_OUT ("can't continue without loopback handle");

    test_handle_invalid_args (h);

    flux_comms_error_set (h, comms_err, NULL);

    /* Test flux_opt_set, flux_opt_get.
     */
    errno = 0;
    ok (flux_opt_set (h, "nonexistent", NULL, 0) < 0 && errno == EINVAL,
        "flux_opt_set fails with EINVAL on unknown option");
    errno = 0;
    ok (flux_opt_get (h, "nonexistent", NULL, 0) < 0 && errno == EINVAL,
        "flux_opt_get fails with EINVAL on unknown option");

    /* Test flux_aux_get, flux_aux_set
     */
    s = flux_aux_get (h, "handletest::thing1");
    ok (s == NULL,
        "flux_aux_get returns NULL on unknown key");
    flux_aux_set (h, "handletest::thing1", xstrdup ("hello"), aux_free);
    s = flux_aux_get (h, "handletest::thing1");
    ok (s != NULL && !strcmp (s, "hello"),
        "flux_aux_get returns what was set");
    flux_aux_set (h, "handletest::thing1", NULL, NULL);
    ok (aux_destroyed,
        "flux_aux_set key to NULL invokes destructor");
    s = flux_aux_get (h, "handletest::thing1");
    ok (s == NULL,
        "flux_aux_get returns NULL on destroyed key");

    /* Test flux_flags_set, flux_flags_unset, flux_flags_get
     */
    ok (flux_flags_get (h) == 0,
        "flux_flags_get returns flags handle was opened with");
    flux_flags_set (h, (FLUX_O_TRACE | FLUX_O_MATCHDEBUG));
    ok (flux_flags_get (h) == (FLUX_O_TRACE | FLUX_O_MATCHDEBUG),
        "flux_flags_set sets specified flags");
    flux_flags_unset (h, FLUX_O_MATCHDEBUG);
    ok (flux_flags_get (h) == FLUX_O_TRACE,
        "flux_flags_unset clears specified flag without clearing others");
    flux_flags_set (h, FLUX_O_MATCHDEBUG);
    ok (flux_flags_get (h) == (FLUX_O_TRACE | FLUX_O_MATCHDEBUG),
        "flux_flags_set sets specified flag without clearing others");
    flux_flags_set (h, 0);
    ok (flux_flags_get (h) == (FLUX_O_TRACE | FLUX_O_MATCHDEBUG),
        "flux_flags_set (0) has no effect");
    flux_flags_unset (h, 0);
    ok (flux_flags_get (h) == (FLUX_O_TRACE | FLUX_O_MATCHDEBUG),
        "flux_flags_unset (0) has no effect");
    flux_flags_unset (h, ~0);
    ok (flux_flags_get (h) == 0,
        "flux_flags_unset (~0) clears all flags");
    flux_flags_set (h, FLUX_O_RPCTRACK);
    ok (flux_flags_get (h) == 0,
    "flux_flags_set flags=FLUX_O_RPCTRACK has no effect");

    /* Test flux_send, flux_recv, flux_requeue
     * Check flux_pollevents along the way.
     */
    ok (flux_pollevents (h) == FLUX_POLLOUT,
       "flux_pollevents returns only FLUX_POLLOUT on empty queue");
    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("couldn't encode request");
    ok (flux_send (h, msg, 0) == 0,
        "flux_send works");
    flux_msg_destroy (msg);
    ok ((flux_pollevents (h) & FLUX_POLLIN) != 0,
       "flux_pollevents shows FLUX_POLLIN set on non-empty queue");
    ok ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL
        && flux_request_decode (msg, &topic, NULL) == 0
        && !strcmp (topic, "foo"),
        "flux_recv works and sent message was received");
    ok ((flux_pollevents (h) & FLUX_POLLIN) == 0,
       "flux_pollevents shows FLUX_POLLIN clear after queue is emptied");

    /* flux_requeue bad flags */
    errno = 0;
    ok (flux_requeue (h, msg, 0) < 0 && errno == EINVAL,
        "flux_requeue fails with EINVAL if HEAD|TAIL unspecified");
    flux_msg_destroy (msg);

    /* flux_requeue: add foo, bar to HEAD; then receive bar, foo */
    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("couldn't encode request");
    ok (flux_requeue (h, msg, FLUX_RQ_HEAD) == 0,
        "flux_requeue foo HEAD works");
    flux_msg_destroy (msg);
    if (!(msg = flux_request_encode ("bar", NULL)))
        BAIL_OUT ("couldn't encode request");
    ok (flux_requeue (h, msg, FLUX_RQ_HEAD) == 0,
        "flux_requeue bar HEAD works");
    flux_msg_destroy (msg);
    ok ((flux_pollevents (h) & FLUX_POLLIN) != 0,
       "flux_pollevents shows FLUX_POLLIN set after requeue");
    ok ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL
        && flux_request_decode (msg, &topic, NULL) == 0
        && !strcmp (topic, "bar"),
        "flux_recv got bar");
    flux_msg_destroy (msg);
    ok ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL
        && flux_request_decode (msg, &topic, NULL) == 0
        && !strcmp (topic, "foo"),
        "flux_recv got foo");
    flux_msg_destroy (msg);
    ok ((flux_pollevents (h) & FLUX_POLLIN) == 0,
       "flux_pollevents shows FLUX_POLLIN clear after queue is emptied");

    /* flux_requeue: add foo, bar to TAIL; then receive foo, bar */
    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("couldn't encode request");
    ok (flux_requeue (h, msg, FLUX_RQ_TAIL) == 0,
        "flux_requeue foo TAIL works");
    flux_msg_destroy (msg);
    if (!(msg = flux_request_encode ("bar", NULL)))
        BAIL_OUT ("couldn't encode request");
    ok (flux_requeue (h, msg, FLUX_RQ_TAIL) == 0,
        "flux_requeue bar TAIL works");
    flux_msg_destroy (msg);
    ok ((flux_pollevents (h) & FLUX_POLLIN) != 0,
       "flux_pollevents shows FLUX_POLLIN set after requeue");
    ok ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL
        && flux_request_decode (msg, &topic, NULL) == 0
        && !strcmp (topic, "foo"),
        "flux_recv got foo");
    flux_msg_destroy (msg);
    ok ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL
        && flux_request_decode (msg, &topic, NULL) == 0
        && !strcmp (topic, "bar"),
        "flux_recv got bar");
    flux_msg_destroy (msg);
    ok ((flux_pollevents (h) & FLUX_POLLIN) == 0,
       "flux_pollevents shows FLUX_POLLIN clear after queue is emptied");

    /* matchtags */
    matchtag = flux_matchtag_alloc (h);
    ok (matchtag != FLUX_MATCHTAG_NONE,
        "flux_matchtag_alloc works");
    flux_matchtag_free (h, matchtag);

    /* reconnect */
    errno = 0;
    ok (flux_reconnect (NULL) < 0 && errno == EINVAL,
        "flux_reconnect h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_reconnect (h) < 0 && errno == ENOSYS,
        "flux_reconnect with null reconnect method fails with ENOSYS");

    flux_close (h);

    /* flux_open_ex() */
    test_flux_open_ex ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

