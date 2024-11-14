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
#include <flux/core.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "ccan/str/str.h"

void rpctest_incr_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    int counter;

    if (flux_request_unpack (msg, NULL, "{s:i}", "counter", &counter) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "{s:i}", "counter", counter + 1) < 0)
        BAIL_OUT ("flux_respond: %s", flux_strerror (errno));
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "rpctest.incr",   rpctest_incr_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

/* Body of server thread
 */
int test_server (flux_t *h, void *arg)
{
    flux_msg_handler_t **handlers = NULL;
    if (flux_msg_handler_addvec (h, htab, NULL, &handlers) < 0) {
        diag ("flux_msg_handler_addvec failed");
        return -1;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        diag ("flux_reactor_run failed");
        return -1;
    }
    flux_msg_handler_delvec (handlers);
    return 0;
}

int comms_err (flux_t *h, void *arg)
{
    BAIL_OUT ("fatal comms error: %s", strerror (errno));
    return -1;
}

flux_future_t *incr (flux_t *h, int n)
{
    return flux_rpc_pack (h, "rpctest.incr", FLUX_NODEID_ANY, 0,
                          "{s:i}", "counter", n);
}

int incr_get (flux_future_t *f, int *n)
{
    return flux_rpc_get_unpack (f, "{s:i}", "counter", n);
}

void test_sanity_now (flux_t *h)
{
    flux_future_t *f;
    int count;

    ok ((f = incr (h, 0)) != NULL
        && incr_get (f, &count) == 0
        && count == 1,
        "sanity checked test RPC (now mode)");
    flux_future_destroy (f);
}

void sanity_continuation (flux_future_t *f, void *arg)
{
    int *result = arg;

    if (incr_get (f, result) < 0)
        flux_reactor_stop_error (flux_future_get_reactor (f));
    flux_future_destroy (f);
}

void test_sanity_then (flux_t *h)
{
    flux_future_t *f;
    int count = 0;

    ok ((f = incr (h, 0)) != NULL
        && flux_future_then (f, -1., sanity_continuation, &count) == 0
        && flux_reactor_run (flux_get_reactor (h), 0) == 0
        && count == 1,
        "sanity checked test RPC (then mode)");
    /* future destroyed in continuation */
}

/* continuation internal to incr2 implementation
 * Get result of first incr() and feed it into next incr().
 */
void incr2_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    flux_future_t *f_next;
    int n;

    if (incr_get (f, &n) < 0)
        goto error;
    if (!(f_next = incr (h, n)))
        goto error;
    if (flux_future_continue (f, f_next) < 0) {
        flux_future_destroy (f_next);
        goto error;
    }
    /* done with f */
    flux_future_destroy (f);
    return;
error:
    flux_future_continue_error (f, errno, NULL);
    flux_future_destroy (f);
}

/* Composite future that calls incr() twice
 */
flux_future_t *incr2 (flux_t *h, int n)
{
    flux_future_t *f;
    flux_future_t *f_next;

    if (!(f = incr (h, n)))
        return NULL;
    if (!(f_next = flux_future_and_then (f, incr2_continuation, NULL))) {
        flux_future_destroy (f);
        goto error;
    }
    return f_next;
error:
    return NULL;
}

void test_chained_now (flux_t *h)
{
    flux_future_t *f;
    int count;

    ok ((f = incr2 (h, 0)) != NULL,
        "chained-now: request sent");
    ok (incr_get (f, &count) == 0,
        "chained-now: response received");
    ok (count == 2,
        "chained-now: result is correct");
    flux_future_destroy (f);
}

void chained_continuation (flux_future_t *f, void *arg)
{
    flux_reactor_t *r = flux_future_get_reactor (f);
    int *result = arg;

    if (incr_get (f, result) < 0)
        flux_reactor_stop_error (r);
    else
        flux_reactor_stop (r);
    flux_future_destroy (f);
}

void test_chained_then (flux_t *h)
{
    flux_future_t *f;
    int count = 0;
    int rc;

    ok ((f = incr2 (h, 0)) != NULL,
        "chained-then: request sent");
    rc = flux_future_then (f, -1., chained_continuation, &count);
    ok (rc == 0,
        "chained-then: continuation registered");
    if (rc < 0)
        diag ("flux_future_then: %s", flux_strerror (errno));
    skip (rc < 0, 2);
    rc = flux_reactor_run (flux_get_reactor (h), 0);
    ok (rc >= 0,
        "chained-then: reactor returned success");
    ok (rc == 0,
        "chained-then: reactor had no watchers");
    if (rc > 0)
        diag ("there were %d watchers", rc);
    ok (count == 2,
        "chained-then: result is correct");
    end_skip;
    /* future destroyed in continuation */
}

