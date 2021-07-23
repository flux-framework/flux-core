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
#include <czmq.h>
#include <zmq.h>
#include <flux/core.h>
#include <inttypes.h>
#include <jansson.h>
#include <uuid.h>

#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libzmqutil/reactor.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/kary.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errno_safe.h"

#include "overlay.h"
#include "attr.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif

#define FLUX_ZAP_DOMAIN "flux"
#define ZAP_ENDPOINT "inproc://zeromq.zap.01"

#define DEFAULT_FANOUT 2

enum {
    KEEPALIVE_STATUS_NORMAL = 0,
    KEEPALIVE_STATUS_DISCONNECT = 1,
};

struct child {
    int lastseen;
    uint32_t rank;
    char uuid[UUID_STR_LEN];
    bool connected;
    bool idle;
};

struct parent {
    zsock_t *zsock;         // NULL on rank 0
    char *uri;
    flux_watcher_t *w;
    int lastsent;
    char *pubkey;
    uint32_t rank;
    char uuid[UUID_STR_LEN];
    bool hello_error;
    bool hello_responded;
};

/* Wake up periodically (between 'sync_min' and 'sync_max' seconds) and:
 * 1) send keepalive to parent if nothing was sent in 'idle_min' seconds
 * 2) find children that have not been heard from in 'idle_max' seconds
 */
static const double sync_min = 1.0;
static const double sync_max = 5.0;

static const double idle_min = 5.0;
static const double idle_max = 30.0;

struct overlay {
    zcert_t *cert;
    zcertstore_t *certstore;
    zsock_t *zap;
    flux_watcher_t *zap_w;

    flux_t *h;
    attr_t *attrs;
    flux_reactor_t *reactor;
    flux_msg_handler_t **handlers;
    flux_future_t *f_sync;

    uint32_t size;
    uint32_t rank;
    int fanout;
    char uuid[UUID_STR_LEN];
    int version;

    struct parent parent;

    zsock_t *bind_zsock;        // NULL if no downstream peers
    char *bind_uri;
    flux_watcher_t *bind_w;
    struct child *children;
    int child_count;
    zhashx_t *child_hash;

    overlay_monitor_f child_monitor_cb;
    void *child_monitor_arg;

    overlay_recv_f recv_cb;
    void *recv_arg;
};

static void overlay_mcast_child (struct overlay *ov, const flux_msg_t *msg);
static int overlay_sendmsg_child (struct overlay *ov, const flux_msg_t *msg);
static int overlay_sendmsg_parent (struct overlay *ov, const flux_msg_t *msg);
static void hello_response_handler (struct overlay *ov, const flux_msg_t *msg);
static void hello_request_handler (struct overlay *ov, const flux_msg_t *msg);

/* Convenience iterator for ov->children
 */
#define foreach_overlay_child(ov, child) \
    for ((child) = &(ov)->children[0]; \
            (child) - &(ov)->children[0] < (ov)->child_count; \
            (child)++)


static __attribute__ ((format (printf, 3, 4)))
void overlay_child_log (struct overlay *ov,
                        struct child *child,
                        const char *fmt, ...)
{
    va_list ap;
    char buf[256];

    va_start (ap, fmt);
    snprintf (buf, sizeof(buf), fmt, ap);
    va_end (ap);

    flux_log (ov->h,
              LOG_DEBUG,
              "overlay child %lu %s connnected=%s %s",
              (unsigned long)child->rank,
              child->uuid,
              child->connected ? "true" : "false",
              buf);
}

static void overlay_monitor_notify (struct overlay *ov)
{
    if (ov->child_monitor_cb)
        ov->child_monitor_cb (ov, ov->child_monitor_arg);
}

static int child_count (uint32_t rank, uint32_t size, int k)
{
    int count;
    for (count = 0; kary_childof (k, size, rank, count) != KARY_NONE; count++)
        ;
    return count;
}

