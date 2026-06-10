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
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <zmq.h>
#include <unistd.h>
#include <flux/core.h>
#include <inttypes.h>
#include <jansson.h>
#include <uuid.h>

#include "src/common/libpmi/bizcard.h"
#include "src/common/libpmi/upmi.h"
#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libzmqutil/sockopt.h"
#include "src/common/libzmqutil/zwatcher.h"
#include "src/common/libzmqutil/zap.h"
#include "src/common/libzmqutil/cert.h"
#include "src/common/libzmqutil/monitor.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/librouter/rpc_track.h"
#include "src/common/libflux/message_route.h" // for msg_route_sendto()
#include "ccan/str/str.h"
#ifndef HAVE_STRLCPY
#include "src/common/libmissing/strlcpy.h"
#endif

#include "overlay.h"
#include "compat.h"
#include "src/broker/trace.h"
#include "src/broker/state_machine.h"
#include "boot_util.h"
#include "boot_pmi.h"
#include "boot_config.h"
#include "ovconf.h"
#include "children.h"

/* Module debug flag to create zombie socket that blocks zmq_ctx_term().
 * Enable with: flux module debug --setbit 1 overlay
 */
#define DEBUG_ZOMBIESOCKET 1

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif

#define FLUX_ZAP_DOMAIN "flux"

/* Names array is indexed with subtree_status enum.
 * Convert with subtree_status_str()
 */
static const char *subtree_status_names[] = {
    "unknown",
    "full",
    "partial",
    "degraded",
    "lost",
    "offline",
    NULL,
};

struct parent {
    void *zsock;            // NULL on rank 0
    char *uri;
    flux_watcher_t *w;
    int lastsent;
    char *pubkey;
    uint32_t rank;
    char uuid[UUID_STR_LEN];
    bool hello_error;
    bool hello_responded;
    bool offline;           // set upon receipt of CONTROL_DISCONNECT
    bool goodbye_sent;
    struct rpc_track *tracker;
    struct zmqutil_monitor *monitor;
};

/* Wake up periodically (between 'sync_min' and 'sync_max' seconds) and:
 * 1) send control to parent if nothing was sent in 'torpid_min' seconds
 * 2) find children that have not been heard from in 'torpid_max' seconds
 */
static const double sync_min = 1.0;
static const double sync_max = 5.0;

struct overlay {
    void *zctx;
    bool zctx_external;
    struct cert *cert;

    flux_t *h;
    char *hostname;
    flux_reactor_t *reactor;
    flux_msg_handler_t **handlers;
    flux_future_t *f_sync;
    flux_future_t *f_state;
    broker_state_t broker_state;

    struct topology *topo;
    uint32_t size;
    uint32_t rank;
    int event_seq;              // used for sequence verification
    char uuid[UUID_STR_LEN];
    int version;

    struct ovconf config;

    struct parent parent;

    bool spurn_hello;           // no new downstream connections permitted
    struct bizcard *bizcard;
    struct children *children;
    enum subtree_status status;
    struct timespec status_timestamp;

    struct flux_msglist *monitor_requests;

    flux_t *h_channel;
    flux_watcher_t *w_channel;

    struct flux_msglist *health_requests;
    struct flux_msglist *trace_requests;
};

static void overlay_mcast_child (struct overlay *ov, flux_msg_t *msg);
static int overlay_sendmsg_child (struct overlay *ov, const flux_msg_t *msg);
static int overlay_sendmsg_parent (struct overlay *ov, const flux_msg_t *msg);
static void hello_response_handler (struct overlay *ov, const flux_msg_t *msg);
static void hello_request_handler (struct overlay *ov, const flux_msg_t *msg);
static int overlay_control_parent (struct overlay *ov,
                                   enum control_type type,
                                   int status);
static void overlay_health_respond_all (struct overlay *ov);
static int overlay_goodbye_parent (struct overlay *overlay, flux_error_t *errp);
static void overlay_goodbye_cb (struct overlay *ov, const flux_msg_t *msg);
static void overlay_event_checkseq (struct overlay *ov, const flux_msg_t *msg);


static const char *subtree_status_str (enum subtree_status status)
{
    if (status > SUBTREE_STATUS_MAXIMUM)
        return "unknown";
    return subtree_status_names[status];
}

/* Call this function after a child->status changes.
 * It calculates a new subtree status based on the state of children,
 * then if the status has changed, the parent is informed with a
 * control message and any waiting health requests are processed.
 */
static void subtree_status_update (struct overlay *ov)
{
    enum subtree_status status = SUBTREE_STATUS_FULL;
    struct child *child;

    children_foreach (ov->children, child) {
        switch (child->status) {
            case SUBTREE_STATUS_FULL:
                break;
            case SUBTREE_STATUS_PARTIAL:
            case SUBTREE_STATUS_OFFLINE:
                if (status == SUBTREE_STATUS_FULL)
                    status = SUBTREE_STATUS_PARTIAL;
                break;
            case SUBTREE_STATUS_DEGRADED:
            case SUBTREE_STATUS_LOST:
                if (status != SUBTREE_STATUS_DEGRADED)
                    status = SUBTREE_STATUS_DEGRADED;
                break;
            case SUBTREE_STATUS_UNKNOWN:
                break;
        }
    }
    if (ov->status != status) {
        ov->status = status;
        monotime (&ov->status_timestamp);
        overlay_control_parent (ov, CONTROL_STATUS, ov->status);
        overlay_health_respond_all (ov);
    }
}

static void overlay_state_machine_post (struct overlay *ov,
                                        const char *event,
                                        bool is_error)
{
    flux_future_t *f;
    if (!(f = flux_rpc_pack (ov->h,
                             "state-machine.post",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_NORESPONSE,
                             "{s:s s:b}",
                             "event", event,
                             "error", is_error ? 1 : 0)))
        flux_log_error (ov->h, "error posting %s event", event);
    flux_future_destroy (f);
}

static void overlay_monitor_respond_one (flux_t *h,
                                         const flux_msg_t *msg,
                                         struct child *child)
{
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:s s:b}",
                           "rank", child->rank,
                           "status", subtree_status_str (child->status),
                           "torpid", child->torpid ? 1 : 0) < 0)
        flux_log_error (h, "error responding to overlay.monitor");
}

static void overlay_monitor_notify (struct overlay *ov, uint32_t rank)
{
    const flux_msg_t *msg;

    /* Notify monitor requests of possible new peer status or torpidity.
     */
    msg = flux_msglist_first (ov->monitor_requests);
    while (msg) {
        struct child *child;
        if ((child = children_lookup_byrank (ov->children, rank)))
            overlay_monitor_respond_one (ov->h, msg, child);
        msg = flux_msglist_next (ov->monitor_requests);
    }

    /* Notify state machine once all child subtrees are offline
     */
    if (ov->broker_state == STATE_SHUTDOWN
        && ov->children->count > 0
        && children_get_online_count (ov->children) == 0)
        overlay_state_machine_post (ov, "children-complete", false);
}

int overlay_set_topology (struct overlay *ov, struct topology *topo)
{
    ov->topo = topology_incref (topo);

    if (!cert_meta_get (ov->cert, "name")) {
        char val[16];
        snprintf (val, sizeof (val), "%lu", (unsigned long)ov->rank);
        if (cert_meta_set (ov->cert, "name", val) < 0)
            return -1;
    }
    if (!(ov->children = children_create (ov->h, topo)))
        return -1;
    if (ov->children->count > 0) {
        ov->status = SUBTREE_STATUS_PARTIAL;
    }
    else
        ov->status = SUBTREE_STATUS_FULL;
    monotime (&ov->status_timestamp);
    if (ov->rank > 0) {
        ov->parent.rank = topology_get_parent (topo);
        ov->parent.tracker = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG);
    }
    return 0;
}

void overlay_test_set_rank (struct overlay *ov, uint32_t rank)
{
    ov->rank = rank;
}

static bool overlay_parent_error (struct overlay *ov)
{
    return ((ov->parent.hello_responded && ov->parent.hello_error)
            || ov->parent.offline);
}

void overlay_test_set_version (struct overlay *ov, int version)
{
    ov->version = version;
}

void overlay_set_ipv6 (struct overlay *ov, int enable)
{
    ovconf_set_ipv6 (&ov->config, enable);
}

