#include <czmq.h>
#include "src/common/libflux/message.h"
#include "src/common/libflux/handle.h"
#include "src/common/libflux/rpc.h"
#include "src/common/libflux/request.h"
#include "src/common/libflux/response.h"
#include "src/common/libflux/reactor.h"
#include "src/common/libflux/info.h"
#include "src/common/libflux/attr.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libcompat/compat.h"

#include "src/common/libtap/tap.h"

static uint32_t fake_size = 1;

/* request nodeid and flags returned in response */
static int nodeid_fake_error = -1;
int rpctest_nodeid_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    int errnum = 0;
    uint32_t nodeid;
    JSON o = NULL;
    int flags;

    if (flux_request_decode (*zmsg, NULL, NULL) < 0
            || flux_msg_get_nodeid (*zmsg, &nodeid, &flags) < 0) {
        errnum = errno;
        goto done;
    }
    if (nodeid == nodeid_fake_error) {
        nodeid_fake_error = -1;
        errnum = EPERM; /* an error not likely to be seen */
        goto done;
    }
    o = Jnew ();
    Jadd_int (o, "nodeid", nodeid);
    Jadd_int (o, "flags", flags);
done:
    (void)flux_respond (h, *zmsg, errnum, Jtostr (o));
    Jput (o);
    zmsg_destroy (zmsg);
    return 0;
}

/* request payload echoed in response */
int rpctest_echo_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    int errnum = 0;
    const char *json_str;

    if (flux_request_decode (*zmsg, NULL, &json_str) < 0) {
        errnum = errno;
        goto done;
    }
done:
    (void)flux_respond (h, *zmsg, errnum, json_str);
    zmsg_destroy (zmsg);
    return 0;
}

/* no-payload response */
static int hello_count = 0;
int rpctest_hello_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    int errnum = 0;

    if (flux_request_decode (*zmsg, NULL, NULL) < 0) {
        errnum = errno;
        goto done;
    }
    hello_count++;
done:
    (void)flux_respond (h, *zmsg, errnum, NULL);
    zmsg_destroy (zmsg);
    return 0;
}

/* then test - add nodeid to 'then_ns' */
static nodeset_t then_ns = NULL;
static int then_count = 0;
static flux_rpc_t *then_r;
static void then_cb (flux_rpc_t *r, void *arg)
{
    flux_t h = arg;
    uint32_t nodeid;

    if (flux_rpc_get (r, &nodeid, NULL) < 0
            || !nodeset_add_rank (then_ns, nodeid)
            || ++then_count == 128) {
        flux_reactor_stop (flux_get_reactor (h));
    }
}

int rpctest_begin_cb (flux_t h, int type, zmsg_t **zmsg, void *arg)
{
    uint32_t nodeid;
    int i, errors;
    int old_count;
    flux_rpc_t *r;
    const char *json_str;

    errno = 0;
    ok (!(r = flux_rpc_multi (h, NULL, "foo", "all", 0)) && errno == EINVAL,
        "flux_rpc_multi [0] with NULL topic fails with EINVAL");
    errno = 0;
    ok (!(r = flux_rpc_multi (h, "bar", "foo", NULL, 0)) && errno == EINVAL,
        "flux_rpc_multi [0] with NULL nodeset fails with EINVAL");
    errno = 0;
    ok (!(r = flux_rpc_multi (h, "bar", "foo", "xyz", 0)) && errno == EINVAL,
        "flux_rpc_multi [0] with bad nodeset fails with EINVAL");

    /* working no-payload RPC */
    old_count = hello_count;
    ok ((r = flux_rpc_multi (h, "rpctest.hello", NULL, "all", 0)) != NULL,
        "flux_rpc_multi [0] with no payload when none is expected works");
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
    ok ((r = flux_rpc_multi (h, "rpctest.hello", "foo", "all", 0)) != NULL,
        "flux_rpc_multi [0] with unexpected payload works, at first");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errno = 0;
    ok (flux_rpc_get (r, NULL, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_rpc_destroy (r);

    /* fake that we have a larger session */
    fake_size = 128;
    char s[16];
    uint32_t size = 0;
    snprintf (s, sizeof (s), "%u", fake_size);
    flux_attr_fake (h, "size", s, FLUX_ATTRFLAG_IMMUTABLE);
    flux_get_size (h, &size);
    cmp_ok (size, "==", fake_size,
        "successfully faked flux_get_size() of %d", fake_size);

    /* repeat working no-payload RPC test (now with 128 nodes) */
    old_count = hello_count;
    ok ((r = flux_rpc_multi (h, "rpctest.hello", NULL, "all", 0)) != NULL,
        "flux_rpc_multi [0-%d] with no payload when none is expected works",
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
    ok ((r = flux_rpc_multi (h, "rpctest.hello", NULL, "[0-63]", 0)) != NULL,
        "flux_rpc_multi [0-%d] with no payload when none is expected works",
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
    ok ((r = flux_rpc_multi (h, "rpctest.echo", "foo", "[0-63]", 0)) != NULL,
        "flux_rpc_multi [0-%d] ok",
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
    ok ((r = flux_rpc_multi (h, "rpctest.nodeid", NULL, "[0-63]", 0)) != NULL,
        "flux_rpc_multi [0-%d] ok",
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
    ok ((then_r = flux_rpc_multi (h, "rpctest.hello", NULL, "[0-127]", 0)) != NULL,
        "flux_rpc_multi [0-127] ok");
    ok (flux_rpc_then (then_r, then_cb, h) == 0,
        "flux_rpc_then works");
    /* then_cb stops reactor; results reported, then_r destroyed in main() */

    return 0;
}

static bool fatal_tested = false;
static void fatal_err (const char *message, void *arg)
{
    if (fatal_tested)
        BAIL_OUT ("fatal error: %s", message);
    else
        fatal_tested = true;
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
    flux_msg_t *msg;
    flux_t h;
    flux_reactor_t *reactor;

    plan (35);

    (void)setenv ("FLUX_CONNECTOR_PATH", CONNECTOR_PATH, 0);
    ok ((h = flux_open ("loop://", FLUX_O_COPROC)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    ok ((reactor = flux_get_reactor (h)) != NULL,
        "obtained reactor");
    if (!reactor)
        BAIL_OUT ("can't continue without reactor");

    flux_fatal_set (h, fatal_err, NULL);
    flux_fatal_error (h, __FUNCTION__, "Foo");
    ok (fatal_tested == true,
        "flux_fatal function is called on fatal error");

    /* create nodeset for last _then test */
    ok ((then_ns = nodeset_new ()) != NULL,
        "nodeset created ok");

    ok (flux_msghandler_addvec (h, htab, htablen, NULL) == 0,
        "registered message handlers");
    /* test continues in rpctest_begin_cb() so that rpc calls
     * can sleep while we answer them
     */
    ok ((msg = flux_request_encode ("rpctest.begin", NULL)) != NULL
        && flux_send (h, msg, 0) == 0,
        "sent message to initiate test");
    ok (flux_reactor_run (reactor, 0) == 0,
        "reactor completed normally");
    flux_msg_destroy (msg);

    /* Check result of last _then test */
    ok (nodeset_count (then_ns) == 128,
        "then callback worked with correct nodemap");
    nodeset_destroy (then_ns);
    flux_rpc_destroy (then_r);

    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

