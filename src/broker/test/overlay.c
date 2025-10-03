/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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
#include <string.h>
#include <uuid.h>
#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif
#include <flux/core.h>
#include <zmq.h>

#include "src/common/libtap/tap.h"
#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libzmqutil/sockopt.h"
#include "src/common/libzmqutil/cert.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/stdlog.h"
#include "src/common/libutil/unlink_recursive.h"
#include "ccan/str/str.h"

#include "src/broker/overlay.h"
#include "src/broker/attr.h"
#include "src/broker/topology.h"

#include "src/common/libtestutil/test_file.h"

static zlist_t *logs;
void *zctx;

struct context {
    struct overlay *ov;
    flux_t *h;
    attr_t *attrs;
    char name[32];
    char uri[64];
    flux_t *h_channel;
    flux_watcher_t *w_channel;
    int rank;
    int size;
    struct topology *topo;
    char *uuid;
    const flux_msg_t *msg;
};

void clear_list (zlist_t *list)
{
    char *s;
    while ((s = zlist_pop (list)))
        free (s);
}

int match_list (zlist_t *list, const char *key)
{
    char *s;
    int count = 0;

    s = zlist_first (list);
    while (s) {
        if (strstr (s, key) != NULL)
            count++;
        s = zlist_next (list);
    }
    return count;
}

void check_attr (struct context *ctx, const char *k, const char *v)
{
    const char *val;

    ok (attr_get (ctx->attrs, k, &val, NULL)  == 0
        && ((v == NULL && val == NULL)
            || (v != NULL && val != NULL && streq (v, val))),
        "%s: %s=%s", ctx->name, k, v ? v : "NULL");
}

static char *init_broker_uuid (flux_t *h, attr_t *attrs)
{
    uuid_t uuid;
    char *uuid_str;

    if (!(uuid_str = calloc (1, UUID_STR_LEN)))
        return NULL;
    uuid_generate (uuid);
    uuid_unparse (uuid, uuid_str);
    if (attr_add (attrs, "broker.uuid", uuid_str, ATTR_IMMUTABLE) < 0) {
        free (uuid_str);
        return NULL;
    }
    return uuid_str;
}

void ctx_destroy (struct context *ctx)
{
    flux_watcher_destroy (ctx->w_channel);
    flux_close (ctx->h_channel);
    attr_destroy (ctx->attrs);
    overlay_destroy (ctx->ov);
    flux_msg_decref (ctx->msg);
    topology_decref (ctx->topo);
    free (ctx->uuid);
    free (ctx);
}