int overlay_set_geometry (struct overlay *ov, uint32_t size, uint32_t rank)
{
    ov->size = size;
    ov->rank = rank;
    ov->child_count = child_count (rank, size, ov->fanout);
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
            child->rank = kary_childof (ov->fanout, size, rank, i);
        }
    }
    if (rank > 0) {
        ov->parent.rank = kary_parentof (ov->fanout, rank);
    }

    return 0;
}

int overlay_get_fanout (struct overlay *ov)
{
    return ov->fanout;
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
    return (ov->parent.hello_responded
            && ov->parent.hello_error);
}

bool overlay_parent_success (struct overlay *ov)
{
    return (ov->parent.hello_responded
            && !ov->parent.hello_error);
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
        if (child->connected)
            count++;
    }
    return count;
}

void overlay_log_idle_children (struct overlay *ov)
{
    struct child *child;
    double now = flux_reactor_now (ov->reactor);
    char fsd[64];
    int idle;

    if (idle_max > 0) {
        foreach_overlay_child (ov, child) {
            if (child->connected) {
                idle = now - child->lastseen;

                if (idle >= idle_max) {
                    (void)fsd_format_duration (fsd, sizeof (fsd), idle);
                    if (!child->idle) {
                        flux_log (ov->h,
                                  LOG_ERR,
                                  "child %lu idle for %s",
                                  (unsigned long)child->rank,
                                  fsd);
                        child->idle = true;
                    }
                }
                else {
                    if (child->idle) {
                        flux_log (ov->h,
                                  LOG_ERR,
                                  "child %lu no longer idle",
                                  (unsigned long)child->rank);
                        child->idle = false;
                    }
                }
            }
        }
    }
}

static struct child *child_lookup (struct overlay *ov, const char *id)
{
    return ov->child_hash ?  zhashx_lookup (ov->child_hash, id) : NULL;
}

/* Given a rank, find a (direct) child peer.
 * Since child ranks are numerically contiguous, perform a range check
 * and index into the child array directly.
 * Returns NULL on lookup failure.
 */
static struct child *child_lookup_byrank (struct overlay *ov, uint32_t rank)
{
    uint32_t first;
    int i;

    if ((first = kary_childof (ov->fanout, ov->size, ov->rank, 0)) == KARY_NONE
        || (i = rank - first) < 0
        || i >= ov->child_count)
        return NULL;
    return &ov->children[i];
}

/* Look up child that provides route to 'rank' (NULL if none).
 */
static struct child *child_lookup_route (struct overlay *ov, uint32_t rank)
{
    uint32_t child_rank;

    child_rank = kary_child_route (ov->fanout, ov->size, ov->rank, rank);
    if (child_rank == KARY_NONE)
        return NULL;
    return child_lookup_byrank (ov, child_rank);
}

bool overlay_uuid_is_child (struct overlay *ov, const char *uuid)
{
    if (child_lookup (ov, uuid) != NULL)
        return true;
    return false;
}

