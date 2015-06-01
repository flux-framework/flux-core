#include "src/common/libflux/handle.h"
#include "src/common/libflux/rpc.h"
#include "src/common/libflux/request.h"
#include "src/common/libflux/response.h"
#include "src/common/libflux/reactor.h"

#include "src/common/libutil/shortjson.h"

#include "src/common/libtap/tap.h"

/* request nodeid and flags returned in response */
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
int rpctest_hello_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    int rc;
    zmsg_t *response = NULL;

    if (flux_request_decode (*zmsg, NULL, NULL) < 0)
        goto done;
    if (!(response = flux_response_encode_ok (*zmsg, NULL)))
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

int rpctest_begin_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    const char *json_str;
    flux_rpc_t r;

    errno = 0;
    ok (!(r = flux_rpc (h, NULL, NULL, FLUX_NODEID_ANY, 0))
        && errno == EINVAL,
        "flux_rpc with NULL topic fails with EINVAL");

    /* working no-payload RPC */
    ok ((r = flux_rpc (h, "rpctest.hello", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with no payload when none is expected works");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_get (r, NULL, NULL) == 0,
        "flux_rpc_get works");
    flux_rpc_destroy (r);

    /* cause remote EPROTO (unexpected payload) - will be picked up in _get() */
    ok ((r = flux_rpc (h, "rpctest.hello", "foo", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when none is expected works, at first");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errno = 0;
    ok (flux_rpc_get (r, NULL, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_rpc_destroy (r);

    /* cause remote EPROTO (missing payload) - will be picked up in _get() */
    errno = 0;
    ok ((r = flux_rpc (h, "rpctest.echo", NULL, FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with no payload when payload is expected works, at first");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errno = 0;
    ok (flux_rpc_get (r, NULL, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_rpc_destroy (r);

    /* working with-payload RPC */
    ok ((r = flux_rpc (h, "rpctest.echo", "foo", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    json_str = NULL;
    ok (flux_rpc_get (r, NULL, &json_str) == 0
        && json_str && !strcmp (json_str, "foo"),
        "flux_rpc_get works and returned expected payload");
    flux_rpc_destroy (r);

    flux_reactor_stop (h);
    return 0;
}

static void then_cb (flux_rpc_t r, void *arg)
{
    flux_t h = arg;
    const char *json_str;

    ok (flux_rpc_check (r) == true,
        "flux_rpc_check says get won't block in then callback");
    json_str = NULL;
    ok (flux_rpc_get (r, NULL, &json_str) == 0
        && json_str && !strcmp (json_str, "xxx"),
        "flux_rpc_get works and returned expected payload in then callback");
    flux_rpc_destroy (r);
    flux_reactor_stop (h);
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "rpctest.begin",          rpctest_begin_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.hello",          rpctest_hello_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.echo",           rpctest_echo_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.nodeid",         rpctest_nodeid_cb},
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int main (int argc, char *argv[])
{
    zmsg_t *zmsg;
    flux_t h;

    plan (24);

    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", FLUX_O_COPROC)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");

    ok (flux_msghandler_addvec (h, htab, htablen, NULL) == 0,
        "registered message handlers");
    /* test continues in rpctest_begin_cb() so that rpc calls
     * can sleep while we answer them
     */
    ok ((zmsg = flux_request_encode ("rpctest.begin", NULL)) != NULL,
        "encoded rpctest.begin request OK");
    ok (flux_request_send (h, NULL, &zmsg) == 0,
        "sent rpctest.begin request");
    ok (flux_reactor_start (h) == 0,
        "reactor completed normally");

    /* test _then */
    flux_rpc_t r;
    ok ((r = flux_rpc (h, "rpctest.echo", "xxx", FLUX_NODEID_ANY, 0)) != NULL,
        "flux_rpc with payload when payload is expected works");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_then (r, then_cb, h) == 0,
        "flux_rpc_then works");
    ok (flux_reactor_start (h) == 0,
        "reactor completed normally");
    /* N.B. then_cb destroys rpc */

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