static void log_torpid_child (flux_t *h,
                               uint32_t rank,
                               bool torpid,
                               double duration)
{
    if (torpid) {
        char fsd[64] = "unknown duration";
        (void)fsd_format_duration (fsd, sizeof (fsd), duration);
        flux_log (h,
                  LOG_ERR,
                  "broker on %s (rank %lu) has been unresponsive for %s",
                  flux_get_hostbyrank (h, rank),
                  (unsigned long)rank,
                  fsd);
    }
    else {
        flux_log (h,
                  LOG_ERR,
                  "broker on %s (rank %lu) is responsive now",
                  flux_get_hostbyrank (h, rank),
                  (unsigned long)rank);
    }
}

/* Find children that have not been heard from in a while.
 * If torpid_max is set to zero it means torpid node flagging is disabled.
 * This value may be set on the fly during runtime, so ensure that any
 * torpid nodes are immediately transitioned to non-torpid if that occurs.
 */
static void update_torpid_children (struct overlay *ov)
{
    struct child *child;
    double now = flux_reactor_now (ov->reactor);

    children_foreach (ov->children, child) {
        if (child_is_online (child) && child->lastseen > 0) {
            double duration = now - child->lastseen;

            if (duration >= ov->config.torpid_max
                && ov->config.torpid_max > 0) {
                if (!child->torpid) {
                    log_torpid_child (ov->h, child->rank, true, duration);
                    child->torpid = true;
                    overlay_monitor_notify (ov, child->rank);
                }
            }
            else { // duration < torpid_max OR torpid_max == 0
                if (child->torpid) {
                    log_torpid_child (ov->h, child->rank, false, 0.);
                    child->torpid = false;
                    overlay_monitor_notify (ov, child->rank);
                }
            }
        }
    }
}

int overlay_set_parent_pubkey (struct overlay *ov, const char *pubkey)
{
    if (!(ov->parent.pubkey = strdup (pubkey)))
        return -1;
    return 0;
}

int overlay_set_parent_uri (struct overlay *ov, const char *uri)
{
    free (ov->parent.uri);
    if (!(ov->parent.uri = strdup (uri)))
        return -1;
    return 0;
}

static int overlay_sendmsg_parent (struct overlay *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->parent.zsock || ov->parent.offline || ov->parent.goodbye_sent) {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = zmqutil_msg_send (ov->parent.zsock, msg);
    if (rc == 0) {
        ov->parent.lastsent = flux_reactor_now (ov->reactor);
        trace_overlay_msg (ov->h,
                           "tx",
                           ov->parent.rank,
                           ov->trace_requests,
                           msg);
    }
done:
    return rc;
}

static int overlay_control_parent (struct overlay *ov,
                                   enum control_type type,
                                   int status)
{
    flux_msg_t *msg = NULL;

    if (ov->parent.zsock) {
        if (!(msg = flux_control_encode (type, status)))
            return -1;
        flux_msg_route_enable (msg);
        if (overlay_sendmsg_parent (ov, msg) < 0)
            goto error;
        flux_msg_destroy (msg);
    }
    return 0;
error:
    flux_msg_destroy (msg);
    return -1;
}

static int overlay_control_child (struct overlay *ov,
                                  const char *uuid,
                                  enum control_type type,
                                  int status)
{
    flux_msg_t *msg;

    if (!(msg = flux_control_encode (type, status)))
        return -1;
    flux_msg_route_enable (msg);
    if (flux_msg_route_push (msg, uuid) < 0)
        goto error;
    if (overlay_sendmsg_child (ov, msg) < 0)
        goto error;
    flux_msg_destroy (msg);
    return 0;
error:
    flux_msg_destroy (msg);
    return -1;
}

static void channel_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    flux_t *h = flux_handle_watcher_get_flux (w);
    struct overlay *ov = arg;
    flux_msg_t *msg;
    int type;
    const char *uuid;
    uint32_t nodeid;
    struct child *child = NULL;

    if (!(msg = flux_recv (h, FLUX_MATCH_ANY, 0))
        || flux_msg_get_type (msg, &type) < 0) {
        flux_msg_decref (msg);
        return;
    }
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            /* If message is being routed downstream to reach 'nodeid',
             * push the local uuid, then the next hop onto the messages's
             * route stack so that the ROUTER socket can pop off next hop to
             * select the peer, and our uuid remains as part of the source addr.
             */
            if (flux_msg_get_nodeid (msg, &nodeid) < 0)
                goto done;
            bool upstream = flux_msg_has_flag (msg, FLUX_MSGFLAG_UPSTREAM);
            // downstream
            if (!(upstream && nodeid == ov->rank)
                && (child = children_lookup_route (ov->children, nodeid))) {
                if (!child_is_online (child)) {
                    errno = EHOSTUNREACH;
                    goto request_error;
                }
                if (flux_msg_route_push (msg, ov->uuid) < 0
                    || flux_msg_route_push (msg, child->uuid) < 0)
                    goto request_error;
                if (overlay_sendmsg_child (ov, msg) < 0)
                    goto request_error;
                if (!child) {
                    if ((uuid = flux_msg_route_last (msg)))
                        child = children_lookup_online (ov->children, ov->uuid);
                }
                if (child)
                    rpc_track_update (child->tracker, msg);
            }
            // upstream
            else {
                /* RFC 3 states that requests to unmatched services get ENOSYS,
                 * while unroutable rank-addressed requests get EHOSTUNREACH.
                 * Special-case ENOSYS here.
                 */
                if (ov->rank == 0
                    && !upstream
                    && (nodeid == 0 || nodeid == FLUX_NODEID_ANY)) {
                    errno = ENOSYS;
                    goto request_error;
                }
                if (overlay_sendmsg_parent (ov, msg) < 0)
                    goto request_error;
                rpc_track_update (ov->parent.tracker, msg);
            }
            break;
        case FLUX_MSGTYPE_RESPONSE:
            /* Assume if next route matches parent, the message goes upstream;
             * otherwise downstream.  The send downstream will fail with
             * EHOSTUNREACH if uuid doesn't match an immediate peer.
             */
            if (ov->rank > 0
                && (uuid = flux_msg_route_last (msg)) != NULL
                && streq (uuid, ov->parent.uuid)) {
                if (overlay_sendmsg_parent (ov, msg) < 0)
                    goto done;
            }
            else {
                if (overlay_sendmsg_child (ov, msg) < 0)
                    goto done;
            }
            break;
        case FLUX_MSGTYPE_EVENT:
            /* On rank 0, the broker sends events to the overlay for downstream
             * distribution.  On other ranks, the broker sends unpublished
             * events for upstream publication.
             */
            if (ov->rank == 0) {
                overlay_event_checkseq (ov, msg);
                overlay_mcast_child (ov, msg);
            }
            else {
                flux_msg_route_enable (msg);
                if (overlay_sendmsg_parent (ov, msg) < 0) {
                    flux_log_error (ov->h, "error forwarding event upstream");
                    goto done;
                }
            }
            break;
        default:
            break;
    }
done:
    flux_msg_decref (msg);
    return;
request_error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (ov->h, "error responding to broker request");
    flux_msg_decref (msg);
}

/* The broker state has changed.
 */
static void state_continuation (flux_future_t *f, void *arg)
{
    struct overlay *ov = arg;
    broker_state_t state;

    if (flux_rpc_get_unpack (f, "{s:i}", "state", &state) < 0) {
        if (errno != ENODATA)
            flux_log_error (ov->h, "state-machine.monitor");
        return;
    }
    switch (state) {
        case STATE_LOAD_BUILTINS:
        case STATE_JOIN:
        case STATE_CONFIG_SYNC:
        case STATE_INIT:
        case STATE_QUORUM:
        case STATE_RUN:
            break;
        case STATE_CLEANUP:
            ov->spurn_hello = true;
            break;
        case STATE_SHUTDOWN:
            /* When the (online) child peer count drops to zero while
             * in SHUTDOWN state, children-complete will be posted in
             * overlay_monitor_notify().  If that is already true, for example
             * if there are no children or they are not connected, post
             * children-none here.
             */
            if (children_get_online_count (ov->children) == 0)
                overlay_state_machine_post (ov, "children-none", false);
            ov->spurn_hello = true;
            break;
        case STATE_FINALIZE:
            break;
        case STATE_GOODBYE:
            /* Send an overlay.goodbye request to parent, if any.
             * The overlay.goodbye response handler posts goodbye.
             * If the request cannot be sent, post it here.
             * N.B. on rank 0, the broker shutdown logic posts goodbye.
             */
            if (ov->rank > 0) {
                flux_error_t error;
                if (overlay_goodbye_parent (ov, &error) < 0) {
                    flux_log (ov->h, LOG_ERR, "%s", error.text);
                    overlay_state_machine_post (ov, "goodbye", false);
                }
            }
            break;
        case STATE_UNLOAD_BUILTINS:
        case STATE_EXIT:
            break;
    }
    ov->broker_state = state;

    flux_future_reset (f);
}

