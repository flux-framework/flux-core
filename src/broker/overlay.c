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

#include "src/common/libutil/log.h"
#include "src/common/libutil/kary.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errno_safe.h"

#include "overlay.h"
#include "attr.h"

#define FLUX_ZAP_DOMAIN "flux"
#define ZAP_ENDPOINT "inproc://zeromq.zap.01"

enum {
    KEEPALIVE_STATUS_NORMAL = 0,
    KEEPALIVE_STATUS_DISCONNECT = 1,
};

struct child {
    int lastseen;
    unsigned long rank;
    char rank_str[16];
    bool connected;
    bool idle;
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
    flux_msg_handler_t **handlers;
    flux_future_t *f_sync;

    uint32_t size;
    uint32_t rank;
    int tbon_k;
    char rank_str[16];

    zsock_t *parent_zsock;      // NULL on rank 0
    char *parent_uri;
    flux_watcher_t *parent_w;
    int parent_lastsent;
    char *parent_pubkey;
    char parent_rank_str[16];

    zsock_t *bind_zsock;        // NULL if no downstream peers
    char *bind_uri;
    flux_watcher_t *bind_w;
    struct child *children;
    int child_count;

    overlay_monitor_f child_monitor_cb;
    void *child_monitor_arg;

    overlay_recv_f recv_cb;
    void *recv_arg;
};

static void overlay_mcast_child (struct overlay *ov, const flux_msg_t *msg);
static int overlay_sendmsg_child (struct overlay *ov, const flux_msg_t *msg);
static int overlay_sendmsg_parent (struct overlay *ov, const flux_msg_t *msg);

/* Convenience iterator for ov->children
 */
#define foreach_overlay_child(ov, child) \
    for ((child) = &(ov)->children[0]; \
            (child) - &(ov)->children[0] < (ov)->child_count; \
            (child)++)

static void overlay_monitor_notify (struct overlay *ov)
{
    if (ov->child_monitor_cb)
        ov->child_monitor_cb (ov, ov->child_monitor_arg);
}

/* Allocate children array based on static tree topology, and return size.
 */
static int alloc_children (uint32_t rank,
                           uint32_t size,
                           int k,
                           struct child **chp)
{
    struct child *ch = NULL;
    int count;
    int i;

    for (count = 0; kary_childof (k, size, rank, count) != KARY_NONE; count++)
        ;
    if (count > 0) {
        if (!(ch = calloc (count, sizeof (*ch))))
            return -1;
        for (i = 0; i < count; i++) {
            ch[i].rank = kary_childof (k, size, rank, i);
            snprintf (ch[i].rank_str, sizeof (ch[i].rank_str),"%lu",
                      ch[i].rank);
        }
    }
    *chp = ch;
    return count;
}

int overlay_set_geometry (struct overlay *ov,
                          uint32_t size,
                          uint32_t rank,
                          int tbon_k)
{
    ov->size = size;
    ov->rank = rank;
    ov->tbon_k = tbon_k;
    if ((ov->child_count = alloc_children (rank,
                                           size,
                                           tbon_k,
                                           &ov->children)) < 0)
        return -1;
    snprintf (ov->rank_str, sizeof (ov->rank_str), "%"PRIu32, rank);
    if (rank > 0) {
        snprintf (ov->parent_rank_str,
                  sizeof (ov->parent_rank_str),
                  "%"PRIu32,
                  kary_parentof (tbon_k, rank));
    }

    return 0;
}

uint32_t overlay_get_rank (struct overlay *ov)
{
    return ov->rank;
}

uint32_t overlay_get_size (struct overlay *ov)
{
    return ov->size;
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
    double now = flux_reactor_now (flux_get_reactor (ov->h));
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
                                  child->rank,
                                  fsd);
                        child->idle = true;
                    }
                }
                else {
                    if (child->idle) {
                        flux_log (ov->h,
                                  LOG_ERR,
                                  "child %lu no longer idle",
                                  child->rank);
                        child->idle = false;
                    }
                }
            }
        }
    }
}

static struct child *child_lookup (struct overlay *ov, const char *uuid)
{
    struct child *child;

    foreach_overlay_child (ov, child) {
        if (!strcmp (uuid, child->rank_str))
            return child;
    }
    return NULL;
}

int overlay_set_parent_pubkey (struct overlay *ov, const char *pubkey)
{
    if (!(ov->parent_pubkey = strdup (pubkey)))
        return -1;
    return 0;
}

int overlay_set_parent_uri (struct overlay *ov, const char *uri)
{
    free (ov->parent_uri);
    if (!(ov->parent_uri = strdup (uri)))
        return -1;
    return 0;
}

