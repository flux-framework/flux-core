#include "src/common/libflux/handle.h"
#include "src/common/libflux/rpc.h"
#include "src/common/libflux/request.h"
#include "src/common/libflux/response.h"
#include "src/common/libflux/reactor.h"
#include "src/common/libflux/info.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/nodeset.h"

#include "src/common/libtap/tap.h"

/* request nodeid and flags returned in response */
static int nodeid_fake_error = -1;
int rpctest_nodeid_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    int rc;
    zmsg_t *response = NULL;
    uint32_t nodeid;
    JSON o = NULL;
    int flags;

    if (flux_request_decode (*zmsg, NULL, NULL) < 0)
        goto done;
    if (flux_msg_get_nodeid (*zmsg, &nodeid, &flags) < 0)
        goto done;
    if (nodeid == nodeid_fake_error) {
        nodeid_fake_error = -1;
        errno = EPERM; /* an error not likely to be seen */
        goto done;
    }
    o = Jnew ();
    Jadd_int (o, "nodeid", nodeid);
    Jadd_int (o, "flags", flags);
    if (!(response = flux_response_encode_ok (*zmsg, Jtostr (o))))
        goto done;
done:
    if (!response)
        response = flux_response_encode_err (*zmsg, errno);
    assert (response != NULL);
    rc = flux_response_send (h, &response);
    assert (rc == 0);
    zmsg_destroy (zmsg);
    zmsg_destroy (&response);
    return 0;
}

/* request payload echoed in response */
int rpctest_echo_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    int rc;
    zmsg_t *response = NULL;
    const char *json_str;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (!(response = flux_response_encode_ok (*zmsg, json_str)))
        goto done;
done:
    if (!response)
        response = flux_response_encode_err (*zmsg, errno);
    assert (response != NULL);
    rc = flux_response_send (h, &response);
    assert (rc == 0);
    zmsg_destroy (zmsg);
    zmsg_destroy (&response);
    return 0;
}

/* no-payload response */
static int hello_count = 0;
int rpctest_hello_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    int rc;
    zmsg_t *response = NULL;

    if (flux_request_decode (*zmsg, NULL, NULL) < 0)
        goto done;
    if (!(response = flux_response_encode_ok (*zmsg, NULL)))
        goto done;
    hello_count++;
done:
    if (!response)
        response = flux_response_encode_err (*zmsg, errno);
    assert (response != NULL);
    rc = flux_response_send (h, &response);
    assert (rc == 0);
    zmsg_destroy (zmsg);
    zmsg_destroy (&response);
    return 0;
}

/* flux_multrpc() makes a flux_size() call, faked here for loop connector.
 */
static volatile int fake_size = 1;
int cmb_info_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    int rc;
    zmsg_t *response = NULL;
    JSON o = Jnew ();

    if (flux_request_decode (*zmsg, NULL, NULL) < 0)
        goto done;
    Jadd_bool (o, "treeroot", true);
    Jadd_int (o, "rank", 0);
    Jadd_int (o, "size", fake_size);
    if (!(response = flux_response_encode_ok (*zmsg, Jtostr (o))))
        goto done;
done:
    if (!response)
        response = flux_response_encode_err (*zmsg, errno);
    assert (response != NULL);
    rc = flux_response_send (h, &response);
    assert (rc == 0);
    zmsg_destroy (zmsg);
    zmsg_destroy (&response);
    Jput (o);
    return 0;
}

/* then test - add nodeid to 'then_ns' */
static nodeset_t then_ns = NULL;
int then_count = 0;
static void then_cb (flux_rpc_t r, void *arg)
{
    flux_t h = arg;
    uint32_t nodeid;

    if (flux_rpc_get (r, &nodeid, NULL) < 0
            || !nodeset_add_rank (then_ns, nodeid)
            || ++then_count == 128) {
        flux_reactor_stop (h);
        flux_rpc_destroy (r);
    }
}