static void sync_cb (flux_future_t *f, void *arg)
{
    struct overlay *ov = arg;
    double now = flux_reactor_now (ov->reactor);

    if (now - ov->parent.lastsent > ov->config.torpid_min)
        overlay_control_parent (ov, CONTROL_HEARTBEAT, 0);
    update_torpid_children (ov);

    flux_future_reset (f);
}

/* Set up periodic callback for sending heartbeat control messages to the
 * TBON parent and checking for torpid children.
 * Also, stream broker state machine updates since the overlay must take
 * actions on transition into certain states.
 */
int overlay_start (struct overlay *ov)
{
    if (!(ov->f_sync = flux_sync_create (ov->h, sync_min))
        || flux_future_then (ov->f_sync, sync_max, sync_cb, ov) < 0
        || !(ov->f_state = flux_rpc_pack (ov->h,
                                          "state-machine.monitor",
                                          FLUX_NODEID_ANY,
                                          FLUX_RPC_STREAMING,
                                          "{s:i}",
                                          "final", STATE_GOODBYE))
           || flux_future_then (ov->f_state, -1, state_continuation, ov) < 0)
        return -1;
    return 0;
}

const struct bizcard *overlay_get_bizcard (struct overlay *ov)
{
    return ov ? ov->bizcard : NULL;
}

/* Log a failure to send tracker EHOSTUNREACH response.  Suppress logging
 * ENOSYS failures, which happen if sending module unloads before completing
 * all RPCs.
 */
static void log_tracker_error (flux_t *h, const flux_msg_t *msg, int errnum)
{
    if (errnum != ENOSYS) {
        const char *topic = "unknown";
        (void)flux_msg_get_topic (msg, &topic);
        flux_log_error (h,
                        "tracker: error sending %s EHOSTUNREACH response",
                        topic);
    }
}

/* Child endpoint disconnected, so any pending RPCs going that way
 * get EHOSTUNREACH responses so they can fail fast.
 */
static void fail_child_rpcs (const flux_msg_t *msg, void *arg)
{
    struct overlay *ov = arg;
    flux_msg_t *response;
    const char *last_hop;
    struct child *child;

    if (!(response = flux_response_derive (msg, EHOSTUNREACH))
        || flux_msg_route_delete_last (response) < 0
        || flux_msg_route_delete_last (response) < 0)
        goto error;

    last_hop = flux_msg_route_last (response);

    if ((child = children_lookup (ov->children, last_hop))) {
        if (overlay_sendmsg_child (ov, response) < 0)
            goto error;
        flux_msg_decref (response);
    }
    else if (ov->rank > 0 && last_hop && streq (last_hop, ov->parent.uuid)) {
        if (overlay_sendmsg_parent (ov, response) < 0)
            goto error;
        flux_msg_decref (response);
    }
    else {
        if (flux_send_new (ov->h_channel, &response, 0) < 0)
            goto error;
    }
    return;
error:
    log_tracker_error (ov->h, response, errno);
    flux_msg_destroy (response);
}

static void overlay_child_status_update (struct overlay *ov,
                                         struct child *child,
                                         int status,
                                         const char *reason)
{
    bool went_offline;

    if (children_set_status (ov->children, child, status, &went_offline)) {
        if (went_offline)
            rpc_track_purge (child->tracker, fail_child_rpcs, ov);
        subtree_status_update (ov);
        overlay_monitor_notify (ov, child->rank);
        overlay_health_respond_all (ov);
    }
    errprintf (&child->error, "%s", reason ? reason : "");
}

static void log_lost_connection (struct overlay *ov,
                                 struct child *child,
                                 const char *why)
{
    int children = topology_get_descendant_count_at (ov->topo, child->rank);
    char add[128] = { 0 };

    if (children > 0) {
        snprintf (add,
                  sizeof (add),
                  " and severed contact with %d other nodes",
                  children);
    }
    flux_log (ov->h,
              LOG_ERR,
              "%s (rank %d) %s%s",
              flux_get_hostbyrank (ov->h, child->rank),
              (int)child->rank,
              why,
              add);
}

static int overlay_sendmsg_child (struct overlay *ov, const flux_msg_t *msg)
{
    int rc = -1;

    rc = children_sendmsg (ov->children, msg);
    /* Since ROUTER socket has ZMQ_ROUTER_MANDATORY set, EHOSTUNREACH on a
     * connected peer signifies a disconnect.  See zmq_setsockopt(3).
     */
    if (rc < 0 && errno == EHOSTUNREACH) {
        int saved_errno = errno;
        const char *uuid;
        struct child *child;

        if ((uuid = flux_msg_route_last (msg))
            && (child = children_lookup_online (ov->children, uuid))) {
            log_lost_connection (ov, child, "failed");
            overlay_child_status_update (ov,
                                         child,
                                         SUBTREE_STATUS_LOST,
                                         "lost connection");
        }
        errno = saved_errno;
    }
    if (rc == 0 && flux_msglist_count (ov->trace_requests) > 0) {
        const char *uuid;
        struct child *child = NULL;
        int rank = -1;
        int type = 0;

        (void)flux_msg_get_type (msg, &type);

        // N.B. events are traced in overlay_mcast_child()
        if (type != FLUX_MSGTYPE_EVENT) {
            if ((uuid = flux_msg_route_last (msg))
                && (child = children_lookup_online (ov->children, uuid)))
                rank = child->rank;
            trace_overlay_msg (ov->h, "tx", rank, ov->trace_requests, msg);
        }
    }
    return rc;
}

// callback for msg_route_sendto()
static int overlay_mcast_send (const flux_msg_t *msg, void *arg)
{
    struct overlay *ov = arg;
    return overlay_sendmsg_child (ov, msg);
}

/* Forward an event message to downstream peers.
 */
static void overlay_mcast_child (struct overlay *ov, flux_msg_t *msg)
{
    struct child *child;
    int count = 0;

    flux_msg_route_enable (msg);

    children_foreach (ov->children, child) {
        if (child_is_online (child)) {
            if (msg_route_sendto (msg,
                                  child->uuid,
                                  overlay_mcast_send,
                                  ov) < 0) {
                if (errno != EHOSTUNREACH) {
                    flux_log_error (ov->h,
                                    "mcast error to child rank %lu",
                                    (unsigned long)child->rank);
                }
            }
            else
                count++;
        }
    }
    if (count > 0) {
        trace_overlay_msg (ov->h,
                           "tx",
                           FLUX_NODEID_ANY,
                           ov->trace_requests,
                           msg);
    }
}

static void logdrop (struct overlay *ov,
                     const char *where,
                     const flux_msg_t *msg,
                     const char *fmt,
                     ...)
{
    char reason[128];
    va_list ap;
    const char *topic = NULL;
    int type = -1;
    const char *child_uuid = NULL;

    (void)flux_msg_get_type (msg, &type);
    (void)flux_msg_get_topic (msg, &topic);
    if (streq (where, "downstream"))
        child_uuid = flux_msg_route_last (msg);

    va_start (ap, fmt);
    (void)vsnprintf (reason, sizeof (reason), fmt, ap);
    va_end (ap);

    flux_log (ov->h,
              LOG_ERR,
              "DROP %s %s topic %s %s%s: %s",
              where,
              type != -1 ? flux_msg_typestr (type) : "message",
              topic ? topic : "-",
              child_uuid ? "from " : "",
              child_uuid ? child_uuid : "",
              reason);
}

static int clear_msg_role (flux_msg_t *msg, uint32_t role)
{
    uint32_t rolemask;

    if (flux_msg_get_rolemask (msg, &rolemask) < 0)
        return -1;
    rolemask &= ~role;
    if (flux_msg_set_rolemask (msg, rolemask) < 0)
        return -1;
    return 0;
}

/* Handle a message received from TBON child (downstream).
 */