const char *overlay_get_parent_uri (struct overlay *ov)
{
    return ov->parent_uri;
}

static int overlay_sendmsg_parent (struct overlay *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->parent_zsock) {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = flux_msg_sendzsock (ov->parent_zsock, msg);
    if (rc == 0)
        ov->parent_lastsent = flux_reactor_now (flux_get_reactor (ov->h));
done:
    return rc;
}

static int overlay_keepalive_parent (struct overlay *ov, int status)
{
    flux_msg_t *msg = NULL;

    if (ov->parent_zsock) {
        if (!(msg = flux_keepalive_encode (0, status)))
            return -1;
        if (flux_msg_enable_route (msg) < 0)
            goto error;
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
    char *uuid = NULL;
    uint32_t nodeid;
    uint32_t route;
    char rte[16];
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
                    if ((route = kary_child_route (ov->tbon_k,
                                                   ov->size,
                                                   ov->rank,
                                                   nodeid)) != KARY_NONE) {
                        if (!(cpy = flux_msg_copy (msg, true)))
                            goto error;
                        if (flux_msg_push_route (cpy, ov->rank_str) < 0)
                            goto error;
                        snprintf (rte, sizeof (rte), "%"PRIu32, route);
                        if (flux_msg_push_route (cpy, rte) < 0)
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
                    && flux_msg_get_route_last (msg, &uuid) == 0
                    && uuid != NULL
                    && !strcmp (uuid, ov->parent_rank_str))
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
                    if (flux_msg_enable_route (cpy) < 0)
                        goto error;
                    msg = cpy;
                }
                if (overlay_sendmsg_parent (ov, msg) < 0)
                    goto error;
            }
            break;
        default:
            goto inval;
    }
    free (uuid);
    flux_msg_decref (cpy);
    return 0;
inval:
    errno = EINVAL;
error:
    ERRNO_SAFE_WRAP (free, uuid);
    flux_msg_decref (cpy);
    return -1;
}

static void sync_cb (flux_future_t *f, void *arg)
{
    struct overlay *ov = arg;
    double now = flux_reactor_now (flux_get_reactor (ov->h));

    if (now - ov->parent_lastsent > idle_min)
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
    rc = flux_msg_sendzsock_ex (ov->bind_zsock, msg, true);
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
    if (flux_msg_enable_route (cpy) < 0)
        goto done;
    if (flux_msg_push_route (cpy, child->rank_str) < 0)
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
                if (errno == EHOSTUNREACH) {
                    child->connected = false;
                    disconnects++;
                }
                else
                    flux_log_error (ov->h,
                                    "mcast error to child rank %lu",
                                    child->rank);
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
    char *uuid = NULL;
    struct child *child;
    int status;
    bool connected = true;

    if (!(msg = flux_msg_recvzsock (ov->bind_zsock)))
        return;
    if (flux_msg_get_type (msg, &type) < 0
        || flux_msg_get_route_last (msg, &uuid) < 0
        || !(child = child_lookup (ov, uuid))) {
        flux_log (ov->h,
                  LOG_ERR, "DROP downstream %s from %s",
                  type != -1 ? flux_msg_typestr (type) : "message",
                  uuid != NULL ? uuid : "unknown");
        goto done;
    }
    switch (type) {
        case FLUX_MSGTYPE_KEEPALIVE:
            if (flux_keepalive_decode (msg, NULL, &status) == 0
                && status == KEEPALIVE_STATUS_DISCONNECT) {
                connected = false;
            }
            break;
        case FLUX_MSGTYPE_REQUEST:
            break;
        case FLUX_MSGTYPE_RESPONSE:
            /* Response message traveling upstream requires special handling:
             * ROUTER socket will have pushed peer uuid onto message as if it
             * were a request, but the effect we want for responses is to have
             * a route popped off at each router hop.
             */
            (void)flux_msg_pop_route (msg, NULL); // child uuid from ROUTER
            (void)flux_msg_pop_route (msg, NULL); // my uuid
            break;
        case FLUX_MSGTYPE_EVENT:
            break;
    }
    child->lastseen = flux_reactor_now (flux_get_reactor (ov->h));
    if (child->connected != connected) {
        child->connected = connected;
        overlay_monitor_notify (ov);
    }
    if (type != FLUX_MSGTYPE_KEEPALIVE)
        ov->recv_cb (msg, OVERLAY_DOWNSTREAM, ov->recv_arg);
done:
    free (uuid);
    flux_msg_decref (msg);
}