struct context *ctx_create (flux_t *h,
                            int size,
                            int rank,
                            const char *topo_uri,
                            flux_watcher_f cb)
{
    struct context *ctx;
    flux_error_t error;
    flux_reactor_t *r;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        BAIL_OUT ("calloc failed");
    if (!(ctx->attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");
    if (!(ctx->uuid = init_broker_uuid (h, ctx->attrs)))
        BAIL_OUT ("error creating broker.uuid");
    if (!(ctx->topo = topology_create (topo_uri, size, &error)))
        BAIL_OUT ("cannot create '%s' topology: %s", topo_uri, error.text);
    if (topology_set_rank (ctx->topo, rank) < 0)
        BAIL_OUT ("cannot set topology rank");
    ctx->h = h;
    ctx->size = size;
    ctx->rank = rank;
    snprintf (ctx->name, sizeof (ctx->name), "test%d", rank);
    snprintf (ctx->uri, sizeof (ctx->uri), "interthread://test%d", rank);
    if (!(ctx->ov = overlay_create (h,
                                    ctx->name,
                                    ctx->attrs,
                                    zctx,
                                    ctx->uri,
                                    &error)))
        BAIL_OUT ("overlay_create: %s", error.text);
    if (!(r = flux_get_reactor (h)))
        BAIL_OUT ("flux_get_reactor failed");
    if (!(ctx->h_channel = flux_open (ctx->uri, 0))
        || flux_set_reactor (ctx->h_channel, r))
        BAIL_OUT ("open %s: %s", ctx->uri, strerror (errno));
    if (cb) {
        ctx->w_channel = flux_handle_watcher_create (r,
                                                     ctx->h_channel,
                                                     FLUX_POLLIN,
                                                     cb,
                                                     ctx);
        if (!ctx->w_channel)
            BAIL_OUT ("could not create handle watcher");
    }
    diag ("created %s: rank %d size %d uuid %s",
          ctx->name, ctx->rank, ctx->size, ctx->uuid);

    return ctx;
}

void single (flux_t *h)
{
    flux_reactor_t *r = flux_get_reactor (h);
    struct context *ctx = ctx_create (h, 1, 0, "kary:2", NULL);
    flux_msg_t *msg;
    char *s;
    struct idset *critical_ranks;
    const char *topic;
    uint32_t seq;

    ok (overlay_set_topology (ctx->ov, ctx->topo) == 0,
        "%s: overlay_set_topology size=1 rank=0 works", ctx->name);

    ok (overlay_get_size (ctx->ov) == 1,
        "%s: overlay_get_size returns 1", ctx->name);
    ok (overlay_get_rank (ctx->ov) == 0,
        "%s: overlay_get_rank returns 0", ctx->name);

    ok ((critical_ranks = overlay_get_default_critical_ranks (ctx->ov)) != NULL,
        "%s: overlay_get_default_critical_ranks works", ctx->name);
    if (!(s = idset_encode (critical_ranks, IDSET_FLAG_RANGE)))
        BAIL_OUT ("idset_encode");
    is (s, "0",
        "%s: overlay_get_default_critical_ranks returned %s",
        ctx->name, s);
    free (s);
    idset_destroy (critical_ranks);

    ok (overlay_register_attrs (ctx->ov) == 0,
        "%s: overlay_register_attrs works", ctx->name);
    check_attr (ctx, "tbon.parent-endpoint", NULL);
    check_attr (ctx, "rank", "0");
    check_attr (ctx, "size", "1");
    check_attr (ctx, "tbon.level", "0");
    check_attr (ctx, "tbon.maxlevel", "0");
    check_attr (ctx, "tbon.descendants", "0");

    /* No parent uri.
     * No bind uri because no children
     */
    ok (overlay_get_parent_uri (ctx->ov) == NULL,
        "%s: overlay_get_parent_uri returned NULL", ctx->name);
    ok (overlay_get_bind_uri (ctx->ov) == NULL,
        "%s: overlay_get_bind_uri returned NULL", ctx->name);

    /* Event
     * Overlay re-publishes non-sequenced message, so we get it
     * back with a sequence number.
     */
    if (!(msg = flux_event_encode ("foo_event", NULL)))
        BAIL_OUT ("flux_event_encode failed");
    ok (flux_send (ctx->h_channel, msg, 0) == 0,
        "%s: flux_send event works", ctx->name);
    flux_msg_decref (msg);

    ok (flux_reactor_run (r, FLUX_REACTOR_ONCE) >= 0,
        "flux_reactor_run ONCE");

    msg = flux_recv (ctx->h_channel, FLUX_MATCH_EVENT, FLUX_O_NONBLOCK);
    ok (flux_msg_get_topic (msg, &topic) == 0 && streq (topic, "foo_event"),
        "%s: overlay published our message", ctx->name);
    ok (flux_msg_get_seq (msg, &seq) == 0 && seq == 1,
        "%s: event sequence = 1", ctx->name);
    flux_msg_decref (msg);

    /* Event publish request
     */
    if (!(msg = flux_request_encode ("overlay.publish", NULL))
        || flux_msg_pack (msg,
                          "{s:s s:i}",
                          "topic", "smurf",
                          "flags", FLUX_MSGFLAG_PRIVATE) < 0)
        BAIL_OUT ("flux_request_encode failed");
    ok (flux_send (ctx->h, msg, 0) == 0,
        "%s: flux_send event works", ctx->name);
    flux_msg_decref (msg);

    ok (flux_reactor_run (r, FLUX_REACTOR_ONCE) >= 0,
        "flux_reactor_run ONCE");

    msg = flux_recv (ctx->h, FLUX_MATCH_RESPONSE, FLUX_O_NONBLOCK);
    ok (flux_msg_get_topic (msg, &topic) == 0
        && streq (topic, "overlay.publish"),
        "%s overlay responded to publish request", ctx->name);
    flux_msg_decref (msg);

    msg = flux_recv (ctx->h_channel, FLUX_MATCH_EVENT, FLUX_O_NONBLOCK);
    ok (flux_msg_get_topic (msg, &topic) == 0 && streq (topic, "smurf"),
        "%s: event message is received", ctx->name);
    ok (flux_msg_get_seq (msg, &seq) == 0 && seq == 2,
        "%s: event sequence is 2", ctx->name);
    ok (flux_msg_is_private (msg),
        "%s: privacy flag is set", ctx->name);
    flux_msg_decref (msg);

    /* Response
     * Will try child but there isn't one, so message is dropped.
     */
    if (!(msg = flux_response_encode ("foo_response", NULL)))
        BAIL_OUT ("flux_response_encode failed");
    ok (flux_send (ctx->h_channel, msg, 0) == 0,
        "%s: flux_send response works", ctx->name);
    flux_msg_decref (msg);

    ok (flux_reactor_run (r, FLUX_REACTOR_ONCE) >= 0,
        "flux_reactor_run ONCE");

    ok (!flux_recv (ctx->h_channel, FLUX_MATCH_ANY, FLUX_O_NONBLOCK),
        "flux_recv got nothing (response was dropped)");
    ok (match_list (logs, "error sending response to child") > 0,
        "%s: overlay logged expected error", ctx->name);

    /* Request
     * Should get an ENOSYS response since request is not rank-addressed
     */
    if (!(msg = flux_request_encode ("foo_request", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    errno = 0;
    ok (flux_send (ctx->h_channel, msg, 0) == 0,
        "%s: flux_send request works", ctx->name);
    flux_msg_decref (msg);

    ok (flux_reactor_run (r, FLUX_REACTOR_ONCE) >= 0,
        "flux_reactor_run ONCE");

    msg = flux_recv (ctx->h_channel, FLUX_MATCH_ANY, FLUX_O_NONBLOCK);
    ok (flux_msg_get_topic (msg, &topic) == 0 && streq (topic, "foo_request"),
        "%s: overlay responded to our request", ctx->name);
    errno = 0;
    ok (flux_response_decode (msg, NULL, NULL) < 0 && errno == ENOSYS,
        "%s: and response is ENOSYS", ctx->name);
    flux_msg_decref (msg);

    /* Request - address to rank 1
     * Should get an EHOSTUNREACH response.
     */
    if (!(msg = flux_request_encode ("foo_request", NULL))
        || flux_msg_set_nodeid (msg, 1) < 0)
        BAIL_OUT ("flux_request_encode failed");
    errno = 0;
    ok (flux_send (ctx->h_channel, msg, 0) == 0,
        "%s: flux_send request works", ctx->name);
    flux_msg_decref (msg);

    ok (flux_reactor_run (r, FLUX_REACTOR_ONCE) >= 0,
        "flux_reactor_run ONCE");

    msg = flux_recv (ctx->h_channel, FLUX_MATCH_ANY, FLUX_O_NONBLOCK);
    ok (flux_msg_get_topic (msg, &topic) == 0 && streq (topic, "foo_request"),
        "%s: overlay responded to our request", ctx->name);
    errno = 0;
    ok (flux_response_decode (msg, NULL, NULL) < 0 && errno == EHOSTUNREACH,
        "%s: and response is EHOSTUNREACH", ctx->name);
    flux_msg_decref (msg);

    ok (overlay_get_child_peer_count (ctx->ov) == 0,
        "%s: overlay_get_child_peer_count returns 0", ctx->name);

    ctx_destroy (ctx);
}

void recv_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    flux_t *h = flux_handle_watcher_get_flux (w);
    struct context *ctx = arg;
    flux_msg_t *msg;

    if ((msg = flux_recv (h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
        diag ("%s: message received", ctx->name);
        ctx->msg = msg;
        flux_reactor_stop (flux_get_reactor (ctx->h));
    }
}

void timeout_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    diag ("receive timeout");
    errno = ETIMEDOUT;
    flux_reactor_stop_error (r);
}

/* Receive a message with timeout.
 * Returns 0 on success, or -1 with errno=ETIMEDOUT.
 */
const flux_msg_t *recvmsg_timeout (struct context *ctx, double timeout)
{

    flux_reactor_t *r = flux_get_reactor (ctx->h);
    flux_watcher_t *w;
    int rc;

    flux_msg_decref (ctx->msg);
    ctx->msg = NULL;

    if (!(w = flux_timer_watcher_create (r, timeout, 0., timeout_cb, ctx)))
        BAIL_OUT ("flux_timer_watcher_create failed");
    flux_watcher_start (w);

    flux_watcher_start (ctx->w_channel);

    rc = flux_reactor_run (r, 0);

    flux_watcher_stop (ctx->w_channel);

    flux_watcher_destroy (w);

    return rc < 0 ? NULL : ctx->msg;
}

/* Rank 0,1 are properly configured.
 * Rank 2 will try to get involved without proper credentials etc.
 */
void trio (flux_t *h)
{
    struct context *ctx[2];
    int size = 3;
    char parent_uri[PATH_MAX + 64], uri[PATH_MAX + 64];
    const char *server_pubkey;
    const char *client_pubkey;
    const char *tmp;
    const flux_msg_t *rmsg;
    flux_msg_t *msg;
    const char *topic;
    void *zsock_none;
    void *zsock_curve;
    struct cert *cert;
    const char *sender;
    flux_error_t error;

    ctx[0] = ctx_create (h, size, 0, "kary:2", recv_cb);

    ok (overlay_set_topology (ctx[0]->ov, ctx[0]->topo) == 0,
        "%s: overlay_set_topology works", ctx[0]->name);

    ok ((server_pubkey = overlay_cert_pubkey (ctx[0]->ov)) != NULL,
        "%s: overlay_cert_pubkey works", ctx[0]->name);

    snprintf (parent_uri, sizeof (parent_uri), "ipc://%s/flux_ipc_%s", get_test_dir (), ctx[0]->name);
    ok (overlay_bind (ctx[0]->ov, parent_uri, NULL, NULL) == 0,
        "%s: overlay_bind %s works", ctx[0]->name, parent_uri);

    ctx[1] = ctx_create (h, size, 1, "kary:2", recv_cb);

    ok (overlay_set_topology (ctx[1]->ov, ctx[1]->topo) == 0,
        "%s: overlay_set_topology works", ctx[1]->name);

    ok ((client_pubkey = overlay_cert_pubkey (ctx[1]->ov)) != NULL,
        "%s: overlay_cert_pubkey works", ctx[1]->name);
    ok (overlay_set_parent_uri (ctx[1]->ov, parent_uri) == 0,
        "%s: overlay_set_parent_uri %s works", ctx[1]->name, parent_uri);
    tmp = overlay_get_parent_uri (ctx[1]->ov);
    ok (tmp != NULL && streq (tmp, parent_uri),
        "%s: overlay_get_parent_uri returns same string", ctx[1]->name);
    ok (overlay_set_parent_pubkey (ctx[1]->ov, server_pubkey) == 0,
        "%s: overlay_set_parent_pubkey works", ctx[1]->name);

    ok (overlay_authorize (ctx[0]->ov, ctx[0]->name, client_pubkey) == 0,
        "%s: overlay_authorize %s works", ctx[0]->name, client_pubkey);
    ok (overlay_connect (ctx[1]->ov) == 0,
        "%s: overlay_connect works", ctx[1]->name);

    errno = 0;
    ok (overlay_authorize (ctx[0]->ov, "foo", "1234") < 0 && errno == EINVAL,
        "overlay_authorize with short pubkey fails with EINVAL");

    /* Send request 1->0
     * Side effect: during recvmsg_timeout(), reactor allows hello request
     * from 1->0 to be processed at 0.
     */
    if (!(msg = flux_request_encode ("meep", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    ok (flux_send (ctx[1]->h_channel, msg, 0) == 0,
        "%s: flux_send request works", ctx[1]->name);
    flux_msg_decref (msg);

    rmsg = recvmsg_timeout (ctx[0], 5);
    ok (rmsg != NULL,
        "%s: request was received by overlay", ctx[0]->name);
    ok (!flux_msg_is_local (rmsg),
        "%s: flux_msg_is_local fails on parent from child",
        ctx[1]->name);
    ok (flux_msg_get_topic (rmsg, &topic) == 0 && streq (topic, "meep"),
        "%s: received message has expected topic", ctx[0]->name);
    ok ((sender = flux_msg_route_first (rmsg)) != NULL
        && streq (sender, ctx[1]->uuid),
        "%s: received message sender is rank 1", ctx[0]->name);

    /* Send request 0->1
     * Side effect: during recvmsg_timeout(), reactor allows hello response
     * from 0->1 to be processed at 1.
     */
    if (!(msg = flux_request_encode ("errr", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    if (flux_msg_set_nodeid (msg, 1) < 0)
        BAIL_OUT ("flux_msg_set_nodeid failed");
    ok (flux_send (ctx[0]->h_channel, msg, 0) == 0,
        "%s: flux_send request nodeid=1 works", ctx[0]->name);
    flux_msg_decref (msg);

    rmsg = recvmsg_timeout (ctx[1], 5);
    ok (rmsg != NULL,
        "%s: request was received by overlay", ctx[1]->name);
    ok (!flux_msg_is_local (rmsg),
        "%s: flux_msg_is_local fails on child from parent",
        ctx[1]->name);
    ok (flux_msg_get_topic (rmsg, &topic) == 0 && streq (topic, "errr"),
        "%s: request has expected topic", ctx[1]->name);
    ok ((sender = flux_msg_route_first (rmsg)) != NULL
        && streq (sender, ctx[0]->uuid),
        "%s: request sender is rank 0", ctx[1]->name);

    /* Response 1->0
     */
    if (!(msg = flux_response_encode ("m000", NULL)))
        BAIL_OUT ("flux_response_encode failed");
    if (flux_msg_route_push (msg, ctx[0]->uuid) < 0)
        BAIL_OUT ("flux_msg_route_push failed");
    ok (flux_send (ctx[1]->h_channel, msg, 0) == 0,
        "%s: flux_send response works", ctx[1]->name);
    flux_msg_decref (msg);

    rmsg = recvmsg_timeout (ctx[0], 5);
    ok (rmsg != NULL,
        "%s: response was received by overlay", ctx[0]->name);
    ok (!flux_msg_is_local (rmsg),
        "%s: flux_msg_is_local returns false for response from child",
        ctx[0]->name);
    ok (flux_msg_get_topic (rmsg, &topic) == 0 && streq (topic, "m000"),
        "%s: received message has expected topic", ctx[0]->name);
    ok (flux_msg_route_count (rmsg) == 0,
        "%s: received message has no routes", ctx[0]->name);

    /* Event 1->0
     */
    if (!(msg = flux_event_encode ("eeek", NULL)))
        BAIL_OUT ("flux_event_encode failed");
    ok (flux_send (ctx[1]->h_channel, msg, 0) == 0,
        "%s: flux_send event works", ctx[1]->name);
    flux_msg_decref (msg);

    rmsg = recvmsg_timeout (ctx[0], 5);
    ok (rmsg != NULL,
        "%s: event was received by overlay", ctx[0]->name);
    ok (flux_msg_get_topic (rmsg, &topic) == 0 && streq (topic, "eeek"),
        "%s: received message has expected topic", ctx[0]->name);
    ok (!flux_msg_is_local (rmsg),
        "%s: flux_msg_is_local returns false for event from child",
        ctx[0]->name);

    rmsg = recvmsg_timeout (ctx[1], 5);
    ok (rmsg != NULL,
        "%s: event was received by overlay", ctx[1]->name);
    ok (flux_msg_get_topic (rmsg, &topic) == 0 && streq (topic, "eeek"),
        "%s: received message has expected topic", ctx[1]->name);

    /* Response 0->1
     */
    if (!(msg = flux_response_encode ("moop", NULL)))
        BAIL_OUT ("flux_response_encode failed");
    if (flux_msg_route_push (msg, ctx[1]->uuid) < 0)
        BAIL_OUT ("flux_msg_route_push failed");
    ok (flux_send (ctx[0]->h_channel, msg, 0) == 0,
        "%s: overlay_sendmsg response uuid of rank 1 works", ctx[0]->name);
    flux_msg_decref (msg);

    rmsg = recvmsg_timeout (ctx[1], 5);
    ok (msg != NULL,
        "%s: response was received by overlay", ctx[1]->name);
    ok (flux_msg_get_topic (rmsg, &topic) == 0 && streq (topic, "moop"),
        "%s: response has expected topic", ctx[1]->name);
    ok (flux_msg_route_count (rmsg) == 0,
        "%s: response has no routes", ctx[1]->name);

    /* Event 0->1,0
     */
    if (!(msg = flux_event_encode ("eeeb", NULL)))
        BAIL_OUT ("flux_event_encode failed");
    ok (flux_send (ctx[0]->h_channel, msg, 0) == 0,
        "%s: overlay_sendmsg event works", ctx[0]->name);
    flux_msg_decref (msg);

    rmsg = recvmsg_timeout (ctx[1], 5);
    ok (rmsg != NULL,
        "%s: event was received by overlay", ctx[1]->name);
    ok (flux_msg_get_topic (rmsg, &topic) == 0 && streq (topic, "eeeb"),
        "%s: received message has expected topic", ctx[1]->name);

    rmsg = recvmsg_timeout (ctx[0], 5);
    ok (rmsg != NULL,
        "%s: event was received by overlay", ctx[0]->name);
    ok (flux_msg_get_topic (rmsg, &topic) == 0 && streq (topic, "eeeb"),
        "%s: received message has expected topic", ctx[0]->name);

    /* Cover some error code in overlay_bind() where the ZAP handler
     * fails to initialize because its endpoint is already bound.
     */
    errno = 0;
    if (snprintf (uri, sizeof uri, "ipc://%s/flux_ipc_foo", get_test_dir ()) < 0)
        BAIL_OUT("asprintf failed");
    err_init (&error);
    ok (overlay_bind (ctx[1]->ov, uri, NULL, &error) < 0
        && errno == EADDRINUSE,
        "%s: second overlay_bind in proc fails with EADDRINUSE", ctx[0]->name);
    diag ("%s", error.text);

    /* Various tests of rank 2 without proper authorization.
     * First a baseline - resend 1->0 and make sure timed recv works.
     * Test message will be reused below.
     */
    /* 0) Baseline
     * 'msg' created here will be reused in each test.
     */
    if (!(msg = flux_request_encode ("erp", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    ok (flux_send (ctx[1]->h_channel, msg, 0) == 0,
        "%s: flux_send request works", ctx[1]->name);
    rmsg = recvmsg_timeout (ctx[0], 5);
    ok (rmsg != NULL,
        "%s: message was received by overlay", ctx[0]->name);
    errno = 0;
    ok (recvmsg_timeout (ctx[0], 0.1) == NULL  && errno == ETIMEDOUT,
        "%s: test reactor timed out as expected", ctx[0]->name);

    /* 1) No security
     */
    if (!(zsock_none = zmq_socket (zctx, ZMQ_DEALER))
        || zsetsockopt_int (zsock_none, ZMQ_LINGER, 5) < 0
        || zsetsockopt_str (zsock_none, ZMQ_IDENTITY, "2") < 0)
        BAIL_OUT ("zmq_socket failed");
    ok (zmq_connect (zsock_none, parent_uri) == 0,
        "none-2: zmq_connect %s (no security) works", parent_uri);
    ok (zmqutil_msg_send (zsock_none, msg) == 0,
        "none-2: zsock_msg_sendzsock works");

    /* 2) Curve, and correct server public key, but client public key
     * was not authorized
     */
    if (!(zsock_curve = zmq_socket (zctx, ZMQ_DEALER))
        || zsetsockopt_int (zsock_curve, ZMQ_LINGER, 5) < 0
        || zsetsockopt_str (zsock_curve, ZMQ_ZAP_DOMAIN, "flux") < 0
        || zsetsockopt_str (zsock_curve, ZMQ_CURVE_SERVERKEY, server_pubkey) < 0
        || zsetsockopt_str (zsock_curve, ZMQ_IDENTITY, "2") < 0)
        BAIL_OUT ("zmq_socket failed");
    if (!(cert = cert_create ()))
        BAIL_OUT ("zcert_new failed");
    cert_apply (cert, zsock_curve);
    cert_destroy (cert);
    ok (zmq_connect (zsock_curve, parent_uri) == 0,
        "curve-2: zmq_connect %s works", parent_uri);
    ok (zmqutil_msg_send (zsock_curve, msg) == 0,
        "curve-2: zmqutil_msg_send works");

    /* Neither of the above attempts should have gotten a message through.
     */
    errno = 0;
    ok (recvmsg_timeout (ctx[0], 1.0) == NULL  && errno == ETIMEDOUT,
        "%s: no messages received within 1.0s", ctx[0]->name);

    flux_msg_decref (msg);
    zmq_close (zsock_none);
    zmq_close (zsock_curve);

    // ENOENT is acceptable because in some cases the ipc node is already
    // cleaned up here
    if (unlink (parent_uri) < 0 && errno != ENOENT)
        BAIL_OUT ("could not remove %s", parent_uri);
    if (unlink (uri) < 0 && errno != ENOENT)
        BAIL_OUT ("could not remove %s", uri);

    ctx_destroy (ctx[1]);
    ctx_destroy (ctx[0]);
}

void test_create (flux_t *h,
                  int size,
                  struct context *ctx[])
{
    char uri[PATH_MAX + 200] = { 0 };
    int rank;
    flux_error_t error;

    for (rank = 0; rank < size; rank++) {
        ctx[rank] = ctx_create (h, size, rank, NULL, recv_cb);
        if (overlay_set_topology (ctx[rank]->ov, ctx[rank]->topo) < 0)
            BAIL_OUT ("%s: overlay_set_topology failed", ctx[rank]->name);
        if (rank == 0) {
            snprintf (uri, sizeof (uri), "ipc://%s/flux_ipc_%s", get_test_dir (), ctx[0]->name);
            /* Call overlay_bind() before overlay_authorize() is called
             * for the other ranks, since overlay_bind() creates the ZAP
             * handler, and overlay_authorize() will fail if it doesn't
             * exist.
             */
            if (overlay_bind (ctx[0]->ov, uri, NULL, &error) < 0)
                BAIL_OUT ("%s: overlay_bind failed: %s",
                          ctx[0]->name,
                          error.text);
        }
        else {
            if (overlay_authorize (ctx[0]->ov,
                                   ctx[rank]->name,
                                   overlay_cert_pubkey (ctx[rank]->ov)) < 0)
                BAIL_OUT ("%s: overlay_authorize failed", ctx[rank]->name);
            if (overlay_set_parent_pubkey (ctx[rank]->ov,
                                   overlay_cert_pubkey (ctx[0]->ov)) < 0)
                BAIL_OUT ("%s: overlay_set_parent_pubkey failed", ctx[1]->name);
            if (overlay_set_parent_uri (ctx[rank]->ov, uri) < 0)
                BAIL_OUT ("%s: overlay_set_parent_uri %s failed", ctx[1]->name);
        }
    }

}

void test_destroy (int size, struct context *ctx[])
{
    char uri[PATH_MAX + 200] = { 0 };
    int rank;

    snprintf (uri, sizeof (uri), "ipc://%s/flux_ipc_%s", get_test_dir (), ctx[0]->name);
    if (unlink (uri) < 0 && errno != ENOENT)
        BAIL_OUT ("could not remove %s", uri);

    for (rank = 0; rank < size; rank++)
        ctx_destroy (ctx[rank]);
}

void monitor_diag_cb (struct overlay *ov, uint32_t rank, void *arg)
{
    struct context *ctx = arg;
    diag ("%s: rank=%d status=%s children=%d parent_error=%s",
          ctx->name,
          (int)rank,
          overlay_get_subtree_status (ov, rank),
          overlay_get_child_peer_count (ov),
          overlay_parent_error (ov) ? "true" : "false");
}

void monitor_cb (struct overlay *ov, uint32_t rank, void *arg)
{
    struct context *ctx = arg;
    const char *status = overlay_get_subtree_status (ov, rank);
    monitor_diag_cb (ov, rank, arg);
    if (overlay_parent_error (ov)
        || streq (status, "full")
        || streq (status, "partial")
        || streq (status, "lost")
        || streq (status, "offline"))
        flux_reactor_stop (flux_get_reactor (ctx->h));
}

void check_monitor (flux_t *h)
{
    const int size = 5;
    struct context *ctx[size];

    diag ("check_monitor BEGIN");

    test_create (h, size, ctx);

    diag ("check_monitor test_create returned");

    /* If anything changes on rank 0, stop the reactor
     */
    overlay_set_monitor_cb (ctx[0]->ov, monitor_cb, ctx[0]);

    /* connect (1->0) - rank 0 stops reactor on connect */
    overlay_set_monitor_cb (ctx[1]->ov, monitor_diag_cb, ctx[1]);
    if (overlay_connect (ctx[1]->ov) < 0)
        BAIL_OUT ("%s: overlay_connect failed", ctx[1]->name);
    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "%s: reactor ran until child connected", ctx[0]->name);
    ok (overlay_get_child_peer_count (ctx[0]->ov) == 1,
        "%s: overlay_get_child_peer_count returns 1", ctx[0]->name);
    overlay_set_monitor_cb (ctx[0]->ov, monitor_diag_cb, ctx[0]);

    /* connect (2->0) - rank 2 stops reactor on connect */
    overlay_set_monitor_cb (ctx[2]->ov, monitor_cb, ctx[2]);
    if (overlay_connect (ctx[2]->ov) < 0)
        BAIL_OUT ("%s: overlay_connect failed", ctx[2]->name);

    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "%s: reactor ran until child connected", ctx[0]->name);
    ok (overlay_get_child_peer_count (ctx[0]->ov) == 2,
        "%s: overlay_get_child_peer_count returns 2", ctx[0]->name);
    ok (overlay_parent_error (ctx[2]->ov) == false,
        "%s: overlay_parent_error returns false", ctx[2]->name);

    /* rank 3 will try to connect with simulated wrong flux-core version
     * Disable rank 0 stopping the reactor and enable rank 3 to do it.
     */
    overlay_set_monitor_cb (ctx[3]->ov, monitor_cb, ctx[3]);
    overlay_test_set_version (ctx[3]->ov, 0xffffff);
    if (overlay_connect (ctx[3]->ov) < 0)
        BAIL_OUT ("%s: overlay_connect failed", ctx[3]->name);

    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "%s: reactor ran until bad version connection fails", ctx[0]->name);
    ok (overlay_get_child_peer_count (ctx[0]->ov) == 2,
        "%s: overlay_get_child_peer_count is still 2", ctx[0]->name);
    ok (overlay_parent_error (ctx[3]->ov) == true,
        "%s: overlay_parent_error returns true", ctx[3]->name);
    overlay_set_monitor_cb (ctx[3]->ov, monitor_diag_cb, ctx[3]);

    /* rank 4 will have its rank altered to '42' for overlay.hello
     */
    overlay_set_monitor_cb (ctx[4]->ov, monitor_cb, ctx[4]);
    overlay_test_set_rank (ctx[4]->ov, 42);
    if (overlay_connect (ctx[4]->ov) < 0)
        BAIL_OUT ("%s: overlay_connect failed", ctx[4]->name);

    ok (flux_reactor_run (flux_get_reactor (h), 0) >= 0,
        "%s: reactor ran until bad rank connection fails", ctx[0]->name);
    ok (overlay_get_child_peer_count (ctx[0]->ov) == 2,
        "%s: overlay_get_child_peer_count is still 2", ctx[0]->name);
    ok (overlay_parent_error (ctx[4]->ov) == true,
        "%s: overlay_parent_error returns true", ctx[4]->name);

    test_destroy (size, ctx);
}

/* Probe some possible failure cases
 */
void wrongness (flux_t *h)
{
    struct overlay *ov;
    attr_t *attrs;
    flux_error_t error;
    char *uuid;

    if (!(attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");

    err_init (&error);
    errno = 0;
    ok (overlay_create (h,
                        "test0",
                        attrs,
                        zctx,
                        "interthread://x",
                        &error) == NULL
        && errno == ENOENT,
        "overlay_create w/o broker.uuid fails with ENOENT");
    diag ("%s", error.text);

    if (!init_broker_uuid (h, attrs))
        BAIL_OUT ("error initializing uuid");

    err_init (&error);
    errno = 0;
    ok (overlay_create (NULL,
                        "test0",
                        attrs,
                        zctx,
                        "interthread://x",
                        &error) == NULL
        && errno == EINVAL,
        "overlay_create h=NULL fails with EINVAL");
    diag ("%s", error.text);

    err_init (&error);
    errno = 0;
    ok (overlay_create (h,
                        "test0",
                        NULL,
                        zctx,
                        "interthread://x",
                        &error) == NULL
        && errno == EINVAL,
        "overlay_create attrs=NULL fails with EINVAL");
    diag ("%s", error.text);
    attr_destroy (attrs);

    if (!(attrs = attr_create ()))
        BAIL_OUT ("attr_create failed");
    if (!(uuid = init_broker_uuid (h, attrs)))
        BAIL_OUT ("error creating broker.uuid");
    free (uuid);
    if (!(ov = overlay_create (h,
                               "test0",
                               attrs,
                               zctx,
                               "interthread://x",
                               &error)))
        BAIL_OUT ("overlay_create failed: %s", error.text);

    errno = 0;
    struct context *ctx = ctx_create (h, 1, 0, "kary:2", NULL);
    char * uri = NULL;
    if (asprintf (&uri, "ipc://%s/flux_ipc_foobar", get_test_dir ()) < 0)
        BAIL_OUT ("asprintf failed");
    err_init (&error);
    ok (overlay_bind (ov, uri, NULL, &error) < 0 && errno == EINVAL,
        "overlay_bind fails if called before rank is known");
    diag ("%s", error.text);
    ctx_destroy(ctx);
    if (unlink (uri) < 0 && errno != ENOENT)
        BAIL_OUT ("could not remove %s", uri);
    free (uri);

    ok (!flux_msg_is_local (NULL),
        "flux_msg_is_local (NULL) returns false");

    overlay_destroy (ov);
    attr_destroy (attrs);
}

void diag_logger (const char *buf, int len, void *arg)
{
    struct stdlog_header hdr;
    const char *msg;
    size_t msglen;
    int severity;
    char *s;

    if (stdlog_decode (buf, len, &hdr, NULL, NULL, &msg, &msglen) < 0)
        BAIL_OUT ("stdlog_decode failed");
    severity = STDLOG_SEVERITY (hdr.pri);
    if (asprintf (&s,
                  "%s: %.*s\n",
                  stdlog_severity_to_string (severity),
                  (int)msglen,
                  msg) < 0)
        BAIL_OUT ("asprintf failed");
    diag (s);
    if (zlist_append (logs, s) < 0)
        BAIL_OUT ("zlist_append failed");
}

int main (int argc, char *argv[])
{
    flux_t *h;

    plan (NO_PLAN);

    if (!(zctx = zmq_ctx_new ()))
        BAIL_OUT ("zmq_ctx_new failed");

    if (!(logs = zlist_new ()))
        BAIL_OUT ("zlist_new failed");
    if (!(h = flux_open ("loop://", 0)))
        BAIL_OUT ("could not create loop handle");
    if (flux_attr_set_cacheonly (h, "rank", "0") < 0)
        BAIL_OUT ("flux_attr_set_cacheonly rank failed");
    if (flux_attr_set_cacheonly (h, "hostlist", "test[0-7]") < 0)
        BAIL_OUT ("flux_attr_set_cacheonly hostlist failed");
    flux_log_set_redirect (h, diag_logger, NULL);
    flux_log (h, LOG_INFO, "test log message");

    single (h);
    clear_list (logs);

    trio (h);
    clear_list (logs);

    /* trio() and check_monitor() tests will bind to the same address
     * in their tests.  Test can be racy and fail with EADDRINUSE if
     * prior tests did not complete cleanup.  To ensure there are no
     * issues, destroy & recreate zctx.  See issue 6404.
     */
    zmq_ctx_term (zctx);
    if (!(zctx = zmq_ctx_new ()))
        BAIL_OUT ("failed to recreate zmq context");

    check_monitor (h);
    clear_list (logs);

    wrongness (h);
    clear_list (logs);

    flux_close (h);
    zlist_destroy (&logs);

    zmq_ctx_term (zctx);

    done_testing ();
}

/*
 * vi: ts=4 sw=4 expandtab
 */



