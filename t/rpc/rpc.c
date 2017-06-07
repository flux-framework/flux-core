#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libtap/tap.h"
#include "util.h"

/* increment integer and send it back */
void rpctest_incr_cb (flux_t *h, flux_msg_handler_t *w,
                     const flux_msg_t *msg, void *arg)
{
    int i;

    if (flux_request_decodef (msg, NULL, "{s:i}", "n", &i) < 0)
        flux_respond (h, msg, errno, NULL);
    else
        flux_respondf (h, msg, "{s:i}", "n", i + 1);
}

/* request nodeid and flags returned in response */
void rpctest_nodeid_cb (flux_t *h, flux_msg_handler_t *w,
                        const flux_msg_t *msg, void *arg)
{
    int errnum = 0;
    uint32_t nodeid;
    json_object *o = NULL;
    int flags;

    if (flux_request_decode (msg, NULL, NULL) < 0
            || flux_msg_get_nodeid (msg, &nodeid, &flags) < 0) {
        errnum = errno;
        goto done;
    }
    o = Jnew ();
    Jadd_int (o, "nodeid", nodeid);
    Jadd_int (o, "flags", flags);
done:
    (void)flux_respond (h, msg, errnum, Jtostr (o));
    Jput (o);
}

/* request payload echoed in response */
void rpctest_echo_cb (flux_t *h, flux_msg_handler_t *w,
                      const flux_msg_t *msg, void *arg)
{
    int errnum = 0;
    const char *json_str;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        errnum = errno;
        goto done;
    }
    if (!json_str) {
        errnum = EPROTO;
        goto done;
    }
done:
    (void)flux_respond (h, msg, errnum, json_str);
}

/* raw request payload echoed in response */
void rpctest_rawecho_cb (flux_t *h, flux_msg_handler_t *w,
                         const flux_msg_t *msg, void *arg)
{
    int errnum = 0;
    void *d = NULL;
    int l = 0;

    if (flux_request_decode_raw (msg, NULL, &d, &l) < 0) {
        errnum = errno;
        goto done;
    }
done:
    (void)flux_respond_raw (h, msg, errnum, d, l);
}

/* no-payload response */
void rpctest_hello_cb (flux_t *h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    int errnum = 0;
    const char *json_str;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        errnum = errno;
        goto done;
    }
    if (json_str) {
        errnum = EPROTO;
        goto done;
    }
done:
    if (flux_respond (h, msg, errnum, NULL) < 0)
        diag ("%s: flux_respond: %s", __FUNCTION__, flux_strerror (errno));
}

void rpcftest_hello_cb (flux_t *h, flux_msg_handler_t *w,
                        const flux_msg_t *msg, void *arg)
{
    int errnum = 0;

    if (flux_request_decodef (msg, NULL, "{ ! }") < 0) {
        errnum = errno;
        goto done;
    }
 done:
    if (errnum)
        (void)flux_respond (h, msg, errnum, NULL);
    else
        (void)flux_respondf (h, msg, "{}");
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "rpctest.incr",    rpctest_incr_cb, 0, NULL},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.hello",   rpctest_hello_cb, 0, NULL},
    { FLUX_MSGTYPE_REQUEST,   "rpcftest.hello",  rpcftest_hello_cb, 0, NULL},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.echo",    rpctest_echo_cb, 0, NULL},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.rawecho", rpctest_rawecho_cb, 0, NULL},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.nodeid",  rpctest_nodeid_cb, 0, NULL},
    FLUX_MSGHANDLER_TABLE_END,
};

int test_server (flux_t *h, void *arg)
{
    if (flux_msg_handler_addvec (h, htab, NULL) < 0) {
        diag ("flux_msg_handler_addvec failed");
        return -1;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        diag ("flux_reactor_run failed");
        return -1;
    }
    flux_msg_handler_delvec (htab);
    return 0;
}


void *auxfree_arg = NULL;
void auxfree (void *arg)
{
    auxfree_arg = arg;
}