static void parent_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    struct overlay *ov = arg;
    flux_msg_t *msg;
    int type;
    bool dropped = false;

    if (!(msg = flux_msg_recvzsock (ov->parent_zsock)))
        return;
    if (flux_msg_get_type (msg, &type) < 0) {
        dropped = true;
        goto done;
    }
    if (type == FLUX_MSGTYPE_EVENT) {
        if (flux_msg_clear_route (msg) < 0) {
            dropped = true;
            goto done;
        }
    }
    ov->recv_cb (msg, OVERLAY_UPSTREAM, ov->recv_arg);
done:
    if (dropped)
        flux_log (ov->h,
                  LOG_ERR, "DROP upstream %s",
                  type != -1 ? flux_msg_typestr (type) : "message");
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
    if (!(ov->zap_w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
                                               ov->zap,
                                               FLUX_POLLIN,
                                               overlay_zap_cb,
                                               ov)))
        return -1;
    flux_watcher_start (ov->zap_w);
    return 0;
}

int overlay_connect (struct overlay *ov)
{
    if (ov->rank > 0) {
        if (!ov->h || ov->rank == FLUX_NODEID_ANY || !ov->parent_uri) {
            errno = EINVAL;
            return -1;
        }
        if (!(ov->parent_zsock = zsock_new_dealer (NULL)))
            goto nomem;
        zsock_set_zap_domain (ov->parent_zsock, FLUX_ZAP_DOMAIN);
        zcert_apply (ov->cert, ov->parent_zsock);
        zsock_set_curve_serverkey (ov->parent_zsock, ov->parent_pubkey);
        zsock_set_identity (ov->parent_zsock, ov->rank_str);
        if (zsock_connect (ov->parent_zsock, "%s", ov->parent_uri) < 0)
            goto nomem;
        if (!(ov->parent_w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
                                                      ov->parent_zsock,
                                                      FLUX_POLLIN,
                                                      parent_cb,
                                                      ov)))
        return -1;
        flux_watcher_start (ov->parent_w);
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
    if (!(ov->bind_w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
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

int overlay_register_attrs (struct overlay *overlay, attr_t *attrs)
{
    int tbon_level = kary_levelof (overlay->tbon_k, overlay->rank);
    int tbon_maxlevel = kary_levelof (overlay->tbon_k, overlay->size - 1);
    int tbon_descendants = kary_sum_descendants (overlay->tbon_k,
                                                 overlay->size,
                                                 overlay->rank);

    if (attr_add_active (attrs, "tbon.parent-endpoint",
                         FLUX_ATTRFLAG_READONLY,
                         overlay_attr_get_cb, NULL, overlay) < 0)
        return -1;
    if (attr_add_uint32 (attrs, "rank", overlay->rank,
                         FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_uint32 (attrs, "size", overlay->size,
                         FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.arity", overlay->tbon_k,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.level", tbon_level,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.maxlevel", tbon_maxlevel,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.descendants", tbon_descendants,
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

static json_t *lspeer_object_create (struct overlay *ov)
{
    json_t *o = NULL;
    json_t *child_o;
    double now = flux_reactor_now (flux_get_reactor (ov->h));
    struct child *child;

    if (!(o = json_object ()))
        goto nomem;
    foreach_overlay_child (ov, child) {
        if (!(child_o = json_pack ("{s:f}",
                                   "idle",
                                   now - child->lastseen)))
            goto nomem;
        if (json_object_set_new (o, child->rank_str, child_o) < 0) {
            json_decref (child_o);
            goto nomem;
        }
    }
    return o;
nomem:
    json_decref (o);
    errno = ENOMEM;
    return NULL;
}

static void lspeer_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct overlay *ov = arg;
    json_t *o;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(o = lspeer_object_create (ov)))
        goto error;
    if (flux_respond_pack (h, msg, "O", o) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    json_decref (o);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
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

        zsock_destroy (&ov->parent_zsock);
        free (ov->parent_uri);
        flux_watcher_destroy (ov->parent_w);
        free (ov->parent_pubkey);

        zsock_destroy (&ov->bind_zsock);
        free (ov->bind_uri);
        flux_watcher_destroy (ov->bind_w);

        free (ov->children);
        free (ov);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "overlay.lspeer", lspeer_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

struct overlay *overlay_create (flux_t *h, overlay_recv_f cb, void *arg)
{
    struct overlay *ov;

    if (!(ov = calloc (1, sizeof (*ov))))
        return NULL;
    ov->rank = FLUX_NODEID_ANY;
    ov->parent_lastsent = -1;
    ov->h = h;
    ov->recv_cb = cb;
    ov->recv_arg = arg;
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
