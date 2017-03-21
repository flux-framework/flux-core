#include <inttypes.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libtap/tap.h"

static uint32_t fake_size = 1;

/* request nodeid and flags returned in response */
static int nodeid_fake_error = -1;
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
    if (nodeid == nodeid_fake_error) {
        nodeid_fake_error = -1;
        errnum = EPERM; /* an error not likely to be seen */
        goto done;
    }
    o = Jnew ();
    Jadd_int (o, "nodeid", nodeid);
    Jadd_int (o, "flags", flags);
done:
    (void)flux_respond (h, msg, errnum, Jtostr (o));
    Jput (o);
}

void rpcftest_nodeid_cb (flux_t *h, flux_msg_handler_t *w,
                         const flux_msg_t *msg, void *arg)
{
    int errnum = 0;
    uint32_t nodeid = 0;
    int flags = 0;

    if (flux_request_decodef (msg, NULL, "{}") < 0
            || flux_msg_get_nodeid (msg, &nodeid, &flags) < 0) {
        errnum = errno;
        goto done;
    }
    if (nodeid == nodeid_fake_error) {
        nodeid_fake_error = -1;
        errnum = EPERM; /* an error not likely to be seen */
        goto done;
    }

done:
    (void)flux_respondf (h, msg, "{ s:i s:i s:i }", "errnum", errnum,
                         "nodeid", nodeid, "flags", flags);
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
done:
    (void)flux_respond (h, msg, errnum, json_str);
}

/* no-payload response */
static int hello_count = 0;
void rpctest_hello_cb (flux_t *h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    int errnum = 0;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        errnum = errno;
        goto done;
    }
    hello_count++;
done:
    (void)flux_respond (h, msg, errnum, NULL);
}

void rpcftest_hello_cb (flux_t *h, flux_msg_handler_t *w,
                        const flux_msg_t *msg, void *arg)
{
    int errnum = 0;

    if (flux_request_decodef (msg, NULL, "{ ! }") < 0) {
        errnum = errno;
        goto done;
    }
    hello_count++;
done:
    if (errnum)
        (void)flux_respond (h, msg, errnum, NULL);
    else
        (void)flux_respondf (h, msg, "{}");
}

/* then test - add nodeid to 'then_ns' */
static nodeset_t *then_ns = NULL;
static int then_count = 0;
static flux_rpc_t *then_r;
static void then_cb (flux_rpc_t *r, void *arg)
{
    flux_t *h = arg;
    uint32_t nodeid;

    if (flux_rpc_get_nodeid (r, &nodeid) < 0
            || flux_rpc_get (r, NULL) < 0
            || !nodeset_add_rank (then_ns, nodeid)
            || ++then_count == 128) {
        flux_reactor_stop (flux_get_reactor (h));
    }
}

static void thenf_cb (flux_rpc_t *r, void *arg)
{
    flux_t *h = arg;
    uint32_t nodeid;

    if (flux_rpc_get_nodeid (r, &nodeid) < 0
            || flux_rpc_getf (r, "{}") < 0
            || !nodeset_add_rank (then_ns, nodeid)
            || ++then_count == 128) {
        flux_reactor_stop (flux_get_reactor (h));
    }
}

static bool fatal_tested = false;
static void fatal_err (const char *message, void *arg)
{
    if (fatal_tested)
        BAIL_OUT ("fatal error: %s", message);
    else
        fatal_tested = true;
}

static void rpctest_set_size (flux_t *h, uint32_t newsize)
{
    fake_size = newsize;
    char s[16];
    uint32_t size = 0;
    snprintf (s, sizeof (s), "%"PRIu32, fake_size);
    flux_attr_fake (h, "size", s, FLUX_ATTRFLAG_IMMUTABLE);
    flux_get_size (h, &size);
    cmp_ok (size, "==", fake_size,
        "successfully faked flux_get_size() of %d", fake_size);
}

