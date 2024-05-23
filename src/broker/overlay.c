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
#include <assert.h>
#include <flux/core.h>
#include <inttypes.h>
#include <jansson.h>
#include <uuid.h>

#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libzmqutil/sockopt.h"
#include "src/common/libzmqutil/reactor.h"
#include "src/common/libzmqutil/zap.h"
#include "src/common/libzmqutil/cert.h"
#include "src/common/libzmqutil/monitor.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/librouter/rpc_track.h"
#include "ccan/str/str.h"

#include "overlay.h"
#include "attr.h"

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

/* Overlay control messages
 */
enum control_type {
    CONTROL_HEARTBEAT = 0, // child sends when connection is idle
    CONTROL_STATUS = 1,    // child tells parent of subtree status change
    CONTROL_DISCONNECT = 2,// parent tells child to immediately disconnect
};

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
    flux_future_t *f_goodbye;
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

struct overlay_monitor {
    overlay_monitor_f cb;
    void *arg;
};

struct overlay {
    void *zctx;
    bool zctx_external;
    struct cert *cert;
    struct zmqutil_zap *zap;
    int enable_ipv6;

    flux_t *h;
    attr_t *attrs;
    flux_reactor_t *reactor;
    flux_msg_handler_t **handlers;
    flux_future_t *f_sync;

    struct topology *topo;
    uint32_t size;
    uint32_t rank;
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

    bool shutdown_in_progress;  // no new downstream connections permitted
    void *bind_zsock;           // NULL if no downstream peers
    char *bind_uri;
    flux_watcher_t *bind_w;
    struct child *children;
    int child_count;
    zhashx_t *child_hash;
    enum subtree_status status;
    struct timespec status_timestamp;
    struct zmqutil_monitor *bind_monitor;

    zlist_t *monitor_callbacks;

    overlay_recv_f recv_cb;
    void *recv_arg;