static void child_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct overlay *ov = arg;
    flux_msg_t *msg;
    int type = -1;
    const char *topic = NULL;
    const char *uuid = NULL;
    struct child *child;

    if (!(msg = children_recvmsg (ov->children)))
        return;
    if (clear_msg_role (msg, FLUX_ROLE_LOCAL) < 0) {
        logdrop (ov, "downstream", msg, "failed to clear local role");
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0
        || !(uuid = flux_msg_route_last (msg))) {
        logdrop (ov, "downstream", msg, "malformed message");
        goto done;
    }
    if (!(child = children_lookup_online (ov->children, uuid))) {
        bool is_hello = false;
        json_int_t rank = FLUX_NODEID_ANY;

        if (type == FLUX_MSGTYPE_REQUEST
            && flux_msg_get_topic (msg, &topic) == 0
            && streq (topic, "overlay.hello")) {
            // extract the hello rank so we can include it in the trace
            (void)flux_request_unpack (msg, NULL, "{s:I}", "rank", &rank);
            is_hello = true;
        }
        trace_overlay_msg (ov->h, "rx", rank, ov->trace_requests, msg);
        /* This is a new peer trying to introduce itself by sending an
         * overlay.hello request.
         * N.B. the broker generates a new UUID on startup, and hello is only
         * sent once on startup, in overlay_connect().  Therefore, it is
         * assumed that a overlay.hello is always introducing a new UUID and
         * we don't bother checking if we've seen this UUID before, which can
         * be slow given current design.  See flux-framework/flux-core#5864.
         */
        if (is_hello && !ov->spurn_hello) {
            hello_request_handler (ov, msg);
        }
        /* Or one of the following cases occurred that requires (or at least
         * will not be harmed by) a DISCONNECT message sent to the peer:
         * 1) This is a known peer that has transitioned to offline/lost _here_
         *    but the DISCONNECT is still in flight to the peer.
         * 2) This is a known peer that has transitioned to offline/lost
         *    as a result of a network partition, but the child never received
         *    the DISCONNECT and connectivity has been restored.
         * 3) This is a new-to-us peer because *we* restarted without getting
         *    a message through (e.g. crash)
         * 4) A peer said hello while shutdown is in progress
         * Control send failures may occur, see flux-framework/flux-core#4464.
         * Don't log here, see flux-framework/flux-core#4180.
         */
        else {
            (void)overlay_control_child (ov, uuid, CONTROL_DISCONNECT, 0);
        }
        goto done;
    }
    /* N.B. We just looked up the child subtree in the online list, so
     * it is guaranteed to be online at this point.
     */

    child->lastseen = flux_reactor_now (ov->reactor);
    switch (type) {
        case FLUX_MSGTYPE_CONTROL: {
            int type, status;
            if (flux_control_decode (msg, &type, &status) == 0
                && type == CONTROL_STATUS) {
                trace_overlay_msg (ov->h,
                                   "rx",
                                   child->rank,
                                   ov->trace_requests,
                                   msg);
                overlay_child_status_update (ov, child, status, NULL);
            }
            goto done;
        }
        case FLUX_MSGTYPE_REQUEST:
            if (flux_msg_get_topic (msg, &topic) == 0
                && streq (topic, "overlay.goodbye")) {
                overlay_goodbye_cb (ov, msg);
                goto done;
            }
            break;
        case FLUX_MSGTYPE_RESPONSE:
            /* Response message traveling upstream requires special handling:
             * ROUTER socket will have pushed peer id onto message as if it
             * were a request, but the effect we want for responses is to have
             * a route popped off at each router hop.
             */
            (void)flux_msg_route_delete_last (msg); // child id from ROUTER
            (void)flux_msg_route_delete_last (msg); // my id
            rpc_track_update (child->tracker, msg);
            break;
        case FLUX_MSGTYPE_EVENT:
            /* An event message traveling upstream will always be unpublished.
             * Forward upstream, or on rank 0, to local broker for publication.
             */
            if (ov->rank > 0) {
                flux_msg_route_enable (msg);
                overlay_sendmsg_parent (ov, msg);
                goto done;
            }
            flux_msg_route_disable (msg);
            // fall through to forward message to broker
            break;
    }
    trace_overlay_msg (ov->h, "rx", child->rank, ov->trace_requests, msg);
    if (flux_send_new (ov->h_channel, &msg, 0) < 0)
        goto done;
    return;
done:
    flux_msg_decref (msg);
}

/* Parent endpoint disconnected, so any pending RPCs going that way
 * get EHOSTUNREACH responses so they can fail fast.
 */
static void fail_parent_rpc (const flux_msg_t *msg, void *arg)
{
    struct overlay *ov = arg;
    flux_msg_t *response;
    const char *last_hop;

    if (!(response = flux_response_derive (msg, EHOSTUNREACH))
        || flux_msg_set_string (response, "overlay disconnect") < 0)
        goto error;
    last_hop = flux_msg_route_last (response);
    if (children_lookup (ov->children, last_hop)) {
        if (overlay_sendmsg_child (ov, response) < 0)
            goto error;
        flux_msg_decref (response);
    }
    else {
        if (flux_send_new (ov->h_channel, &response, 0) < 0)
            goto error;
    }
    return;
error:
    log_tracker_error (ov->h, msg, errno);
    flux_msg_decref (response);
}

static void parent_disconnect (struct overlay *ov)
{
    if (ov->parent.zsock) {
        (void)zmq_disconnect (ov->parent.zsock, ov->parent.uri);
        ov->parent.offline = true;
        rpc_track_purge (ov->parent.tracker, fail_parent_rpc, ov);
        overlay_monitor_notify (ov, FLUX_NODEID_ANY);
        if (overlay_parent_error (ov))
            overlay_state_machine_post (ov, "parent-fail", true);
    }
}

/* Sanity check that event messages are properly sequenced.
 */
static void overlay_event_checkseq (struct overlay *ov, const flux_msg_t *msg)
{
    uint32_t seq;

    if (ov->rank == 0)
        return;
    if (flux_msg_get_seq (msg, &seq) < 0) {
        flux_log (ov->h, LOG_ERR, "event message is malformed");
        return;
    }
    if (seq <= ov->event_seq) {
        flux_log (ov->h, LOG_DEBUG, "duplicate event %d", seq);
        return;
    }
    if (ov->event_seq > 0) { /* don't log initial missed events */
        int first = ov->event_seq + 1;
        int count = seq - first;
        if (count > 1)
            flux_log (ov->h, LOG_ERR, "lost events %d-%d", first, seq - 1);
        else if (count == 1)
            flux_log (ov->h, LOG_ERR, "lost event %d", first);
    }
    ov->event_seq = seq;
}

static void parent_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg)
{
    struct overlay *ov = arg;
    flux_msg_t *msg;
    int type;
    const char *topic = NULL;

    if (!(msg = zmqutil_msg_recv (ov->parent.zsock)))
        return;
    if (clear_msg_role (msg, FLUX_ROLE_LOCAL) < 0) {
        logdrop (ov, "upstream", msg, "failed to clear local role");
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0) {
        logdrop (ov, "upstream", msg, "malformed message");
        goto done;
    }
    if (!ov->parent.hello_responded) {
        /* process hello response */
        if (type == FLUX_MSGTYPE_RESPONSE
            && flux_msg_get_topic (msg, &topic) == 0
            && streq (topic, "overlay.hello")) {
            hello_response_handler (ov, msg);
            goto done;
        }
        else if (type != FLUX_MSGTYPE_CONTROL) {
            logdrop (ov,
                     "upstream",
                     msg,
                     "message received before hello handshake completed");
            goto done;
        }
    }
    switch (type) {
        case FLUX_MSGTYPE_RESPONSE:
            if (ov->broker_state == STATE_GOODBYE
                && flux_msg_get_topic (msg, &topic) == 0
                && streq (topic, "overlay.goodbye")) {
                overlay_state_machine_post (ov, "goodbye", false);
                goto done;
            }
            rpc_track_update (ov->parent.tracker, msg);
            break;
        case FLUX_MSGTYPE_EVENT:
            /* Upstream broker enables routing and pushes our uuid, then
             * router socket pops it off, but leaves routing enabled.
             * An event type message should not have routing enabled
             * under normal circumstances, so turn it off here.
             */
            overlay_event_checkseq (ov, msg);
            overlay_mcast_child (ov, msg);
            flux_msg_route_disable (msg);
            // fall through and let local broker distribute locally
            break;
        case FLUX_MSGTYPE_CONTROL: {
            int ctrl_type, reason;
            if (flux_control_decode (msg, &ctrl_type, &reason) < 0) {
                logdrop (ov, "upstream", msg, "malformed control");
            }
            else if (ctrl_type == CONTROL_DISCONNECT) {
                flux_log (ov->h, LOG_CRIT,
                          "%s (rank %lu) sent disconnect control message",
                          flux_get_hostbyrank (ov->h, ov->parent.rank),
                          (unsigned long)ov->parent.rank);
                parent_disconnect (ov);
            }
            else
                logdrop (ov, "upstream", msg, "unknown control type");
            goto done;
        }
        default:
            break;
    }
    trace_overlay_msg (ov->h, "rx", ov->parent.rank, ov->trace_requests, msg);
    if (flux_send_new (ov->h_channel, &msg, 0) < 0)
        goto done;
    return;
done:
    flux_msg_destroy (msg);
}


