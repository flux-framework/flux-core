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
#include <jansson.h>

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/libtestutil/util_rpc.h"
#include "ccan/str/str.h"

/* increment integer and send it back */
void rpctest_incr_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    int i;

    if (flux_request_unpack (msg, NULL, "{s:i}", "n", &i) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "{s:i}", "n", i + 1) < 0)
        BAIL_OUT ("flux_respond_pack: %s", flux_strerror (errno));
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
}

/* request nodeid and flags returned in response */
void rpctest_nodeid_cb (flux_t *h, flux_msg_handler_t *mh,
                        const flux_msg_t *msg, void *arg)
{
    uint32_t nodeid;
    uint8_t flags;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_msg_get_nodeid (msg, &nodeid) < 0)
        goto error;
    if (flux_msg_get_flags (msg, &flags) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "s:i s:i",
                                   "nodeid", (int)nodeid,
                                   "flags", flags) < 0)
        BAIL_OUT ("flux_respond_pack: %s", flux_strerror (errno));
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
}

/* request payload echoed in response */
void rpctest_echo_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    const char *s;

    if (flux_request_decode (msg, NULL, &s) < 0)
        goto error;
    if (!s) {
        errno = EPROTO;
        goto error;
    }
    if (flux_respond (h, msg, s) < 0)
        BAIL_OUT ("flux_respond: %s", flux_strerror (errno));
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
}

/* request payload sets error response content */
void rpctest_echo_error_cb (flux_t *h, flux_msg_handler_t *mh,
                            const flux_msg_t *msg, void *arg)
{
    int errnum;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:i s?s}",
                             "errnum", &errnum, "errstr", &errstr) < 0)
        goto error;
    if (errstr) {
        if (flux_respond_error (h, msg, errnum, errstr) < 0)
            BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
    }
    else {
        if (flux_respond_error (h, msg, errnum, NULL) < 0)
            BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
}


/* raw request payload echoed in response */
void rpctest_rawecho_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg)
{
    const void *d = NULL;
    int l = 0;

    if (flux_request_decode_raw (msg, NULL, &d, &l) < 0)
        goto error;
    if (flux_respond_raw (h, msg, d, l) < 0)
        BAIL_OUT ("flux_respond_raw: %s", flux_strerror (errno));
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
}

/* no-payload response */
void rpctest_hello_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    const char *s;

    if (flux_request_decode (msg, NULL, &s) < 0)
        goto error;
    if (s) {
        errno = EPROTO;
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        BAIL_OUT ("flux_respond: %s", flux_strerror (errno));
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
}

void rpcftest_hello_cb (flux_t *h, flux_msg_handler_t *mh,
                        const flux_msg_t *msg, void *arg)
{
    if (flux_request_unpack (msg, NULL, "{ ! }") < 0)
        goto error;
    if (flux_respond_pack (h, msg, "{}") < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
}

/* Send back the requested number of responses followed an ENODATA error.
 */
void rpctest_multi_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    int i, count;
    uint8_t flags;
    int noterm;

    if (flux_request_unpack (msg, NULL, "{s:i s:i}", "count", &count,
                                                     "noterm", &noterm) < 0)
        goto error;
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msg_get_flags (msg, &flags) < 0)
        goto error;
    for (i = 0; i < count; i++) {
        if (flux_respond_pack (h, msg, "{s:i s:i}", "seq", i,
                                                    "flags", flags) < 0)
            BAIL_OUT ("flux_respond_pack: %s", flux_strerror (errno));
    }
    if (noterm)
        return;
    errno = ENODATA; // EOF of sorts
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        BAIL_OUT ("flux_respond_error: %s", flux_strerror (errno));
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "rpctest.incr",    rpctest_incr_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "rpctest.hello",   rpctest_hello_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "rpcftest.hello",  rpcftest_hello_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "rpctest.echo",    rpctest_echo_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "rpctest.echoerr", rpctest_echo_error_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "rpctest.rawecho", rpctest_rawecho_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "rpctest.nodeid",  rpctest_nodeid_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "rpctest.multi",   rpctest_multi_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

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

