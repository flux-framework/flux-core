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
#include <errno.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libtap/tap.h"

int cb2_called;
void cb2 (flux_t *h, flux_msg_handler_t *mh, const flux_msg_t *msg, void *arg)
{
    cb2_called++;
}

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

    ok ((m.matchtag = flux_matchtag_alloc (h)) != FLUX_MATCHTAG_NONE,
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

    flux_msg_route_enable (msg);
    ok (flux_msg_route_push (msg, "myuuid") == 0,
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

/* Verify that a non-glob request handler overrides earlier-registered one.
 * "Built-in" methods like "ping" should be overridable.
 */
void test_method_override (flux_t *h)
{
    flux_future_t *f;
    struct flux_match match = FLUX_MATCH_REQUEST;
    flux_msg_handler_t *mh, *mh2;
    int rc;

    /* early foo.bar request handler */
    match.topic_glob = "foo.bar";
    mh = flux_msg_handler_create (h, match, cb, NULL);
    flux_msg_handler_start (mh);
    ok (mh != NULL,
        "foo.bar first request handler created and started");

    /* override foo.bar request handler */
    match.topic_glob = "foo.bar";
    mh2 = flux_msg_handler_create (h, match, cb2, NULL);
    flux_msg_handler_start (mh2);
    ok (mh2 != NULL,
        "foo.bar second request handler created and started");

    cb_called = 0;
    cb2_called = 0;

    /* send message - who got it?
     * N.B. The test doesn't generate a response so just destroy f
     * after message is sent.
     */
    f = flux_rpc (h, "foo.bar", NULL, FLUX_NODEID_ANY, 0);
    ok (f != NULL,
        "sent foo.bar RPC");
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run NOWAIT ran");
    diag ("%d %d", cb_called, cb2_called);
    ok (cb_called == 0 && cb2_called == 1,
        "first handler not called, second handler called");
    flux_future_destroy (f);

    /* now remove mh2 and ensure old mh is reinstated */
    flux_msg_handler_destroy (mh2);

    cb_called = 0;
    cb2_called = 0;

    /* send message - who got it?
     * N.B. The test doesn't generate a response so just destroy f
     * after message is sent.
     */
    f = flux_rpc (h, "foo.bar", NULL, FLUX_NODEID_ANY, 0);
    ok (f != NULL,
        "sent foo.bar RPC");
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run NOWAIT ran");
    diag ("%d %d", cb_called, cb2_called);
    ok (cb_called == 1 && cb2_called == 0,
        "first handler called, second handler not called");
    flux_future_destroy (f);

    /* override foo.bar request handler again */
    match.topic_glob = "foo.bar";
    mh2 = flux_msg_handler_create (h, match, cb2, NULL);
    flux_msg_handler_start (mh2);
    ok (mh2 != NULL,
        "foo.bar second request handler created and started");

    /* now remove original message handler *after* override added */
    flux_msg_handler_destroy (mh);
    diag ("removed first message handler");

    cb_called = 0;
    cb2_called = 0;

    /* send message - who got it?
     * N.B. The test doesn't generate a response so just destroy f
     * after message is sent.
     */
    f = flux_rpc (h, "foo.bar", NULL, FLUX_NODEID_ANY, 0);
    ok (f != NULL,
        "sent foo.bar RPC");
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run NOWAIT ran");
    diag ("%d %d", cb_called, cb2_called);
    ok (cb_called == 0 && cb2_called == 1,
        "first handler not called, second handler called");
    flux_future_destroy (f);
    flux_msg_handler_destroy (mh2);
}


/* Verify that a request handler for a specific method is matched before
 * one for a glob.  A "router" should be able to register a catch-all
 * request handler that doesn't override its own service methods.
 */
void test_request_catchall (flux_t *h)
{
    flux_future_t *f;
    struct flux_match match = FLUX_MATCH_REQUEST;
    flux_msg_handler_t *mh, *mh2;
    int rc;

    /* register foo.bar request handler */
    match.topic_glob = "foo.bar";
    mh = flux_msg_handler_create (h, match, cb, NULL);
    flux_msg_handler_start (mh);
    ok (mh != NULL,
        "foo.bar method handler created and started");

    /* register catchall request handler */
    match.topic_glob = NULL; // NULL is the same as "*"
    mh2 = flux_msg_handler_create (h, match, cb2, NULL);
    flux_msg_handler_start (mh2);
    ok (mh2 != NULL,
        "catchall request handler created and started");

    cb_called = 0;
    cb2_called = 0;

    /* send message to method - who got it?
     */
    f = flux_rpc (h, "foo.bar", NULL, FLUX_NODEID_ANY, 0);
    ok (f != NULL,
        "sent foo.bar RPC");
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run NOWAIT ran");
    ok (cb_called == 1 && cb2_called == 0,
        "method handler called, catchall not called");

    flux_future_destroy (f);
    flux_msg_handler_destroy (mh2);
    flux_msg_handler_destroy (mh);
}

/* Verify that an RPC response is matched before one for a glob.
 * A "router" should be able to register a catch-all response handler
 * that doesn't override its own service methods.
 */
void test_response_catchall (flux_t *h)
{
    flux_msg_t *msg;
    struct flux_match match = FLUX_MATCH_RESPONSE;
    uint32_t mtag;
    flux_msg_handler_t *mh, *mh2;
    int rc;

    /* craft response message with valid matchtag */
    if ((mtag = flux_matchtag_alloc (h)) == FLUX_MATCHTAG_NONE)
        BAIL_OUT ("flux_matchtag_alloc failed");
    msg = flux_response_encode ("baz.fop", NULL);
    ok (msg != NULL,
        "baz.fop RPC response created");
    if (flux_msg_set_matchtag (msg, mtag) < 0)
        BAIL_OUT ("flux_msg_set_matchtag failed");
    ok (flux_msg_cmp (msg, match),
        "RPC response matches match object");

    /* register RPC response handler */
    match.matchtag = mtag;
    if (!(mh = flux_msg_handler_create (h, match, cb, NULL)))
        BAIL_OUT ("flux_msg_handler_create");
    flux_msg_handler_start (mh);
    ok (mh != NULL,
        "baz.fop RPC response handler created and started mh=%p", mh);

    /* register catchall response handler */
    match.matchtag = FLUX_MATCHTAG_NONE;
    mh2 = flux_msg_handler_create (h, match, cb2, NULL);
    flux_msg_handler_start (mh2);
    ok (mh2 != NULL,
        "catchall response handler created and started mh=%p", mh2);

    cb_called = 0;
    cb2_called = 0;

    /* send message to method - who got it?
     */
    ok (flux_send (h, msg, 0) == 0,
        "sent baz.fop response message on loop connector");
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run NOWAIT ran");
    ok (cb_called == 1 && cb2_called == 0,
        "RPC response handler called, catchall not called (%d, %d)",
        cb_called, cb2_called);

    flux_matchtag_free (h, mtag);
    flux_msg_destroy (msg);
    flux_msg_handler_destroy (mh2);
    flux_msg_handler_destroy (mh);
}

/* A response with a non-empty route stack should not match
 * a RPC response handler, since its matchtag is likely from
 * another handle's tagpool.
 */
void test_response_with_routes (flux_t *h)
{
    flux_msg_t *msg;
    struct flux_match match = FLUX_MATCH_RESPONSE;
    uint32_t mtag;
    flux_msg_handler_t *mh, *mh2;
    int rc;

    /* craft response message with valid matchtag + routes*/
    if ((mtag = flux_matchtag_alloc (h)) == FLUX_MATCHTAG_NONE)
        BAIL_OUT ("flux_matchtag_alloc failed");
    msg = flux_response_encode ("foo.bar", NULL);
    ok (msg != NULL,
        "foo.bar RPC response created");
    if (flux_msg_set_matchtag (msg, mtag) < 0)
        BAIL_OUT ("flux_msg_set_matchtag failed");
    flux_msg_route_enable (msg);
    if (flux_msg_route_push (msg, "9") < 0)
        BAIL_OUT ("flux_msg_route_push failed");

    /* register RPC response handler */
    match.matchtag = mtag;
    if (!(mh = flux_msg_handler_create (h, match, cb, NULL)))
        BAIL_OUT ("flux_msg_handler_create");
    flux_msg_handler_start (mh);
    ok (mh != NULL,
        "foo.bar RPC response handler created and started");

    /* register catchall response handler */
    match.matchtag = FLUX_MATCHTAG_NONE;
    mh2 = flux_msg_handler_create (h, match, cb2, NULL);
    flux_msg_handler_start (mh2);
    ok (mh2 != NULL,
        "catchall response handler created and started");

    cb_called = 0;
    cb2_called = 0;

    /* send message to method - who got it?
     */
    ok (flux_send (h, msg, 0) == 0,
        "sent foo.bar response message on loop connector");
    rc = flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_NOWAIT);
    ok (rc >= 0,
        "flux_reactor_run NOWAIT ran");
    ok (cb_called == 0 && cb2_called == 1,
        "RPC response handler not called due to route stack; catchall called");

    flux_matchtag_free (h, mtag);
    flux_msg_destroy (msg);
    flux_msg_handler_destroy (mh2);
    flux_msg_handler_destroy (mh);
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
    m.matchtag = flux_matchtag_alloc (h);
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
    m2.matchtag = flux_matchtag_alloc (h);
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
     * handles only one message per loop by default (see
     * FLUX_OPT_RECV_BATCH_COUNT), so we need to call it twice to handle
     * the expected two messages.
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

/* Batch test:
 * Send N event messages with the recv batch count set to B.  With B=1
 * each reactor loop handles one message; with B>=N one loop handles all.
 * Then verify that stopping the reactor from a handler ends the batch.
 */
static void stop_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    cb_called++;
    flux_reactor_stop (flux_get_reactor (h));
}

static int stop_check_fired;
static void stop_check_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    stop_check_fired++;
    flux_reactor_stop (r);
}

