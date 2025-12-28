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
#include "attr.h"
#include "trace.h"
#include "state_machine.h"
#include "boot_pmi.h"
#include "boot_config.h"

/* How long to wait (seconds) for a peer broker's TCP ACK before disconnecting.
 * This can be configured via TOML and on the broker command line.
 */
static const double default_tcp_user_timeout = 20.;
#ifdef ZMQ_TCP_MAXRT
static bool have_tcp_maxrt = true;
#else
static bool have_tcp_maxrt = false;
#endif

/* How long to wait (seconds) for a connect attempt to time out before
 * reconnecting.
 */
static const double default_connect_timeout = 30.;
#ifdef ZMQ_CONNECT_TIMEOUT
static bool have_connect_timeout = true;
#else
static bool have_connect_timeout = false;
#endif

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif

#define FLUX_ZAP_DOMAIN "flux"

/* Numerical values for "subtree health" so we can send them in control
 * messages.  Textual values below will be used for communication with front
 * end diagnostic tool.
 */
enum subtree_status {
    SUBTREE_STATUS_UNKNOWN = 0,
    SUBTREE_STATUS_FULL = 1,
    SUBTREE_STATUS_PARTIAL = 2,
    SUBTREE_STATUS_DEGRADED = 3,
    SUBTREE_STATUS_LOST = 4,
    SUBTREE_STATUS_OFFLINE = 5,
    SUBTREE_STATUS_MAXIMUM = 5,
};

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

struct child {
    double lastseen;
    uint32_t rank;
    char uuid[UUID_STR_LEN];
    enum subtree_status status;
    struct timespec status_timestamp;
    bool torpid;
    struct rpc_track *tracker;
    flux_error_t error;
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

static const double default_torpid_min = 5.0;
static const double default_torpid_max = 30.0;

static const char *default_interface_hint = "default-route";

struct overlay {
    void *zctx;
    bool zctx_external;
    struct cert *cert;
    struct zmqutil_zap *zap;
    int enable_ipv6;

    flux_t *h;
    char *hostname;
    attr_t *attrs;
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
    int zmqdebug;
    int zmq_io_threads;
    double torpid_min;
    double torpid_max;
    double tcp_user_timeout;
    double connect_timeout;
    int child_rcvhwm;

    struct parent parent;

