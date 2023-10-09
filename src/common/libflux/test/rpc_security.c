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
#include <inttypes.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

static int cred_get (flux_t *h, struct flux_msg_cred *cr)
{
    if (flux_opt_get (h, FLUX_OPT_TESTING_USERID,
                       &cr->userid, sizeof (cr->userid)) < 0)
        return -1;
    if (flux_opt_get (h, FLUX_OPT_TESTING_ROLEMASK,
                       &cr->rolemask, sizeof (cr->rolemask)) < 0)
        return -1;
    return 0;
}

static int cred_set (flux_t *h, struct flux_msg_cred *cr)
{
    if (flux_opt_set (h, FLUX_OPT_TESTING_USERID,
                       &cr->userid, sizeof (cr->userid)) < 0)
        return -1;
    if (flux_opt_set (h, FLUX_OPT_TESTING_ROLEMASK,
                       &cr->rolemask, sizeof (cr->rolemask)) < 0)
        return -1;
    return 0;
}

static void check_rpc_oneway (flux_t *h)
{
    flux_future_t *f = NULL;
    flux_msg_t *msg = NULL;
    struct flux_msg_cred cr;

    f = flux_rpc (h, "testrpc0", NULL, FLUX_NODEID_ANY, FLUX_RPC_NORESPONSE);
    ok (f != NULL,
        "sent request");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    flux_future_destroy (f);

    msg = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "received looped back request");
    ok (flux_msg_get_cred (msg, &cr) == 0
        && cr.userid == getuid ()
        && cr.rolemask == FLUX_ROLE_OWNER,
        "request contains userid=UID, rolemask=OWNER");
    flux_msg_destroy (msg);
}

static void check_rpc_oneway_faked (flux_t *h)
{
    flux_future_t *f = NULL;
    flux_msg_t *msg = NULL;
    struct flux_msg_cred saved, new, cr;

    ok (cred_get (h, &saved) == 0
        && saved.userid == getuid() && saved.rolemask == FLUX_ROLE_OWNER,
        "saved connector creds, with expected values");

    new.userid = 9999;
    new.rolemask = 0x80000000;
    ok (cred_set (h, &new) == 0 && cred_get (h, &cr) == 0
        && cr.userid == new.userid && cr.rolemask == new.rolemask,
       "set userid/rolemask to test values");

    f = flux_rpc (h, "testrpc1", NULL, FLUX_NODEID_ANY, FLUX_RPC_NORESPONSE);
    ok (f != NULL,
        "sent request");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    flux_future_destroy (f);

    msg = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "received looped back request");
    ok (flux_msg_get_cred (msg, &cr) == 0
        && cr.userid == new.userid
        && cr.rolemask == new.rolemask,
        "request contains test userid and rolemask");
    flux_msg_destroy (msg);

    ok (cred_set (h, &saved) == 0,
        "restored connector creds");
}

static bool testrpc1_called;
static void testrpc1 (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    diag ("testrpc1 handler invoked");
    testrpc1_called = true;
    if (flux_respond (h, msg, NULL) < 0)
        diag ("flux_respond: %s", flux_strerror (errno));
}

flux_msg_handler_t *testrpc1_handler_create (flux_t *h)
{
    struct flux_match match = FLUX_MATCH_REQUEST;
    flux_msg_handler_t *w;
    match.topic_glob = "testrpc1";

    if (!(w = flux_msg_handler_create (h, match, testrpc1, NULL)))
        return NULL;
    flux_msg_handler_start (w);
    return w;
}

