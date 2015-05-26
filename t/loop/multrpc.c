#include "src/common/libflux/handle.h"
#include "src/common/libflux/rpc.h"
#include "src/common/libflux/request.h"
#include "src/common/libflux/response.h"
#include "src/common/libflux/reactor.h"
#include "src/common/libflux/info.h"

#include "src/common/libutil/shortjson.h"

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

int rpctest_begin_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    int i, errors, old_count;
    flux_mresponse_t r;
    const char *json_str;

    errno = 0;
    ok (flux_multrpc (h, 0, NULL, NULL, NULL) == -1 && errno == EINVAL,
        "flux_multrpc [0] with NULL topic fails with EINVAL");

    ok (flux_multrpc (h, 0, "rpctest.hello", "foo", NULL) == -1
        && errno == EPROTO,
        "flux_multrpc [0] with unexpected payload fails with EPROTO");

    old_count = hello_count;
    ok (flux_multrpc (h, 0, "rpctest.hello", NULL, NULL) == 0
        && hello_count == old_count + 1,
        "flux_multrpc [0] with expected payload works");

    /* fake that we have a larger session */
    fake_size = 128;
    cmp_ok (flux_size (h), "==", fake_size,
        "successfully faked flux_size() of %d", fake_size);

    old_count = hello_count;
    ok (flux_multrpc (h, 0, "rpctest.hello", NULL, NULL) == 0,
        "flux_multrpc [0-%d] works", fake_size - 1);
    cmp_ok (hello_count - old_count, "==", fake_size,
        "service was called %d times", fake_size);

    old_count = hello_count;
    ok (flux_multrpcto (h, 0, "rpctest.hello", NULL, NULL, "[0-63]") == 0,
        "flux_multrpcto [0-63] works");
    cmp_ok (hello_count - old_count, "==", 64,
        "service was called 64 times");

    /* no payload */
    r = NULL;
    ok (flux_multrpcto (h, 0, "rpctest.hello", NULL, &r, "[0-63]") == 0
        && r != NULL,
        "flux_multrpcto [0-63] works, no payload");
    errors = 0;
    for (i = 0; i < 64; i++)
        if (flux_mresponse_decode (r, i, NULL, NULL) < 0)
            errors++;
    ok (errors == 0,
        "flux_mresponse_decode works, no return payload");
    flux_mresponse_destroy (r);

    /* with payload */
    r = NULL;
    ok (flux_multrpcto (h, 0, "rpctest.echo", "foo", &r, "[0-63]") == 0
        && r != NULL,
        "flux_multrpcto [0-63] works, with payload");
    errors = 0;
    for (i = 0; i < 64; i++)
        if (flux_mresponse_decode (r, i, NULL, &json_str) < 0
                || strcmp (json_str, "foo") != 0)
            errors++;
    ok (errors == 0,
        "flux_mresponse_decode works, return payload verified");
    flux_mresponse_destroy (r);

    /* response ranks properly mapped */
    r = NULL;
    ok (flux_multrpcto (h, 0, "rpctest.nodeid", NULL, &r, "[0-63]") == 0
        && r != NULL,
        "flux_multrpcto [0-63] works, return payload only");
    errors = 0;
    for (i = 0; i < 64; i++) {
        JSON o = NULL;
        int nodeid = -1, flags = -1;
        if (flux_mresponse_decode (r, i, NULL, &json_str) < 0
                || !(o = Jfromstr (json_str))
                || !Jget_int (o, "nodeid", &nodeid)
                || !Jget_int (o, "flags", &flags)
                || nodeid != i
                || flags != 0) {
            errors++;
            fprintf (stderr, "%d: nodeid:%d, flags:%d\n", i, nodeid, flags);
        }
        Jput (o);
    }
    ok (errors == 0,
        "flux_mresponse_decode works, nodes properly mapped");
    flux_mresponse_destroy (r);

    /* detect partial failure without mresponse */
    nodeid_fake_error = 20;
    ok (flux_multrpcto (h, 0, "rpctest.nodeid", NULL, NULL, "[0-63]") < 0
        && errno == EPERM,
        "flux_multrpcto [0-63] correctly reports single error response");

    /* detect partial failure with mresponse */
    nodeid_fake_error = 20;
    r = NULL;
    ok (flux_multrpcto (h, 0, "rpctest.nodeid", NULL, &r, "[0-63]") == 0
        && r != NULL,
        "flux_multrpcto [0-63] returns success with error in mresponse");
    for (i = 0; i < 64; i++)
        if (flux_mresponse_decode (r, i, NULL, &json_str) < 0)
            break;
    ok (i == 20 && errno == EPERM,
        "flux_mresponse_decode correctly reports single error");
    flux_mresponse_destroy (r);

    flux_reactor_stop (h);
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

    plan (21);

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
    ok ((zmsg = flux_request_encode ("rpctest.begin", NULL)) != NULL
        && flux_request_send (h, NULL, &zmsg) == 0,
        "sent message to initiate test");
    ok (flux_reactor_start (h) == 0,
        "reactor completed normally");
    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