bool overlay_uuid_is_parent (struct overlay *ov, const char *uuid)
{
    if (ov->rank > 0 && !strcmp (uuid, ov->parent.uuid))
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

    if (!ov->parent.zsock) {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = zmqutil_msg_send (ov->parent.zsock, msg);
    if (rc == 0)
        ov->parent.lastsent = flux_reactor_now (ov->reactor);
done:
    return rc;
}

static int overlay_keepalive_parent (struct overlay *ov, int status)
{
    flux_msg_t *msg = NULL;

    if (ov->parent.zsock) {
        if (!(msg = flux_keepalive_encode (0, status)))
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

int overlay_sendmsg (struct overlay *ov,
                     const flux_msg_t *msg,
                     overlay_where_t where)
{
    int type;
    uint8_t flags;
    flux_msg_t *cpy = NULL;
    const char *uuid;
    uint32_t nodeid;
    struct child *child;
    int rc;

    if (flux_msg_get_type (msg, &type) < 0
        || flux_msg_get_flags (msg, &flags) < 0)
        return -1;
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            /* If message is being routed downstream to reach 'nodeid',
             * push the local uuid, then the next hop onto the messages's
             * route stack so that the ROUTER socket can pop off next hop to
             * select the peer, and our uuid remains as part of the source addr.
             */
            if (where == OVERLAY_ANY) {
                if (flux_msg_get_nodeid (msg, &nodeid) < 0)
                    goto error;
                if ((flags & FLUX_MSGFLAG_UPSTREAM) && nodeid == ov->rank)
                    where = OVERLAY_UPSTREAM;
                else {
                    if ((child = child_lookup_route (ov, nodeid))) {
                        if (!child->connected) {
                            errno = EHOSTUNREACH;
                            goto error;
                        }
                        if (!(cpy = flux_msg_copy (msg, true)))
                            goto error;
                        if (flux_msg_route_push (cpy, ov->uuid) < 0)
                            goto error;
                        if (flux_msg_route_push (cpy, child->uuid) < 0)
                            goto error;
                        msg = cpy;
                        where = OVERLAY_DOWNSTREAM;
                    }
                    else
                        where = OVERLAY_UPSTREAM;
                }
            }
            if (where == OVERLAY_UPSTREAM)
                rc = overlay_sendmsg_parent (ov, msg);
            else
                rc = overlay_sendmsg_child (ov, msg);
            if (rc < 0)
                goto error;
            break;
        case FLUX_MSGTYPE_RESPONSE:
            /* Assume if next route matches parent, the message goes upstream;
             * otherwise downstream.  The send downstream will fail with
             * EHOSTUNREACH if uuid doesn't match an immediate peer.
             */
            if (where == OVERLAY_ANY) {
                if (ov->rank > 0
                    && (uuid = flux_msg_route_last (msg)) != NULL
                    && !strcmp (uuid, ov->parent.uuid))
                    where = OVERLAY_UPSTREAM;
                else
                    where = OVERLAY_DOWNSTREAM;
            }
            if (where == OVERLAY_UPSTREAM)
                rc = overlay_sendmsg_parent (ov, msg);
            else
                rc = overlay_sendmsg_child (ov, msg);
            if (rc < 0)
                goto error;
            break;
        case FLUX_MSGTYPE_EVENT:
            if (where == OVERLAY_DOWNSTREAM || where == OVERLAY_ANY)
                overlay_mcast_child (ov, msg);
            else {
                /* N.B. add route delimiter if needed to pass unpublished
                 * event message upstream through router socket.
                 */
                if (!(flags & FLUX_MSGFLAG_ROUTE)) {
                    if (!(cpy = flux_msg_copy (msg, true)))
                        goto error;
                    flux_msg_route_enable (cpy);
                    msg = cpy;
                }
                if (overlay_sendmsg_parent (ov, msg) < 0)
                    goto error;
            }
            break;
        default:
            goto inval;
    }
    flux_msg_decref (cpy);
    return 0;
inval:
    errno = EINVAL;
error:
    flux_msg_decref (cpy);
    return -1;
}

static void sync_cb (flux_future_t *f, void *arg)
{
    struct overlay *ov = arg;
    double now = flux_reactor_now (ov->reactor);

    if (now - ov->parent.lastsent > idle_min)
        overlay_keepalive_parent (ov, KEEPALIVE_STATUS_NORMAL);
    overlay_log_idle_children (ov);

    flux_future_reset (f);
}

const char *overlay_get_bind_uri (struct overlay *ov)
{
    return ov->bind_uri;
}

static int overlay_sendmsg_child (struct overlay *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->bind_zsock) {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = zmqutil_msg_send_ex (ov->bind_zsock, msg, true);
done:
    return rc;
}

static int overlay_mcast_child_one (struct overlay *ov,
                                    const flux_msg_t *msg,
                                    struct child *child)
{
    flux_msg_t *cpy;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    flux_msg_route_enable (cpy);
    if (flux_msg_route_push (cpy, child->uuid) < 0)
        goto done;
    if (overlay_sendmsg_child (ov, cpy) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (cpy);
    return rc;
}

static void overlay_mcast_child (struct overlay *ov, const flux_msg_t *msg)
{
    struct child *child;
    int disconnects = 0;

    foreach_overlay_child (ov, child) {
        if (child->connected) {
            if (overlay_mcast_child_one (ov, msg, child) < 0) {
                /* N.B. ROUTER socket has ZMQ_ROUTER_MANDATORY set.
                 * If peer has disconnected (id no longer valid), zmq_sendmsg()
                 * should fail with EHOSTUNREACH per zmq_setsockopt(3).
                 */
                if (errno == EHOSTUNREACH) {
                    child->connected = false;
                    overlay_child_log (ov, child,
                                       "Mcast failed, child has disconnected");
                    zhashx_delete (ov->child_hash, child->uuid);
                    disconnects++;
                }
                else
                    flux_log_error (ov->h,
                                    "mcast error to child rank %lu",
                                    (unsigned long)child->rank);
            }
        }
    }
    if (disconnects)
        overlay_monitor_notify (ov);
}

/* Handle a message received from TBON child (downstream).
 */
static void child_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    struct overlay *ov = arg;
    flux_msg_t *msg;
    int type = -1;
    const char *topic = NULL;
    const char *sender = NULL;
    struct child *child;
    int status;

    if (!(msg = zmqutil_msg_recv (ov->bind_zsock)))
        return;
    if (flux_msg_get_type (msg, &type) < 0
        || !(sender = flux_msg_route_last (msg)))
        goto drop;
    if (!(child = child_lookup (ov, sender)) || !child->connected) {
        if (type == FLUX_MSGTYPE_REQUEST
            && flux_msg_get_topic (msg, &topic) == 0
            && !strcmp (topic, "overlay.hello"))
            hello_request_handler (ov, msg);
        goto handled; // don't log drops until hello completes successfully
    }
    child->lastseen = flux_reactor_now (ov->reactor);
    switch (type) {
        case FLUX_MSGTYPE_KEEPALIVE:
            if (flux_keepalive_decode (msg, NULL, &status) == 0
                && status == KEEPALIVE_STATUS_DISCONNECT
                && child->connected == true) {
                child->connected = false;
                overlay_child_log (ov, child, "Sent DISCONNECT");
                zhashx_delete (ov->child_hash, child->uuid);
                overlay_monitor_notify (ov);
            }
            goto handled;
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
            break;
        case FLUX_MSGTYPE_EVENT:
            break;
    }
    ov->recv_cb (msg, OVERLAY_DOWNSTREAM, ov->recv_arg);
handled:
    flux_msg_decref (msg);
    return;
drop:
    if (!topic && type != FLUX_MSGTYPE_KEEPALIVE)
        (void)flux_msg_get_topic (msg, &topic);
    flux_log (ov->h,
              LOG_ERR, "DROP downstream %s topic %s from %s",
              type != -1 ? flux_msg_typestr (type) : "message",
              topic ? topic : "-",
              sender != NULL ? sender : "unknown");
    flux_msg_decref (msg);
}

static void parent_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    struct overlay *ov = arg;
    flux_msg_t *msg;
    int type;
    const char *topic = NULL;

    if (!(msg = zmqutil_msg_recv (ov->parent.zsock)))
        return;
    if (flux_msg_get_type (msg, &type) < 0) {
        goto drop;
    }
    if (!ov->parent.hello_responded) {
        if (type != FLUX_MSGTYPE_RESPONSE
            || flux_msg_get_topic (msg, &topic) < 0
            || strcmp (topic, "overlay.hello") != 0)
            goto drop;
        hello_response_handler (ov, msg);
        goto handled;
    }
    if (type == FLUX_MSGTYPE_EVENT)
        flux_msg_route_disable (msg);
    ov->recv_cb (msg, OVERLAY_UPSTREAM, ov->recv_arg);
handled:
    flux_msg_destroy (msg);
    return;
drop:
    if (!topic && type != FLUX_MSGTYPE_KEEPALIVE)
        (void)flux_msg_get_topic (msg, &topic);
    flux_log (ov->h,
              LOG_ERR, "DROP upstream %s topic %s",
              type != -1 ? flux_msg_typestr (type) : "message",
              topic ? topic : "-");
    flux_msg_destroy (msg);
}

static zframe_t *get_zmsg_nth (zmsg_t *msg, int n)
{
    zframe_t *zf;
    int count = 0;

    zf = zmsg_first (msg);
    while (zf) {
        if (count++ == n)
            return zf;
        zf = zmsg_next (msg);
    }
    return NULL;
}

static bool streq_zmsg_nth (zmsg_t *msg, int n, const char *s)
{
    zframe_t *zf = get_zmsg_nth (msg, n);
    if (zf && zframe_streq (zf, s))
        return true;
    return false;
}

static bool pubkey_zmsg_nth (zmsg_t *msg, int n, char *pubkey_txt)
{
    zframe_t *zf = get_zmsg_nth (msg, n);
    if (!zf || zframe_size (zf) != 32)
        return false;
    zmq_z85_encode (pubkey_txt, zframe_data (zf), 32);
    return true;
}

static bool add_zmsg_nth (zmsg_t *dst, zmsg_t *src, int n)
{
    zframe_t *zf = get_zmsg_nth (src, n);
    if (!zf || zmsg_addmem (dst, zframe_data (zf), zframe_size (zf)) < 0)
        return false;
    return true;
}

/* ZAP 1.0 messages have the following parts
 * REQUEST                              RESPONSE
 *   0: version                           0: version
 *   1: sequence                          1: sequence
 *   2: domain                            2: status_code
 *   3: address                           3: status_text
 *   4: identity                          4: user_id
 *   5: mechanism                         5: metadata
 *   6: client_key
 */
static void overlay_zap_cb (flux_reactor_t *r,
                            flux_watcher_t *w,
                            int revents,
                            void *arg)
{
    struct overlay *ov = arg;
    zmsg_t *req = NULL;
    zmsg_t *rep = NULL;
    char pubkey[41];
    const char *status_code = "400";
    const char *status_text = "No access";
    const char *user_id = "";
    zcert_t *cert;
    const char *name = NULL;
    int log_level = LOG_ERR;

    if ((req = zmsg_recv (ov->zap))) {
        if (!streq_zmsg_nth (req, 0, "1.0")
                || !streq_zmsg_nth (req, 5, "CURVE")
                || !pubkey_zmsg_nth (req, 6, pubkey)) {
            log_err ("ZAP request decode error");
            goto done;
        }
        if ((cert = zcertstore_lookup (ov->certstore, pubkey)) != NULL) {
            status_code = "200";
            status_text = "OK";
            user_id = pubkey;
            name = zcert_meta (cert, "name");
            log_level = LOG_INFO;
        }
        if (!name)
            name = "unknown";
        flux_log (ov->h, log_level, "overlay auth %s %s", name, status_text);

        if (!(rep = zmsg_new ()))
            goto done;
        if (!add_zmsg_nth (rep, req, 0)
                || !add_zmsg_nth (rep, req, 1)
                || zmsg_addstr (rep, status_code) < 0
                || zmsg_addstr (rep, status_text) < 0
                || zmsg_addstr (rep, user_id) < 0
                || zmsg_addmem (rep, NULL, 0) < 0) {
            log_err ("ZAP response encode error");
            goto done;
        }
        if (zmsg_send (&rep, ov->zap) < 0)
            log_err ("ZAP send error");
    }
done:
    zmsg_destroy (&req);
    zmsg_destroy (&rep);
}

static int overlay_zap_init (struct overlay *ov)
{
    if (!(ov->zap = zsock_new (ZMQ_REP)))
        return -1;
    if (zsock_bind (ov->zap, ZAP_ENDPOINT) < 0) {
        errno = EINVAL;
        log_err ("could not bind to %s", ZAP_ENDPOINT);
        return -1;
    }
    if (!(ov->zap_w = zmqutil_watcher_create (ov->reactor,
                                              ov->zap,
                                              FLUX_POLLIN,
                                              overlay_zap_cb,
                                              ov)))
        return -1;
    flux_watcher_start (ov->zap_w);
    return 0;
}

/* Check child flux-core version 'v1' against this broker's version 'v2'.
 * For now we require an exact match of (major,minor,patch) and
 * ignore any commit id appended to the version string.
 * Return 0 on error, or -1 on failure with message for child in 'errubuf'.
 */
static bool version_check (int v1, int v2, char *errbuf, int errbufsz)
{
    if (v1 != v2) {
        snprintf (errbuf, errbufsz,
                  "flux-core v%u.%u.%u mismatched with parent v%u.%u.%u",
                  (v1 >> 16) & 0xff,
                  (v1 >> 8) & 0xff,
                  v1 & 0xff,
                  (v2 >> 16) & 0xff,
                  (v2 >> 8) & 0xff,
                  v2 & 0xff);
        return false;
    }
    return true;
}

/* Handle overlay.hello request from downstream (child) TBON peer.
 * The peer may be rejected here if it is improperly configured.
 * If successful the peer is marked 'connected' and the state machine is
 * notified.
 *
 * N.B. use overlay sockets directly to handle this message instead of higher
 * level API to allow child->connected to gate the flow of messages from a
 * peer, and to avoid complicating the standalone overlay unit test,
 */
static void hello_request_handler (struct overlay *ov, const flux_msg_t *msg)
{
    struct child *child;
    json_int_t rank;
    int version;
    const char *errmsg = NULL;
    char errbuf[128];
    flux_msg_t *response;
    const char *uuid;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:i s:s}",
                             "rank", &rank,
                             "version", &version,
                             "uuid", &uuid) < 0
        || flux_msg_authorize (msg, FLUX_USERID_UNKNOWN) < 0)
        goto error; // EPROTO or EPERM (unlikely)

    if (!(child = child_lookup_byrank (ov, rank))) {
        snprintf (errbuf, sizeof (errbuf),
                  "rank %lu is not a peer of parent %lu: mismatched config?",
                  (unsigned long)rank,
                  (unsigned long)ov->parent.rank);
        errmsg = errbuf;
        errno = EINVAL;
        goto error_log;
    }
    if (!version_check (version, ov->version, errbuf, sizeof (errbuf))) {
        errmsg = errbuf;
        errno = EINVAL;
        goto error_log;
    }
    if (child->connected) { // crash
        overlay_child_log (ov, child, "About to replace with fresh hello");
        zhashx_delete (ov->child_hash, child->uuid);
    }

    snprintf (child->uuid, sizeof (child->uuid), "%s", uuid);
    zhashx_insert (ov->child_hash, child->uuid, child);
    child->connected = true;
    overlay_monitor_notify (ov);

    overlay_child_log (ov, child, "Sent hello");

    if (!(response = flux_response_derive (msg, 0))
        || flux_msg_pack (response, "{s:s}", "uuid", ov->uuid) < 0
        || overlay_sendmsg_child (ov, response) < 0)
        flux_log_error (ov->h, "error responding to overlay.hello request");
    flux_msg_destroy (response);
    return;