#define V_MAJOR(v)  (((v) >> 16) & 0xff)
#define V_MINOR(v)  (((v) >> 8) & 0xff)
#define V_PATCH(v)  ((v) & 0xff)

/* Check child flux-core version 'v1' against this broker's version 'v2'.
 * For now we require an exact match of MAJOR.MINOR, but not PATCH.
 * ignore any commit id appended to the version string.
 * Return 0 on error, or -1 on failure with message for child in 'error'.
 */
static bool version_check (int v1, int v2, flux_error_t *error)
{
    if (V_MAJOR (v1) != V_MAJOR (v2) || V_MINOR (v1) != V_MINOR (v2)) {
        errprintf (error,
                  "client (%u.%u.%u) version mismatched with server (%u.%u.%u)",
                  V_MAJOR (v1),
                  V_MINOR (v1),
                  V_PATCH (v1),
                  V_MAJOR (v2),
                  V_MINOR (v2),
                  V_PATCH (v2));
        return false;
    }
    return true;
}

/* Handle overlay.hello request from downstream (child) TBON peer.
 * The peer may be rejected here if it is improperly configured.
 * If successful the child's status is updated to reflect its online
 * state and the state machine is notified.
 * N.B. respond using overlay_sendmsg_child() to avoid the main message path
 * during initialization.
 */
static void hello_request_handler (struct overlay *ov, const flux_msg_t *msg)
{
    struct child *child;
    json_int_t rank;
    int version;
    const char *errmsg = NULL;
    flux_error_t error;
    flux_msg_t *response;
    const char *uuid;
    int status;
    const char *hostname = NULL;
    int hello_log_level = LOG_DEBUG;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:i s:s s:i s?s}",
                             "rank", &rank,
                             "version", &version,
                             "uuid", &uuid,
                             "status", &status,
                             "hostname", &hostname) < 0)
        goto error; // EPROTO (unlikely)

    if (flux_msg_authorize (msg, FLUX_USERID_UNKNOWN) < 0) {
        /* special handling for v0.46.1 flux-framework/flux-core#4886.
         * The rolemask/userid are sent in the wrong byte order, so
         * authorization silently fails. Log something helpful.
         */
        if (version == 0x002e01) {
            flux_log (ov->h, LOG_ERR,
                      "rejecting connection from %s (rank %lu): %s",
                      flux_get_hostbyrank (ov->h, rank),
                      (unsigned long)rank,
                      "v0.46.1 has a message encoding bug, please upgrade");
        }
        goto error; // EPERM
    }
    // flux-framework/flux-core#6389
    if (hostname && !streq (hostname, flux_get_hostbyrank (ov->h, rank))) {
        errprintf (&error,
                  "%s is configured as rank %lu: mismatched config?",
                  flux_get_hostbyrank (ov->h, rank),
                  (unsigned long)rank);
        flux_log (ov->h,
                  LOG_ERR,
                  "rejecting connection from %s (rank %lu): %s",
                  hostname,
                  (unsigned long)rank,
                  error.text);
        errno = EINVAL;
        goto error;
    }
    if (!(child = children_lookup_byrank (ov->children, rank))) {
        errprintf (&error,
                  "rank %lu is not a peer of parent %lu: mismatched config?",
                  (unsigned long)rank,
                  (unsigned long)ov->parent.rank);
        flux_log (ov->h, LOG_ERR,
                  "rejecting connection from %s (rank %lu): %s",
                  flux_get_hostbyrank (ov->h, rank),
                  (unsigned long)rank,
                  error.text);
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }
    /* Oops, child was previously online, but is now saying hello.
     * Update the (old) child's subtree status to LOST.  If the hello
     * request is successful, another update will immediately follow.
     */
    if (child_is_online (child)) { // crash
        flux_log (ov->h, LOG_ERR,
        "%s (rank %lu) reconnected after crash, dropping old connection state",
                  flux_get_hostbyrank (ov->h, child->rank),
                  (unsigned long)child->rank);
        overlay_child_status_update (ov, child, SUBTREE_STATUS_LOST, NULL);
        hello_log_level = LOG_ERR; // want hello log to stand out in this case
    }

    if (!version_check (version, ov->version, &error)) {
        child->error = error; // capture this error message for health report
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }

    if (strlcpy (child->uuid,
                 uuid,
                 sizeof (child->uuid)) >= sizeof (child->uuid)) {
        errno = EOVERFLOW;
        goto error;
    }
    overlay_child_status_update (ov, child, status, NULL);

    flux_log (ov->h,
              hello_log_level,
              "accepting connection from %s (rank %lu) status %s",
              flux_get_hostbyrank (ov->h, child->rank),
              (unsigned long)child->rank,
              subtree_status_str (child->status));

    if (!(response = flux_response_derive (msg, 0))
        || flux_msg_pack (response, "{s:s}", "uuid", ov->uuid) < 0
        || overlay_sendmsg_child (ov, response) < 0)
        flux_log_error (ov->h, "error responding to overlay.hello request");
    flux_msg_destroy (response);
    return;
error:
    if (!(response = flux_response_derive (msg, errno))
        || (errmsg && flux_msg_set_string (response, errmsg) < 0)
        || overlay_sendmsg_child (ov, response) < 0)
        flux_log_error (ov->h, "error responding to overlay.hello request");
    flux_msg_destroy (response);
}

/* Process overlay.hello response.
 * If the response indicates an error, set in motion a clean broker exit by
 * printing the error message to stderr and notifying the state machine.
 */
static void hello_response_handler (struct overlay *ov, const flux_msg_t *msg)
{
    const char *errstr = NULL;
    const char *uuid;

    if (flux_response_decode (msg, NULL, NULL) < 0
        || flux_msg_unpack (msg, "{s:s}", "uuid", &uuid) < 0) {
        int saved_errno = errno;
        (void)flux_msg_get_string (msg, &errstr);
        errno = saved_errno;
        goto error;
    }
    flux_log (ov->h,
              LOG_DEBUG,
              "hello parent %lu %s",
              (unsigned long)ov->parent.rank,
              uuid);
    if (strlcpy (ov->parent.uuid,
                 uuid,
                 sizeof (ov->parent.uuid)) >= sizeof (ov->parent.uuid)) {
        errno = EOVERFLOW;
        goto error;
    }
    ov->parent.hello_responded = true;
    ov->parent.hello_error = false;
    overlay_monitor_notify (ov, FLUX_NODEID_ANY);
    if (overlay_parent_error (ov))
        overlay_state_machine_post (ov, "parent-fail", true);
    return;
error:
    flux_log (ov->h,
              LOG_ERR,
              "overlay.hello: %s",
              errstr ? errstr : strerror (errno));
    ov->parent.hello_responded = true;
    ov->parent.hello_error = true;
    overlay_monitor_notify (ov, FLUX_NODEID_ANY);
    if (overlay_parent_error (ov))
        overlay_state_machine_post (ov, "parent-fail", true);
}

/* Send overlay.hello message to TBON parent.
 */
static int hello_request_send (struct overlay *ov,
                               json_int_t rank,
                               int version)
{
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("overlay.hello", NULL))
        || flux_msg_pack (msg,
                          "{s:I s:i s:s s:i s:s}",
                          "rank", rank,
                          "version", ov->version,
                          "uuid", ov->uuid,
                          "status", ov->status,
                          "hostname", ov->hostname) < 0
        || flux_msg_set_rolemask (msg, FLUX_ROLE_OWNER) < 0
        || overlay_sendmsg_parent (ov, msg) < 0) {
        flux_msg_decref (msg);
        return -1;
    }
    flux_msg_decref (msg);
    return 0;
}

static void parent_monitor_cb (struct zmqutil_monitor *mon, void *arg)
{
    struct overlay *ov = arg;
    struct monitor_event event;

    if (zmqutil_monitor_get (mon, &event) == 0) {
        flux_log (ov->h,
                  zmqutil_monitor_iserror (&event) ? LOG_ERR : LOG_DEBUG,
                  "parent sockevent %s %s%s%s",
                  event.endpoint,
                  event.event_str,
                  *event.value_str ? ": " : "",
                  event.value_str);
    }
}