void test_corner_case (flux_t *h)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");

    errno = 0;
    ok (flux_rpc_message (NULL, msg, 0, 0) == NULL
        && errno == EINVAL,
        "flux_rpc_message fails with EINVAL on NULL handle");

    errno = 0;
    ok (flux_rpc (NULL, "topic", "data", 0, 0) == NULL
        && errno == EINVAL,
        "flux_rpc fails with EINVAL on NULL handle");

    errno = 0;
    ok (flux_rpc_raw (NULL, "topic", "data", 4, 0, 0) == NULL
        && errno == EINVAL,
        "flux_rpc_raw fails with EINVAL on NULL handle");

    errno = 0;
    ok (flux_rpc_pack (NULL, "topic", 0, 0, "{ s:s }", "foo", "bar") == NULL
        && errno == EINVAL,
        "flux_rpc_pack fails with EINVAL on NULL handle");

    errno = 0;
    ok (flux_rpc_message (h, NULL, 0, 0) == NULL
        && errno == EINVAL,
        "flux_rpc_message fails with EINVAL on NULL msg");

    errno = 0;
    ok (flux_rpc (h, NULL, "data", 0, 0) == NULL
        && errno == EINVAL,
        "flux_rpc fails with EINVAL on NULL topic");

    errno = 0;
    ok (flux_rpc_raw (h, NULL, "data", 4, 0, 0) == NULL
        && errno == EINVAL,
        "flux_rpc_raw fails with EINVAL on NULL topic");

    errno = 0;
    ok (flux_rpc_pack (h, NULL, 0, 0, "{ s:s }", "foo", "bar") == NULL
        && errno == EINVAL,
        "flux_rpc_pack fails with EINVAL on NULL topic");

    errno = 0;
    ok (flux_rpc_message (h, msg, 0, 0xFF) == NULL
        && errno == EINVAL,
        "flux_rpc_message fails with EINVAL on invalid flags");

    errno = 0;
    ok (flux_rpc (h, "topic", "data", 0, 0xFF) == NULL
        && errno == EINVAL,
        "flux_rpc fails with EINVAL on invalid flags");

    errno = 0;
    ok (flux_rpc_raw (h, "topic", "data", 4, 0, 0xFF) == NULL
        && errno == EINVAL,
        "flux_rpc_raw fails with EINVAL on invalid flags");

    errno = 0;
    ok (flux_rpc_pack (h, "topic", 0, 0xFF, "{ s:s }", "foo", "bar") == NULL
        && errno == EINVAL,
        "flux_rpc_pack fails with EINVAL on invalid flags");

    flux_msg_destroy (msg);
}

