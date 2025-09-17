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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

static int fdcount (void)
{
    int fd, fdlimit = sysconf (_SC_OPEN_MAX);
    int count = 0;
    for (fd = 0; fd < fdlimit; fd++) {
        if (fcntl (fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

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

void test_handle_invalid_args (void)
{
    flux_t *h;
    flux_msg_t *msg;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("can't continue without loop handle");
    flux_comms_error_set (h, comms_err, NULL);

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
    ok (flux_send_new (NULL, &msg, 0) < 0 && errno == EINVAL,
       "flux_send_new h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_send_new (h, NULL, 0) < 0 && errno == EINVAL,
       "flux_send_new msg=NULL fails with EINVAL");
    errno = 0;
    flux_msg_t *nullmsg = NULL;
    ok (flux_send_new (h, &nullmsg, 0) < 0 && errno == EINVAL,
       "flux_send_new *msg=NULL fails with EINVAL");
    ok (flux_send_new (h, &msg, 0x100000) < 0 && errno == EINVAL,
       "flux_send_new flags=BOGUS fails with EINVAL");

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

    flux_close (h);
}

static void test_flux_open_ex (void)
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

static void test_send_new (void)
{
    flux_msg_t *msg;
    flux_msg_t *msg2;
    void *msgptr;
    flux_t *h1;
    flux_t *h2;
    int type;

    if (!(h1 = flux_open ("interthread://xyz", 0)))
        BAIL_OUT ("can't continue without interthread pair");
    if (!(h2 = flux_open ("interthread://xyz", 0)))
        BAIL_OUT ("can't continue without interthread pair");

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        BAIL_OUT ("could not create message");
    if (flux_msg_aux_set (msg, "foo", "bar", NULL) < 0)
        BAIL_OUT ("could not set message aux item");
    msgptr = msg;

    flux_msg_incref (msg);
    errno = 0;
    ok (flux_send_new (h1, &msg, 0) < 0 && errno == EINVAL,
        "flux_send_new fails if message refcount > 1");
    flux_msg_decref (msg);

    ok (flux_send_new (h1, &msg, 0) == 0,
        "flux_send_new works");
    ok (msg == NULL,
        "msg was set to NULL after send");

    ok ((msg2 = flux_recv (h2, FLUX_MATCH_ANY, 0)) != NULL,
        "flux_recv got the message");
    ok (msg2 == msgptr,
        "received message was not copied");
    ok (flux_msg_get_type (msg2, &type) == 0 && type == FLUX_MSGTYPE_EVENT,
        "and message is the correct type");
    ok (flux_msg_aux_get (msg2, "foo") == NULL,
        "aux item in sent message is cleared in received message");
    flux_msg_destroy (msg2);

    flux_close (h1);
    flux_close (h2);
}

void test_basic (void)
{
    flux_t *h;
    char *s;
    flux_msg_t *msg;
    flux_msg_t *msg2;
    const char *topic;
    uint32_t matchtag;

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("can't continue without loop handle");

    flux_comms_error_set (h, comms_err, NULL);

    /* Test flux_opt_set, flux_opt_get.
     */
    errno = 0;
    uint32_t uid = 999;
    ok (flux_opt_set (NULL, FLUX_OPT_TESTING_USERID, &uid, sizeof (uid)) < 0
        && errno == EINVAL,
        "flux_opt_set h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_opt_set (h, NULL, &uid, sizeof (uid)) < 0
        && errno == EINVAL,
        "flux_opt_set option=NULL fails with EINVAL");
    errno = 0;
    ok (flux_opt_set (h, FLUX_OPT_TESTING_USERID, NULL, sizeof (uid)) < 0
        && errno == EINVAL,
        "flux_opt_set option=testing_userid val=NULL fails with EINVAL");
    errno = 0;
    ok (flux_opt_set (h, FLUX_OPT_TESTING_USERID, &uid, sizeof (uid)+1) < 0
        && errno == EINVAL,
        "flux_opt_set option=testing_userid size=wrong fails with EINVAL");
    errno = 0;
    ok (flux_opt_set (h, "nonexistent", NULL, 0) < 0 && errno == EINVAL,
        "flux_opt_set fails with EINVAL on unknown option");

    errno = 0;
    ok (flux_opt_get (NULL, FLUX_OPT_TESTING_USERID, &uid, sizeof (uid)) < 0
        && errno == EINVAL,
        "flux_opt_get h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_opt_get (h, NULL, &uid, sizeof (uid)) < 0
        && errno == EINVAL,
        "flux_opt_get option=NULL fails with EINVAL");
    errno = 0;
    ok (flux_opt_get (h, FLUX_OPT_TESTING_USERID, NULL, sizeof (uid)) < 0
        && errno == EINVAL,
        "flux_opt_get option=testing_userid val=NULL fails with EINVAL");
    errno = 0;
    ok (flux_opt_get (h, FLUX_OPT_TESTING_USERID, &uid, sizeof (uid)+1) < 0
        && errno == EINVAL,
        "flux_opt_get option=testing_userid size=wrong fails with EINVAL");

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
    ok (s != NULL && streq (s, "hello"),
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
        && streq (topic, "foo"),
        "flux_recv works and sent message was received");
    ok ((flux_pollevents (h) & FLUX_POLLIN) == 0,
       "flux_pollevents shows FLUX_POLLIN clear after queue is emptied");

    /* flux_requeue bad args */
    errno = 0;
    ok (flux_requeue (NULL, msg, FLUX_RQ_HEAD) < 0 && errno == EINVAL,
        "flux_requeue h=NULL fails with EINVAL");
    errno = 0;
    ok (flux_requeue (h, NULL, FLUX_RQ_HEAD) < 0 && errno == EINVAL,
        "flux_requeue msg=NULL fails with EINVAL");
    errno = 0;
    ok (flux_requeue (h, msg, 0) < 0 && errno == EINVAL,
        "flux_requeue fails with EINVAL if HEAD|TAIL unspecified");
    flux_msg_destroy (msg);

    /* requeue preserves aux container (kvs needs this) */
    if (!(msg = flux_request_encode ("foo", NULL)))
        BAIL_OUT ("couldn't encode request");
    if (flux_msg_aux_set (msg, "fubar", "xyz", NULL) < 0)
        BAIL_OUT ("couldn't attach something to message aux container");
    ok (flux_requeue (h, msg, FLUX_RQ_HEAD) == 0,
        "flux_requeue works");
    msg2 = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (msg2 == msg,
        "flux_recv returned requeued message and it has the same address");
    ok (flux_msg_aux_get (msg2, "fubar") != NULL,
        "and aux was preserved");
    flux_msg_decref (msg);
    flux_msg_decref (msg2);

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
        && streq (topic, "bar"),
        "flux_recv got bar");
    flux_msg_destroy (msg);
    ok ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL
        && flux_request_decode (msg, &topic, NULL) == 0
        && streq (topic, "foo"),
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
        && streq (topic, "foo"),
        "flux_recv got foo");
    flux_msg_destroy (msg);
    ok ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL
        && flux_request_decode (msg, &topic, NULL) == 0
        && streq (topic, "bar"),
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
}

int main (int argc, char *argv[])
{
    int fd_before = fdcount ();
    plan (NO_PLAN);

    test_basic ();
    test_handle_invalid_args ();
    test_flux_open_ex ();
    test_send_new ();

    int fd_after = fdcount ();
    ok (fd_after == fd_before,
        "no file descriptors were leaked");
    if (fd_after != fd_before)
        diag ("leaked %d file descriptors", fd_after - fd_before);

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

