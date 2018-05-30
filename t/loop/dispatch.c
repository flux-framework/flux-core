#include <errno.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

int cb_called;
flux_t *cb_h;
flux_msg_handler_t *cb_mh;
const flux_msg_t *cb_msg;
void *cb_arg;
void cb (flux_t *h, flux_msg_handler_t *mh, const flux_msg_t *msg, void *arg)
{
    cb_called++;
    cb_h = h;
    cb_mh = mh;
    cb_msg = msg;
    cb_arg = arg;
}

/* Simple test:
 * create message handler for all events
 * send an event on the loop handler
 * run reactor - handler not called (not started)
 * start message handler
 * run reactor - handler called once with appropriate args
 */
void test_simple_msg_handler (flux_t *h)
{
    flux_msg_handler_t *mh;
    flux_msg_t *msg;
    int rc;

    ok ((mh = flux_msg_handler_create (h, FLUX_MATCH_EVENT, cb, &mh)) != NULL,
        "handle created dispatcher on demand");
    ok ((msg = flux_event_encode ("test", NULL)) != NULL,
        "encoded event message");
    ok (flux_send (h, msg, 0) == 0,
        "sent event message on loop connector");
    cb_called = 0;
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 0,
        "message handler that was not started did not run");
    cb_called = 0;
    cb_h = NULL;
    cb_mh = NULL;
    cb_arg = NULL;
    flux_msg_handler_start (mh);
    diag ("started message handler");
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 1,
        "message handler was called after being started");
    ok (cb_h == h && cb_mh == mh && cb_arg == &mh && cb_msg != NULL,
        "message handler was called with appropriate args");
    flux_msg_destroy (msg);
    flux_msg_handler_destroy (mh);
    diag ("destroyed message and message handler");
}

/* Check fastpath response matching
 */
void test_fastpath (flux_t *h)
{
    struct flux_match m = FLUX_MATCH_RESPONSE;
    flux_msg_handler_t *mh;
    flux_msg_t *msg;
    int rc;

    ok ((m.matchtag = flux_matchtag_alloc (h, 0)) != FLUX_MATCHTAG_NONE,
        "allocated matchtag");
    ok ((mh = flux_msg_handler_create (h, m, cb, NULL)) != NULL,
        "created handler for response");
    ok ((msg = flux_response_encode ("foo", NULL)) != NULL,
        "encoded response message");
    ok (flux_msg_set_matchtag (msg, m.matchtag) == 0,
        "set matchtag in response");
    ok (flux_send (h, msg, 0) == 0,
        "sent response message on loop connector");
    cb_called = 0;
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 0,
        "message handler that was not started did not run");
    flux_msg_handler_start (mh);
    diag ("started message handler");
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 1,
        "message handler was called after being started");

    ok (flux_msg_enable_route (msg) == 0
        && flux_msg_push_route (msg, "myuuid") == 0,
        "added route to message");
    ok (flux_send (h, msg, 0) == 0,
        "sent response message on loop connector");
    cb_called = 0;
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 0,
        "dispatch did not match response in wrong matchtag domain");
    ok (flux_recv (h, FLUX_MATCH_ANY, 0) == NULL,
        "unmatched message was discarded by dispatcher");

    flux_matchtag_free (h, m.matchtag);
    flux_msg_destroy (msg);
    flux_msg_handler_destroy (mh);
    diag ("freed matchtag, destroyed message and message handler");
}