void test_service (flux_t *h)
{
    flux_future_t *r;
    flux_msg_t *msg;
    const char *topic;
    int count;
    uint32_t matchtag;
    struct flux_msg_cred cred;
    int rc;

    errno = 0;
    r = flux_rpc (h, NULL, NULL, FLUX_NODEID_ANY, 0);
    ok (r == NULL && errno == EINVAL,
        "flux_rpc with NULL topic fails with EINVAL");

    count = flux_matchtag_avail (h);
    r = flux_rpc (h, "rpctest.hello", NULL, FLUX_NODEID_ANY, 0);
    ok (r != NULL,
        "flux_rpc sent request to rpctest.hello service");
    ok (flux_rpc_get_nodeid (r) == FLUX_NODEID_ANY,
        "flux_rpc_get_nodeid works");
    ok (flux_matchtag_avail (h) == count - 1,
        "flux_rpc allocated one matchtag");
    msg = flux_recv (h, FLUX_MATCH_RESPONSE, 0);
    ok (msg != NULL,
        "flux_recv matched response");
    rc = flux_msg_get_topic (msg, &topic);
    ok (rc == 0 && streq (topic, "rpctest.hello"),
        "response has expected topic %s", topic);
    rc = flux_msg_get_matchtag (msg, &matchtag);
    ok (rc == 0 && matchtag == 1,
        "response has first matchtag", matchtag);
    rc = flux_msg_get_cred (msg, &cred);
    ok (rc == 0
        && cred.userid == getuid ()
        && (cred.rolemask & FLUX_ROLE_OWNER),
        "response has cred.userid=UID, cred.rolemask including OWNER");
    errno = 0;
    rc = flux_msg_route_count (msg);
    ok ((rc == -1 && errno == EINVAL) || (rc == 0),
        "response has no residual route stack");
    flux_future_destroy (r);
    ok (flux_matchtag_avail (h) == count - 1,
        "flux_future_destroy did not free matchtag");
    // requeue "lost" response so matchtag can be reclaimed
    ok (flux_requeue (h, msg, FLUX_RQ_HEAD) == 0,
        "flux_requeue response worked");
    flux_msg_destroy (msg);

    ok (reclaim_matchtag (h, 1, 1.) == 0,
        "matchtag from prematurely destroyed RPC was reclaimed");

    diag ("completed test with rpc request, flux_recv response");
}

void test_basic (flux_t *h)
{
    flux_future_t *r;

    r = flux_rpc (h, "rpctest.hello", NULL, FLUX_NODEID_ANY, 0);
    ok (r != NULL,
        "flux_rpc sent request to rpctest.hello service");

    errno = 0;
    ok (flux_future_wait_for (r, 0.) < 0 && errno == ETIMEDOUT,
        "flux_future_wait_for (0.) timed out (not ready)");
    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get works");
    ok (flux_future_wait_for (r, 0.) == 0,
        "flux_future_wait_for (0.) works (ready)");
    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get works a second time");
    flux_future_destroy (r);

    diag ("completed synchronous rpc test");
}

void test_error (flux_t *h)
{
    flux_future_t *f;
    const char *errstr;

    /* Error response with error message payload.
     */
    f = flux_rpc_pack (h, "rpctest.echoerr", FLUX_NODEID_ANY, 0,
                       "{s:i s:s}",
                       "errnum", 69,
                       "errstr", "Hello world");
    ok (f != NULL,
        "flux_rpc_pack sent request to rpctest.echoerr service");
    errno = 0;
    ok (flux_future_get (f, NULL) < 0 && errno == 69,
        "flux_future_get failed with expected errno");
    errno = 0;
    ok (flux_rpc_get (f, NULL) < 0 && errno == 69,
        "flux_rpc_get failed with expected errno");
    errstr = flux_future_error_string (f);
    ok (errstr != NULL && streq (errstr, "Hello world"),
        "flux_rpc_get_error returned expected error string");
    flux_future_destroy (f);

    /* Error response with no error message payload.
     */
    f = flux_rpc_pack (h, "rpctest.echoerr", FLUX_NODEID_ANY, 0,
                       "{s:i}",
                       "errnum", ENOTDIR);
    ok (f != NULL,
        "flux_rpc_pack sent request to rpctest.echoerr service (no errstr)");
    errno = 0;
    ok (flux_future_get (f, NULL) < 0 && errno == ENOTDIR,
        "flux_future_get failed with expected errno");
    errno = 0;
    ok (flux_rpc_get (f, NULL) < 0 && errno == ENOTDIR,
        "flux_rpc_get failed with expected errno");
    errstr = flux_future_error_string (f);
    ok (errstr != NULL && streq (errstr, "Not a directory"),
        "flux_future_error_string returned ENOTDIR strerror string");
    flux_future_destroy (f);
}

