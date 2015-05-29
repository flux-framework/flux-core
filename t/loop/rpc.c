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
    const char *topic;
    const char *json_str;
    int nodeid, flags;
    JSON o = NULL;
    zmsg_t *response;

    errno = 0;
    ok (flux_rpc (h, NULL, NULL, NULL) == -1 && errno == EINVAL,
        "flux_rpc with NULL topic fails with EINVAL");

    ok (flux_rpc (h, "rpctest.hello", NULL, NULL) == 0,
        "flux_rpc with no payload when none is expected works");

    errno = 0;
    ok (flux_rpc (h, "rpctest.hello", "foo", NULL) == -1 && errno == EPROTO,
        "flux_rpc with payload when none is expected fails with EPROTO");

    errno = 0;
    ok (flux_rpc (h, "rpctest.echo", NULL, NULL) == -1 && errno == EPROTO,
        "flux_rpc with no payload when payload is expected fails with EPROTO");

    ok (flux_rpc (h, "rpctest.echo", "foo", NULL) == 0,
        "flux_rpc with payload when payload expected works");

    ok (flux_rpc (h, "rpctest.echo", "foo", &response) == 0 && response != NULL,
        "flux_rpc returns response message");
    ok (flux_response_decode (response, &topic, &json_str) == 0
        && topic && !strcmp (topic, "rpctest.echo")
        && json_str && !strcmp (json_str, "foo"),
        "and response contains expected topic and payload");
    zmsg_destroy (&response);

    ok (flux_rpc (h, "rpctest.nodeid", NULL, &response) == 0
        && response != NULL
        && flux_response_decode (response, NULL, &json_str) == 0
        && (o = Jfromstr (json_str)) != NULL
        && Jget_int (o, "nodeid", &nodeid) && Jget_int (o, "flags", &flags)
        && nodeid == FLUX_NODEID_ANY && flags == 0,
        "flux_rpc sent request with FLUX_NODEID_ANY");
    zmsg_destroy (&response);
    Jput (o);

    ok (flux_rpcto (h, "rpctest.nodeid", NULL, &response, 64) == 0
        && response != NULL
        && flux_response_decode (response, NULL, &json_str) == 0
        && (o = Jfromstr (json_str)) != NULL
        && Jget_int (o, "nodeid", &nodeid) && Jget_int (o, "flags", &flags)
        && nodeid == 64 && flags == 0,
        "flux_rpcto sent request with nodeid set properly");
    zmsg_destroy (&response);
    Jput (o);

    ok (flux_rpcto (h, "rpctest.nodeid", NULL, &response, FLUX_NODEID_UPSTREAM) == 0
        && response != NULL
        && flux_response_decode (response, NULL, &json_str) == 0
        && (o = Jfromstr (json_str)) != NULL
        && Jget_int (o, "nodeid", &nodeid) && Jget_int (o, "flags", &flags)
        && nodeid == 0 && flags == FLUX_MSGFLAG_UPSTREAM,
        "flux_rpcto FLUX_NODEID_UPSTREAM correctly constructed request");
    zmsg_destroy (&response);
    Jput (o);

    zmsg_destroy (zmsg);
    flux_reactor_stop (h);
    return 0;
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

    plan (15);

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
    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

