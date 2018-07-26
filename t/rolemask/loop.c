#include <errno.h>
#include <flux/core.h>
#include <inttypes.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

struct creds {
    uint32_t userid;
    uint32_t rolemask;
};

static int cred_get (flux_t *h, struct creds *cr)
{
    if (flux_opt_get (h, FLUX_OPT_TESTING_USERID,
                       &cr->userid, sizeof (cr->userid)) < 0)
        return -1;
    if (flux_opt_get (h, FLUX_OPT_TESTING_ROLEMASK,
                       &cr->rolemask, sizeof (cr->rolemask)) < 0)
        return -1;
    return 0;
}

static int cred_set (flux_t *h, struct creds *cr)
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
    struct creds cr;

    f = flux_rpc (h, "testrpc0", NULL, FLUX_NODEID_ANY, FLUX_RPC_NORESPONSE);
    ok (f != NULL,
        "sent request");
    if (f == NULL)
        BAIL_OUT ("flux_rpc: %s", flux_strerror (errno));
    flux_future_destroy (f);

    msg = flux_recv (h, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "received looped back request");
    ok (flux_msg_get_userid (msg, &cr.userid) == 0
        && cr.userid == geteuid (),
        "request contains userid belonging to instance owner");
    ok (flux_msg_get_rolemask (msg, &cr.rolemask) == 0
        && cr.rolemask == FLUX_ROLE_OWNER,
        "request contains rolemask set to FLUX_ROLE_OWNER");
    flux_msg_destroy (msg);
}

static void check_rpc_oneway_faked (flux_t *h)
{
    flux_future_t *f = NULL;
    flux_msg_t *msg = NULL;
    struct creds saved, new, cr;

    ok (cred_get (h, &saved) == 0
        && saved.userid == geteuid() && saved.rolemask == FLUX_ROLE_OWNER,
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
    ok (flux_msg_get_userid (msg, &cr.userid) == 0
        && cr.userid == new.userid,
        "request contains test userid");
    ok (flux_msg_get_rolemask (msg, &cr.rolemask) == 0
        && cr.rolemask == new.rolemask,
        "request contains test rolemask");
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
    if (flux_respond (h, msg, 0, NULL) < 0)
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
    struct creds saved, new, cr;
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
        && flux_rpc_get (f, NULL) == 0,
        "default-creds: handler was called and returned success response");
    flux_future_destroy (f);

    /* Attempt with non-owner creds
     */
    ok (cred_get (h, &saved) == 0
        && saved.userid == geteuid() && saved.rolemask == FLUX_ROLE_OWNER,
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
        && flux_rpc_get (f, NULL) == -1 && errno == EPERM,
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
    struct creds saved, new, cr;
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
    ok (testrpc1_called == true && flux_rpc_get (f, NULL) == 0,
        "default-creds: handler was called and returned success response");
    flux_future_destroy (f);

    /* Attempt with non-owner creds
     */
    ok (cred_get (h, &saved) == 0
        && saved.userid == geteuid() && saved.rolemask == FLUX_ROLE_OWNER,
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
    ok (testrpc1_called == true && flux_rpc_get (f, NULL) == 0,
        "random-creds: handler was called and returned success response");
    flux_future_destroy (f);
    ok (cred_set (h, &saved) == 0,
        "restored connector creds");

    flux_msg_handler_destroy (mh);
}

static void check_rpc_targetted_policy (flux_t *h)
{
    flux_future_t *f;
    flux_msg_handler_t *mh;
    struct creds saved, new, cr;
    uint32_t allow = 0x1000;
    int rc;

    ok ((mh = testrpc1_handler_create (h)) != NULL,
        "created message handler with targetted policy");
    if (mh == NULL)
        BAIL_OUT ("flux_msg_handler_create: %s", flux_strerror (errno));
    flux_msg_handler_deny_rolemask (mh, FLUX_ROLE_ALL);
    flux_msg_handler_allow_rolemask (mh, allow);

    ok (cred_get (h, &saved) == 0
        && saved.userid == geteuid() && saved.rolemask == FLUX_ROLE_OWNER,
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
    ok (testrpc1_called == true && flux_rpc_get (f, NULL) == 0,
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
    ok (testrpc1_called == true && flux_rpc_get (f, NULL) == 0,
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
        && flux_rpc_get (f, NULL) == -1 && errno == EPERM,
        "nontarget-creds: handler was NOT called and dispatcher returned EPERM response");
    flux_future_destroy (f);

    ok (cred_set (h, &saved) == 0,
        "restored connector creds");
    flux_msg_handler_destroy (mh);
}

static void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("flux_open: %s", flux_strerror (errno));
    flux_fatal_set (h, fatal_err, NULL);

    check_rpc_oneway (h);
    check_rpc_oneway_faked (h);
    check_rpc_default_policy (h);
    check_rpc_open_policy (h);
    check_rpc_targetted_policy (h);

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