    bool spurn_hello;           // no new downstream connections permitted
    void *bind_zsock;           // NULL if no downstream peers
    struct bizcard *bizcard;
    flux_watcher_t *bind_w;
    struct child *children;
    int child_count;
    zhashx_t *child_hash;
    enum subtree_status status;
    struct timespec status_timestamp;
    struct zmqutil_monitor *bind_monitor;

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
static struct child *child_lookup_byrank (struct overlay *ov, uint32_t rank);
static int overlay_goodbye_parent (struct overlay *overlay, flux_error_t *errp);
static int overlay_get_child_online_peer_count (struct overlay *ov);
static void overlay_event_checkseq (struct overlay *ov, const flux_msg_t *msg);

/* Convenience iterator for ov->children
 */
#define foreach_overlay_child(ov, child) \
    for ((child) = &(ov)->children[0]; \
            (child) - &(ov)->children[0] < (ov)->child_count; \
            (child)++)

static const char *subtree_status_str (enum subtree_status status)
{
    if (status > SUBTREE_STATUS_MAXIMUM)
        return "unknown";
    return subtree_status_names[status];
}

static bool subtree_is_online (enum subtree_status status)
{
    switch (status) {
        case SUBTREE_STATUS_FULL:
        case SUBTREE_STATUS_PARTIAL:
        case SUBTREE_STATUS_DEGRADED:
            return true;
        default:
            return false;
    }
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

    foreach_overlay_child (ov, child) {
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
        if ((child = child_lookup_byrank (ov, rank)))
            overlay_monitor_respond_one (ov->h, msg, child);
        msg = flux_msglist_next (ov->monitor_requests);
    }

    /* Notify state machine once all child subtrees are offline
     */
    if (ov->broker_state == STATE_SHUTDOWN
        && ov->child_count > 0
        && overlay_get_child_online_peer_count (ov) == 0)
        overlay_state_machine_post (ov, "children-complete", false);
}

int overlay_set_topology (struct overlay *ov, struct topology *topo)
{
    int *child_ranks = NULL;
    ssize_t child_count;

    ov->topo = topology_incref (topo);
    /* Determine which ranks, if any are direct children of this one.
     */
    if ((child_count = topology_get_child_ranks (topo, NULL, 0)) < 0
        || !(child_ranks = calloc (child_count, sizeof (child_ranks[0])))
        || topology_get_child_ranks (topo, child_ranks, child_count) < 0)
        goto error;

    if (!cert_meta_get (ov->cert, "name")) {
        char val[16];
        snprintf (val, sizeof (val), "%lu", (unsigned long)ov->rank);
        if (cert_meta_set (ov->cert, "name", val) < 0)
            goto error;
    }
    ov->child_count = child_count;
    if (ov->child_count > 0) {
        int i;

        if (!(ov->children = calloc (ov->child_count, sizeof (struct child))))
            return -1;
        if (!(ov->child_hash = zhashx_new ()))
            return -1;
        zhashx_set_key_duplicator (ov->child_hash, NULL);
        zhashx_set_key_destructor (ov->child_hash, NULL);
        for (i = 0; i < ov->child_count; i++) {
            struct child *child = &ov->children[i];
            child->rank = child_ranks[i];
            child->status = SUBTREE_STATUS_OFFLINE;
            monotime (&child->status_timestamp);
            child->tracker = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG);
            if (!child->tracker)
                return -1;
            if (topology_rank_aux_set (topo,
                                       child->rank,
                                       "child",
                                       child,
                                       NULL) < 0)
                return -1;
        }
        ov->status = SUBTREE_STATUS_PARTIAL;
    }
    else
        ov->status = SUBTREE_STATUS_FULL;
    monotime (&ov->status_timestamp);
    if (ov->rank > 0) {
        ov->parent.rank = topology_get_parent (topo);
        ov->parent.tracker = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG);
    }
    free (child_ranks);
    return 0;
error:
    free (child_ranks);
    return -1;
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

static int overlay_get_child_online_peer_count (struct overlay *ov)
{
    struct child *child;
    int count = 0;

    foreach_overlay_child (ov, child) {
        if (subtree_is_online (child->status))
            count++;
    }
    return count;
}

void overlay_set_ipv6 (struct overlay *ov, int enable)
{
    ov->enable_ipv6 = enable;
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

    foreach_overlay_child (ov, child) {
        if (subtree_is_online (child->status) && child->lastseen > 0) {
            double duration = now - child->lastseen;

            if (duration >= ov->torpid_max && ov->torpid_max > 0) {
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

/* N.B. overlay_child_status_update() ensures child_lookup_online() only
 * succeeds for online peers.
 */
static struct child *child_lookup_online (struct overlay *ov, const char *id)
{
    return ov->child_hash ?  zhashx_lookup (ov->child_hash, id) : NULL;
}

/* Lookup (direct) child peer by rank.
 * Returns NULL on lookup failure.
 */
static struct child *child_lookup_byrank (struct overlay *ov, uint32_t rank)
{
    return topology_rank_aux_get (ov->topo, rank, "child");
}

/* Look up child that provides route to 'rank' (NULL if none).
 */
static struct child *child_lookup_route (struct overlay *ov, uint32_t rank)
{
    int child_rank;

    child_rank = topology_get_child_route (ov->topo, rank);
    if (child_rank < 0)
        return NULL;
    return child_lookup_byrank (ov, child_rank);
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
                && (child = child_lookup_route (ov, nodeid))) {
                if (!subtree_is_online (child->status)) {
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
                        child = child_lookup_online (ov, ov->uuid);
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
        case STATE_NONE:
        case STATE_JOIN:
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
            if (overlay_get_child_online_peer_count (ov) == 0)
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

    if (now - ov->parent.lastsent > ov->torpid_min)
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
    flux_msg_t *rep;

    if (!(rep = flux_response_derive (msg, EHOSTUNREACH))
        || flux_msg_route_delete_last (rep) < 0
        || flux_msg_route_delete_last (rep) < 0
        || flux_send (ov->h, rep, 0) < 0)
        log_tracker_error (ov->h, rep, errno);
    flux_msg_destroy (rep);
}

static void overlay_child_status_update (struct overlay *ov,
                                         struct child *child,
                                         int status,
                                         const char *reason)
{
    if (child->status != status) {
        if (subtree_is_online (child->status)
            && !subtree_is_online (status)) {
            zhashx_delete (ov->child_hash, child->uuid);
            rpc_track_purge (child->tracker, fail_child_rpcs, ov);
        }
        else if (!subtree_is_online (child->status)
            && subtree_is_online (status)) {
            zhashx_insert (ov->child_hash, child->uuid, child);
        }

        child->status = status;
        monotime (&child->status_timestamp);

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

    if (!ov->bind_zsock) {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = zmqutil_msg_send_ex (ov->bind_zsock, msg, true);
    /* Since ROUTER socket has ZMQ_ROUTER_MANDATORY set, EHOSTUNREACH on a
     * connected peer signifies a disconnect.  See zmq_setsockopt(3).
     */
    if (rc < 0 && errno == EHOSTUNREACH) {
        int saved_errno = errno;
        const char *uuid;
        struct child *child;

        if ((uuid = flux_msg_route_last (msg))
            && (child = child_lookup_online (ov, uuid))) {
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
                && (child = child_lookup_online (ov, uuid)))
                rank = child->rank;
            trace_overlay_msg (ov->h, "tx", rank, ov->trace_requests, msg);
        }
    }
done:
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

    foreach_overlay_child (ov, child) {
        if (subtree_is_online (child->status)) {
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

    if (!(msg = zmqutil_msg_recv (ov->bind_zsock)))
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
    if (!(child = child_lookup_online (ov, uuid))) {
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

    if (flux_respond_error (ov->h,
                            msg,
                            EHOSTUNREACH,
                            "overlay disconnect") < 0)
        log_tracker_error (ov->h, msg, errno);
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
    if (!(child = child_lookup_byrank (ov, rank))) {
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
    if (subtree_is_online (child->status)) { // crash
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

static void bind_monitor_cb (struct zmqutil_monitor *mon, void *arg)
{
    struct overlay *ov = arg;
    struct monitor_event event;

    if (zmqutil_monitor_get (mon, &event) == 0) {
        flux_log (ov->h,
                  zmqutil_monitor_iserror (&event) ? LOG_ERR : LOG_DEBUG,
                  "child sockevent %s %s%s%s",
                  event.endpoint,
                  event.event_str,
                  *event.value_str ? ": " : "",
                  event.value_str);
    }
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
        if (ov->rank > 0 && ov->zmq_io_threads != 1) {
            const char *key = "tbon.zmq_io_threads";
            if (attr_set_flags (ov->attrs, key, 0) < 0
                || attr_delete (ov->attrs, key, true) < 0
                || attr_add_int (ov->attrs, key, 1, ATTR_IMMUTABLE) < 0)
                return -1;
            ov->zmq_io_threads = 1;
        }
        if (zmq_ctx_set (ov->zctx,
                         ZMQ_IO_THREADS,
                         ov->zmq_io_threads) < 0)
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
            || zsetsockopt_int (ov->parent.zsock, ZMQ_IPV6, ov->enable_ipv6) < 0
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
        if (ov->zmqdebug) {
            ov->parent.monitor = zmqutil_monitor_create (ov->zctx,
                                                         ov->parent.zsock,
                                                         ov->reactor,
                                                         parent_monitor_cb,
                                                         ov);
        }
#ifdef ZMQ_CONNECT_TIMEOUT
        if (ov->connect_timeout > 0) {
            if (zsetsockopt_int (ov->parent.zsock,
                                 ZMQ_CONNECT_TIMEOUT,
                                 ov->connect_timeout * 1000) < 0)
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

static void zaplogger (int severity, const char *message, void *arg)
{
    struct overlay *ov = arg;

    flux_log (ov->h, severity, "%s", message);
}

static int bind_uri (struct overlay *ov, const char *uri)
{
    char *new_uri;
    if (zmq_bind (ov->bind_zsock, uri) < 0)
        return -1;
    /* Capture URI after zmq_bind() processing, so it reflects expanded
     * wildcards and normalized addresses.
     */
    if (zgetsockopt_str (ov->bind_zsock, ZMQ_LAST_ENDPOINT, &new_uri) < 0)
        return -1;
    /* Record the concretized URI in the business card for sharing with peers.
     */
    if (bizcard_uri_append (ov->bizcard, new_uri) < 0) {
        ERRNO_SAFE_WRAP (free, new_uri);
        return -1;
    }
    /* Ensure that any AF_UNIX sockets are unlinked when the broker exits,
     * but only if they begin with '/'.  (Abstract sockets begin with '@')
     */
    if (strstarts (new_uri, "ipc:///"))
        cleanup_push_string (cleanup_file, new_uri + 6);
    flux_log (ov->h, LOG_DEBUG, "listening on %s", new_uri);
    free (new_uri);
    return 0;
}

int overlay_bind (struct overlay *ov,
                  const char *uri,
                  const char *uri2,
                  flux_error_t *errp)
{
    if (!ov->h || ov->rank == FLUX_NODEID_ANY || ov->bind_zsock) {
        errno = EINVAL;
        return errprintf (errp, "overlay_bind: invalid arguments");
    }
    if (overlay_zmq_init (ov) < 0) {
        return errprintf (errp,
                          "error creating zeromq context: %s",
                          strerror (errno));
    }
    if (ov->zap != NULL) {
        errno = EINVAL;
        return errprintf (errp, "ZAP is already initialized!");
    }
    if (!(ov->zap = zmqutil_zap_create (ov->zctx, ov->reactor))) {
        return errprintf (errp,
                          "error creating ZAP server: %s",
                          strerror (errno));
    }
    zmqutil_zap_set_logger (ov->zap, zaplogger, ov);

    if (!(ov->bind_zsock = zmq_socket (ov->zctx, ZMQ_ROUTER))
        || zsetsockopt_int (ov->bind_zsock, ZMQ_SNDHWM, 0) < 0
        || zsetsockopt_int (ov->bind_zsock, ZMQ_RCVHWM, ov->child_rcvhwm) < 0
        || zsetsockopt_int (ov->bind_zsock, ZMQ_LINGER, 5) < 0
        || zsetsockopt_int (ov->bind_zsock, ZMQ_ROUTER_MANDATORY, 1) < 0
        || zsetsockopt_int (ov->bind_zsock, ZMQ_IPV6, ov->enable_ipv6) < 0
        || zsetsockopt_str (ov->bind_zsock, ZMQ_ZAP_DOMAIN, FLUX_ZAP_DOMAIN) < 0
        || zsetsockopt_int (ov->bind_zsock, ZMQ_CURVE_SERVER, 1) < 0) {
        return errprintf (errp,
                          "error creating zmq ROUTER socket: %s",
                          strerror (errno));
    }
    /* The socket monitor is only used for logging.
     * Setup may fail if libzmq is too old.
     */
    if (ov->zmqdebug) {
        ov->bind_monitor = zmqutil_monitor_create (ov->zctx,
                                                   ov->bind_zsock,
                                                   ov->reactor,
                                                   bind_monitor_cb,
                                                   ov);
    }
#ifdef ZMQ_TCP_MAXRT
    if (ov->tcp_user_timeout > 0) {
        if (zsetsockopt_int (ov->bind_zsock,
                             ZMQ_TCP_MAXRT,
                             ov->tcp_user_timeout * 1000) < 0) {
            return errprintf (errp,
                              "error setting TCP_MAXRT option"
                              " on bind socket: %s",
                              strerror (errno));
        }
    }
#endif
    if (cert_apply (ov->cert, ov->bind_zsock) < 0) {
        return errprintf (errp,
                          "error setting curve socket options: %s",
                          strerror (errno));
    }
    if (bind_uri (ov, uri) < 0) {
        return errprintf (errp,
                          "error binding to %s: %s",
                          uri,
                          strerror (errno));
    }
    if (uri2 && bind_uri (ov, uri2) < 0) {
        return errprintf (errp,
                          "error binding to %s: %s",
                          uri2,
                          strerror (errno));
    }
    if (!(ov->bind_w = zmqutil_watcher_create (ov->reactor,
                                               ov->bind_zsock,
                                               FLUX_POLLIN,
                                               child_cb,
                                               ov))) {
        return errprintf (errp,
                          "error creating watcher for bind socket: %s",
                          strerror (errno));
    }
    flux_watcher_start (ov->bind_w);
    return 0;
}

/* Call after overlay bootstrap (bind/connect),
 * to get concretized 0MQ endpoints.
 */
static int overlay_register_attrs (struct overlay *overlay)
{
    if (attr_add (overlay->attrs,
                  "tbon.parent-endpoint",
                  overlay->parent.uri,
                  ATTR_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (overlay->attrs,
                      "tbon.level",
                      topology_get_level (overlay->topo),
                      ATTR_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (overlay->attrs,
                      "tbon.maxlevel",
                      topology_get_maxlevel (overlay->topo),
                      ATTR_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (overlay->attrs,
                      "tbon.descendants",
                      topology_get_descendant_count (overlay->topo),
                      ATTR_IMMUTABLE) < 0)
        return -1;

    return 0;
}

/* A child has sent an overlay.goodbye request.
 * Respond, then transition it to OFFLINE.
 */
static void overlay_goodbye_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    struct overlay *ov = arg;
    const char *uuid;
    struct child *child;
    flux_msg_t *response = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0
        || !(uuid = flux_msg_route_last (msg))
        || !(child = child_lookup_online (ov, uuid)))
        goto error;
    if (!(response = flux_response_derive (msg, 0)))
        goto error;
    if (overlay_sendmsg_child (ov, response) < 0) {
        flux_msg_decref (response);
        goto error;
    }
    overlay_child_status_update (ov,
                                 child,
                                 SUBTREE_STATUS_OFFLINE,
                                 "administrative shutdown");
    flux_msg_decref (response);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to overlay.goodbye");
}

/* The parent has responded to overlay.goodbye.  Post the goodbye event
 * so the state machine can make progress.
 */
static void overlay_goodbye_response_cb (flux_t *h,
                                         flux_msg_handler_t *mh,
                                         const flux_msg_t *msg,
                                         void *arg)
{
    struct overlay *ov = arg;
    if (ov->broker_state == STATE_GOODBYE)
        overlay_state_machine_post (ov, "goodbye", false);
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
    int count = 0;
    int i;
    for (i = 0; i < ov->child_count; i++)
        count += rpc_track_count (ov->children[i].tracker);
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
    int child_connected = overlay_get_child_online_peer_count (ov);
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i s:i s:i s:i s:{s:i s:i}}",
                           "child-count", ov->child_count,
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
    foreach_overlay_child (ov, child) {
        duration = monotime_since (child->status_timestamp) / 1000.0;
        if (!(entry = json_pack ("{s:i s:s s:f}",
                                 "rank", child->rank,
                                 "status", subtree_status_str (child->status),
                                 "duration", duration)))
            goto nomem;
        if (!subtree_is_online (child->status) && child->error.text[0]) {
            json_t *o;
            if (!(o = json_string (child->error.text))
                || json_object_set_new (entry, "error", o) < 0) {
                json_decref (o);
                // if this fails (unlikely), soldier on
            }
        }
        if (json_array_append_new (array, entry) < 0) {
            json_decref (entry);
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
    foreach_overlay_child (ov, child) {
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
    if (rank != ov->rank && !child_lookup_byrank (ov, rank)) {
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
    if (!(child = child_lookup_byrank (ov, rank))) {
        errstr = "requested rank is not this broker's direct child";
        errno = ENOENT;
        goto error;
    }
    if (!subtree_is_online (child->status)) {
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
    if (!ov->zap) {
        errno = EINVAL;
        return -1;
    }
    return zmqutil_zap_authorize (ov->zap, name, pubkey);
}

static int overlay_configure_attr (attr_t *attrs,
                                   const char *name,
                                   const char *default_value,
                                   const char **valuep)
{
    const char *val = default_value;
    int flags;

    if (attr_get (attrs, name, &val, &flags) == 0) {
        if (!(flags & ATTR_IMMUTABLE)) {
            flags |= ATTR_IMMUTABLE;
            if (attr_set_flags (attrs, name, flags) < 0)
                return -1;
        }
    }
    else {
        val = default_value;
        if (attr_add (attrs, name, val, ATTR_IMMUTABLE) < 0)
            return -1;
    }
    if (valuep)
        *valuep = val;
    return 0;
}

static int overlay_configure_attr_int (attr_t *attrs,
                                       const char *name,
                                       int default_value,
                                       int *valuep,
                                       flux_error_t *errp)
{
    int value = default_value;
    const char *val;
    char *endptr;

    if (attr_get (attrs, name, &val, NULL) == 0) {
        errno = 0;
        value = strtol (val, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            errno = EINVAL;
            return errprintf (errp, "%s value must be an integer", name);
        }
        if (attr_delete (attrs, name, true) < 0) {
            return errprintf (errp,
                              "attr_delete %s: %s",
                              name,
                              strerror (errno));
        }
    }
    if (attr_add_int (attrs, name, value, ATTR_IMMUTABLE) < 0)
        return errprintf (errp, "attr_add %s: %s", name, strerror (errno));
    if (valuep)
        *valuep = value;
    return 0;
}

static int set_torpid (const char *name, const char *val, void *arg)
{
    struct overlay *ov = arg;
    double d;

    if (fsd_parse_duration (val, &d) < 0)
        return -1;
    if (streq (name, "tbon.torpid_max"))
        ov->torpid_max = d;
    else if (streq (name, "tbon.torpid_min")) {
        if (d == 0)
            goto error;
        ov->torpid_min = d;
    }
    else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static int get_torpid (const char *name, const char **val, void *arg)
{
    struct overlay *ov = arg;
    static char buf[64];
    double d;

    if (streq (name, "tbon.torpid_max"))
        d = ov->torpid_max;
    else if (streq (name, "tbon.torpid_min"))
        d = ov->torpid_min;
    else
        goto error;
    if (fsd_format_duration (buf, sizeof (buf), d) < 0)
        return -1;
    *val = buf;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

/* Set attribute with the following precedence:
 * 1. broker attribute
 * 2. TOML config
 * 3. legacy environment variables
 * Leave it unset if none of those are available.
 * The bootstrap methods set it later, but only if not already set.
 */
static int overlay_configure_interface_hint (struct overlay *ov,
                                             const char *table,
                                             const char *name,
                                             flux_error_t *errp)
{
    char long_name[128];
    const char *val = NULL;
    const char *config_val = NULL;
    const char *attr_val = NULL;
    int flags;
    const flux_conf_t *cf;
    flux_error_t error;

    if ((cf = flux_get_conf (ov->h))) {
        if (flux_conf_unpack (cf,
                              &error,
                              "{s?{s?s}}",
                              table,
                                name, &config_val) < 0) {
            return errprintf (errp,
                              "Config file error [%s]: %s",
                              table,
                              error.text);
        }
    }
    (void)snprintf (long_name, sizeof (long_name), "%s.%s", table, name);
    (void)attr_get (ov->attrs, long_name, &attr_val, &flags);

    if (attr_val)
        val = attr_val;
    else if (config_val)
        val = config_val;
    else if ((val = getenv ("FLUX_IPADDR_INTERFACE")))
        ;
    else if (getenv ("FLUX_IPADDR_HOSTNAME"))
        val = "hostname";
    else
        val = default_interface_hint;

    if (val && !attr_val) {
         if (attr_add (ov->attrs, long_name, val, 0) < 0) {
            return errprintf (errp,
                              "Error setting %s attribute value: %s",
                              long_name,
                              strerror (errno));
         }
    }
    return 0;
}

static int overlay_configure_torpid (struct overlay *ov, flux_error_t *errp)
{
    const flux_conf_t *cf;

    /* Start with compiled in defaults.
     */
    ov->torpid_min = default_torpid_min;
    ov->torpid_max = default_torpid_max;

    /* Override with config file settings, if any.
     */
    if ((cf = flux_get_conf (ov->h))) {
        flux_error_t error;
        const char *min_fsd = NULL;
        const char *max_fsd = NULL;

        if (flux_conf_unpack (flux_get_conf (ov->h),
                              &error,
                              "{s?{s?s s?s}}",
                              "tbon",
                                "torpid_min", &min_fsd,
                                "torpid_max", &max_fsd) < 0) {
            return errprintf (errp,
                              "Config file error [tbon]: %s",
                              error.text);
        }
        if (min_fsd) {
            if (fsd_parse_duration (min_fsd, &ov->torpid_min) < 0
                || ov->torpid_min == 0) {
                return errprintf (errp,
                                  "Config file error parsing"
                                  " tbon.torpid_min value");
            }
        }
        if (max_fsd) {
            if (fsd_parse_duration (max_fsd, &ov->torpid_max) < 0) {
                return errprintf (errp,
                                  "Config file error parsing"
                                  " tbon.torpid_max value");
            }
        }
    }

    /* Override with broker attribute (command line/runtime) settings, if any.
     */
    if (attr_add_active (ov->attrs,
                         "tbon.torpid_max",
                         0,
                         get_torpid,
                         set_torpid,
                         ov) < 0)
        return errprintf (errp, "%s", strerror (errno));
    if (attr_add_active (ov->attrs,
                         "tbon.torpid_min",
                         0,
                         get_torpid,
                         set_torpid,
                         ov) < 0)
        return errprintf (errp, "%s", strerror (errno));

    return 0;
}

static int overlay_configure_timeout (struct overlay *ov,
                                      const char *table,
                                      const char *name,
                                      bool enabled,
                                      double default_value,
                                      double *valuep,
                                      flux_error_t *errp)
{
    const flux_conf_t *cf;
    const char *fsd = NULL;
    bool override = false;
    double value = default_value;
    char long_name[128];

    (void)snprintf (long_name, sizeof (long_name), "%s.%s", table, name);

    if ((cf = flux_get_conf (ov->h))) {
        flux_error_t error;

        if (flux_conf_unpack (cf, &error, "{s?{s?s}}", table, name, &fsd) < 0) {
            return errprintf (errp,
                              "Config file error [%s]: %s",
                              table,
                              error.text);
        }
        if (fsd) {
            if (fsd_parse_duration (fsd, &value) < 0) {
                return errprintf (errp,
                                  "Config file error parsing %s",
                                  long_name);
            }
            override = true;
        }
    }
    /* Override with broker attribute (command line only) settings, if any.
     */
    if (attr_get (ov->attrs, long_name, &fsd, NULL) == 0) {
        if (fsd_parse_duration (fsd, &value) < 0)
            return errprintf (errp, "Error parsing %s attribute", long_name);
        if (attr_delete (ov->attrs, long_name, true) < 0) {
            return errprintf (errp,
                              "attr_delete %s: %s",
                              long_name,
                              strerror (errno));
        }
        override = true;
    }
    if (enabled) {
        char buf[64];
        if (fsd_format_duration (buf, sizeof (buf), value) < 0)
            return errprintf (errp, "fsd format: %s", strerror (errno));
        if (attr_add (ov->attrs, long_name, buf, ATTR_IMMUTABLE) < 0) {
            return errprintf (errp,
                              "attr_add %s: %s",
                              long_name,
                              strerror (errno));
        }
    }
    else {
        if (override) {
            return errprintf (errp,
                              "%s unsupported by this zeromq version",
                              long_name);
        }
    }
    *valuep = value;
    return 0;
}

static int overlay_configure_tbon_int (struct overlay *ov,
                                       const char *name,
                                       int *value,
                                       int default_value,
                                       flux_error_t *errp)
{
    const flux_conf_t *cf;
    char attrname[128];

    *value = default_value;
    if ((cf = flux_get_conf (ov->h))) {
        flux_error_t error;

        if (flux_conf_unpack (cf,
                              &error,
                              "{s?{s?i}}",
                              "tbon",
                                name, value) < 0) {
            errprintf (errp, "Config file error [tbon]: %s", error.text);
            return -1;
        }
    }
    (void)snprintf (attrname, sizeof (attrname), "tbon.%s", name);
    if (overlay_configure_attr_int (ov->attrs,
                                    attrname,
                                    *value,
                                    value,
                                    errp) < 0)
        return -1;
    return 0;
}

/* Configure tbon.topo attribute.
 * Ascending precedence: compiled-in default, TOML config, command line.
 * Topology creation is deferred to bootstrap, when we know the instance size.
 */
static int overlay_configure_topo (struct overlay *ov, flux_error_t *errp)
{
    const char *topo_uri = "kary:32";
    const flux_conf_t *cf;

    if ((cf = flux_get_conf (ov->h))) {
        flux_error_t error;

        if (flux_conf_unpack (cf, NULL, "{s:{}}", "bootstrap") == 0)
            topo_uri = "custom"; // adjust default for config boot

        if (flux_conf_unpack (cf,
                              &error,
                              "{s?{s?s}}",
                              "tbon",
                                "topo", &topo_uri) < 0) {
            errprintf (errp, "Config file error [tbon]: %s", error.text);
            return -1;
        }
    }
    /* Treat tbon.fanout=K as an alias for tbon.topo=kary:K.
     */
    const char *fanout;
    char buf[16];
    if (attr_get (ov->attrs, "tbon.fanout", &fanout, NULL) == 0) {
        snprintf (buf, sizeof (buf), "kary:%s", fanout);
        topo_uri = buf;
    }
    if (overlay_configure_attr (ov->attrs,
                                "tbon.topo",
                                topo_uri,
                                NULL) < 0) {
        errprintf (errp,
                   "Error manipulating tbon.topo attribute: %s",
                   strerror (errno));
        return -1;
    }
    return 0;
}

void overlay_destroy (struct overlay *ov)
{
    if (ov) {
        int saved_errno = errno;

        flux_msglist_destroy (ov->health_requests);

        cert_destroy (ov->cert);
        zmqutil_zap_destroy (ov->zap);

        flux_future_destroy (ov->f_sync);
        flux_future_destroy (ov->f_state);
        flux_msg_handler_delvec (ov->handlers);
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
        zmq_close (ov->bind_zsock);
        flux_watcher_destroy (ov->bind_w);
        zmqutil_monitor_destroy (ov->bind_monitor);

        zhashx_destroy (&ov->child_hash);
        if (ov->children) {
            int i;
            for (i = 0; i < ov->child_count; i++)
                rpc_track_destroy (ov->children[i].tracker);
            free (ov->children);
        }
        rpc_track_destroy (ov->parent.tracker);
        flux_msglist_destroy (ov->monitor_requests);
        flux_msglist_destroy (ov->trace_requests);
        topology_decref (ov->topo);
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
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.goodbye",
        overlay_goodbye_cb,
        0,
    },
    {
        FLUX_MSGTYPE_RESPONSE,
        "overlay.goodbye",
        overlay_goodbye_response_cb,
        0,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct overlay *overlay_create (flux_t *h,
                                struct bootstrap *boot,
                                struct upmi_info *info,
                                const char *hostname,
                                attr_t *attrs,
                                void *zctx,
                                const char *uri,
                                flux_error_t *errp)
{
    struct overlay *ov;

    if (!(ov = calloc (1, sizeof (*ov))))
        goto error;
    if (!(ov->hostname = strdup (hostname)))
        goto error;
    ov->attrs = attrs;
    ov->parent.lastsent = -1;
    ov->h = h;
    ov->rank = info->rank;
    ov->size = info->size;
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

    const char *uuid;
    if (attr_get (attrs, "broker.uuid", &uuid, NULL) < 0)
        goto error;
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
    if (overlay_configure_attr_int (ov->attrs,
                                    "tbon.prefertcp",
                                    0,
                                    NULL,
                                    errp) < 0)
        goto error_hasmsg;
    if (overlay_configure_interface_hint (ov,
                                          "tbon",
                                          "interface-hint",
                                          errp) < 0)
        goto error_hasmsg;
    if (overlay_configure_torpid (ov, errp) < 0)
        goto error_hasmsg;
    if (overlay_configure_timeout (ov,
                                   "tbon",
                                   "tcp_user_timeout",
                                   have_tcp_maxrt,
                                   default_tcp_user_timeout,
                                   &ov->tcp_user_timeout,
                                   errp) < 0)
        goto error_hasmsg;
    if (overlay_configure_timeout (ov,
                                   "tbon",
                                   "connect_timeout",
                                   have_connect_timeout,
                                   default_connect_timeout,
                                   &ov->connect_timeout,
                                   errp) < 0)
        goto error_hasmsg;
    if (overlay_configure_tbon_int (ov,
                                    "zmqdebug",
                                    &ov->zmqdebug,
                                    0,
                                    errp) < 0)
        goto error_hasmsg;
    if (overlay_configure_tbon_int (ov,
                                    "child_rcvhwm",
                                    &ov->child_rcvhwm,
                                    0,
                                    errp) < 0)
        goto error_hasmsg;
    if (ov->child_rcvhwm < 0 || ov->child_rcvhwm == 1) {
        errprintf (errp, "tbon.child_rcvhwm must be 0 (unlimited) or >= 2");
        errno = EINVAL;
        goto error_hasmsg;
    }
    if (overlay_configure_tbon_int (ov,
                                    "zmq_io_threads",
                                    &ov->zmq_io_threads,
                                    1,
                                    errp) < 0)
        goto error_hasmsg;
    if (ov->zmq_io_threads < 1) {
        errprintf (errp, "tbon.zmq_io_threads must be >= 1");
        errno = EINVAL;
        goto error_hasmsg;
    }
    if (overlay_configure_topo (ov, errp) < 0)
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
    if (boot) {
        if (streq (bootstrap_method (boot), "config")) {
            if (boot_config (boot, info, h, hostname, ov, attrs, errp) < 0)
                goto error;;
        }
        else {
            if (boot_pmi (boot, info, hostname, ov, attrs, errp) < 0)
                goto error;
        }
    }
    if (overlay_register_attrs (ov) < 0) {
        errprintf (errp, "overlay setattr error: %s", strerror (errno));
        goto error;
    }
    if (boot) {
        struct idset *ids = overlay_get_default_critical_ranks (ov);
        char *crit;
        if (!ids || !(crit = idset_encode (ids, IDSET_FLAG_RANGE))) {
            errprintf (errp,
                       "error calculating default critical ranks: %s",
                       strerror (errno));
            idset_destroy (ids);
            goto error_hasmsg;
        }
        idset_destroy (ids);
        if (bootstrap_finalize (boot, crit, errp) < 0) {
            free (crit);
            goto error_hasmsg;
        }
        free (crit);
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