void test_encoding (flux_t *h)
{
    json_t *o;
    char *s;
    const char *json_str;
    flux_future_t *r;

    /* cause remote EPROTO (unexpected payload) - will be picked up in _get() */
    ok ((r = flux_rpc (h, "rpctest.hello", "{}", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when none is expected works, at first");
    errno = 0;
    ok (flux_rpc_get (r, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_future_destroy (r);

    /* cause remote EPROTO (missing payload) - will be picked up in _get() */
    errno = 0;
    ok ((r = flux_rpc (h, "rpctest.echo", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with no payload when payload is expected works, at first");
    errno = 0;
    ok (flux_rpc_get (r, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_future_destroy (r);

    /* receive NULL payload on empty response */
    ok ((r = flux_rpc (h, "rpctest.hello", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with empty payload works");
    errno = 0;
    ok (flux_rpc_get (r, &json_str) == 0
        && json_str == NULL,
        "flux_rpc_get gets NULL payload on empty response");
    flux_future_destroy (r);

    /* flux_rpc_get is ok if user doesn't desire response payload */
    errno = 0;
    if (!(o = json_pack ("{s:i}", "foo", 42)))
        BAIL_OUT ("json_pack failed");
    if (!(s = json_dumps (o, JSON_COMPACT)))
        BAIL_OUT ("json_dumps failed");
    ok ((r = flux_rpc (h, "rpctest.echo", s, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload works");
    free (s);
    errno = 0;
    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get is ok if user doesn't desire response payload");
    flux_future_destroy (r);
    json_decref (o);

    /* working with-payload RPC */
    ok ((r = flux_rpc (h, "rpctest.echo", "{}", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    json_str = NULL;
    ok (flux_rpc_get (r, &json_str) == 0
        && json_str && streq (json_str, "{}"),
        "flux_rpc_get works and returned expected payload");
    flux_future_destroy (r);

    /* working with-payload RPC (raw) */
    const void *d;
    const char data[] = "aaaaaaaaaaaaaaaaaaaa";
    int l, len = strlen (data);
    ok ((r = flux_rpc_raw (h, "rpctest.rawecho", data, len,
                          FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc_raw with payload when payload is expected works");
    d = NULL;
    l = -1;
    ok (flux_rpc_get_raw (r, &d, &l) == 0,
        "flux_rpc_get_raw works");
    ok (d != NULL && l == len && memcmp (data, d, len) == 0,
        "flux_rpc_get_raw returned expected payload");
    flux_future_destroy (r);

    /* use newish pack/unpack payload interfaces */
    int i = 0;
    ok ((r = flux_rpc_pack (h, "rpctest.incr", FLUX_NODEID_ANY, 0,
                            "{s:i}", "n", 107)) != NULL,
        "flux_rpc_pack works");
    ok (flux_rpc_get_unpack (r, NULL) < 0
        && errno == EINVAL,
        "flux_rpc_get_unpack fails with EINVAL");
    ok (flux_rpc_get_unpack (r, "{s:i}", "n", &i) == 0,
        "flux_rpc_get_unpack works");
    ok (i == 108,
        "and service returned incremented value");
    flux_future_destroy (r);

    /* cause remote EPROTO (unexpected payload) - will be picked up in _getf() */
    ok ((r = flux_rpc_pack (h, "rpcftest.hello", FLUX_NODEID_ANY, 0,
                            "{ s:i }", "foo", 42)) != NULL,
        "flux_rpc_pack with payload when none is expected works, at first");
    errno = 0;
    ok (flux_rpc_get_unpack (r, "{}") < 0
        && errno == EPROTO,
        "flux_rpc_get_unpack fails with EPROTO");
    flux_future_destroy (r);

    /* cause local EPROTO (user incorrectly expects payload) */
    ok ((r = flux_rpc_pack (h, "rpcftest.hello", FLUX_NODEID_ANY, 0, "{}")) != NULL,
        "flux_rpc_pack with empty payload works");
    errno = 0;
    ok (flux_rpc_get_unpack (r, "{ s:i }", "foo", &i) < 0
        && errno == EPROTO,
        "flux_rpc_get_unpack fails with EPROTO");
    flux_future_destroy (r);

    /* cause local EPROTO (user incorrectly expects empty payload) */
    errno = 0;
    ok ((r = flux_rpc_pack (h, "rpctest.echo", FLUX_NODEID_ANY, 0, "{ s:i }", "foo", 42)) != NULL,
        "flux_rpc_pack with payload works");
    errno = 0;
    ok (flux_rpc_get_unpack (r, "{ ! }") < 0
        && errno == EPROTO,
        "flux_rpc_get_unpack fails with EPROTO");
    flux_future_destroy (r);

    diag ("completed encoding/api test");
}

static void then_cb (flux_future_t *r, void *arg)
{
    flux_t *h = arg;
    const char *json_str;

    ok (flux_future_wait_for (r, 0.) == 0,
        "flux_future_wait_for works (ready) in continuation");
    json_str = NULL;
    ok (flux_rpc_get (r, &json_str) == 0
        && json_str && streq (json_str, "{}"),
        "flux_rpc_get works and returned expected payload in continuation");
    flux_reactor_stop (flux_get_reactor (h));
}

void test_then (flux_t *h)
{
    flux_future_t *r;
    const char *json_str;

    ok ((r = flux_rpc (h, "rpctest.echo", "{}", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    ok (flux_future_then (r, -1., then_cb, h) == 0,
        "flux_future_then works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "reactor completed normally");
    flux_future_destroy (r);

    /* ensure continuation is called if "get" called before "then"
     */
    ok ((r = flux_rpc (h, "rpctest.echo", "{}", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    json_str = NULL;
    ok (flux_rpc_get (r, &json_str) == 0
        && json_str && streq (json_str, "{}"),
        "flux_rpc_get works synchronously and returned expected payload");
    ok (flux_future_then (r, -1., then_cb, h) == 0,
        "flux_future_then works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "reactor completed normally");
    flux_future_destroy (r);

    diag ("completed test of continuations");
}

void test_multi_response (flux_t *h)
{
    flux_future_t *f;
    int seq = -1;
    int inflags = 0;
    uint8_t outflags = 0;
    int count = 0;
    int t1, t2;
    const flux_msg_t *response;

    f = flux_rpc_pack (h, "rpctest.multi", FLUX_NODEID_ANY, FLUX_RPC_STREAMING,
                          "{s:i s:i}", "count", 3, "noterm", 0);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    errno = 0;
    while (flux_rpc_get_unpack (f, "{s:i s:i}", "seq", &seq,
                                                "flags", &inflags) == 0) {
        if (flux_future_get (f, (const void **)&response) == 0)
            (void)flux_msg_get_flags (response, &outflags);
        count++;
        flux_future_reset (f);
    }
    ok (errno == ENODATA,
        "multi-now: got ENODATA as EOF");
    ok (count == 3,
        "multi-now: received 3 valid responses");

    ok ((inflags & FLUX_MSGFLAG_STREAMING) != 0,
        "multi-now: MSGFLAG_STREAMING was set in the request");
    ok ((outflags & FLUX_MSGFLAG_STREAMING),
        "multi-now: MSGFLAG_STREAMING was set in the response");


    t1 = flux_matchtag_avail (h);
    flux_future_destroy (f);
    t2 = flux_matchtag_avail (h);
    cmp_ok (t1, "<", t2,
        "multi-now: stream terminated w/ ENODATA, matchtag retired");
}

void test_multi_response_noterm (flux_t *h)
{
    flux_future_t *f;
    int seq = -1;
    int t1, t2;

    // service will send two responses: seq=0, ENODATA
    f = flux_rpc_pack (h, "rpctest.multi", FLUX_NODEID_ANY, FLUX_RPC_STREAMING,
                          "{s:i s:i}", "count", 1, "noterm", 0);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    // consume seq=0 response
    ok (flux_rpc_get_unpack (f, "{s:i}", "seq", &seq) == 0,
        "mutil-now-noterm: got valid response");
    // destroy should leak matchtag since ENODATA is unconsumed
    t1 = flux_matchtag_avail (h);
    flux_future_destroy (f);
    t2 = flux_matchtag_avail (h);
    cmp_ok (t1, "==", t2,
        "multi-now-noterm: unterminated stream leaked matchtag");
    ok (reclaim_matchtag (h, 1, 1.) == 0,
        "multi-now-noterm: matchtag from prematurely destroyed RPC reclaimed");
}

/* Like above, except service doesn't terminate stream.
 * Abandon RPC.  Matchtag reclaim logic MUST NOT reclaim matchtag.
 */
void test_multi_response_server_noterm (flux_t *h)
{
    flux_future_t *f;

    // service will send only seq=0 (not ENODATA).
    f = flux_rpc_pack (h, "rpctest.multi", FLUX_NODEID_ANY, FLUX_RPC_STREAMING,
                          "{s:i s:i}", "count", 1, "noterm", 1);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    flux_future_destroy (f);

    // seq=0 response will be discarded without retiring matchtag
    // ENODATA will not arrive, so this must time out
    ok (reclaim_matchtag (h, 1, 0.1) < 0,
        "matchtag reclaim did not prematurely retire orphaned matchtag");
}


static void multi_then_cb (flux_future_t *f, void *arg)
{
    int seq = 0;
    static int count = 0;

    errno = 0;
    if (flux_rpc_get_unpack (f, "{s:i}", "seq", &seq) == 0) {
        flux_future_reset (f);
        count++;
        return;
    }
    ok (errno == ENODATA,
        "multi-then: got ENODATA as EOF in continuation");
    ok (count == 3,
        "multi-then: received 3 valid responses");
    flux_reactor_stop (flux_future_get_reactor (f));
    flux_future_destroy (f);
}

void test_multi_response_then (flux_t *h)
{
    flux_future_t *f;

    f = flux_rpc_pack (h, "rpctest.multi", FLUX_NODEID_ANY, FLUX_RPC_STREAMING,
                          "{s:i s:i}", "count", 3, "noterm", 0);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    ok (flux_future_then (f, -1., multi_then_cb, NULL) == 0,
        "multi-then: flux_future_then works");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        BAIL_OUT ("flux_reactor_run failed");
}

static void multi_then_next_cb (flux_future_t *f, void *arg)
{
    int seq = 0;
    static int count = 0;

    errno = 0;
    if (flux_rpc_get_unpack (f, "{s:i}", "seq", &seq) == 0) {
        flux_future_reset (f);
        count++;
        return;
    }
    ok (errno == ENODATA,
        "multi-then-chain: got ENODATA as EOF in continuation");
    ok (count == 2,
        "multi-then-chain: received 2 valid responses after first");
    flux_reactor_stop (flux_future_get_reactor (f));
    flux_future_destroy (f);
}

static void multi_then_first_cb (flux_future_t *f, void *arg)
{
    int seq = 0;
    int rc;

    rc = flux_rpc_get_unpack (f, "{s:i}", "seq", &seq);
    ok (rc == 0,
        "multi-then-chain: received first response");
    if (rc == 0) {
        flux_future_reset (f);
        ok (flux_future_then (f, -1., multi_then_next_cb, NULL) == 0,
            "multi-then-chain: flux_future_then works");
    }
    else {
        flux_reactor_stop_error (flux_future_get_reactor (f));
        flux_future_destroy (f);
    }
}

void test_multi_response_then_chain (flux_t *h)
{
    flux_future_t *f;

    f = flux_rpc_pack (h, "rpctest.multi", FLUX_NODEID_ANY, FLUX_RPC_STREAMING,
                          "{s:i s:i}", "count", 3, "noterm", 0);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    ok (flux_future_then (f, -1., multi_then_first_cb, NULL) == 0,
        "multi-then: flux_future_then works");
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        BAIL_OUT ("flux_reactor_run failed");
}

static void then_nodestroy_cb (flux_future_t *f, void *arg)
{
    int *pdone = arg;
    *pdone = 1;
}

void test_rpc_active_count (flux_t *h)
{
    flux_future_t *f;
    int rc;
    int done = 0;
    int loopcount = 0;

    f = flux_rpc (h, "rpctest.echo", NULL, FLUX_NODEID_ANY, 0);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    ok (flux_future_then (f, -1., then_nodestroy_cb, &done) == 0,
        "rpc_active_count: flux_future_then works");
     while ((rc = flux_reactor_run (flux_get_reactor (h),
                                   FLUX_REACTOR_NOWAIT))) {
        if (done == 1 && loopcount++ > 1) {
            diag ("rpc complete but reactor active count = %d", rc);
            break;
        }
    }
    ok (rc == 0,
        "rpc_active_count: rpc active count is 0 after future fulfilled");
    flux_future_destroy (f);

    /* active count is also decremented on error */
    done = 0;
    loopcount = 0;
    f = flux_rpc (h, "foo", NULL, FLUX_NODEID_ANY, 0);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    ok (flux_future_then (f, -1., then_nodestroy_cb, &done) == 0,
        "rpc_active_count: flux_future_then works");
    while ((rc = flux_reactor_run (flux_get_reactor (h),
                                   FLUX_REACTOR_NOWAIT))) {
        if (done == 1 && loopcount++ > 1) {
            diag ("rpc complete but reactor active count = %d", rc);
            break;
        }
    }
    ok (rc == 0,
        "rpc_active_count: rpc active count is 0 after future error");
    flux_future_destroy (f);
}

static void multi_then_nodestroy_cb (flux_future_t *f, void *arg)
{
    int *pdone = arg;
    int seq = 0;
    static int count = 0;

    errno = 0;
    if (flux_rpc_get_unpack (f, "{s:i}", "seq", &seq) == 0) {
        flux_future_reset (f);
        count++;
        return;
    }
    ok (errno == ENODATA,
        "multi-then: got ENODATA as EOF in continuation");
    ok (count == 3,
        "multi-then: received 3 valid responses");
    *pdone = 1;
}

void test_multi_rpc_active_count (flux_t *h)
{
    flux_future_t *f;
    int rc;
    int done = 0;
    int loopcount = 0;

    f = flux_rpc_pack (h,
                       "rpctest.multi",
                        FLUX_NODEID_ANY,
                        FLUX_RPC_STREAMING,
                        "{s:i s:i}",
                        "count", 3,
                        "noterm", 0);
    if (!f)
        BAIL_OUT ("flux_rpc_pack failed");
    ok (flux_future_then (f, -1., multi_then_nodestroy_cb, &done) == 0,
        "multi-rpc-active-count: flux_future_then works");
    while ((rc = flux_reactor_run (flux_get_reactor (h),
                                   FLUX_REACTOR_NOWAIT))) {
        if (done == 1 && loopcount++ > 1) {
            diag ("multi-rpc complete but reactor active count = %d", rc);
            break;
        }
    }
    ok (rc == 0,
        "mult-rpc: flux_reactor_run() returns 0 after streaming rpc complete");
    flux_future_destroy (f);
}

/* Try flux_rpc_message() with various bad arguments
 */
void test_rpc_message_inval (flux_t *h)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_response_create failed");

    errno = 0;
    ok (flux_rpc_message (NULL, msg, FLUX_NODEID_ANY, 0) == NULL
        && errno == EINVAL,
        "flux_rpc_message h=NULL fails with EINVAL");

    errno = 0;
    ok (flux_rpc_message (h, NULL, FLUX_NODEID_ANY, 0) == NULL
        && errno == EINVAL,
        "flux_rpc_message msg=NULL fails with EINVAL");

    errno = 0;
    ok (flux_rpc_message (h, msg, FLUX_NODEID_ANY, 0xffff) == NULL
        && errno == EINVAL,
        "flux_rpc_message flags=wrong fails with EINVAL");

    flux_msg_destroy (msg);

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_EVENT)))
        BAIL_OUT ("flux_response_create failed");

    errno = 0;
    ok (flux_rpc_message (h, msg , FLUX_NODEID_ANY, 0) == NULL
        && errno == EINVAL,
        "flux_rpc_message msg=event fails with EINVAL");

    flux_msg_destroy (msg);

}

/* Try flux_rpc_message() hello world
 */
void test_rpc_message (flux_t *h)
{
    flux_msg_t *msg;
    flux_future_t *f;
    const char *s;

    if (!(msg = flux_request_encode ("rpctest.hello", NULL)))
        BAIL_OUT ("flux_request_encode failed");

    ok ((f = flux_rpc_message (h, msg, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc_message works");
    errno = 0;
    ok (flux_rpc_get (f, &s) == 0 && s == NULL,
        "flux_rpc_message response received from rpctest.hello");

    flux_future_destroy (f);
    flux_msg_destroy (msg);
}

/* Bit of code to test the test framework.
 */
static int fake_server (flux_t *h, void *arg)
{
    flux_msg_t *msg = NULL;

    while ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL) {
        const char *topic = "unknown";
        (void)flux_msg_get_topic (msg, &topic);
        if (streq (topic, "shutdown"))
            break;
        flux_msg_destroy (msg);
    }
    flux_msg_destroy (msg);
    return 0;
}

static int fake_server_reactor (flux_t *h, void *arg)
{
    return flux_reactor_run (flux_get_reactor (h), 0);
}

void test_fake_server (void)
{
    flux_t *h;

    ok ((h = test_server_create (0, fake_server, NULL)) != NULL,
        "test_server_create (recv loop)");
    ok (test_server_stop (h) == 0,
        "test_server_stop worked");
    flux_close (h);
    diag ("completed test with server recv loop");

    ok ((h = test_server_create (0, fake_server_reactor, NULL)) != NULL,
        "test_server_create (reactor)");
    ok ((test_server_stop (h)) == 0,
        "test_server_stop worked");
    diag ("completed test with server reactor loop");
    flux_close (h);
}

static void test_rpc_get_nodeid (flux_t *h)
{
    flux_future_t *f;

    errno = 0;
    f = flux_rpc (h, "rpctest.hello", NULL, 0, 0);
    ok (f != NULL,
        "flux_rpc sent request to rpctest.hello service");
    ok (flux_rpc_get_nodeid (f) == 0,
        "flux_rpc_get_nodeid works");
    ok (flux_future_get (f, NULL) == 0,
        "flux_future_get works");
    ok (flux_rpc_get_nodeid (f) == 0,
        "flux_rpc_get_nodeid still works after future_get()");

    flux_future_destroy (f);
}

static int comms_err (flux_t *h, void *arg)
{
    BAIL_OUT ("fatal coms error: %s", strerror (errno));
    return -1;
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    test_server_environment_init ("rpc-test");

    test_fake_server ();

    h = test_server_create (0, test_server, NULL);
    ok (h != NULL,
        "created test server thread");
    if (!h)
        BAIL_OUT ("can't continue without test server");
    flux_comms_error_set (h, comms_err, NULL);
    flux_flags_set (h, FLUX_O_MATCHDEBUG);

    test_corner_case (h);
    test_service (h);
    test_basic (h);
    test_error (h);
    test_encoding (h);
    test_then (h);
    test_multi_response (h);
    test_multi_response_noterm (h);
    test_multi_response_server_noterm (h);
    test_multi_response_then (h);
    test_multi_response_then_chain (h);
    test_rpc_message_inval (h);
    test_rpc_message (h);

    test_rpc_active_count (h);
    test_multi_rpc_active_count (h);

    test_rpc_get_nodeid (h);

    ok (test_server_stop (h) == 0,
        "stopped test server thread");
    flux_close (h); // destroys test server

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