void test_service (flux_t *h)
{
    flux_rpc_t *r;
    flux_msg_t *msg;
    const char *topic;
    int count;
    uint32_t rolemask, userid, matchtag;
    int rc;

    errno = 0;
    r = flux_rpc (h, NULL, NULL, FLUX_NODEID_ANY, 0);
    ok (r == NULL && errno == EINVAL,
        "flux_rpc with NULL topic fails with EINVAL");

    count = flux_matchtag_avail (h, 0);
    r = flux_rpc (h, "rpctest.hello", NULL, FLUX_NODEID_ANY, 0);
    ok (r != NULL,
        "flux_rpc sent request to rpctest.hello service");
    ok (flux_matchtag_avail (h, 0) == count - 1,
        "flux_rpc allocated one matchtag");
    msg = flux_recv (h, FLUX_MATCH_RESPONSE, 0);
    ok (msg != NULL,
        "flux_recv matched response");
    rc = flux_msg_get_topic (msg, &topic);
    ok (rc == 0 && !strcmp (topic, "rpctest.hello"),
        "response has expected topic %s", topic);
    rc = flux_msg_get_matchtag (msg, &matchtag);
    ok (rc == 0 && matchtag == 1,
        "response has first matchtag", matchtag);
    rc = flux_msg_get_userid (msg, &userid);
    ok (rc == 0 && userid == geteuid (),
        "response has userid equal to effective uid of test");
    rc = flux_msg_get_rolemask (msg, &rolemask);
    ok (rc == 0 && (rolemask & FLUX_ROLE_OWNER) != 0,
        "response has rolemask including instance owner");
    errno = 0;
    rc = flux_msg_get_route_count (msg);
    ok ((rc == -1 && errno == EINVAL) || (rc == 0),
        "response has no residual route stack");
    flux_msg_destroy (msg);
    flux_rpc_destroy (r);
    ok (flux_matchtag_avail (h, 0) == count - 1,
        "flux_rpc_destroy did not free matchtag");

    diag ("completed test with rpc request, flux_recv response");
}

void test_basic (flux_t *h)
{
    flux_rpc_t *r;

    r = flux_rpc (h, "rpctest.hello", NULL, FLUX_NODEID_ANY, 0);
    ok (r != NULL,
        "flux_rpc sent request to rpctest.hello service");

    int count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);

    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get works");
    ok (flux_rpc_check (r) == true,
        "flux_rpc_check still returns true");
    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get works a second time");
    flux_rpc_destroy (r);

    diag ("completed synchronous rpc test");
}