static int send_events (flux_t *h, int count)
{
    for (int i = 0; i < count; i++) {
        flux_msg_t *msg;
        if (!(msg = flux_event_encode ("test", NULL))
            || flux_send (h, msg, 0) < 0) {
            flux_msg_destroy (msg);
            return -1;
        }
        flux_msg_destroy (msg);
    }
    return 0;
}

void test_batch (flux_t *h)
{
    flux_reactor_t *r = flux_get_reactor (h);
    flux_msg_handler_t *mh;
    int batch;

    ok ((mh = flux_msg_handler_create (h, FLUX_MATCH_EVENT, cb, NULL)) != NULL,
        "created event handler");
    flux_msg_handler_start (mh);

    batch = 1;
    ok (flux_opt_set (h,
                      FLUX_OPT_RECV_BATCH_COUNT,
                      &batch,
                      sizeof (batch)) == 0,
        "set recv batch count to 1");
    ok (send_events (h, 4) == 0,
        "sent 4 events");
    cb_called = 0;
    ok (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 1,
        "batch=1: one message handled per reactor loop");
    for (int i = 0; i < 3; i++)
        (void)flux_reactor_run (r, FLUX_REACTOR_NOWAIT);
    ok (cb_called == 4,
        "batch=1: remaining messages handled by subsequent loops");

    batch = 32;
    ok (flux_opt_set (h,
                      FLUX_OPT_RECV_BATCH_COUNT,
                      &batch,
                      sizeof (batch)) == 0,
        "set recv batch count to 32");
    ok (send_events (h, 4) == 0,
        "sent 4 events");
    cb_called = 0;
    ok (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 4,
        "batch=32: all four messages handled in one reactor loop");
    ok (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) >= 0 && cb_called == 4,
        "batch=32: nothing left for the next loop");

    batch = 2;
    ok (flux_opt_set (h,
                      FLUX_OPT_RECV_BATCH_COUNT,
                      &batch,
                      sizeof (batch)) == 0,
        "set recv batch count to 2");
    ok (send_events (h, 5) == 0,
        "sent 5 events");
    cb_called = 0;
    ok (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) >= 0 && cb_called == 2,
        "batch=2: two messages handled on first loop");
    ok (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) >= 0 && cb_called == 4,
        "batch=2: two more on second loop");
    ok (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) >= 0 && cb_called == 5,
        "batch=2: last one on third loop");
    flux_msg_handler_destroy (mh);

    /* A handler that stops the reactor ends the batch early.
     */
    mh = flux_msg_handler_create (h, FLUX_MATCH_EVENT, stop_cb, NULL);
    ok (mh != NULL,
        "created event handler that stops the reactor");
    flux_msg_handler_start (mh);
    batch = 32;
    ok (flux_opt_set (h,
                      FLUX_OPT_RECV_BATCH_COUNT,
                      &batch,
                      sizeof (batch)) == 0,
        "set recv batch count to 32");
    ok (send_events (h, 4) == 0,
        "sent 4 events");
    cb_called = 0;
    ok (flux_reactor_run (r, 0) >= 0,
        "flux_reactor_run ran");
    ok (cb_called == 1,
        "batch=32: handler stopped reactor after one message");
    flux_msg_handler_destroy (mh);

    /* A stop requested by another watcher earlier in the same reactor
     * iteration does not suppress delivery of the first message, matching
     * pre-batching behavior.  The handle watcher delivers messages from a
     * check watcher at default priority, so a check watcher at higher
     * priority that stops the reactor runs before handle_cb().
     */
    ok ((mh = flux_msg_handler_create (h, FLUX_MATCH_EVENT, cb, NULL)) != NULL,
        "created event handler");
    flux_msg_handler_start (mh);
    flux_watcher_t *cw;
    cw = flux_check_watcher_create (r, stop_check_cb, NULL);
    ok (cw != NULL,
        "created check watcher that stops the reactor");
    flux_watcher_set_priority (cw, 1);
    flux_watcher_start (cw);
    ok (send_events (h, 2) == 0,
        "sent 2 events");
    cb_called = 0;
    stop_check_fired = 0;
    ok (flux_reactor_run (r, 0) >= 0,
        "flux_reactor_run ran");
    diag ("stop_check_fired=%d cb_called=%d", stop_check_fired, cb_called);
    ok (stop_check_fired == 1 && cb_called == 1,
        "batch=32: reactor stopped before handle_cb, one message delivered");
    flux_watcher_destroy (cw);
    flux_msg_handler_destroy (mh);

    /* Drain leftovers so they don't disturb later tests.
     */
    ok ((mh = flux_msg_handler_create (h, FLUX_MATCH_EVENT, cb, NULL)) != NULL,
        "created event handler");
    flux_msg_handler_start (mh);
    cb_called = 0;
    ok (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) >= 0 && cb_called == 4,
        "remaining 4 messages handled on the next loop");
    flux_msg_handler_destroy (mh);
    batch = 1;
    ok (flux_opt_set (h,
                      FLUX_OPT_RECV_BATCH_COUNT,
                      &batch,
                      sizeof (batch)) == 0,
        "restored recv batch count to 1");
}