int rpctest_begin_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    uint32_t nodeid;
    int i, errors;
    int old_count;
    flux_rpc_t r;
    const char *json_str;

    errno = 0;
    ok (!(r = flux_multrpc (h, NULL, "foo", "all", 0)) && errno == EINVAL,
        "flux_multrpc [0] with NULL topic fails with EINVAL");
    errno = 0;
    ok (!(r = flux_multrpc (h, "bar", "foo", NULL, 0)) && errno == EINVAL,
        "flux_multrpc [0] with NULL nodeset fails with EINVAL");
    errno = 0;
    ok (!(r = flux_multrpc (h, "bar", "foo", "xyz", 0)) && errno == EINVAL,
        "flux_multrpc [0] with bad nodeset fails with EINVAL");

    /* working no-payload RPC */
    old_count = hello_count;
    ok ((r = flux_multrpc (h, "rpctest.hello", NULL, "all", 0)) != NULL,
        "flux_multrpc [0] with no payload when none is expected works");
    if (!r)
        BAIL_OUT ("can't continue without successful rpc call");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_get (r, NULL, NULL) == 0,
        "flux_rpc_get works");
    ok (hello_count == old_count + 1,
        "rpc was called once");
    flux_rpc_destroy (r);

    /* cause remote EPROTO (unexpected payload) - picked up in _get() */
    ok ((r = flux_multrpc (h, "rpctest.hello", "foo", "all", 0)) != NULL,
        "flux_multrpc [0] with unexpected payload works, at first");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errno = 0;
    ok (flux_rpc_get (r, NULL, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_rpc_destroy (r);

    /* fake that we have a larger session */
    fake_size = 128;
    cmp_ok (flux_size (h), "==", fake_size,
        "successfully faked flux_size() of %d", fake_size);

    /* repeat working no-payload RPC test (now with 128 nodes) */
    old_count = hello_count;
    ok ((r = flux_multrpc (h, "rpctest.hello", NULL, "all", 0)) != NULL,
        "flux_multrpc [0-%d] with no payload when none is expected works",
        fake_size - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errors = 0;
    for (i = 0; i < fake_size; i++)
        if (flux_rpc_get (r, NULL, NULL) < 0)
            errors++;
    ok (errors == 0,
        "flux_rpc_get succeded %d times", fake_size);

    cmp_ok (hello_count - old_count, "==", fake_size,
        "rpc was called %d times", fake_size);
    flux_rpc_destroy (r);

    /* same with a subset */
    old_count = hello_count;
    ok ((r = flux_multrpc (h, "rpctest.hello", NULL, "[0-63]", 0)) != NULL,
        "flux_multrpc [0-%d] with no payload when none is expected works",
        64 - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errors = 0;
    for (i = 0; i < 64; i++)
        if (flux_rpc_get (r, &nodeid, NULL) < 0 || nodeid != i)
            errors++;
    ok (errors == 0,
        "flux_rpc_get succeded %d times, with correct nodeid map", 64);

    cmp_ok (hello_count - old_count, "==", 64,
        "rpc was called %d times", 64);
    flux_rpc_destroy (r);

    /* same with echo payload */
    ok ((r = flux_multrpc (h, "rpctest.echo", "foo", "[0-63]", 0)) != NULL,
        "flux_multrpc [0-%d] ok",
        64 - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errors = 0;
    for (i = 0; i < 64; i++) {
        if (flux_rpc_get (r, NULL, &json_str) < 0
                || !json_str || strcmp (json_str, "foo") != 0)
            errors++;
    }
    ok (errors == 0,
        "flux_rpc_get succeded %d times, with correct return payload", 64);
    flux_rpc_destroy (r);

    /* detect partial failure without mresponse */
    nodeid_fake_error = 20;
    ok ((r = flux_multrpc (h, "rpctest.nodeid", NULL, "[0-63]", 0)) != NULL,
        "flux_multrpc [0-%d] ok",
        64 - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    for (i = 0; i < 64; i++) {
        if (flux_rpc_get (r, &nodeid, &json_str) < 0)
            break;
    }
    ok (i == 20 && errno == EPERM,
        "flux_rpc_get correctly reports single error");
    flux_rpc_destroy (r);

    /* test _then (still at fake session size of 128) */
    ok ((r = flux_multrpc (h, "rpctest.hello", NULL, "[0-127]", 0)) != NULL,
        "flux_multrpc [0-127] ok");
    ok (flux_rpc_then (r, then_cb, h) == 0,
        "flux_rpc_then works");
    /* then_cb terminates reactor, results reported in main() */

    return 0;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "rpctest.begin",          rpctest_begin_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.hello",          rpctest_hello_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.echo",           rpctest_echo_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.nodeid",         rpctest_nodeid_cb},
    { FLUX_MSGTYPE_REQUEST,   "cmb.info",               cmb_info_cb},
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int main (int argc, char *argv[])
{
    zmsg_t *zmsg;
    flux_t h;

    plan (33);

    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", FLUX_O_COPROC)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");

    /* create nodeset for last _then test */
    ok ((then_ns = nodeset_new ()) != NULL,
        "nodeset created ok");

    ok (flux_msghandler_addvec (h, htab, htablen, NULL) == 0,
        "registered message handlers");
    /* test continues in rpctest_begin_cb() so that rpc calls
     * can sleep while we answer them
     */
    ok ((zmsg = flux_request_encode ("rpctest.begin", NULL)) != NULL
        && flux_request_send (h, NULL, &zmsg) == 0,
        "sent message to initiate test");
    ok (flux_reactor_start (h) == 0,
        "reactor completed normally");

    /* Check result of last _then test */
    ok (nodeset_count (then_ns) == 128,
        "then callback worked with correct nodemap");
    nodeset_destroy (then_ns);

    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