    struct flux_msglist *health_requests;
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

static void overlay_monitor_notify (struct overlay *ov, uint32_t rank)
{
    struct overlay_monitor *mon;

    mon = zlist_first (ov->monitor_callbacks);
    while (mon) {
        mon->cb (ov, rank, mon->arg);
        mon = zlist_next (ov->monitor_callbacks);
    }
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

    ov->size = topology_get_size (topo);
    ov->rank = topology_get_rank (topo);
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

uint32_t overlay_get_rank (struct overlay *ov)
{
    return ov->rank;
}

void overlay_set_rank (struct overlay *ov, uint32_t rank)
{
    ov->rank = rank;
}

uint32_t overlay_get_size (struct overlay *ov)
{
    return ov->size;
}

const char *overlay_get_uuid (struct overlay *ov)
{
    return ov->uuid;
}

bool overlay_parent_error (struct overlay *ov)
{
    return ((ov->parent.hello_responded && ov->parent.hello_error)
            || ov->parent.offline);
}

void overlay_set_version (struct overlay *ov, int version)
{
    ov->version = version;
}

int overlay_get_child_peer_count (struct overlay *ov)
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

bool overlay_peer_is_torpid (struct overlay *ov, uint32_t rank)
{
    struct child *child;

    if (!(child = child_lookup_byrank (ov, rank)))
        return false;
    return child->torpid;
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

bool overlay_uuid_is_child (struct overlay *ov, const char *uuid)
{
    if (child_lookup_online (ov, uuid) != NULL)
        return true;
    return false;
}

bool overlay_uuid_is_parent (struct overlay *ov, const char *uuid)
{
    if (ov->rank > 0 && streq (uuid, ov->parent.uuid))
        return true;
    return false;
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

const char *overlay_get_parent_uri (struct overlay *ov)
{
    return ov->parent.uri;
}

static int overlay_sendmsg_parent (struct overlay *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->parent.zsock || ov->parent.offline || ov->parent.goodbye_sent) {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = zmqutil_msg_send (ov->parent.zsock, msg);
    if (rc == 0)
        ov->parent.lastsent = flux_reactor_now (ov->reactor);
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

int overlay_sendmsg_new (struct overlay *ov,
                         flux_msg_t **msg,
                         overlay_where_t where)
{
    int type;
    const char *uuid;
    uint32_t nodeid;
    struct child *child = NULL;

    if (flux_msg_get_type (*msg, &type) < 0)
        return -1;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            /* If message is being routed downstream to reach 'nodeid',
             * push the local uuid, then the next hop onto the messages's
             * route stack so that the ROUTER socket can pop off next hop to
             * select the peer, and our uuid remains as part of the source addr.
             */
            if (where == OVERLAY_ANY) {
                if (flux_msg_get_nodeid (*msg, &nodeid) < 0)
                    return -1;
                if (flux_msg_has_flag (*msg, FLUX_MSGFLAG_UPSTREAM)
                    && nodeid == ov->rank)
                    where = OVERLAY_UPSTREAM;
                else {
                    if ((child = child_lookup_route (ov, nodeid))) {
                        if (!subtree_is_online (child->status)) {
                            errno = EHOSTUNREACH;
                            return -1;
                        }
                        if (flux_msg_route_push (*msg, ov->uuid) < 0
                            || flux_msg_route_push (*msg, child->uuid) < 0)
                            return -1;
                        where = OVERLAY_DOWNSTREAM;
                    }
                    else
                        where = OVERLAY_UPSTREAM;
                }
            }
            if (where == OVERLAY_UPSTREAM) {
                if (overlay_sendmsg_parent (ov, *msg) < 0)
                    return -1;
                rpc_track_update (ov->parent.tracker, *msg);
            }
            else {
                if (overlay_sendmsg_child (ov, *msg) < 0)
                    return -1;
                if (!child) {
                    if ((uuid = flux_msg_route_last (*msg)))
                        child = child_lookup_online (ov, ov->uuid);
                }
                if (child)
                    rpc_track_update (child->tracker, *msg);
            }
            break;
        case FLUX_MSGTYPE_RESPONSE:
            /* Assume if next route matches parent, the message goes upstream;
             * otherwise downstream.  The send downstream will fail with
             * EHOSTUNREACH if uuid doesn't match an immediate peer.
             */
            if (where == OVERLAY_ANY) {
                if (ov->rank > 0
                    && (uuid = flux_msg_route_last (*msg)) != NULL
                    && streq (uuid, ov->parent.uuid))
                    where = OVERLAY_UPSTREAM;
                else
                    where = OVERLAY_DOWNSTREAM;
            }
            if (where == OVERLAY_UPSTREAM) {
                if (overlay_sendmsg_parent (ov, *msg) < 0)
                    return -1;
            }
            else {
                if (overlay_sendmsg_child (ov, *msg) < 0)
                    return -1;
            }
            break;
        case FLUX_MSGTYPE_EVENT:
            if (where == OVERLAY_DOWNSTREAM || where == OVERLAY_ANY)
                overlay_mcast_child (ov, *msg);
            else {
                /* N.B. add route delimiter if needed to pass unpublished
                 * event message upstream through router socket.
                 */
                if (flux_msg_route_count (*msg) < 0)
                    flux_msg_route_enable (*msg);
                if (overlay_sendmsg_parent (ov, *msg) < 0)
                    return -1;
            }
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    flux_msg_decref (*msg);
    *msg = NULL;
    return 0;
}

int overlay_sendmsg (struct overlay *ov,
                     const flux_msg_t *msg,
                     overlay_where_t where)
{
    flux_msg_t *cpy;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (overlay_sendmsg_new (ov, &cpy, where) < 0) {
        flux_msg_destroy (cpy);
        return -1;
    }
    return 0;
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

int overlay_control_start (struct overlay *ov)
{
    if (!ov->f_sync) {
        if (!(ov->f_sync = flux_sync_create (ov->h, sync_min))
            || flux_future_then (ov->f_sync, sync_max, sync_cb, ov) < 0)
            return -1;
    }
    return 0;
}

const char *overlay_get_bind_uri (struct overlay *ov)
{
    return ov->bind_uri;
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
                                         int status)
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
            flux_log (ov->h,
                      LOG_ERR,
                      "%s (rank %d) has disconnected unexpectedly."
                      " Marking it LOST.",
                      flux_get_hostbyrank (ov->h, child->rank),
                      (int)child->rank);
            overlay_child_status_update (ov, child, SUBTREE_STATUS_LOST);
        }
        errno = saved_errno;
    }
done:
    return rc;
}

/* Push child->uuid onto the message, then pop it off again after sending.
 */
static int overlay_mcast_child_one (struct overlay *ov,
                                    flux_msg_t *msg,
                                    struct child *child)
{
    if (flux_msg_route_push (msg, child->uuid) < 0)
        return -1;
    int rc = overlay_sendmsg_child (ov, msg);
    (void)flux_msg_route_delete_last (msg);
    return rc;
}

static void overlay_mcast_child (struct overlay *ov, flux_msg_t *msg)
{
    struct child *child;

    flux_msg_route_enable (msg);

    foreach_overlay_child (ov, child) {
        if (subtree_is_online (child->status)) {
            if (overlay_mcast_child_one (ov, msg, child) < 0) {
                if (errno != EHOSTUNREACH) {
                    flux_log_error (ov->h,
                                    "mcast error to child rank %lu",
                                    (unsigned long)child->rank);
                }
            }
        }
    }
}

static void logdrop (struct overlay *ov,
                     overlay_where_t where,
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
    if (where == OVERLAY_DOWNSTREAM)
        child_uuid = flux_msg_route_last (msg);

    va_start (ap, fmt);
    (void)vsnprintf (reason, sizeof (reason), fmt, ap);
    va_end (ap);

    flux_log (ov->h,
              LOG_ERR,
              "DROP %s %s topic %s %s%s: %s",
              where == OVERLAY_UPSTREAM ? "upstream" : "downstream",
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
        logdrop (ov, OVERLAY_DOWNSTREAM, msg, "failed to clear local role");
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0
        || !(uuid = flux_msg_route_last (msg))) {
        logdrop (ov, OVERLAY_DOWNSTREAM, msg, "malformed message");
        goto done;
    }
    if (!(child = child_lookup_online (ov, uuid))) {
        /* This is a new peer trying to introduce itself by sending an
         * overlay.hello request.
         * N.B. the broker generates a new UUID on startup, and hello is only
         * sent once on startup, in overlay_connect().  Therefore, it is
         * assumed that a overlay.hello is always introducing a new UUID and
         * we don't bother checking if we've seen this UUID before, which can
         * be slow given current design.  See flux-framework/flux-core#5864.
         */
        if (type == FLUX_MSGTYPE_REQUEST
            && flux_msg_get_topic (msg, &topic) == 0
            && streq (topic, "overlay.hello")
            && !ov->shutdown_in_progress) {
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
    assert (subtree_is_online (child->status));

    child->lastseen = flux_reactor_now (ov->reactor);
    switch (type) {
        case FLUX_MSGTYPE_CONTROL: {
            int type, status;
            if (flux_control_decode (msg, &type, &status) == 0
                && type == CONTROL_STATUS)
                overlay_child_status_update (ov, child, status);
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
            break;
    }
    if (ov->recv_cb (&msg, OVERLAY_DOWNSTREAM, ov->recv_arg) < 0)
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
        logdrop (ov, OVERLAY_UPSTREAM, msg, "failed to clear local role");
        goto done;
    }
    if (flux_msg_get_type (msg, &type) < 0) {
        logdrop (ov, OVERLAY_UPSTREAM, msg, "malformed message");
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
            logdrop (ov, OVERLAY_UPSTREAM, msg,
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
            flux_msg_route_disable (msg);
            break;
        case FLUX_MSGTYPE_CONTROL: {
            int ctrl_type, reason;
            if (flux_control_decode (msg, &ctrl_type, &reason) < 0) {
                logdrop (ov, OVERLAY_UPSTREAM, msg, "malformed control");
            }
            else if (ctrl_type == CONTROL_DISCONNECT) {
                flux_log (ov->h, LOG_CRIT,
                          "%s (rank %lu) sent disconnect control message",
                          flux_get_hostbyrank (ov->h, ov->parent.rank),
                          (unsigned long)ov->parent.rank);
                (void)zmq_disconnect (ov->parent.zsock, ov->parent.uri);
                ov->parent.offline = true;
                rpc_track_purge (ov->parent.tracker, fail_parent_rpc, ov);
                overlay_monitor_notify (ov, FLUX_NODEID_ANY);
            }
            else
                logdrop (ov, OVERLAY_UPSTREAM, msg, "unknown control type");
            goto done;
        }
        default:
            break;
    }
    if (ov->recv_cb (&msg, OVERLAY_UPSTREAM, ov->recv_arg) < 0)
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
    int hello_log_level = LOG_DEBUG;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:i s:s s:i}",
                             "rank", &rank,
                             "version", &version,
                             "uuid", &uuid,
                             "status", &status) < 0)
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
        overlay_child_status_update (ov, child, SUBTREE_STATUS_LOST);
        hello_log_level = LOG_ERR; // want hello log to stand out in this case
    }

    if (!version_check (version, ov->version, &error)) {
        flux_log (ov->h, LOG_ERR,
                  "rejecting connection from %s (rank %lu): %s",
                  flux_get_hostbyrank (ov->h, rank),
                  (unsigned long)rank,
                  error.text);
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }

    snprintf (child->uuid, sizeof (child->uuid), "%s", uuid);
    overlay_child_status_update (ov, child, status);

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
 * printing the error message to stderr and notifying the state machine
 * that it should check overlay_parent_error() / overlay_parent_success().
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
    snprintf (ov->parent.uuid, sizeof (ov->parent.uuid), "%s", uuid);
    ov->parent.hello_responded = true;
    ov->parent.hello_error = false;
    overlay_monitor_notify (ov, FLUX_NODEID_ANY);
    return;
error:
    log_msg ("overlay.hello: %s", errstr ? errstr : flux_strerror (errno));
    ov->parent.hello_responded = true;
    ov->parent.hello_error = true;
    overlay_monitor_notify (ov, FLUX_NODEID_ANY);
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
                          "{s:I s:i s:s s:i}",
                          "rank", rank,
                          "version", ov->version,
                          "uuid", ov->uuid,
                          "status", ov->status) < 0
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

int overlay_bind (struct overlay *ov, const char *uri)
{
    if (!ov->h || ov->rank == FLUX_NODEID_ANY || ov->bind_zsock) {
        errno = EINVAL;
        log_err ("overlay_bind: invalid arguments");
        return -1;
    }
    if (overlay_zmq_init (ov) < 0) {
        log_err ("error creating zeromq context");
        return -1;
    }
    assert (ov->zap == NULL);
    if (!(ov->zap = zmqutil_zap_create (ov->zctx, ov->reactor))) {
        log_err ("error creating ZAP server");
        return -1;
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
        log_err ("error creating zmq ROUTER socket");
        return -1;
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
            log_err ("error setting TCP_MAXRT option on bind socket");
            return -1;
        }
    }
#endif
    if (cert_apply (ov->cert, ov->bind_zsock) < 0) {
        log_err ("error setting curve socket options");
        return -1;
    }
    if (zmq_bind (ov->bind_zsock, uri) < 0) {
        log_err ("error binding to %s", uri);
        return -1;
    }
    /* Capture URI after zmq_bind() processing, so it reflects expanded
     * wildcards and normalized addresses.
     */
    if (zgetsockopt_str (ov->bind_zsock,
                         ZMQ_LAST_ENDPOINT,
                         &ov->bind_uri) < 0) {
        log_err ("error obtaining concretized bind URI");
        return -1;
    }
    if (!(ov->bind_w = zmqutil_watcher_create (ov->reactor,
                                               ov->bind_zsock,
                                               FLUX_POLLIN,
                                               child_cb,
                                               ov))) {
        log_err ("error creating watcher for bind socket");
        return -1;
    }
    flux_watcher_start (ov->bind_w);
    /* Ensure that ipc files are removed when the broker exits.
     */
    char *ipc_path = strstr (ov->bind_uri, "ipc://");
    if (ipc_path)
        cleanup_push_string (cleanup_file, ipc_path + 6);
    return 0;
}

/* Don't allow downstream peers to reconnect while we are shutting down.
 */
void overlay_shutdown (struct overlay *overlay, bool unbind)
{
    overlay->shutdown_in_progress = true;
    if (unbind) {
        if (overlay->bind_zsock && overlay->bind_uri)
            if (zmq_unbind (overlay->bind_zsock, overlay->bind_uri) < 0)
                flux_log (overlay->h, LOG_ERR, "zmq_unbind failed");
    }
}

/* Call after overlay bootstrap (bind/connect),
 * to get concretized 0MQ endpoints.
 */
int overlay_register_attrs (struct overlay *overlay)
{
    if (attr_add (overlay->attrs,
                  "tbon.parent-endpoint",
                  overlay->parent.uri,
                  ATTR_IMMUTABLE) < 0)
        return -1;
    if (attr_add_uint32 (overlay->attrs,
                         "rank",
                         overlay->rank,
                         ATTR_IMMUTABLE) < 0)
        return -1;
    if (attr_add_uint32 (overlay->attrs,
                         "size", overlay->size,
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

int overlay_set_monitor_cb (struct overlay *ov,
                            overlay_monitor_f cb,
                            void *arg)
{
    struct overlay_monitor *mon;

    if (!(mon = calloc (1, sizeof (*mon))))
        return -1;
    mon->cb = cb;
    mon->arg = arg;
    if (zlist_append (ov->monitor_callbacks, mon) < 0) {
        free (mon);
        errno = ENOMEM;
        return -1;
    }
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
    overlay_child_status_update (ov, child, SUBTREE_STATUS_OFFLINE);
    flux_msg_decref (response);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to overlay.goodbye");
}

/* The parent has responded to overlay.goodbye.  Fulfill the future
 * returned by overlay_goodbye_parent() so the state machine can
 * make progress.
 */
static void overlay_goodbye_response_cb (flux_t *h,
                                         flux_msg_handler_t *mh,
                                         const flux_msg_t *msg,
                                         void *arg)
{
    struct overlay *ov = arg;
    flux_future_fulfill (ov->parent.f_goodbye, NULL, NULL);
}

/* This allows the state machine to delay overlay_destroy() and its
 * disconnection from the parent until the parent has marked this peer
 * offline and sent an acknowledgement.  If we simply send a control status
 * offline message and disconnect, the parent may log errors if it sends any
 * messages to this peer (such as broadcasts) before the offline message is
 * processed and gets an EHOSTUNREACH for an online peer.
 * See flux-framework/flux-core#5881.
 */
flux_future_t *overlay_goodbye_parent (struct overlay *ov)
{
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("overlay.goodbye", NULL))
        || flux_msg_set_rolemask (msg, FLUX_ROLE_OWNER) < 0
        || overlay_sendmsg_parent (ov, msg) < 0) {
        flux_msg_decref (msg);
        return NULL;
    }
    ov->parent.goodbye_sent = true; // suppress further sends to parent
    flux_msg_decref (msg);
    flux_future_incref (ov->parent.f_goodbye);
    return ov->parent.f_goodbye;
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

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i s:i s:i s:i}",
                           "child-count", ov->child_count,
                           "child-connected", overlay_get_child_peer_count (ov),
                           "parent-count", ov->rank > 0 ? 1 : 0,
                           "parent-rpc", rpc_track_count (ov->parent.tracker),
                           "child-rpc", child_rpc_track_count (ov)) < 0)
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

