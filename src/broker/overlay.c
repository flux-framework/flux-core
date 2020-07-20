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

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/kary.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/zsecurity.h"

#include "heartbeat.h"
#include "overlay.h"
#include "attr.h"

struct endpoint {
    zsock_t *zs;
    char *uri;
    flux_watcher_t *w;
};

struct overlay {
    zsecurity_t *sec;             /* security context (MT-safe) */
    bool sec_initialized;
    flux_t *h;
    zhash_t *children;          /* child_t - by uuid */
    flux_msg_handler_t **handlers;
    int epoch;

    uint32_t size;
    uint32_t rank;
    int tbon_k;
    int tbon_level;
    int tbon_maxlevel;
    int tbon_descendants;

    struct endpoint *parent;    /* DEALER - requests to parent */
    overlay_sock_cb_f parent_cb;
    void *parent_arg;
    int parent_lastsent;

    struct endpoint *child;     /* ROUTER - requests from children */
    overlay_sock_cb_f child_cb;
    void *child_arg;

    zsock_t *child_monitor_sock;
    flux_watcher_t *child_monitor_w;
    int child_peer_count;
    overlay_monitor_cb_f child_monitor_cb;
    void *child_monitor_arg;

    overlay_init_cb_f init_cb;
    void *init_arg;

    int idle_warning;
};

typedef struct {
    int lastseen;
} child_t;

static void endpoint_destroy (struct endpoint *ep)
{
    if (ep) {
        free (ep->uri);
        flux_watcher_destroy (ep->w);
        zsock_destroy (&ep->zs);
        free (ep);
    }
}

static struct endpoint *endpoint_vcreate (const char *fmt, va_list ap)
{
    struct endpoint *ep = calloc (1, sizeof (*ep));
    if (vasprintf (&ep->uri, fmt, ap) < 0) {
        free (ep);
        errno = ENOMEM;
        return NULL;
    }
    return ep;
}

void overlay_set_init_callback (struct overlay *ov,
                                overlay_init_cb_f cb,
                                void *arg)
{
    ov->init_cb = cb;
    ov->init_arg = arg;
}

int overlay_init (struct overlay *overlay,
                  uint32_t size,
                  uint32_t rank,
                  int tbon_k)
{
    overlay->size = size;
    overlay->rank = rank;
    overlay->tbon_k = tbon_k;
    overlay->tbon_level = kary_levelof (tbon_k, rank);
    overlay->tbon_maxlevel = kary_levelof (tbon_k, size - 1);
    overlay->tbon_descendants = kary_sum_descendants (tbon_k, size, rank);
    if (overlay->init_cb)
        return (*overlay->init_cb) (overlay, overlay->init_arg);
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
    return ov->child_peer_count;
}

void overlay_set_idle_warning (struct overlay *ov, int heartbeats)
{
    ov->idle_warning = heartbeats;
}

void overlay_log_idle_children (struct overlay *ov)
{
    const char *uuid;
    child_t *child;
    int idle;

    if (ov->idle_warning > 0) {
        FOREACH_ZHASH (ov->children, uuid, child) {
            idle = ov->epoch - child->lastseen;
            if (idle >= ov->idle_warning)
                flux_log (ov->h, LOG_CRIT, "child %s idle for %d heartbeats",
                          uuid, idle);
        }
    }
}

void overlay_checkin_child (struct overlay *ov, const char *uuid)
{
    child_t *child  = zhash_lookup (ov->children, uuid);
    if (!child) {
        child = xzmalloc (sizeof (*child));
        zhash_update (ov->children, uuid, child);
        zhash_freefn (ov->children, uuid, (zhash_free_fn *)free);
    }
    child->lastseen = ov->epoch;
}

int overlay_set_parent (struct overlay *ov, const char *fmt, ...)
{
    int rc = -1;
    if (ov->parent)
        endpoint_destroy (ov->parent);
    va_list ap;
    va_start (ap, fmt);
    if ((ov->parent = endpoint_vcreate (fmt, ap)))
        rc = 0;
    va_end (ap);
    return rc;
}

const char *overlay_get_parent (struct overlay *ov)
{
    if (!ov->parent)
        return NULL;
    return ov->parent->uri;
}