error_log:
    flux_log (ov->h, LOG_ERR, "overlay hello child %lu %s rejected",
              (unsigned long)rank,
              uuid);
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
 * N.B. see note in hello_request_handler() on direct use of overlay sockets.
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
    flux_log (ov->h, LOG_DEBUG, "hello parent %lu %s",
              (unsigned long)ov->parent.rank, uuid);
    snprintf (ov->parent.uuid, sizeof (ov->parent.uuid), "%s", uuid);
    ov->parent.hello_responded = true;
    ov->parent.hello_error = false;
    overlay_monitor_notify (ov);
    return;
error:
    log_msg ("overlay.hello: %s", errstr ? errstr : flux_strerror (errno));
    ov->parent.hello_responded = true;
    ov->parent.hello_error = true;
    overlay_monitor_notify (ov);
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
                          "{s:I s:i s:s}",
                          "rank", rank,
                          "version", ov->version,
                          "uuid", ov->uuid) < 0
        || flux_msg_set_rolemask (msg, FLUX_ROLE_OWNER) < 0
        || overlay_sendmsg_parent (ov, msg) < 0) {
        flux_msg_decref (msg);
        return -1;
    }
    flux_msg_decref (msg);
    return 0;
}

int overlay_connect (struct overlay *ov)
{
    if (ov->rank > 0) {
        if (!ov->h || ov->rank == FLUX_NODEID_ANY || !ov->parent.uri) {
            errno = EINVAL;
            return -1;
        }
        if (!(ov->parent.zsock = zsock_new_dealer (NULL)))
            goto nomem;
        zsock_set_zap_domain (ov->parent.zsock, FLUX_ZAP_DOMAIN);
        zcert_apply (ov->cert, ov->parent.zsock);
        zsock_set_curve_serverkey (ov->parent.zsock, ov->parent.pubkey);
        zsock_set_identity (ov->parent.zsock, ov->uuid);
        if (zsock_connect (ov->parent.zsock, "%s", ov->parent.uri) < 0)
            goto nomem;
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
nomem:
    errno = ENOMEM;
    return -1;
}

int overlay_bind (struct overlay *ov, const char *uri)
{
    if (!ov->h || ov->rank == FLUX_NODEID_ANY || ov->bind_zsock) {
        errno = EINVAL;
        return -1;
    }
    if (!ov->zap && overlay_zap_init (ov) < 0)
        return -1;
    if (!(ov->bind_zsock = zsock_new_router (NULL)))
        return -1;
    zsock_set_router_mandatory (ov->bind_zsock, 1);

    zsock_set_zap_domain (ov->bind_zsock, FLUX_ZAP_DOMAIN);
    zcert_apply (ov->cert, ov->bind_zsock);
    zsock_set_curve_server (ov->bind_zsock, 1);

    if (zsock_bind (ov->bind_zsock, "%s", uri) < 0)
        return -1;
    /* Capture URI after zsock_bind() processing, so it reflects expanded
     * wildcards and normalized addresses.
     */
    if (!(ov->bind_uri = zsock_last_endpoint (ov->bind_zsock)))
        return -1;
    if (!(ov->bind_w = zmqutil_watcher_create (ov->reactor,
                                               ov->bind_zsock,
                                               FLUX_POLLIN,
                                               child_cb,
                                               ov)))
        return -1;
    flux_watcher_start (ov->bind_w);
    /* Ensure that ipc files are removed when the broker exits.
     */
    char *ipc_path = strstr (ov->bind_uri, "ipc://");
    if (ipc_path)
        cleanup_push_string (cleanup_file, ipc_path + 6);
    return 0;
}

/* A callback of type attr_get_f to allow retrieving some information
 * from an struct overlay through attr_get().
 */
static int overlay_attr_get_cb (const char *name, const char **val, void *arg)
{
    struct overlay *overlay = arg;
    int rc = -1;

    if (!strcmp (name, "tbon.parent-endpoint"))
        *val = overlay_get_parent_uri (overlay);
    else {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int overlay_register_attrs (struct overlay *overlay)
{
    if (attr_add_active (overlay->attrs,
                         "tbon.parent-endpoint",
                         FLUX_ATTRFLAG_READONLY,
                         overlay_attr_get_cb,
                         NULL,
                         overlay) < 0)
        return -1;
    if (attr_add_uint32 (overlay->attrs,
                         "rank",
                         overlay->rank,
                         FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_uint32 (overlay->attrs,
                         "size", overlay->size,
                         FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (overlay->attrs,
                      "tbon.level",
                      kary_levelof (overlay->fanout, overlay->rank),
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (overlay->attrs,
                      "tbon.maxlevel",
                      kary_levelof (overlay->fanout, overlay->size - 1),
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (overlay->attrs,
                      "tbon.descendants",
                      kary_sum_descendants (overlay->fanout,
                                            overlay->size,
                                            overlay->rank),
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;

    return 0;
}

void overlay_set_monitor_cb (struct overlay *ov,
                             overlay_monitor_f cb,
                             void *arg)
{
    ov->child_monitor_cb = cb;
    ov->child_monitor_arg = arg;
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
                           "{s:i s:i s:i}",
                           "child-count", ov->child_count,
                           "child-connected", overlay_get_child_peer_count (ov),
                           "parent-count", ov->rank > 0 ? 1 : 0) < 0)
        flux_log_error (h, "error responding to overlay.stats.get");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to overlay.stats.get");
}

int overlay_cert_load (struct overlay *ov, const char *path)
{
    struct stat sb;
    zcert_t *cert;

    if (stat (path, &sb) < 0) {
        log_err ("%s", path);
        return -1;
    }
    if ((sb.st_mode & S_IROTH) | (sb.st_mode & S_IRGRP)) {
        log_msg ("%s: readable by group/other", path);
        errno = EPERM;
        return -1;
    }
    if (!(cert = zcert_load (path))) {
        log_msg ("%s: invalid CURVE certificate", path);
        errno = EINVAL;
        return -1;
    }
    zcert_destroy (&ov->cert);
    ov->cert = cert;
    return 0;
}

const char *overlay_cert_pubkey (struct overlay *ov)
{
    return zcert_public_txt (ov->cert);
}

const char *overlay_cert_name (struct overlay *ov)
{
    return zcert_meta (ov->cert, "name");
}

/* Create a zcert_t and add it to in-memory zcertstore_t.
 */
int overlay_authorize (struct overlay *ov, const char *name, const char *pubkey)
{
    uint8_t public_key[32];
    zcert_t *cert;

    if (strlen (pubkey) != 40 || !zmq_z85_decode (public_key, pubkey)) {
        errno = EINVAL;
        return -1;
    }
    if (!(cert = zcert_new_from (public_key, public_key))) {
        errno = ENOMEM;
        return -1;
    }
    zcert_set_meta (cert, "name", "%s", name);
    zcertstore_insert (ov->certstore, &cert); // takes ownership of cert
    return 0;
}

/* Set tbon.fanout attribute and return fanout value or -1 on error.
 * Allow the value to be set on the broker command line, but no changes
 * after this function is called.
 */
static int overlay_configure_fanout (attr_t *attrs)
{
    int fanout = DEFAULT_FANOUT;
    const char *val;
    char *endptr;

    if (attr_get (attrs, "tbon.fanout", &val, NULL) == 0) {
        errno = 0;
        fanout = strtol (val, &endptr, 10);
        if (errno != 0 || fanout <= 0 || *endptr != '\0') {
            log_msg ("tbon.fanout value must be a positive integer");
            errno = EINVAL;
            return -1;
        }
        if (attr_delete (attrs, "tbon.fanout", true) < 0)
            return -1;
    }
    if (attr_add_int (attrs,
                      "tbon.fanout",
                      fanout,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    return fanout;
}

void overlay_destroy (struct overlay *ov)
{
    if (ov) {
        int saved_errno = errno;

        zcert_destroy (&ov->cert);
        flux_watcher_destroy (ov->zap_w);
        if (ov->zap) {
            zsock_unbind (ov->zap, ZAP_ENDPOINT);
            zsock_destroy (&ov->zap);
        }
        zcertstore_destroy (&ov->certstore);

        flux_future_destroy (ov->f_sync);
        flux_msg_handler_delvec (ov->handlers);
        overlay_keepalive_parent (ov, KEEPALIVE_STATUS_DISCONNECT);

        zsock_destroy (&ov->parent.zsock);
        free (ov->parent.uri);
        flux_watcher_destroy (ov->parent.w);
        free (ov->parent.pubkey);

        zsock_destroy (&ov->bind_zsock);
        free (ov->bind_uri);
        flux_watcher_destroy (ov->bind_w);

        zhashx_destroy (&ov->child_hash);
        free (ov->children);
        free (ov);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "overlay.stats.get",
        overlay_stats_get_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct overlay *overlay_create (flux_t *h,
                                attr_t *attrs,
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
    if ((ov->fanout = overlay_configure_fanout (ov->attrs)) < 0)
        goto error;
    if (flux_msg_handler_addvec (h, htab, ov, &ov->handlers) < 0)
        goto error;
    if (!(ov->f_sync = flux_sync_create (h, sync_min))
        || flux_future_then (ov->f_sync, sync_max, sync_cb, ov) < 0)
        goto error;
    if (!(ov->cert = zcert_new ()))
        goto nomem;
    if (!(ov->certstore = zcertstore_new (NULL)))
        goto nomem;
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