/* A handler that stops the last running handler also stops the handle
 * watcher.  Messages still queued at that point must stay queued, as they
 * do with batch=1, and not be received and discarded as unmatched.
 */
static void stopself_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    cb_called++;
    flux_msg_handler_stop (mh);
}

void test_batch_handler_stop (flux_t *h, int batch)
{
    flux_reactor_t *r = flux_get_reactor (h);
    flux_msg_handler_t *mh;

    ok (flux_opt_set (h,
                      FLUX_OPT_RECV_BATCH_COUNT,
                      &batch,
                      sizeof (batch)) == 0,
        "set recv batch count to %d", batch);
    mh = flux_msg_handler_create (h, FLUX_MATCH_EVENT, stopself_cb, NULL);
    ok (mh != NULL,
        "created event handler that stops itself");
    flux_msg_handler_start (mh);
    ok (send_events (h, 4) == 0,
        "sent 4 events");
    cb_called = 0;
    ok (flux_reactor_run (r, FLUX_REACTOR_NOWAIT) >= 0 && cb_called == 1,
        "batch=%d: handler stopped itself after one message", batch);
    flux_msg_handler_destroy (mh);

    ok ((mh = flux_msg_handler_create (h, FLUX_MATCH_EVENT, cb, NULL)) != NULL,
        "created event handler");
    flux_msg_handler_start (mh);
    cb_called = 0;
    for (int i = 0; i < 4; i++)
        (void)flux_reactor_run (r, FLUX_REACTOR_NOWAIT);
    diag ("batch=%d: %d of 3 remaining messages delivered", batch, cb_called);
    ok (cb_called == 3,
        "batch=%d: remaining 3 messages were not discarded", batch);
    flux_msg_handler_destroy (mh);

    batch = 1;
    (void)flux_opt_set (h, FLUX_OPT_RECV_BATCH_COUNT, &batch, sizeof (batch));
}

int main (int argc, char *argv[])
{
    flux_t *h;
    flux_reactor_t *r;

    plan (NO_PLAN);

    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("can't continue without loop handle");
    ok ((r = flux_get_reactor (h)) != NULL,
        "handle created reactor on demand");

    test_simple_msg_handler (h);
    test_batch (h);
    test_batch_handler_stop (h, 1);
    test_batch_handler_stop (h, 32);
    test_fastpath (h);
    test_cloned_dispatch (h);
    test_method_override (h);
    test_request_catchall (h);
    test_response_catchall (h);
    test_response_with_routes (h);

    flux_close (h);
    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