void rpctest_begin_cb (flux_t *h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    uint32_t nodeid;
    int count;
    int old_count;
    flux_rpc_t *r;
    const char *json_str;

    rpctest_set_size (h, 1);

    errno = 0;
    ok (!(r = flux_rpc_multi (h, NULL, "{}", "all", 0)) && errno == EINVAL,
        "flux_rpc_multi [0] with NULL topic fails with EINVAL");
    errno = 0;
    ok (!(r = flux_rpc_multi (h, "bar", "{}", NULL, 0)) && errno == EINVAL,
        "flux_rpc_multi [0] with NULL nodeset fails with EINVAL");
    errno = 0;
    ok (!(r = flux_rpc_multi (h, "bar", "{}", "xyz", 0)) && errno == EINVAL,
        "flux_rpc_multi [0] with bad nodeset fails with EINVAL");

    /* working no-payload RPC */
    old_count = hello_count;
    ok ((r = flux_rpc_multi (h, "rpctest.hello", NULL, "all", 0)) != NULL,
        "flux_rpc_multi [0] with no payload when none is expected works");
    if (!r)
        BAIL_OUT ("can't continue without successful rpc call");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get works");
    ok (hello_count == old_count + 1,
        "rpc was called once");
    flux_rpc_destroy (r);

    /* working no-payload RPC for "any" */
    old_count = hello_count;
    ok ((r = flux_rpc_multi (h, "rpctest.hello", NULL, "any", 0)) != NULL,
        "flux_rpc_multi [0] with no payload when none is expected works");
    if (!r)
        BAIL_OUT ("can't continue without successful rpc call");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get works");
    ok (hello_count == old_count + 1,
        "rpc was called once");
    flux_rpc_destroy (r);

    /* working no-payload RPC for "upstream" */
    old_count = hello_count;
    ok ((r = flux_rpc_multi (h, "rpctest.hello", NULL, "upstream", 0)) != NULL,
        "flux_rpc_multi [0] with no payload when none is expected works");
    if (!r)
        BAIL_OUT ("can't continue without successful rpc call");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_get (r, NULL) == 0,
        "flux_rpc_get works");
    ok (hello_count == old_count + 1,
        "rpc was called once");
    flux_rpc_destroy (r);

    /* cause remote EPROTO (unexpected payload) - picked up in _get() */
    ok ((r = flux_rpc_multi (h, "rpctest.hello", "{}", "all", 0)) != NULL,
        "flux_rpc_multi [0] with unexpected payload works, at first");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errno = 0;
    ok (flux_rpc_get (r, NULL) < 0
        && errno == EPROTO,
        "flux_rpc_get fails with EPROTO");
    flux_rpc_destroy (r);

    /* fake that we have a larger session */
    rpctest_set_size (h, 128);

    /* repeat working no-payload RPC test (now with 128 nodes) */
    old_count = hello_count;
    ok ((r = flux_rpc_multi (h, "rpctest.hello", NULL, "all", 0)) != NULL,
        "flux_rpc_multi [0-%d] with no payload when none is expected works",
        fake_size - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    count = 0;
    do {
        if (flux_rpc_get (r, NULL) < 0)
            break;
        count++;
    } while (flux_rpc_next (r) == 0);
    ok (count == fake_size,
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
    count = 0;
    do {
        if (flux_rpc_get_nodeid (r, &nodeid) < 0
                || flux_rpc_get (r, NULL) < 0 || nodeid != count)
            break;
        count++;
    } while (flux_rpc_next (r) == 0);
    ok (count == 64,
        "flux_rpc_get succeded %d times, with correct nodeid map", 64);

    cmp_ok (hello_count - old_count, "==", 64,
        "rpc was called %d times", 64);
    flux_rpc_destroy (r);

    /* same with echo payload */
    ok ((r = flux_rpc_multi (h, "rpctest.echo", "{}", "[0-63]", 0)) != NULL,
        "flux_rpc_multi [0-%d] ok",
        64 - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    count = 0;
    do {
        if (flux_rpc_get (r, &json_str) < 0
                || !json_str || strcmp (json_str, "{}") != 0)
            break;
        count++;
    } while (flux_rpc_next (r) == 0);
    ok (count == 64,
        "flux_rpc_get succeded %d times, with correct return payload", 64);
    flux_rpc_destroy (r);

    /* detect partial failure without response */
    nodeid_fake_error = 20;
    ok ((r = flux_rpc_multi (h, "rpctest.nodeid", NULL, "[0-63]", 0)) != NULL,
        "flux_rpc_multi [0-%d] ok",
        64 - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    int fail_count = 0;
    uint32_t fail_nodeid_last = FLUX_NODEID_ANY;
    int fail_errno_last = 0;
    do {
        if (flux_rpc_get_nodeid (r, &nodeid) < 0
                || flux_rpc_get (r, &json_str) < 0) {
            fail_errno_last = errno;
            fail_nodeid_last = nodeid;
            fail_count++;
        }
    } while (flux_rpc_next (r) == 0);
    ok (fail_count == 1 && fail_nodeid_last == 20 && fail_errno_last == EPERM,
        "flux_rpc_get correctly reports single error");
    flux_rpc_destroy (r);

    /* test that a fatal handle error causes flux_rpc_next () to fail */
    flux_fatal_set (h, NULL, NULL); /* reset handler and flag */
    ok (flux_fatality (h) == false,
        "flux_fatality says all is well");
    ok ((r = flux_rpc_multi (h, "rpctest.nodeid", NULL, "[0-1]", 0)) != NULL,
        "flux_rpc_multi [0-1] ok");
    flux_fatal_error (h, __FUNCTION__, "Foo");
    ok (flux_fatality (h) == true,
        "flux_fatality shows simulated failure");
    ok (flux_rpc_next (r) == -1,
        "flux_rpc_next fails");
    flux_fatal_set (h, fatal_err, NULL); /* reset handler and flag  */
    flux_rpc_destroy (r);

    /* test _then (still at fake session size of 128) */
    then_count = 0;
    ok ((then_r = flux_rpc_multi (h, "rpctest.hello", NULL, "[0-127]", 0)) != NULL,
        "flux_rpc_multi [0-127] ok");
    ok (flux_rpc_then (then_r, then_cb, h) == 0,
        "flux_rpc_then works");
    /* then_cb stops reactor; results reported, then_r destroyed in
     * run_multi_test() */
}

void rpcftest_begin_cb (flux_t *h, flux_msg_handler_t *w,
                        const flux_msg_t *msg, void *arg)
{
    uint32_t nodeid;
    int count;
    int old_count;
    flux_rpc_t *r;
    const char *json_str;

    rpctest_set_size (h, 1);

    errno = 0;
    ok (!(r = flux_rpcf_multi (h, NULL, "all", 0, "{}")) && errno == EINVAL,
        "flux_rpcf_multi [0] with NULL topic fails with EINVAL");
    errno = 0;
    ok (!(r = flux_rpcf_multi (h, "bar", NULL, 0, "{}")) && errno == EINVAL,
        "flux_rpcf_multi [0] with NULL nodeset fails with EINVAL");
    errno = 0;
    ok (!(r = flux_rpcf_multi (h, "bar", "xyz", 0, "{}")) && errno == EINVAL,
        "flux_rpcf_multi [0] with bad nodeset fails with EINVAL");
    errno = 0;
    ok (!(r = flux_rpcf_multi (h, "bar", "all", 0, NULL)) && errno == EINVAL,
        "flux_rpcf_multi [0] with NULL fmt fails with EINVAL");
    errno = 0;
    ok (!(r = flux_rpcf_multi (h, "bar", "all", 0, "")) && errno == EINVAL,
        "flux_rpcf_multi [0] with empty string fmt fails with EINVAL");
    errno = 0;
    ok (!(r = flux_rpcf_multi (h, "bar", "all", 0, "{ s }", "foo")) && errno == EINVAL,
        "flux_rpcf_multi [0] with bad string fmt fails with EINVAL");

    /* working empty payload RPC */
    old_count = hello_count;
    ok ((r = flux_rpcf_multi (h, "rpcftest.hello", "all", 0, "{}")) != NULL,
        "flux_rpcf_multi [0] with empty payload when none is expected works");
    if (!r)
        BAIL_OUT ("can't continue without successful rpc call");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_getf (r, "{}") == 0,
        "flux_rpc_getf works");
    ok (hello_count == old_count + 1,
        "rpc was called once");
    flux_rpc_destroy (r);

    /* working empty payload RPC for "any" */
    old_count = hello_count;
    ok ((r = flux_rpcf_multi (h, "rpcftest.hello", "any", 0, "{}")) != NULL,
        "flux_rpcf_multi [0] with empty payload when none is expected works");
    if (!r)
        BAIL_OUT ("can't continue without successful rpc call");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_getf (r, "{}") == 0,
        "flux_rpc_getf works");
    ok (hello_count == old_count + 1,
        "rpc was called once");
    flux_rpc_destroy (r);

    /* working empty payload RPC for "upstream" */
    old_count = hello_count;
    ok ((r = flux_rpcf_multi (h, "rpcftest.hello", "upstream", 0, "{}")) != NULL,
        "flux_rpcf_multi [0] with empty payload when none is expected works");
    if (!r)
        BAIL_OUT ("can't continue without successful rpc call");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    ok (flux_rpc_getf (r, "{}") == 0,
        "flux_rpc_getf works");
    ok (hello_count == old_count + 1,
        "rpc was called once");
    flux_rpc_destroy (r);

    /* cause remote EPROTO (unexpected payload) - picked up in _getf() */
    ok ((r = flux_rpcf_multi (h, "rpcftest.hello", "all", 0,
                              "{ s:i }", "foo", 42)) != NULL,
        "flux_rpcf_multi [0] with unexpected payload works, at first");
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    errno = 0;
    ok (flux_rpc_getf (r, "{}") < 0
        && errno == EPROTO,
        "flux_rpc_getf fails with EPROTO");
    flux_rpc_destroy (r);

    /* fake that we have a larger session */
    rpctest_set_size (h, 128);

    /* repeat working empty-payload RPC test (now with 128 nodes) */
    old_count = hello_count;
    ok ((r = flux_rpcf_multi (h, "rpcftest.hello", "all", 0, "{}")) != NULL,
        "flux_rpcf_multi [0-%d] with empty payload when none is expected works",
        fake_size - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    count = 0;
    do {
        if (flux_rpc_getf (r, "{}") < 0)
            break;
        count++;
    } while (flux_rpc_next (r) == 0);
    ok (count == fake_size,
        "flux_rpc_getf succeded %d times", fake_size);

    cmp_ok (hello_count - old_count, "==", fake_size,
        "rpc was called %d times", fake_size);
    flux_rpc_destroy (r);

    /* same with a subset */
    old_count = hello_count;
    ok ((r = flux_rpcf_multi (h, "rpcftest.hello", "[0-63]", 0, "{}")) != NULL,
        "flux_rpcf_multi [0-%d] with empty payload when none is expected works",
        64 - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    count = 0;
    do {
        if (flux_rpc_get_nodeid (r, &nodeid) < 0
                || flux_rpc_getf (r, "{}") < 0 || nodeid != count)
            break;
        count++;
    } while (flux_rpc_next (r) == 0);
    ok (count == 64,
        "flux_rpc_getf succeded %d times, with correct nodeid map", 64);

    cmp_ok (hello_count - old_count, "==", 64,
        "rpc was called %d times", 64);
    flux_rpc_destroy (r);

    /* same with echo payload */
    ok ((r = flux_rpcf_multi (h, "rpctest.echo", "[0-63]", 0, "{}")) != NULL,
        "flux_rpcf_multi [0-%d] ok",
        64 - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    count = 0;
    do {
        if (flux_rpc_get (r, &json_str) < 0
                || !json_str || strcmp (json_str, "{}") != 0)
            break;
        count++;
    } while (flux_rpc_next (r) == 0);
    ok (count == 64,
        "flux_rpc_get succeded %d times, with correct return payload", 64);
    flux_rpc_destroy (r);

    /* detect partial failure without response */
    nodeid_fake_error = 20;
    ok ((r = flux_rpcf_multi (h, "rpcftest.nodeid", "[0-63]", 0, "{}")) != NULL,
        "flux_rpcf_multi [0-%d] ok",
        64 - 1);
    ok (flux_rpc_check (r) == false,
        "flux_rpc_check says get would block");
    int fail_count = 0;
    uint32_t fail_nodeid_last = FLUX_NODEID_ANY;
    int fail_errno_last = 0;
    int errnum;
    int flags;
    do {
        if (flux_rpc_get_nodeid (r, &nodeid) < 0
            || flux_rpc_getf (r, "{ s:i s:i s:i !}",
                              "errnum", &errnum,
                              "nodeid", &nodeid,
                              "flags", &flags) < 0
            || errnum) {
            fail_errno_last = errnum;
            fail_nodeid_last = nodeid;
            fail_count++;
        }
    } while (flux_rpc_next (r) == 0);
    ok (fail_count == 1 && fail_nodeid_last == 20 && fail_errno_last == EPERM,
        "flux_rpc_getf correctly reports single error");
    flux_rpc_destroy (r);

    /* test that a fatal handle error causes flux_rpc_next () to fail */
    flux_fatal_set (h, NULL, NULL); /* reset handler and flag */
    ok (flux_fatality (h) == false,
        "flux_fatality says all is well");
    ok ((r = flux_rpcf_multi (h, "rpctest.nodeid", "[0-1]", 0, "{}")) != NULL,
        "flux_rpcf_multi [0-1] ok");
    flux_fatal_error (h, __FUNCTION__, "Foo");
    ok (flux_fatality (h) == true,
        "flux_fatality shows simulated failure");
    ok (flux_rpc_next (r) == -1,
        "flux_rpc_next fails");
    flux_fatal_set (h, fatal_err, NULL); /* reset handler and flag  */
    flux_rpc_destroy (r);

    /* test _then (still at fake session size of 128) */
    then_count = 0;
    ok ((then_r = flux_rpcf_multi (h, "rpcftest.hello", "[0-127]", 0, "{}")) != NULL,
        "flux_rpcf_multi [0-127] ok");
    ok (flux_rpc_then (then_r, thenf_cb, h) == 0,
        "flux_rpc_then works");
    /* thenf_cb stops reactor; results reported, then_r destroyed in
     * run_multi_test() */
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "rpctest.begin",          rpctest_begin_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpcftest.begin",         rpcftest_begin_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.hello",          rpctest_hello_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpcftest.hello",         rpcftest_hello_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.echo",           rpctest_echo_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpctest.nodeid",         rpctest_nodeid_cb},
    { FLUX_MSGTYPE_REQUEST,   "rpcftest.nodeid",        rpcftest_nodeid_cb},
    FLUX_MSGHANDLER_TABLE_END,
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

static void run_multi_test (flux_t *h, flux_reactor_t *reactor,
                            const char *topic)
{
    flux_msg_t *msg;

    /* create nodeset for last _then test */
    ok ((then_ns = nodeset_create ()) != NULL,
        "nodeset created ok");

    /* test continues in topic callback function so that rpc calls
     * can sleep while we answer them
     */
    ok ((msg = flux_request_encode (topic, NULL)) != NULL
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
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_reactor_t *reactor;

    plan (NO_PLAN);

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);
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
    flux_fatal_set (h, fatal_err, NULL); /* reset */

    ok (flux_msg_handler_addvec (h, htab, NULL) == 0,
        "registered message handlers");

    run_multi_test (h, reactor, "rpctest.begin");

    run_multi_test (h, reactor, "rpcftest.begin");

    flux_msg_handler_delvec (htab);
    flux_close (h);

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