int overlay_sendmsg_parent (struct overlay *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->parent || !ov->parent->zs) {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = flux_msg_sendzsock (ov->parent->zs, msg);
    if (rc == 0)
        ov->parent_lastsent = ov->epoch;
done:
    return rc;
}

static int overlay_keepalive_parent (struct overlay *ov)
{
    int idle = ov->epoch - ov->parent_lastsent;
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (!ov->parent || !ov->parent->zs || idle <= 1)
        return 0;
    if (!(msg = flux_keepalive_encode (0, 0)))
        goto done;
    if (flux_msg_enable_route (msg) < 0)
        goto done;
    rc = flux_msg_sendzsock (ov->parent->zs, msg);
done:
    flux_msg_destroy (msg);
    return rc;
}

static void heartbeat_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct overlay *ov = arg;

    if (flux_heartbeat_decode (msg, &ov->epoch) < 0)
        return;
    overlay_keepalive_parent (ov);
    overlay_log_idle_children (ov);
}

void overlay_set_parent_cb (struct overlay *ov, overlay_sock_cb_f cb, void *arg)
{
    ov->parent_cb = cb;
    ov->parent_arg = arg;
}

int overlay_set_child (struct overlay *ov, const char *fmt, ...)
{
    int rc = -1;
    if (ov->child)
        endpoint_destroy (ov->child);
    va_list ap;
    va_start (ap, fmt);
    if ((ov->child = endpoint_vcreate (fmt, ap)))
        rc = 0;
    va_end (ap);
    return rc;
}

const char *overlay_get_child (struct overlay *ov)
{
    if (!ov->child)
        return NULL;
    return ov->child->uri;
}

void overlay_set_child_cb (struct overlay *ov, overlay_sock_cb_f cb, void *arg)
{
    ov->child_cb = cb;
    ov->child_arg = arg;
}

int overlay_sendmsg_child (struct overlay *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->child || !ov->child->zs) {
        errno = EINVAL;
        goto done;
    }
    rc = flux_msg_sendzsock (ov->child->zs, msg);
done:
    return rc;
}

static int overlay_mcast_child_one (void *zsock,
                                    const flux_msg_t *msg,
                                    const char *uuid)
{
    flux_msg_t *cpy;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (flux_msg_enable_route (cpy) < 0)
        goto done;
    if (flux_msg_push_route (cpy, uuid) < 0)
        goto done;
    if (flux_msg_sendzsock (zsock, cpy) < 0) {
        if (errno != EHOSTUNREACH) // a child has disconnected - not an error
            goto done;
    }
    rc = 0;
done:
    flux_msg_destroy (cpy);
    return rc;
}

int overlay_mcast_child (struct overlay *ov, const flux_msg_t *msg)
{
    const char *uuid;
    child_t *child;
    int first_errno;
    int failures = 0;

    if (!ov->child || !ov->child->zs || !ov->children)
        return 0;
    FOREACH_ZHASH (ov->children, uuid, child) {
        if (overlay_mcast_child_one (ov->child->zs, msg, uuid) < 0) {
            if (failures == 0)
                first_errno = errno;
            failures++;
        }
    }
    if (failures > 0) {
        errno = first_errno;
        return -1;
    }
    return 0;
}

/* Handle notification of peer connect/disconnect on child monitor socket.
 * Maintain ov->child_peer_count, accessed via overlay_get_child_peer_count().
 * Call a callback, if any, when the count changes.
 * N.B. Assumes monitor protocol in libzmq >= 4.0 (assured by build system)
 */
static void child_monitor_cb (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    struct overlay *ov = arg;
    zframe_t *zf;
    uint16_t event;

    if (!(zf = zframe_recv (ov->child_monitor_sock)))
        return; // spurious wakeup?
    event = *(uint16_t *)zframe_data (zf);
    zframe_destroy (&zf);

    if (!(zf = zframe_recv (ov->child_monitor_sock))) {
        log_msg ("zmq_socket_monitor: expected frame 2!");
        return;
    }
    zframe_destroy (&zf);
    if (event & ZMQ_EVENT_ACCEPTED)
        ov->child_peer_count++;
    if (event & ZMQ_EVENT_DISCONNECTED)
        ov->child_peer_count--;
    if (ov->child_monitor_cb)
        ov->child_monitor_cb (ov, ov->child_monitor_arg);
}