void test_cloned_dispatch (flux_t *orig)
{
    flux_t *h;
    flux_reactor_t *r;
    flux_msg_t *msg;
    flux_msg_handler_t *mh, *mh2;
    struct flux_match m = FLUX_MATCH_RESPONSE;
    struct flux_match m2 = FLUX_MATCH_RESPONSE;
    int rc;
    int type;
    uint32_t matchtag;

    ok (flux_recv (orig, FLUX_MATCH_ANY, 0) == NULL,
        "nothing up my sleve");

    h = flux_clone (orig);
    ok (h != NULL,
        "cloned handle");
    r = flux_reactor_create (0);
    ok (r != NULL,
        "created reactor");
    ok (flux_set_reactor (h, r) == 0,
        "set reactor in cloned handle");

    /* event */
    ok ((mh = flux_msg_handler_create (h, FLUX_MATCH_EVENT, cb, NULL)) != NULL,
        "handle created dispatcher on demand");
    flux_msg_handler_start (mh);
    ok ((msg = flux_event_encode ("test", NULL)) != NULL,
        "encoded event message");
    ok (flux_send (h, msg, 0) == 0,
        "sent event message on cloned connector");
    flux_msg_destroy (msg);
    diag ("started event handler");

    /* response (matched) */
    m.matchtag = flux_matchtag_alloc (h, 0);
    ok (m.matchtag != FLUX_MATCHTAG_NONE,
        "allocated matchtag (%d)", m.matchtag); // 1
    ok ((mh2 = flux_msg_handler_create (h, m, cb, NULL)) != NULL,
        "created handler for response");
    flux_msg_handler_start (mh2);
    ok ((msg = flux_response_encode ("foo", NULL)) != NULL,
        "encoded response message");
    ok (flux_msg_set_matchtag (msg, m.matchtag) == 0,
        "set matchtag in response");
    ok (flux_send (h, msg, 0) == 0,
        "sent response message on cloned connector");
    flux_msg_destroy (msg);
    diag ("started response handler");

    /* response (unmatched) */
    m2.matchtag = flux_matchtag_alloc (h, 0);
    ok (m2.matchtag != FLUX_MATCHTAG_NONE,
        "allocated matchtag (%d)", m2.matchtag); // 2
    ok ((msg = flux_response_encode ("bar", NULL)) != NULL,
        "encoded response message");
    ok (flux_msg_set_matchtag (msg, m2.matchtag) == 0,
        "set matchtag in response");
    ok (flux_send (h, msg, 0) == 0,
        "sent response message on cloned connector");
    flux_msg_destroy (msg);

    /* N.B. libev NOWAIT semantics don't guarantee that all pending
     * events are handled as only one loop is run.  The flux_t handle
     * ensures that only one message is handled per loop, so we need to
     * call it twice to handle the expected two messages.
     */
    cb_called = 0;
    /* 1 */
    rc = flux_reactor_run (r, FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 1,
        "one message handled on first reactor loop");
    /* 2 */
    rc = flux_reactor_run (r, FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 2,
        "another message handled on second reactor loop");
    /* 3 (should get nothing) */
    rc = flux_reactor_run (r, FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 2,
        "no messages handled on third reactor loop");

    /* requeue event and unmatched responses */
    ok (flux_dispatch_requeue (h) == 0,
        "requeued unconsumed messages in clone");

    msg = flux_recv (orig, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "received first message on orig handle");
    skip (msg == NULL, 2);
    rc = flux_msg_get_type (msg, &type);
    ok (rc == 0 && type == FLUX_MSGTYPE_EVENT,
        "and its the event");
    flux_msg_destroy (msg);
    end_skip;

    msg = flux_recv (orig, FLUX_MATCH_ANY, 0);
    ok (msg != NULL,
        "received second message on orig handle");
    skip (msg == NULL, 2);
    rc = flux_msg_get_type (msg, &type);
    ok (rc == 0 && type == FLUX_MSGTYPE_RESPONSE,
        "and its a response");
    rc = flux_msg_get_matchtag (msg, &matchtag);
    ok (rc == 0 && matchtag == 2,
        "and matchtag=2 (%d)", matchtag);
    flux_msg_destroy (msg);
    end_skip;

    ok (flux_recv (orig, FLUX_MATCH_ANY, 0) == NULL,
        "there are no more messages");

    /* close the clone */
    flux_msg_handler_destroy (mh);
    flux_msg_handler_destroy (mh2);
    flux_matchtag_free (h, m.matchtag);
    flux_matchtag_free (h, m2.matchtag);
    flux_close (h);
    flux_reactor_destroy (r);
    diag ("destroyed reactor, closed clone");
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_reactor_t *r;

    plan (NO_PLAN);

    (void)setenv ("FLUX_CONNECTOR_PATH",
                  flux_conf_get ("connector_path", CONF_FLAG_INTREE), 0);
    ok ((h = flux_open ("loop://", 0)) != NULL,
        "opened loop connector");
    if (!h)
        BAIL_OUT ("can't continue without loop handle");
    ok ((r = flux_get_reactor (h)) != NULL,
        "handle created reactor on demand");

    test_simple_msg_handler (h);
    test_fastpath (h);
    test_cloned_dispatch (h);

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

