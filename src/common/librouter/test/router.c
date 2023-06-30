/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
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

#include "src/common/libtap/tap.h"
#include "src/common/libtestutil/util.h"
#include "src/common/librouter/router.h"

/* Test Server
 */

void rtest_hello_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        diag ("flux_respond: %s", flux_strerror (errno));
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        diag ("flux_respond_error: %s", flux_strerror (errno));
}

/* Send 'rtest.event' message on handle.
 */
void rtest_pub_cb (flux_t *h,
                   flux_msg_handler_t *mh,
                   const flux_msg_t *msg,
                   void *arg)
{
    flux_msg_t *event = flux_event_encode ("rtest.event", NULL);
    if (!event) {
        diag ("flux_event_encode failed");
        return;
    }
    if (flux_send (h, event, 0) < 0)
        diag ("flux_send failed");
    flux_msg_destroy (event);
}

/* No-op handler for service.add or service.remove request.
 * This allows router's internal calls to flux_service_add() and
 * flux_service_remove() to succeed.
 */
void service_ok_cb (flux_t *h,
                    flux_msg_handler_t *mh,
                    const flux_msg_t *msg,
                    void *arg)
{
    const char *topic;
    const char *service;

    if (flux_request_unpack (msg,
                             &topic,
                             "{s:s}",
                             "service", &service) < 0)
        goto error;
    diag ("%s %s", topic, service);
    if (flux_respond (h, msg, NULL) < 0)
        diag ("flux_respond failed");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        diag ("flux_respond failed");
}

/* Turn request around and send it to handle.
 */
void rtest_reflect_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    if (flux_send (h, msg, 0) < 0)
        diag ("flux_send failed");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,   "rtest.hello",      rtest_hello_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "rtest.pub",        rtest_pub_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "service.add",      service_ok_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "service.remove",   service_ok_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,   "testfu.bar",       rtest_reflect_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

int server_cb (flux_t *h, void *arg)
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

/* End Test Server
 */

/* router sends message to abcd
 * Expect the test messages and stop the reactor each one.
 */
int basic_recv (const flux_msg_t *msg, void *arg)
{
    flux_reactor_t *r = arg;
    const char *topic;
    int type;

    if (flux_msg_get_type (msg, &type) < 0
            || flux_msg_get_topic (msg, &topic) < 0)
        BAIL_OUT ("router-entry: message decode failure");

    cmp_ok (type, "&", (FLUX_MSGTYPE_RESPONSE | FLUX_MSGTYPE_EVENT
                                              | FLUX_MSGTYPE_REQUEST),
            "router-entry: received %s", flux_msg_typestr (type));

    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            like (topic, "event.subscribe|event.unsubscribe|service.add|service.remove|rtest.hello",
                  "router-entry: response is %s", topic);
            break;
        case FLUX_MSGTYPE_EVENT:
            like (topic, "rtest.event",
                  "router-entry: event is %s", topic);
            break;
        case FLUX_MSGTYPE_REQUEST:
            like (topic, "testfu.bar",
                  "router-entry: request is %s", topic);
            break;
    }
    flux_reactor_stop (r);
    return 0;
}

void test_basic (flux_t *h)
{
    flux_reactor_t *r;
    struct router *rtr;
    struct router_entry *entry;
    flux_msg_t *request;

    if (!(r = flux_get_reactor (h)))
        BAIL_OUT ("flux_get_reactor failed");

    rtr = router_create (h);
    ok (rtr != NULL,
        "basic: router_create worked");

    /* Add "client" (with fake uuid==abcd) which will receive messages
     * via basic_recv().
     */
    entry = router_entry_add (rtr, "abcd", basic_recv, r);
    ok (entry != NULL,
        "basic: registered router entry");

    /* Send an rtest.hello request from client (represented by 'entry').
     * The router conditions the request and sends it to server 'h'.
     * Server responds, and router routes response to entry callback,
     * which stops reactor.
     */
    if (!(request = flux_request_encode ("rtest.hello", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    router_entry_recv (entry, request); // router receives message from abcd
    diag ("basic: sent rtest.hello request");
    flux_msg_destroy (request);
    ok (flux_reactor_run (r, 0) >= 0,
        "basic: reactor processed one message");

    /* Subscribe to rtest events.
     * Cobble together an internal subscribe request for router.
     * Send request and receive response.  Couple notes:
     * - test server connector sub/unsub operations are no-ops
     * - basic_recv() is called in the context of router_entry_recv()
     *   in this case so don't start the reactor.
     */
    if (!(request = flux_request_encode ("event.subscribe",
                                         "{\"topic\":\"rtest\"}")))
        BAIL_OUT ("flux_request_encode failed");
    router_entry_recv (entry, request); // router receives message from abcd
    diag ("basic: sent event.subscribe request");
    flux_msg_destroy (request);

    /* Send an rtest.pub request from client.
     * Send request and receive event as above.
     */
    if (!(request = flux_request_encode ("rtest.pub", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    router_entry_recv (entry, request); // router receives message from abcd
    diag ("basic: sent rtest.pub request");
    flux_msg_destroy (request);
    ok (flux_reactor_run (r, 0) >= 0,
        "basic: reactor processed one message");

    /* Now unsubscribe to rtest events.
     */
    if (!(request = flux_request_encode ("event.unsubscribe",
                                         "{\"topic\":\"rtest\"}")))
        BAIL_OUT ("flux_request_encode failed");
    router_entry_recv (entry, request); // router receives message from abcd
    diag ("basic: sent event.unsubscribe request");
    flux_msg_destroy (request);

    /* Register testfu service.
     * Cobble together an internal service.add request for router.
     * Send request and receive response.  This triggers a flux_service_add()
     * call in the router.
     */
    if (!(request = flux_request_encode ("service.add",
                                         "{\"service\":\"testfu\"}")))
        BAIL_OUT ("flux_request_encode failed");
    router_entry_recv (entry, request); // router receives message from abcd
    diag ("basic: sent service.add request");
    flux_msg_destroy (request);
    ok (flux_reactor_run (r, 0) >= 0, "basic: reactor processed one message");

    /* Send testfu.bar request from client.  This will be reflected back
     * as a request (see rtest_reflect_cb()).
     * Send request and receive request as above.
     */
    if (!(request = flux_request_encode ("testfu.bar", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    router_entry_recv (entry, request); // router receives message from abcd
    diag ("basic: sent testfu.bar request");
    flux_msg_destroy (request);
    ok (flux_reactor_run (r, 0) >= 0,
        "basic: reactor processed one message");

    /* Unregister testfu service
     */
    if (!(request = flux_request_encode ("service.remove",
                                         "{\"service\":\"testfu\"}")))
        BAIL_OUT ("flux_request_encode failed");
    router_entry_recv (entry, request); // router receives message from abcd
    ok (flux_reactor_run (r, 0) >= 0, "basic: reactor processed one message");
    flux_msg_destroy (request);

    router_entry_delete (entry);
    router_destroy (rtr);
}

void test_error (flux_t *h)
{
    ok (router_renew (NULL) == 0,
        "router_renew rtr=NULL works as no-op");
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    diag ("starting test server");
    test_server_environment_init ("test_router");

    if (!(h = test_server_create (FLUX_O_TEST_NOSUB, server_cb, NULL)))
        BAIL_OUT ("test_server_create failed");

    test_basic (h);
    test_error (h);

    diag ("stopping test server");
    if (test_server_stop (h) < 0)
        BAIL_OUT ("test_server_stop failed");
    flux_close (h);

    done_testing ();

    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