/* Monitor child socket.
 * This creates a new socket that receives control messages when downstream
 * peers connect/disconnect.  Set up a watcher to capture these messages and
 * maintain a connected peer count.
 */
static int child_monitor_init (struct overlay *ov, struct endpoint *ep)
{
    const char *uri = "inproc://monitor-child";
    flux_reactor_t *r = flux_get_reactor (ov->h);

    if (zmq_socket_monitor (zsock_resolve (ep->zs),
                            uri,
                            ZMQ_EVENT_ACCEPTED | ZMQ_EVENT_DISCONNECTED) < 0) {
        log_err ("zmq_socket_monitor");
        return -1;
    }
    if (!(ov->child_monitor_sock = zsock_new_pair (uri))) {
        log_err ("zsock_new_pair");
        return -1;
    }
    if (!(ov->child_monitor_w = flux_zmq_watcher_create (r,
                                                         ov->child_monitor_sock,
                                                         FLUX_POLLIN,
                                                         child_monitor_cb,
                                                         ov))) {
        log_err ("flux_zmq_watcher_create");
        return -1;
    }
    flux_watcher_start (ov->child_monitor_w);
    return 0;
}

static void child_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    void *zsock = flux_zmq_watcher_get_zsock (w);
    struct overlay *ov = arg;
    if (ov->child_cb)
        ov->child_cb (ov, zsock, ov->child_arg);
}

/* Cleanup not done in this function, responsibiility of caller to
 * call endpoint_destroy() eventually */
static int bind_child (struct overlay *ov, struct endpoint *ep)
{
    if (!(ep->zs = zsock_new_router (NULL))) {
        log_err ("zsock_new_router");
        return -1;
    }
    if (child_monitor_init (ov, ep) < 0)
        return -1;
    zsock_set_router_mandatory (ep->zs, 1);
    if (zsecurity_ssockinit (ov->sec, ep->zs) < 0) {
        log_msg ("zsecurity_ssockinit: %s", zsecurity_errstr (ov->sec));
        return -1;
    }
    if (zsock_bind (ep->zs, "%s", ep->uri) < 0) {
        log_err ("%s", ep->uri);
        return -1;
    }
    if (strchr (ep->uri, '*')) { /* capture dynamically assigned port */
        free (ep->uri);
        ep->uri = zsock_last_endpoint (ep->zs);
    }
    if (!(ep->w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
                                           ep->zs, FLUX_POLLIN, child_cb, ov))) {
        log_err ("flux_zmq_watcher_create");
        return -1;
    }
    flux_watcher_start (ep->w);
    /* Ensure that ipc files are removed when the broker exits.
     */
    char *ipc_path = strstr (ep->uri, "ipc://");
    if (ipc_path)
        cleanup_push_string (cleanup_file, ipc_path + 6);
    return 0;
}

static void parent_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    void *zsock = flux_zmq_watcher_get_zsock (w);
    struct overlay *ov = arg;
    if (ov->parent_cb)
        ov->parent_cb (ov, zsock, ov->parent_arg);
}

static int connect_parent (struct overlay *ov, struct endpoint *ep)
{
    int savederr;
    char rankstr[16];

    if (!(ep->zs = zsock_new_dealer (NULL)))
        goto error;
    if (zsecurity_csockinit (ov->sec, ep->zs) < 0) {
        savederr = errno;
        log_msg ("zsecurity_csockinit: %s", zsecurity_errstr (ov->sec));
        errno = savederr;
        goto error;
    }
    snprintf (rankstr, sizeof (rankstr), "%"PRIu32, ov->rank);
    zsock_set_identity (ep->zs, rankstr);
    if (zsock_connect (ep->zs, "%s", ep->uri) < 0)
        goto error;
    if (!(ep->w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
                                           ep->zs, FLUX_POLLIN, parent_cb, ov)))
        goto error;
    flux_watcher_start (ep->w);
    return 0;
error:
    if (ep->zs) {
        savederr = errno;
        zsock_destroy (&ep->zs);
        errno = savederr;
    }
    return -1;
}