void test_aux (flux_t *h)
{
    char *aux_data = "Hello";
    flux_rpc_t *r;

    ok ((r = flux_rpc (h, "rpctest.hello", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc works");
    ok (flux_rpc_aux_set (r, "test", aux_data, auxfree) == 0,
        "flux_rpc_aux_set works");
    ok (flux_rpc_aux_get (r, "wrong") == NULL,
        "flux_rpc_aux_get on wrong key returns NULL");
    ok (flux_rpc_aux_get (r, "test") == aux_data,
        "flux_rpc_aux_get on right key returns orig pointer");
    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get works");
    flux_rpc_destroy (r);
    ok (auxfree_arg == aux_data,
        "destroyed rpc and aux destructor was called with correct arg");

    diag ("completed aux test");
}

void test_encoding (flux_t *h)
{
    json_object *o;
    const char *json_str;
    flux_rpc_t *r;
    int count;

    /* cause remote EPROTO (unexpected payload) - will be picked up in _get() */
    ok ((r = flux_rpc (h, "rpctest.hello", "{}", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when none is expected works, at first");
    count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);
    errno = 0;
    ok (flux_rpc_get (r, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_rpc_destroy (r);

    /* cause remote EPROTO (missing payload) - will be picked up in _get() */
    errno = 0;
    ok ((r = flux_rpc (h, "rpctest.echo", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with no payload when payload is expected works, at first");
    count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);
    errno = 0;
    ok (flux_rpc_get (r, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_rpc_destroy (r);

    /* receive NULL payload on empty response */
    ok ((r = flux_rpc (h, "rpctest.hello", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with empty payload works");
    count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);
    errno = 0;
    ok (flux_rpc_get (r, &json_str) == 0
        && json_str == NULL,
        "flux_rpc_get gets NULL payload on empty response");
    flux_rpc_destroy (r);

    /* flux_rpc_get is ok if user doesn't desire response payload */
    errno = 0;
    o = Jnew ();
    Jadd_int (o, "foo", 42);
    json_str = Jtostr (o);
    ok ((r = flux_rpc (h, "rpctest.echo", json_str, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload works");
    count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);
    errno = 0;
    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get is ok if user doesn't desire response payload");
    flux_rpc_destroy (r);
    Jput (o);

    /* working with-payload RPC */
    ok ((r = flux_rpc (h, "rpctest.echo", "{}", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);
    json_str = NULL;
    ok (flux_rpc_get (r, &json_str) == 0
        && json_str && !strcmp (json_str, "{}"),
        "flux_rpc_get works and returned expected payload");
    flux_rpc_destroy (r);

    /* working with-payload RPC (raw) */
    char *d, data[] = "aaaaaaaaaaaaaaaaaaaa";
    int l, len = strlen (data);
    ok ((r = flux_rpc_raw (h, "rpctest.rawecho", data, len,
                          FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc_raw with payload when payload is expected works");
    count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);
    json_str = NULL;
    ok (flux_rpc_get_raw (r, &d, &l) == 0
        && d != NULL && l == len && memcmp (data, d, len) == 0,
        "flux_rpc_get_raw works and returned expected payload");
    flux_rpc_destroy (r);

    /* use newish pack/unpack payload interfaces */
    int i = 0;
    ok ((r = flux_rpcf (h, "rpctest.incr", FLUX_NODEID_ANY, 0,
                        "{s:i}", "n", 107)) != NULL,
        "flux_rpcf works");
    ok (flux_rpc_getf (r, NULL) < 0
        && errno == EINVAL,
        "flux_rpc_getf fails with EINVAL");
    ok (flux_rpc_getf (r, "{s:i}", "n", &i) == 0,
        "flux_rpc_getf works");
    ok (i == 108,
        "and service returned incremented value");
    flux_rpc_destroy (r);

    /* cause remote EPROTO (unexpected payload) - will be picked up in _getf() */
    ok ((r = flux_rpcf (h, "rpcftest.hello", FLUX_NODEID_ANY, 0,
                        "{ s:i }", "foo", 42)) != NULL,
        "flux_rpcf with payload when none is expected works, at first");
    count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);
    errno = 0;
    ok (flux_rpc_getf (r, "{}") < 0
        && errno == EPROTO,
        "flux_rpc_getf fails with EPROTO");
    flux_rpc_destroy (r);

    /* cause local EPROTO (user incorrectly expects payload) */
    ok ((r = flux_rpcf (h, "rpcftest.hello", FLUX_NODEID_ANY, 0, "{}")) != NULL,
        "flux_rpcf with empty payload works");
    count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);
    errno = 0;
    ok (flux_rpc_getf (r, "{ s:i }", "foo", &i) < 0
        && errno == EPROTO,
        "flux_rpc_getf fails with EPROTO");
    flux_rpc_destroy (r);

    /* cause local EPROTO (user incorrectly expects empty payload) */
    errno = 0;
    ok ((r = flux_rpcf (h, "rpctest.echo", FLUX_NODEID_ANY, 0, "{ s:i }", "foo", 42)) != NULL,
        "flux_rpcf with payload works");
    count = 0;
    while (flux_rpc_check (r) == false)
        count++;
    diag ("flux_rpc_check returned true after %d tries", count);
    errno = 0;
    ok (flux_rpc_getf (r, "{ ! }") < 0
        && errno == EPROTO,
        "flux_rpc_getf fails with EPROTO");
    flux_rpc_destroy (r);

    diag ("completed encoding/api test");
}

static void then_cb (flux_rpc_t *r, void *arg)
{
    flux_t *h = arg;
    const char *json_str;

    ok (flux_rpc_check (r) == true,
        "flux_rpc_check says get won't block in then callback");
    json_str = NULL;
    ok (flux_rpc_get (r, &json_str) == 0
        && json_str && !strcmp (json_str, "{}"),
        "flux_rpc_get works and returned expected payload in then callback");
    flux_reactor_stop (flux_get_reactor (h));
}

void test_then (flux_t *h)
{
    flux_rpc_t *r;
    const char *json_str;

    ok ((r = flux_rpc (h, "rpctest.echo", "{}", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    /* reg/unreg _then a couple times for fun */
    ok (flux_rpc_then (r, NULL, 0) == 0,
        "flux_rpc_then with NULL cb works");
    ok (flux_rpc_then (r, then_cb, h) == 0,
        "flux_rpc_then works after NULL");
    ok (flux_rpc_then (r, NULL, 0) == 0,
        "flux_rpc_then with NULL cb after non-NULL works");
    ok (flux_rpc_then (r, then_cb, h) == 0,
        "flux_rpc_then works");
    /* enough of that */
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "reactor completed normally");
    flux_rpc_destroy (r);

    /* ensure contination is called if "get" called before "then"
     */
    ok ((r = flux_rpc (h, "rpctest.echo", "{}", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    json_str = NULL;
    ok (flux_rpc_get (r, &json_str) == 0
        && json_str && !strcmp (json_str, "{}"),
        "flux_rpc_get works synchronously and returned expected payload");
    ok (flux_rpc_then (r, then_cb, h) == 0,
        "flux_rpc_then works");
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "reactor completed normally");
    flux_rpc_destroy (r);

    diag ("completed test of continuations");
}

/* Bit of code to test the test framework.
 */
static int fake_server (flux_t *h, void *arg)
{
    flux_msg_t *msg;

    while ((msg = flux_recv (h, FLUX_MATCH_ANY, 0)) != NULL) {
        const char *topic = "unknown";
        (void)flux_msg_get_topic (msg, &topic);
        if (!strcmp (topic, "shutdown"))
            break;
    }
    return 0;
}

static int fake_server_reactor (flux_t *h, void *arg)
{
    return flux_reactor_run (flux_get_reactor (h), 0);
}

void test_fake_server (void)
{
    flux_t *h;

    ok ((h = test_server_create (fake_server, NULL)) != NULL,
        "test_server_create (recv loop)");
    ok (test_server_stop (h) == 0,
        "test_server_stop worked");
    flux_close (h);
    diag ("completed test with server recv loop");

    ok ((h = test_server_create (fake_server_reactor, NULL)) != NULL,
        "test_server_create (reactor)");
    ok ((test_server_stop (h)) == 0,
        "test_server_stop worked");
    diag ("completed test with server reactor loop");
    flux_close (h);
}

static void fatal_err (const char *message, void *arg)
{
    BAIL_OUT ("fatal error: %s", message);
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    zsys_init ();
    zsys_set_logstream (stderr);
    zsys_set_logident ("rpc-test");
    zsys_handler_set (NULL);
    zsys_set_linger (5); // msec

    test_fake_server ();

    h = test_server_create (test_server, NULL);
    ok (h != NULL,
        "created test server thread");
    if (!h)
        BAIL_OUT ("can't continue without test server");
    flux_fatal_set (h, fatal_err, NULL);

    test_service (h);
    test_basic (h);
    test_aux (h);
    test_encoding (h);
    test_then (h);

    ok (test_server_stop (h) == 0,
        "stopped test server thread");
    flux_close (h); // destroys test server

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