static int overlay_zmq_init (struct overlay *ov)
{
    if (!ov->zctx) {
        if (!(ov->zctx = zmq_ctx_new ()))
            return -1;
        /* At this point, ensure that tbon.zmq_io_threads is only increased on
         * the leader node, on the assumption that it will be less effective on
         * other nodes yet increases the broker's footprint.
         * This could be removed if we decide otherwise later on.
         */
        if (ov->rank > 0 && ov->config.zmq_io_threads != 1) {
            const char *key = "tbon.zmq_io_threads";
            if (compat_attr_set_flags (ov->h, key, 0) < 0
                || compat_attr_delete (ov->h, key, true) < 0
                || compat_attr_add_int (ov->h, key, 1, ATTR_IMMUTABLE) < 0)
                return -1;
            ov->config.zmq_io_threads = 1;
        }
        if (zmq_ctx_set (ov->zctx,
                         ZMQ_IO_THREADS,
                         ov->config.zmq_io_threads) < 0)
            return -1;
    }
    return 0;
}

int overlay_connect (struct overlay *ov)
{
    if (ov->rank > 0) {
        if (!ov->h || ov->rank == FLUX_NODEID_ANY || !ov->parent.uri) {
            errno = EINVAL;
            return -1;
        }
        if (overlay_zmq_init (ov) < 0)
            return -1;

        if (!(ov->parent.zsock = zmq_socket (ov->zctx, ZMQ_DEALER))
            || zsetsockopt_int (ov->parent.zsock, ZMQ_SNDHWM, 0) < 0
            || zsetsockopt_int (ov->parent.zsock, ZMQ_RCVHWM, 0) < 0
            || zsetsockopt_int (ov->parent.zsock, ZMQ_LINGER, 5) < 0
            || zsetsockopt_int (ov->parent.zsock,
                                ZMQ_IPV6,
                                ov->config.enable_ipv6) < 0
            || zsetsockopt_str (ov->parent.zsock, ZMQ_IDENTITY, ov->uuid) < 0
            || zsetsockopt_str (ov->parent.zsock,
                                ZMQ_ZAP_DOMAIN,
                                FLUX_ZAP_DOMAIN) < 0
            || zsetsockopt_str (ov->parent.zsock,
                                ZMQ_CURVE_SERVERKEY,
                                ov->parent.pubkey) < 0)
            return -1;
        /* The socket monitor is only used for logging.
         * Setup may fail if libzmq is too old.
         */
        if (ov->config.zmqdebug) {
            ov->parent.monitor = zmqutil_monitor_create (ov->zctx,
                                                         ov->parent.zsock,
                                                         ov->reactor,
                                                         parent_monitor_cb,
                                                         ov);
        }
#ifdef ZMQ_CONNECT_TIMEOUT
        if (ov->config.connect_timeout > 0) {
            if (zsetsockopt_int (ov->parent.zsock,
                                 ZMQ_CONNECT_TIMEOUT,
                                 ov->config.connect_timeout * 1000) < 0)
                return -1;
        }
#endif
#ifdef ZMQ_TCP_MAXRT
        if (ov->config.tcp_user_timeout > 0) {
            if (zsetsockopt_int (ov->parent.zsock,
                                 ZMQ_TCP_MAXRT,
                                 ov->config.tcp_user_timeout * 1000) < 0)
                return -1;
        }
#endif
        if (cert_apply (ov->cert, ov->parent.zsock) < 0)
            return -1;
        if (zmq_connect (ov->parent.zsock, ov->parent.uri) < 0)
            return -1;
        flux_log (ov->h, LOG_DEBUG, "connecting to %s", ov->parent.uri);
        if (!(ov->parent.w = zmqutil_watcher_create (ov->reactor,
                                                     ov->parent.zsock,
                                                     FLUX_POLLIN,
                                                     parent_cb,
                                                     ov)))
            return -1;
        flux_watcher_start (ov->parent.w);
        if (hello_request_send (ov, ov->rank, FLUX_CORE_VERSION_HEX) < 0)
            return -1;
    }
    return 0;
}

int overlay_bind (struct overlay *ov,
                  const char *uri,
                  const char *uri2,
                  flux_error_t *errp)
{
    char *new_uri = NULL;
    char *new_uri2 = NULL;

    if (!ov->h
        || ov->rank == FLUX_NODEID_ANY
        || children_is_bound (ov->children)) {
        errno = EINVAL;
        return errprintf (errp, "overlay_bind: invalid arguments");
    }
    if (overlay_zmq_init (ov) < 0) {
        return errprintf (errp,
                          "error creating zeromq context: %s",
                          strerror (errno));
    }

    if (children_bind (ov->children,
                       ov->zctx,
                       ov->cert,
                       uri,
                       uri2,
                       &ov->config,
                       &new_uri,
                       uri2 ? &new_uri2 : NULL,
                       errp) < 0)
        return -1;

    /* Record the concretized URIs in the business card for sharing with peers.
     */
    if (bizcard_uri_append (ov->bizcard, new_uri) < 0) {
        ERRNO_SAFE_WRAP (free, new_uri);
        ERRNO_SAFE_WRAP (free, new_uri2);
        errno = ENOMEM;
        return errprintf (errp, "error appending URI to bizcard");
    }
    /* Ensure that any AF_UNIX sockets are unlinked when the broker exits,
     * but only if they begin with '/'.  (Abstract sockets begin with '@')
     */
    if (strstarts (new_uri, "ipc:///"))
        cleanup_push_string (cleanup_file, new_uri + 6);
    free (new_uri);

    if (new_uri2) {
        if (bizcard_uri_append (ov->bizcard, new_uri2) < 0) {
            ERRNO_SAFE_WRAP (free, new_uri2);
            errno = ENOMEM;
            return errprintf (errp, "error appending URI2 to bizcard");
        }
        if (strstarts (new_uri2, "ipc:///"))
            cleanup_push_string (cleanup_file, new_uri2 + 6);
        free (new_uri2);
    }

    if (children_watch (ov->children, child_cb, ov) < 0) {
        return errprintf (errp,
                          "error creating watcher for bind socket: %s",
                          strerror (errno));
    }
    return 0;
}

/* Call after overlay bootstrap (bind/connect),
 * to get concretized 0MQ endpoints.
 */
static int overlay_register_attrs (struct overlay *overlay)
{
    if (compat_attr_add (overlay->h,
                         "tbon.parent-endpoint",
                         overlay->parent.uri,
                         ATTR_IMMUTABLE) < 0)
        return -1;
    if (compat_attr_add_int (overlay->h,
                             "tbon.level",
                             topology_get_level (overlay->topo),
                             ATTR_IMMUTABLE) < 0)
        return -1;
    if (compat_attr_add_int (overlay->h,
                             "tbon.maxlevel",
                             topology_get_maxlevel (overlay->topo),
                             ATTR_IMMUTABLE) < 0)
        return -1;
    if (compat_attr_add_int (overlay->h,
                             "tbon.descendants",
                             topology_get_descendant_count (overlay->topo),
                             ATTR_IMMUTABLE) < 0)
        return -1;

    return 0;
}

/* A child has sent an overlay.goodbye request.
 * Respond, then transition it to OFFLINE.
 */
static void overlay_goodbye_cb (struct overlay *ov, const flux_msg_t *msg)
{
    const char *uuid;
    struct child *child;
    flux_msg_t *response = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0
        || !(uuid = flux_msg_route_last (msg))) {
        flux_log (ov->h, LOG_ERR, "overlay.goodbye: %s", strerror (errno));
        return;
    }
    if (!(child = children_lookup_online (ov->children, uuid))) {
        flux_log (ov->h, LOG_ERR, "overlay.goodbye: uuid unknown");
        return;
    }
    if (!(response = flux_response_derive (msg, 0))
        || overlay_sendmsg_child (ov, response) < 0) {
        flux_log (ov->h,
                  LOG_ERR,
                  "overlay.goodbye: error sending response: %s",
                  strerror (errno));
        flux_msg_decref (response);
        return;
    }
    overlay_child_status_update (ov,
                                 child,
                                 SUBTREE_STATUS_OFFLINE,
                                 "administrative shutdown");
    flux_msg_decref (response);
}

/* This allows the state machine to delay overlay_destroy() and its
 * disconnection from the parent until the parent has marked this peer
 * offline and sent an acknowledgement.  If we simply send a control status
 * offline message and disconnect, the parent may log errors if it sends any
 * messages to this peer (such as broadcasts) before the offline message is
 * processed and gets an EHOSTUNREACH for an online peer.
 * See flux-framework/flux-core#5881.
 */