static int overlay_sec_init (struct overlay *ov)
{
    if (!ov->sec_initialized) {
        if (zsecurity_comms_init (ov->sec) < 0) {
            log_msg ("zsecurity_comms_init: %s", zsecurity_errstr (ov->sec));
            return -1;
        }
        ov->sec_initialized = true;
    }
    return 0;
}

int overlay_connect (struct overlay *ov)
{
    int rc = -1;

    if (!ov->sec || !ov->h || ov->rank == FLUX_NODEID_ANY || !ov->parent_cb) {
        errno = EINVAL;
        goto done;
    }
    if (overlay_sec_init (ov) < 0)
        goto done;
    if (ov->parent && !ov->parent->zs) {
        if (connect_parent (ov, ov->parent) < 0) {
            log_err ("%s", ov->parent->uri);
            goto done;
        }
    }
    rc = 0;
done:
    return rc;
}

int overlay_bind (struct overlay *ov)
{
    int rc = -1;

    if (!ov->sec || !ov->h || ov->rank == FLUX_NODEID_ANY || !ov->child_cb) {
        errno = EINVAL;
        goto done;
    }
    if (overlay_sec_init (ov) < 0)
        goto done;

    if (ov->child && !ov->child->zs) {
        if (bind_child (ov, ov->child) < 0)
            goto done;
    }

    rc = 0;
done:
    return rc;
}

/* A callback of type attr_get_f to allow retrieving some information
 * from an struct overlay through attr_get().
 */
static int overlay_attr_get_cb (const char *name, const char **val, void *arg)
{
    struct overlay *overlay = arg;
    int rc = -1;

    if (!strcmp (name, "tbon.parent-endpoint"))
        *val = overlay_get_parent (overlay);
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
    if (attr_add_int (attrs, "tbon.level", overlay->tbon_level,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.maxlevel", overlay->tbon_maxlevel,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;
    if (attr_add_int (attrs, "tbon.descendants", overlay->tbon_descendants,
                      FLUX_ATTRFLAG_IMMUTABLE) < 0)
        return -1;

    return 0;
}

void overlay_set_monitor_cb (struct overlay *ov,
                             overlay_monitor_cb_f cb,
                             void *arg)
{
    ov->child_monitor_cb = cb;
    ov->child_monitor_arg = arg;
}

static json_t *lspeer_object_create (struct overlay *ov)
{
    json_t *o = NULL;
    json_t *child_o;
    const char *uuid;
    child_t *child;

    if (!(o = json_object ()))
        goto nomem;
    FOREACH_ZHASH (ov->children, uuid, child) {
        if (!(child_o = json_pack ("{s:i}",
                                   "idle",
                                   ov->epoch - child->lastseen)))
            goto nomem;
        if (json_object_set_new (o, uuid, child_o) < 0) {
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

void overlay_destroy (struct overlay *ov)
{
    if (ov) {
        int saved_errno = errno;
        if (ov->sec)
            zsecurity_destroy (ov->sec);
        if (ov->h)
            (void)flux_event_unsubscribe (ov->h, "hb");

        flux_watcher_destroy (ov->child_monitor_w);
        zsock_destroy (&ov->child_monitor_sock);

        flux_msg_handler_delvec (ov->handlers);
        endpoint_destroy (ov->parent);
        endpoint_destroy (ov->child);
        zhash_destroy (&ov->children);
        free (ov);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,  "heartbeat", heartbeat_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  "overlay.lspeer", lspeer_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

struct overlay *overlay_create (flux_t *h, int sec_typemask, const char *keydir)
{
    struct overlay *ov;

    if (!(ov = calloc (1, sizeof (*ov))))
        return NULL;
    ov->rank = FLUX_NODEID_ANY;
    ov->parent_lastsent = -1;
    ov->h = h;
    if (!(ov->children = zhash_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (!(ov->sec = zsecurity_create (sec_typemask, keydir)))
        goto error;

    if (flux_msg_handler_addvec (h, htab, ov, &ov->handlers) < 0)
        goto error;
    if (flux_event_subscribe (ov->h, "hb") < 0)
        goto error;

    return ov;
error:
    overlay_destroy (ov);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
