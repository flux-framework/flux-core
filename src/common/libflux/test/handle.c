/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <errno.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"
#include "util.h"

/* Fake handle flags for testing flux_flags_get/set/unset
 */
enum {
    FAKE_FLAG1 = 0x10000000,
    FAKE_FLAG2 = 0x20000000,
    FAKE_FLAG3 = 0x30000000,
};

/* Destructor for malloc'ed string.
 * Set flag so we no this was called when aux was destroyed.
 */
static bool aux_destroyed = false;
static void aux_free (void *arg)
{
    free (arg);
    aux_destroyed = true;
}

/* First time this is called, don't BAIL_OUT, just set fatal_tested so
 * we can verify that flux_fatal_set() sets the hook.
 * After that, abort the test if the handle suffers a fatality.
 */
static bool fatal_tested = false;
static void fatal_err (const char *message, void *arg)
{
    if (fatal_tested)
        BAIL_OUT ("fatal error: %s", message);
    else
        fatal_tested = true;
}

void test_handle_invalid_args (void)
{
    errno = 0;
    ok (flux_aux_set (NULL, "foo", "bar", NULL) < 0 && errno == EINVAL,
        "flux_aux_set h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_aux_get (NULL, "foo") == NULL && errno == EINVAL,
        "flux_aux_get h=NULL fails with EINVAL");
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

    test_handle_invalid_args ();

    /* Test flux_fatal_set, flux_fatal_err
     */
    flux_fatal_set (h, fatal_err, NULL);
    flux_fatal_error (h, __FUNCTION__, "Foo");
    ok (fatal_tested == true,
        "flux_fatal function is called on fatal error");

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
    flux_flags_set (h, (FAKE_FLAG1 | FAKE_FLAG2));
    ok (flux_flags_get (h) == (FAKE_FLAG1 | FAKE_FLAG2),
        "flux_flags_set sets specified flags");
    flux_flags_unset (h, FAKE_FLAG1);
    ok (flux_flags_get (h) == FAKE_FLAG2,
        "flux_flags_unset clears specified flag without clearing others");
    flux_flags_set (h, FAKE_FLAG1);
    ok (flux_flags_get (h) == (FAKE_FLAG1 | FAKE_FLAG2),
        "flux_flags_set sets specified flag without clearing others");
    flux_flags_set (h, 0);
    ok (flux_flags_get (h) == (FAKE_FLAG1 | FAKE_FLAG2),
        "flux_flags_set (0) has no effect");
    flux_flags_unset (h, 0);
    ok (flux_flags_get (h) == (FAKE_FLAG1 | FAKE_FLAG2),
        "flux_flags_unset (0) has no effect");
    flux_flags_unset (h, ~0);
    ok (flux_flags_get (h) == 0,
        "flux_flags_unset (~0) clears all flags");

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

    /* flux_requeue_nocopy bad flags */
    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("couldn't encode request");
    errno = 0;
    ok (flux_requeue_nocopy (h, msg, 0) < 0 && errno == EINVAL,
        "flux_requeue_nocopy fails with EINVAL if HEAD|TAIL unspecified");
    flux_msg_destroy (msg);

    /* flux_requeue_nocopy: add foo, bar to HEAD; then receive bar, foo */
    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("couldn't encode request");
    ok (flux_requeue_nocopy (h, msg, FLUX_RQ_HEAD) == 0,
        "flux_requeue_nocopy foo HEAD works");
    if (!(msg = flux_request_encode ("bar", NULL)))
        BAIL_OUT ("couldn't encode request");
    ok (flux_requeue_nocopy (h, msg, FLUX_RQ_HEAD) == 0,
        "flux_requeue_nocopy bar HEAD works");
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

    /* flux_requeue_nocopy: add foo, bar to TAIL; then receive foo, bar */
    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("couldn't encode request");
    ok (flux_requeue_nocopy (h, msg, FLUX_RQ_TAIL) == 0,
        "flux_requeue_nocopy foo TAIL works");
    if (!(msg = flux_request_encode ("bar", NULL)))
        BAIL_OUT ("couldn't encode request");
    ok (flux_requeue_nocopy (h, msg, FLUX_RQ_TAIL) == 0,
        "flux_requeue_nocopy bar TAIL works");
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
    matchtag = flux_matchtag_alloc (h, 0);
    ok (matchtag != FLUX_MATCHTAG_NONE,
        "flux_matchtag_alloc (regular) works");
    ok (flux_matchtag_group (matchtag) == false,
        "matchtag is regular type");
    flux_matchtag_free (h, matchtag);

    matchtag = flux_matchtag_alloc (h, FLUX_MATCHTAG_GROUP);
    ok (matchtag != FLUX_MATCHTAG_NONE,
        "flux_matchtag_alloc (group) works");
    ok (flux_matchtag_group (matchtag) == true,
        "matchtag is group type");
    flux_matchtag_free (h, matchtag);

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