static void check_rpc_default_policy (flux_t *h)
{
    flux_future_t *f;
    flux_msg_handler_t *mh;
    struct flux_msg_cred saved, new, cr;
    int rc;

    ok ((mh = testrpc1_handler_create (h)) != NULL,
        "created message handler with default policy");
    if (mh == NULL)
        BAIL_OUT ("flux_msg_handler_create: %s", flux_strerror (errno));

    /* This should be a no-op since "deny all" can't deny FLUX_ROLE_OWNER,
     * and the default policy is to require FLUX_ROLE_OWNER.
     */
    flux_msg_handler_deny_rolemask (mh, FLUX_ROLE_ALL);


    /* Attempt with default creds.
     */
    testrpc1_called = false;
    ok ((f = flux_rpc (h, "testrpc1", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "default-creds: sent request to message handler");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE);
    ok (rc >= 0,
        "default-creds: reactor successfully handled one event");
    ok (testrpc1_called == true
        && flux_future_get (f, NULL) == 0,
        "default-creds: handler was called and returned success response");
    flux_future_destroy (f);

    /* Attempt with non-owner creds
     */
    ok (cred_get (h, &saved) == 0
        && saved.userid == getuid() && saved.rolemask == FLUX_ROLE_OWNER,
        "saved connector creds, with expected values");
    new.userid = 9999;
    new.rolemask = 0x80000000;
    ok (cred_set (h, &new) == 0 && cred_get (h, &cr) == 0
        && cr.userid == new.userid && cr.rolemask == new.rolemask,
       "set userid/rolemask to non-owner test values");
    testrpc1_called = false;
    ok ((f = flux_rpc (h, "testrpc1", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "random-creds: sent request to message handler");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE);
    ok (rc >= 0,
        "random-creds: reactor successfully handled one event");
    errno = 0;
    ok (testrpc1_called == false
        && flux_future_get (f, NULL) == -1 && errno == EPERM,
        "random-creds: handler was NOT called and dispatcher returned EPERM response");
    flux_future_destroy (f);
    ok (cred_set (h, &saved) == 0,
        "restored connector creds");

    flux_msg_handler_destroy (mh);
}

static void check_rpc_open_policy (flux_t *h)
{
    flux_future_t *f;
    flux_msg_handler_t *mh;
    struct flux_msg_cred saved, new, cr;
    int rc;

    ok ((mh = testrpc1_handler_create (h)) != NULL,
        "created message handler with open policy");
    if (mh == NULL)
        BAIL_OUT ("flux_msg_handler_create: %s", flux_strerror (errno));
    flux_msg_handler_allow_rolemask (mh, FLUX_ROLE_ALL);

    /* Attempt with default creds.
     */
    testrpc1_called = false;
    ok ((f = flux_rpc (h, "testrpc1", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "default-creds: sent request to message handler");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE);
    ok (rc >= 0,
        "default-creds: reactor successfully handled one event");
    ok (testrpc1_called == true && flux_future_get (f, NULL) == 0,
        "default-creds: handler was called and returned success response");
    flux_future_destroy (f);

    /* Attempt with non-owner creds
     */
    ok (cred_get (h, &saved) == 0
        && saved.userid == getuid() && saved.rolemask == FLUX_ROLE_OWNER,
        "saved connector creds, with expected values");
    new.userid = 9999;
    new.rolemask = 0x80000000;
    ok (cred_set (h, &new) == 0 && cred_get (h, &cr) == 0
        && cr.userid == new.userid && cr.rolemask == new.rolemask,
       "set userid/rolemask to non-owner test values");
    testrpc1_called = false;
    ok ((f = flux_rpc (h, "testrpc1", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "random-creds: sent request to message handler");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE);
    ok (rc >= 0,
        "random-creds: reactor successfully handled one event");
    ok (testrpc1_called == true && flux_future_get (f, NULL) == 0,
        "random-creds: handler was called and returned success response");
    flux_future_destroy (f);
    ok (cred_set (h, &saved) == 0,
        "restored connector creds");

    flux_msg_handler_destroy (mh);
}

static void check_rpc_targeted_policy (flux_t *h)
{
    flux_future_t *f;
    flux_msg_handler_t *mh;
    struct flux_msg_cred saved, new, cr;
    uint32_t allow = 0x1000;
    int rc;

    ok ((mh = testrpc1_handler_create (h)) != NULL,
        "created message handler with targeted policy");
    if (mh == NULL)
        BAIL_OUT ("flux_msg_handler_create: %s", flux_strerror (errno));
    flux_msg_handler_deny_rolemask (mh, FLUX_ROLE_ALL);
    flux_msg_handler_allow_rolemask (mh, allow);

    ok (cred_get (h, &saved) == 0
        && saved.userid == getuid() && saved.rolemask == FLUX_ROLE_OWNER,
        "saved connector creds, with expected values");

    /* Attempt with default creds.
     */
    testrpc1_called = false;
    ok ((f = flux_rpc (h, "testrpc1", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "default-creds: sent request to message handler");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE);
    ok (rc >= 0,
        "default-creds: reactor successfully handled one event");
    ok (testrpc1_called == true && flux_future_get (f, NULL) == 0,
        "default-creds: handler was called and returned success response");
    flux_future_destroy (f);

    /* Attempt with target creds
     */
    new.userid = 9999;
    new.rolemask = allow;
    ok (cred_set (h, &new) == 0 && cred_get (h, &cr) == 0
        && cr.userid == new.userid && cr.rolemask == new.rolemask,
       "set userid/rolemask to random/target test values");
    testrpc1_called = false;
    ok ((f = flux_rpc (h, "testrpc1", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "target-creds: sent request to message handler");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE);
    ok (rc >= 0,
        "target-creds: reactor successfully handled one event");
    ok (testrpc1_called == true && flux_future_get (f, NULL) == 0,
        "target-creds: handler was called and returned success response");
    flux_future_destroy (f);

    /* attempt with non-target creds
     */
    new.userid = 9999;
    new.rolemask = 0x80000000;
    ok (cred_set (h, &new) == 0 && cred_get (h, &cr) == 0
        && cr.userid == new.userid && cr.rolemask == new.rolemask,
       "set userid/rollmask to random/non-target test values");
    testrpc1_called = false;
    ok ((f = flux_rpc (h, "testrpc1", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "nontarget-creds: sent request to message handler");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE);
    ok (rc >= 0,
        "nontarget-creds: reactor successfully handled one event");
    errno = 0;
    ok (testrpc1_called == false
        && flux_future_get (f, NULL) == -1 && errno == EPERM,
        "nontarget-creds: handler was NOT called and dispatcher returned EPERM response");
    flux_future_destroy (f);

    ok (cred_set (h, &saved) == 0,
        "restored connector creds");
    flux_msg_handler_destroy (mh);
}

static int comms_err (flux_t *h, void *arg)
{
    BAIL_OUT ("fatal comms error: %s", strerror (errno));
    return -1;
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("cannot continue without loop handle");
    flux_comms_error_set (h, comms_err, NULL);

    check_rpc_oneway (h);
    check_rpc_oneway_faked (h);
    check_rpc_default_policy (h);
    check_rpc_open_policy (h);
    check_rpc_targeted_policy (h);

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