/* If a disconnect is received for waiting health request,
 * drop the request.
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
}

const char *overlay_get_subtree_status (struct overlay *ov, int rank)
{
    const char *result = "unknown";

    if (rank == ov->rank)
        result = subtree_status_str (ov->status);
    else {
        struct child *child;
        if ((child = child_lookup_byrank (ov, rank)))
            result = subtree_status_str (child->status);
    }
    return result;
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

/* Recursive function to build subtree topology object.
 * Right now the tree is regular.  In the future support the configuration
 * of irregular tree topologies.
 */
json_t *overlay_get_subtree_topo (struct overlay *ov, int rank)
{
    if (!ov) {
        errno = EINVAL;
        return NULL;
    }
    return topology_get_json_subtree_at (ov->topo, rank);
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
    if (!(topo = overlay_get_subtree_topo (ov, rank)))
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
    flux_log (ov->h,
              LOG_ERR,
              "%s (rank %d) transitioning to LOST due to %s",
              flux_get_hostbyrank (ov->h, child->rank),
              (int)child->rank,
              "administrative action");
    overlay_child_status_update (ov, child, SUBTREE_STATUS_LOST);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to overlay.disconnect-subtree");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to overlay.disconnect-subtree");
}

int overlay_cert_load (struct overlay *ov, const char *path)
{
    struct stat sb;
    int fd;
    FILE *f = NULL;
    struct cert *cert;

    if ((fd = open (path, O_RDONLY)) < 0
        || fstat (fd, &sb) < 0) {
        goto error;
    }
    if ((sb.st_mode & S_IROTH) | (sb.st_mode & S_IRGRP)) {
        log_msg ("%s: readable by group/other", path);
        errno = EPERM;
        goto error_quiet;
    }
    if (!(f = fdopen (fd, "r")))
        goto error;
    fd = -1; // now owned by 'f'
    if (!(cert = cert_read (f)))
        goto error;
    cert_destroy (ov->cert); // replace ov->cert (if any) with this
    ov->cert = cert;
    (void)fclose (f);
    return 0;
error:
    log_err ("%s", path);
error_quiet:
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
                                       int *valuep)
{
    int value = default_value;
    const char *val;
    char *endptr;

    if (attr_get (attrs, name, &val, NULL) == 0) {
        errno = 0;
        value = strtol (val, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            log_msg ("%s value must be an integer", name);
            errno = EINVAL;
            return -1;
        }
        if (attr_delete (attrs, name, true) < 0)
            return -1;
    }
    if (attr_add_int (attrs, name, value, ATTR_IMMUTABLE) < 0)
        return -1;
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

static int overlay_configure_torpid (struct overlay *ov)
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
            log_msg ("Config file error [tbon]: %s", error.text);
            return -1;
        }
        if (min_fsd) {
            if (fsd_parse_duration (min_fsd, &ov->torpid_min) < 0
                || ov->torpid_min == 0) {
                log_msg ("Config file error parsing tbon.torpid_min value");
                return -1;
            }
        }
        if (max_fsd) {
            if (fsd_parse_duration (max_fsd, &ov->torpid_max) < 0) {
                log_msg ("Config file error parsing tbon.torpid_max value");
                return -1;
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
        return -1;
    if (attr_add_active (ov->attrs,
                         "tbon.torpid_min",
                         0,
                         get_torpid,
                         set_torpid,
                         ov) < 0)
        return -1;

    return 0;
}

static int overlay_configure_timeout (struct overlay *ov,
                                      const char *table,
                                      const char *name,
                                      bool enabled,
                                      double default_value,
                                      double *valuep)
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
            log_msg ("Config file error [%s]: %s", table, error.text);
            return -1;
        }
        if (fsd) {
            if (fsd_parse_duration (fsd, &value) < 0) {
                log_msg ("Config file error parsing %s", long_name);
                return -1;
            }
            override = true;
        }
    }
    /* Override with broker attribute (command line only) settings, if any.
     */
    if (attr_get (ov->attrs, long_name, &fsd, NULL) == 0) {
        if (fsd_parse_duration (fsd, &value) < 0) {
            log_msg ("Error parsing %s attribute", long_name);
            return -1;
        }
        if (attr_delete (ov->attrs, long_name, true) < 0)
            return -1;
        override = true;
    }
    if (enabled) {
        char buf[64];
        if (fsd_format_duration (buf, sizeof (buf), value) < 0)
            return -1;
        if (attr_add (ov->attrs, long_name, buf, ATTR_IMMUTABLE) < 0)
            return -1;
    }
    else {
        if (override) {
            log_msg ("%s unsupported by this zeromq version", long_name);
            return -1;
        }
    }
    *valuep = value;
    return 0;
}