void test_chained_then_harder (flux_t *h)
{
    int rc;
    int count = 0;
    flux_future_t *f1, *f2, *f3;

    if (!(f1 = incr (h, count))) {
        fail ("chained-then-harder: failed to create initial future");
        return;
    }
    if (!(f2 = flux_future_and_then (f1, incr2_continuation, NULL))) {
        flux_future_destroy (f1);
        fail ("chained-then-harder: failed to create f2");
        return;
    }
    if (!(f3 = flux_future_and_then (f2, incr2_continuation, NULL))) {
        flux_future_destroy (f2);
        fail ("chained-then-harder: failed to create composite future");
        return;
    }
    pass ("chained-then-harder: created future-and-then 3 levels deep");
    rc = flux_future_then (f3, -1., chained_continuation, &count);
    cmp_ok (rc, "==", 0, "chained-then-harder: flux_future_then (f3)");

    rc = flux_reactor_run (flux_get_reactor (h), 0);
    cmp_ok (rc, "==", 0,
           "chained-then-harder: reactor returned success with no watchers");
    cmp_ok (count, "==", 3, "chained-then-harder: result is correct");
    return;
}

void test_chained_now_harder (flux_t *h)
{
    int count = 0;
    flux_future_t *f1, *f2, *f3;

    if (!(f1 = incr (h, count))) {
        fail ("chained-now-harder: failed to create initial future");
        return;
    }
    if (!(f2 = flux_future_and_then (f1, incr2_continuation, NULL))) {
        flux_future_destroy (f1);
        fail ("chained-now-harder: failed to create f2");
        return;
    }
    if (!(f3 = flux_future_and_then (f2, incr2_continuation, NULL))) {
        flux_future_destroy (f2);
        fail ("chained-now-harder: failed to create composite future");
        return;
    }
    pass ("chained-now-harder: created future-and-then 3 levels deep");
    cmp_ok (incr_get (f3, &count), "==", 0,
            "chained-now-harder: response received");
    cmp_ok (count, "==", 3, "chained-now-harder: result is correct");
    flux_future_destroy (f3);
    return;
}

void or_then_cb (flux_future_t *f, void *arg)
{
    int rc = flux_future_get (f, NULL);
    cmp_ok (rc, "<", 0, "or-then: callback: flux_future_get returns < 0");
    cmp_ok (errno, "==", EPROTO, "or-then: callback: errno is expected");
    flux_future_continue_error (f, errno, NULL);
    flux_future_destroy (f);
}

void and_then_cb (flux_future_t *f, void *arg)
{
    fail ("or-then: and_then callback shouldn't be called");
}

void test_or_then (flux_t *h)
{
    int rc;
    flux_future_t *f, *f2, *f3;
    const char *errmsg;

    /* Send malformed message to force EPROTO */
    if (!(f = flux_rpc_pack (h, "rpctest.incr", FLUX_NODEID_ANY, 0, "{}"))) {
        fail ("or-then: failed to create initial future");
        return;
    }
    if (!(f2 = flux_future_or_then (f, or_then_cb, NULL))) {
        fail ("or-then: failed to create or-then future");
        flux_future_destroy (f);
        return;
    }
    if (!(f3 = flux_future_and_then (f, and_then_cb, NULL))) {
        fail ("or-then: failed to create and-then future");
        flux_future_destroy (f2);
        return;
    }
    ok (f2 == f3, "or-then: composite or_then and and_then futures match");

    /*  Call get() in blocking "now" context */
    rc = flux_future_get (f2, NULL);

    cmp_ok (rc, "<", 0, "or-then: flux_future_get on composite returns < 0");
    cmp_ok (errno, "==", EPROTO, "or-then: errno is expected");
    errmsg = flux_future_error_string (f2);
    ok (streq (errmsg, "Protocol error"),
        "or-then: error string reported correctly");
    flux_future_destroy (f2);
}

void or_then_error_string_cb (flux_future_t *f, void *arg)
{
    int rc = flux_future_get (f, NULL);
    cmp_ok (rc, "<", 0, "or-then: callback: flux_future_get returns < 0");
    cmp_ok (errno, "==", EPROTO, "or-then: callback: errno is expected");
    flux_future_continue_error (f, errno, "my errstr");
    flux_future_destroy (f);
}

void test_or_then_error_string (flux_t *h)
{
    int rc;
    flux_future_t *f, *f2;
    const char *errmsg;

    /* Send malformed message to force EPROTO */
    if (!(f = flux_rpc_pack (h, "rpctest.incr", FLUX_NODEID_ANY, 0, "{}"))) {
        fail ("or-then: failed to create initial future");
        return;
    }
    if (!(f2 = flux_future_or_then (f, or_then_error_string_cb, NULL))) {
        fail ("or-then: failed to create or-then future");
        flux_future_destroy (f);
        return;
    }

    /*  Call get() in blocking "now" context */
    rc = flux_future_get (f2, NULL);
    cmp_ok (rc, "<", 0, "or-then: flux_future_get on composite returns < 0");
    cmp_ok (errno, "==", EPROTO, "or-then: errno is expected");
    errmsg = flux_future_error_string (f2);
    ok (streq (errmsg, "my errstr"),
        "or-then: error string reported correctly");
    flux_future_destroy (f2);
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    h = test_server_create (0, test_server, NULL);
    ok (h != NULL,
        "created test server thread");
    if (!h)
        BAIL_OUT ("can't continue without test server");
    flux_comms_error_set (h, comms_err, NULL);

    test_sanity_now (h);
    test_sanity_then (h);
    test_chained_then (h);
    test_chained_now (h);
    test_chained_then_harder (h);
    test_chained_now_harder (h);
    test_or_then (h);
    test_or_then_error_string (h);

    ok (test_server_stop (h) == 0,
        "stopped test server thread");
    flux_close (h); // destroys test server

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