static int overlay_goodbye_parent (struct overlay *ov, flux_error_t *errp)
{
    flux_msg_t *msg;

    /* Avoid 60s delay on shutdown of followers when upstream is down.
     * flux-framework/flux-core#5991
     */
    if (!(ov->parent.hello_responded)) {
        errprintf (errp,
                   "cannot send overlay.goodbye because overlay.hello"
                   " is still in progress");
        return -1;
    }
    if (!(msg = flux_request_encode ("overlay.goodbye", NULL))
        || flux_msg_set_rolemask (msg, FLUX_ROLE_OWNER) < 0
        || overlay_sendmsg_parent (ov, msg) < 0) {
        flux_msg_decref (msg);
        errprintf (errp, "error sending overlay.goodbye: %s", strerror (errno));
        return -1;
    }
    ov->parent.goodbye_sent = true; // suppress further sends to parent
    flux_msg_decref (msg);
    return 0;
}

static int child_rpc_track_count (struct overlay *ov)
{
    struct child *child;
    int count = 0;

    children_foreach (ov->children, child)
        count += rpc_track_count (child->tracker);
    return count;
}

static void overlay_stats_get_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    struct overlay *ov = arg;
    size_t sendq = 0;
    size_t recvq = 0;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (ov->h_channel) {
        (void)flux_opt_get (ov->h_channel,
                            FLUX_OPT_RECV_QUEUE_COUNT,
                            &recvq,
                            sizeof (recvq));
        (void)flux_opt_get (ov->h_channel,
                            FLUX_OPT_SEND_QUEUE_COUNT,
                            &sendq,
                            sizeof (sendq));
    }
    int child_connected = children_get_online_count (ov->children);
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i s:i s:i s:i s:{s:i s:i}}",
                           "child-count", ov->children->count,
                           "child-connected", child_connected,
                           "parent-count", ov->rank > 0 ? 1 : 0,
                           "parent-rpc", rpc_track_count (ov->parent.tracker),
                           "child-rpc", child_rpc_track_count (ov),
                           "interthread",
                             "sendq", (int)sendq,
                             "recvq", (int)recvq) < 0)
        flux_log_error (h, "error responding to overlay.stats-get");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to overlay.stats-get");
}

static int overlay_health_respond (struct overlay *ov, const flux_msg_t *msg)
{
    struct child *child;
    json_t *array = NULL;
    json_t *entry;
    double duration;

    if (!(array = json_array ()))
        goto nomem;
    children_foreach (ov->children, child) {
        duration = monotime_since (child->status_timestamp) / 1000.0;
        if (!(entry = json_pack ("{s:i s:s s:f}",
                                 "rank", child->rank,
                                 "status", subtree_status_str (child->status),
                                 "duration", duration)))
            goto nomem;
        if (!child_is_online (child) && child->error.text[0]) {
            json_t *o;
            if (!(o = json_string (child->error.text))
                || json_object_set_new (entry, "error", o) < 0) {
                // jansson decrefs the new object on failure; soldier on
            }
        }
        if (json_array_append_new (array, entry) < 0) {
            // jansson decrefs the new object on failure
            goto nomem;
        }
    }
    duration = monotime_since (ov->status_timestamp) / 1000.0;
    if (flux_respond_pack (ov->h,
                           msg,
                           "{s:i s:s s:f s:O}",
                           "rank", ov->rank,
                           "status", subtree_status_str (ov->status),
                           "duration", duration,
                           "children", array) < 0)
        flux_log_error (ov->h, "error responding to overlay.health");
    json_decref (array);
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

static void overlay_health_respond_all (struct overlay *ov)
{
    const flux_msg_t *msg;

    msg = flux_msglist_first (ov->health_requests);
    while (msg) {
        if (overlay_health_respond (ov, msg) < 0)
            flux_log_error (ov->h, "error responding to overlay.health");
        msg = flux_msglist_next (ov->health_requests);
    }
}

static void overlay_health_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct overlay *ov = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_msg_is_streaming (msg)) {
        if (flux_msglist_append (ov->health_requests, msg) < 0)
            goto error;
    }
    if (overlay_health_respond (ov, msg) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to overlay.health");
}

/* The initial overlay.monitor batch of responses omits peers that
 * are offline and not torpid.  Therefore, the caller should assume
 * that state for all peers initially.  It can determine the full set
 * of downstream peers (and their children) by querying overlay.topology
 * for the current rank.
 */
static void overlay_monitor_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    struct overlay *ov = arg;
    struct child *child;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!flux_msg_is_streaming (msg))
        goto eproto;
    children_foreach (ov->children, child) {
        if (child->status != SUBTREE_STATUS_OFFLINE || child->torpid)
            overlay_monitor_respond_one (ov->h, msg, child);
    }
    if (flux_msglist_append (ov->monitor_requests, msg) < 0)
        goto error;
    return;
eproto:
    errno = EPROTO;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to overlay.monitor");
}

/* If a disconnect is received for waiting request, drop the request.
 */
static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct overlay *ov = arg;
    int count;

    if ((count = flux_msglist_disconnect (ov->health_requests, msg)) < 0)
        flux_log_error (h, "error handling overlay.disconnect");
    if (count > 0)
        flux_log (h, LOG_DEBUG, "overlay: goodbye to %d health clients", count);
    if ((count = flux_msglist_disconnect (ov->trace_requests, msg)) < 0)
        flux_log_error (h, "error handling overlay.disconnect");
    if (count > 0)
        flux_log (h, LOG_DEBUG, "overlay: goodbye to %d trace clients", count);
    if ((count = flux_msglist_disconnect (ov->monitor_requests, msg)) < 0)
        flux_log_error (h, "error handling overlay.disconnect");
    if (count > 0) {
        flux_log (h,
                  LOG_DEBUG,
                  "overlay: goodbye to %d monitor clients",
                  count);
    }
}

struct idset *overlay_get_default_critical_ranks (struct overlay *ov)
{
    struct idset *ranks;

    /* For now, return all internal ranks plus rank 0
     */
    if (!(ranks = topology_get_internal_ranks (ov->topo))
        || idset_set (ranks, 0) < 0) {
        idset_destroy (ranks);
        return NULL;
    }
    return ranks;
}

/* Get the topology of the subtree rooted here.
 */
static void overlay_topology_cb (flux_t *h,
                                 flux_msg_handler_t *mh,
                                 const flux_msg_t *msg,
                                 void *arg)
{
    struct overlay *ov = arg;
    json_t *topo = NULL;
    int rank;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:i}", "rank", &rank) < 0)
        goto error;
    if (rank != ov->rank && !children_lookup_byrank (ov->children, rank)) {
        errstr = "requested rank is not this broker or its direct child";
        errno = ENOENT;
        goto error;
    }
    if (!(topo = topology_get_json_subtree_at (ov->topo, rank)))
        goto error;
    if (flux_respond_pack (h, msg, "O", topo) < 0)
        flux_log_error (h, "error responding to overlay.topology");
    json_decref (topo);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to overlay.topology");
    json_decref (topo);
}

/* Administratively force a disconnect of subtree rooted at 'rank'.
 */
static void overlay_disconnect_subtree_cb (flux_t *h,
                                           flux_msg_handler_t *mh,
                                           const flux_msg_t *msg,
                                           void *arg)
{
    struct overlay *ov = arg;
    const char *errstr = NULL;
    char errbuf[128];
    int rank;
    struct child *child;

    if (flux_request_unpack (msg, NULL, "{s:i}", "rank", &rank) < 0)
        goto error;
    if (!(child = children_lookup_byrank (ov->children, rank))) {
        errstr = "requested rank is not this broker's direct child";
        errno = ENOENT;
        goto error;
    }
    if (!child_is_online (child)) {
        snprintf (errbuf, sizeof (errbuf), "rank %d is already %s", rank,
                  subtree_status_str (child->status));
        errstr = errbuf;
        errno = EINVAL;
        goto error;
    }
    if (overlay_control_child (ov,
                               child->uuid,
                               CONTROL_DISCONNECT, 0) < 0) {
        errstr = "failed to send CONTROL_DISCONNECT message";
        goto error;
    }
    log_lost_connection (ov, child, "disconnected by request");
    overlay_child_status_update (ov,
                                 child,
                                 SUBTREE_STATUS_LOST,
                                 "administrative disconnect");
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to overlay.disconnect-subtree");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to overlay.disconnect-subtree");
}

/* Log a message then force the parent to disconnect.
 */
static void overlay_disconnect_parent_cb (flux_t *h,
                                          flux_msg_handler_t *mh,
                                          const flux_msg_t *msg,
                                          void *arg)
{
    struct overlay *ov = arg;
    const char *reason;

    if (flux_request_unpack (msg, NULL, "{s:s}", "reason", &reason) < 0)
        goto error;
    flux_log (h, LOG_CRIT, "disconnecting: %s", reason);
    parent_disconnect (ov);
    return;
error:
    flux_log_error (h, "overlay.disconnect-parent error");
}