static int overlay_configure_tbon_int (struct overlay *ov,
                                       const char *name,
                                       int *value,
                                       int default_value)
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
            log_msg ("Config file error [tbon]: %s", error.text);
            return -1;
        }
    }
    (void)snprintf (attrname, sizeof (attrname), "tbon.%s", name);
    if (overlay_configure_attr_int (ov->attrs, attrname, *value, value) < 0)
        return -1;
    return 0;
}

/* Configure tbon.topo attribute.
 * Ascending precedence: compiled-in default, TOML config, command line.
 * Topology creation is deferred to bootstrap, when we know the instance size.
 */
static int overlay_configure_topo (struct overlay *ov)
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
            log_msg ("Config file error [tbon]: %s", error.text);
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
        log_err ("Error manipulating tbon.topo attribute");
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
        flux_msg_handler_delvec (ov->handlers);
        ov->status = SUBTREE_STATUS_OFFLINE;
        overlay_control_parent (ov, CONTROL_STATUS, ov->status);
        flux_future_destroy (ov->parent.f_goodbye);

        zmq_close (ov->parent.zsock);
        free (ov->parent.uri);
        flux_watcher_destroy (ov->parent.w);
        free (ov->parent.pubkey);
        zmqutil_monitor_destroy (ov->parent.monitor);

        free (ov->bind_uri);
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
        if (ov->monitor_callbacks) {
            struct montior *mon;

            while ((mon = zlist_pop (ov->monitor_callbacks)))
                free (mon);
            zlist_destroy (&ov->monitor_callbacks);
        }
        topology_decref (ov->topo);
        if (!ov->zctx_external)
            zmq_ctx_term (ov->zctx);
        free (ov);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
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
                                attr_t *attrs,
                                void *zctx,
                                overlay_recv_f cb,
                                void *arg)
{
    struct overlay *ov;
    uuid_t uuid;

    if (!(ov = calloc (1, sizeof (*ov))))
        return NULL;
    ov->attrs = attrs;
    ov->rank = FLUX_NODEID_ANY;
    ov->parent.lastsent = -1;
    ov->h = h;
    ov->reactor = flux_get_reactor (h);
    ov->recv_cb = cb;
    ov->recv_arg = arg;
    ov->version = FLUX_CORE_VERSION_HEX;
    uuid_generate (uuid);
    uuid_unparse (uuid, ov->uuid);
    if (zctx) {
        ov->zctx = zctx;
        ov->zctx_external = true;
    }
    if (!(ov->monitor_callbacks = zlist_new ()))
        goto nomem;
    if (overlay_configure_attr_int (ov->attrs, "tbon.prefertcp", 0, NULL) < 0)
        goto error;
    if (overlay_configure_torpid (ov) < 0)
        goto error;
    if (overlay_configure_timeout (ov,
                                   "tbon",
                                   "tcp_user_timeout",
                                   have_tcp_maxrt,
                                   default_tcp_user_timeout,
                                   &ov->tcp_user_timeout) < 0)
        goto error;
    if (overlay_configure_timeout (ov,
                                   "tbon",
                                   "connect_timeout",
                                   have_connect_timeout,
                                   default_connect_timeout,
                                   &ov->connect_timeout) < 0)
        goto error;
    if (overlay_configure_tbon_int (ov, "zmqdebug", &ov->zmqdebug, 0) < 0)
        goto error;
    if (overlay_configure_tbon_int (ov,
                                    "child_rcvhwm",
                                    &ov->child_rcvhwm,
                                    0) < 0)
        goto error;
    if (ov->child_rcvhwm < 0 || ov->child_rcvhwm == 1) {
        log_msg ("tbon.child_rcvhwm must be 0 (unlimited) or >= 2");
        errno = EINVAL;
        goto error;
    }
    if (overlay_configure_tbon_int (ov,
                                    "zmq_io_threads",
                                    &ov->zmq_io_threads,
                                    1) < 0)
        goto error;
    if (ov->zmq_io_threads < 1) {
        log_msg ("tbon.zmq_io_threads must be >= 1");
        errno = EINVAL;
        goto error;
    }
    if (overlay_configure_topo (ov) < 0)
        goto error;
    if (flux_msg_handler_addvec (h, htab, ov, &ov->handlers) < 0)
        goto error;
    if (!(ov->cert = cert_create ()))
        goto nomem;
    if (!(ov->health_requests = flux_msglist_create ()))
        goto error;
    if (!(ov->parent.f_goodbye = flux_future_create (NULL, NULL)))
        goto error;
    flux_future_set_flux (ov->parent.f_goodbye, h);
    return ov;
nomem:
    errno = ENOMEM;
error:
    overlay_destroy (ov);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