static void overlay_trace_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct overlay *ov = arg;
    struct flux_match match = FLUX_MATCH_ANY;
    int nodeid;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i s:s s:i}",
                             "typemask", &match.typemask,
                             "topic_glob", &match.topic_glob,
                             "nodeid", &nodeid) < 0)
        goto error;
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        goto error;
    }
    if (flux_msglist_append (ov->trace_requests, msg) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to overlay.trace");
}

int overlay_cert_load (struct overlay *ov,
                       const char *path,
                       flux_error_t *errp)
{
    struct stat sb;
    int fd;
    FILE *f = NULL;
    struct cert *cert;

    if ((fd = open (path, O_RDONLY)) < 0
        || fstat (fd, &sb) < 0) {
        errprintf (errp, "%s: %s", path, strerror(errno));
        goto error;
    }
    if ((sb.st_mode & S_IROTH) | (sb.st_mode & S_IRGRP)) {
        errprintf (errp, "%s: readable by group/other", path);
        errno = EPERM;
        goto error;
    }
    if (!(f = fdopen (fd, "r"))) {
        errprintf (errp, "%s: %s", path, strerror(errno));
        goto error;
    }
    fd = -1; // now owned by 'f'
    if (!(cert = cert_read (f))) {
        errprintf (errp, "%s: %s", path, strerror(errno));
        goto error;
    }
    cert_destroy (ov->cert); // replace ov->cert (if any) with this
    ov->cert = cert;
    (void)fclose (f);
    return 0;
error:
    if (fd >= 0)
        (void)close (fd);
    if (f)
        (void)fclose (f);
    return -1;
}

const char *overlay_cert_pubkey (struct overlay *ov)
{
    return cert_public_txt (ov->cert);
}

const char *overlay_cert_name (struct overlay *ov)
{
    return cert_meta_get (ov->cert, "name");
}

int overlay_authorize (struct overlay *ov,
                       const char *name,
                       const char *pubkey)
{
    return children_authorize (ov->children, name, pubkey);
}

/* Test hook: create socket with pending message to block zmq_ctx_term().
 * This allows testing of broker.unload-builtins-timeout.
 */
static void test_create_zombie_socket (struct overlay *ov)
{
    void *zombie;
    int linger = -1;  // infinite linger - wait forever for pending sends
    int hwm = 1;

    if (!ov->zctx) {
        flux_log (ov->h, LOG_DEBUG, "test: no zctx (no overlay peers), skipping");
        return;
    }

    flux_log (ov->h, LOG_DEBUG, "test: creating zombie socket");

    if (!(zombie = zmq_socket (ov->zctx, ZMQ_PUSH))) {
        flux_log_error (ov->h, "test: zmq_socket failed");
        return;
    }
    zmq_setsockopt (zombie, ZMQ_LINGER, &linger, sizeof (linger));
    zmq_setsockopt (zombie, ZMQ_SNDHWM, &hwm, sizeof (hwm));

    // Connect to unreachable endpoint
    if (zmq_connect (zombie, "tcp://127.0.0.1:1") < 0) {
        flux_log_error (ov->h, "test: zmq_connect failed");
        zmq_close (zombie);
        return;
    }

    // Send message - will be queued since endpoint is unreachable
    if (zmq_send (zombie, "x", 1, 0) < 0) {
        flux_log_error (ov->h, "test: zmq_send failed");
        zmq_close (zombie);
        return;
    }

    flux_log (ov->h, LOG_DEBUG, "test: zombie socket created successfully");
    // Don't close - zmq_ctx_term will block waiting for this message
}

void overlay_destroy (struct overlay *ov)
{
    if (ov) {
        int saved_errno = errno;

        flux_msglist_destroy (ov->health_requests);

        cert_destroy (ov->cert);

        flux_future_destroy (ov->f_sync);
        flux_future_destroy (ov->f_state);
        flux_msg_handler_delvec (ov->handlers);
        ovconf_fini (&ov->config);
        ov->status = SUBTREE_STATUS_OFFLINE;
        overlay_control_parent (ov, CONTROL_STATUS, ov->status);

        flux_close (ov->h_channel);
        flux_watcher_destroy (ov->w_channel);

        zmq_close (ov->parent.zsock);
        free (ov->parent.uri);
        flux_watcher_destroy (ov->parent.w);
        free (ov->parent.pubkey);
        zmqutil_monitor_destroy (ov->parent.monitor);

        bizcard_decref (ov->bizcard);

        rpc_track_destroy (ov->parent.tracker);
        children_destroy (ov->children);
        flux_msglist_destroy (ov->monitor_requests);
        flux_msglist_destroy (ov->trace_requests);
        topology_decref (ov->topo);
        if (flux_module_debug_test (ov->h, DEBUG_ZOMBIESOCKET, false))
            test_create_zombie_socket (ov);
        if (!ov->zctx_external)
            zmq_ctx_term (ov->zctx);
        free (ov->hostname);
        free (ov);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.trace",
        overlay_trace_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.stats-get",
        overlay_stats_get_cb,
        FLUX_ROLE_USER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.health",
        overlay_health_cb,
        FLUX_ROLE_USER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.monitor",
        overlay_monitor_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.disconnect", // clean up after 'flux overlay status --wait'
        disconnect_cb,
        FLUX_ROLE_USER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.topology",
        overlay_topology_cb,
        FLUX_ROLE_USER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.disconnect-subtree",   // handle 'flux overlay disconnect'
        overlay_disconnect_subtree_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.disconnect-parent",
        overlay_disconnect_parent_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct overlay *overlay_create (flux_t *h,
                                uint32_t rank,
                                uint32_t size,
                                const char *hostname,
                                const char *uuid,
                                const char *boot_method,
                                void *zctx,
                                const char *uri,
                                flux_error_t *errp)
{
    struct overlay *ov;

    if (!(ov = calloc (1, sizeof (*ov))))
        goto error;
    if (!(ov->hostname = strdup (hostname)))
        goto error;
    ov->parent.lastsent = -1;
    ov->h = h;
    ov->rank = rank;
    ov->size = size;
    ov->reactor = flux_get_reactor (h);
    if (!(ov->h_channel = flux_open (uri, 0))
        || flux_set_reactor (ov->h_channel, ov->reactor) < 0
        || !(ov->w_channel = flux_handle_watcher_create (ov->reactor,
                                                         ov->h_channel,
                                                         FLUX_POLLIN,
                                                         channel_cb,
                                                         ov)))
        goto error;
    flux_watcher_start (ov->w_channel);
    ov->version = FLUX_CORE_VERSION_HEX;

    if (strlcpy (ov->uuid, uuid, sizeof (ov->uuid)) >= sizeof (ov->uuid)) {
        errno = EOVERFLOW;
        goto error;
    }

    if (zctx) {
        ov->zctx = zctx;
        ov->zctx_external = true;
    }
    if (!(ov->monitor_requests = flux_msglist_create ()))
        goto error;
    if (ovconf_init (&ov->config, ov->h, errp) < 0)
        goto error_hasmsg;
    if (flux_msg_handler_addvec (h, htab, ov, &ov->handlers) < 0)
        goto error;
    if (!(ov->cert = cert_create ())) {
        errprintf (errp,
                   "could not create curve certificate: %s",
                   strerror (errno));
        goto error_hasmsg;
    }
    if (!(ov->bizcard = bizcard_create (hostname, cert_public_txt (ov->cert))))
        goto error;
    if (!(ov->health_requests = flux_msglist_create ())
        || !(ov->trace_requests = flux_msglist_create ()))
        goto error;
    if (boot_method) {
        if (streq (boot_method, "config")) {
            if (boot_config (h, rank, size, hostname, ov, errp) < 0)
                goto error_hasmsg;
        }
        else {
            if (boot_pmi (h, rank, size, hostname, ov, errp) < 0)
                goto error_hasmsg;
        }
    }
    if (overlay_register_attrs (ov) < 0) {
        errprintf (errp, "overlay setattr error: %s", strerror (errno));
        goto error;
    }
    if (boot_method) {
        struct idset *crit = overlay_get_default_critical_ranks (ov);
        if (!crit) {
            errprintf (errp,
                       "error calculating default critical ranks: %s",
                       strerror (errno));
            goto error_hasmsg;
        }
        if (boot_util_finalize (h, crit, errp) < 0) {
            idset_destroy (crit);
            goto error_hasmsg;
        }
        idset_destroy (crit);
    }
    return ov;
error:
    errprintf (errp, "%s", strerror (errno));
error_hasmsg:
    overlay_destroy (ov);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
