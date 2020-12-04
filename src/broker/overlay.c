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

struct child {
    int lastseen;
    unsigned long rank;
    char uuid[16];
    bool connected;
};


struct overlay {
    zsecurity_t *sec;             /* security context (MT-safe) */
    bool sec_initialized;
    flux_t *h;
    flux_msg_handler_t **handlers;
    int epoch;

    uint32_t size;
    uint32_t rank;
    int tbon_k;
    int tbon_level;
    int tbon_maxlevel;
    int tbon_descendants;

    struct child *children;
    int child_count;

    struct endpoint *parent;    /* DEALER - requests to parent */
    overlay_sock_cb_f parent_cb;
    void *parent_arg;
    int parent_lastsent;

    struct endpoint *child;     /* ROUTER - requests from children */
    overlay_sock_cb_f child_cb;
    void *child_arg;

    overlay_monitor_cb_f child_monitor_cb;
    void *child_monitor_arg;

    overlay_init_cb_f init_cb;
    void *init_arg;

    int idle_warning;
};

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
            snprintf (ch[i].uuid, sizeof (ch[i].uuid), "%lu", ch[i].rank);
        }
    }
    *chp = ch;
    return count;
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
    if ((overlay->child_count = alloc_children (rank,
                                                size,
                                                tbon_k,
                                                &overlay->children)) < 0)
        return -1;
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
    struct child *child;
    int count = 0;

    foreach_overlay_child (ov, child) {
        if (child->connected)
            count++;
    }
    return count;
}

void overlay_set_idle_warning (struct overlay *ov, int heartbeats)
{
    ov->idle_warning = heartbeats;
}

void overlay_log_idle_children (struct overlay *ov)
{
    struct child *child;
    int idle;

    if (ov->idle_warning > 0) {
        foreach_overlay_child (ov, child) {
            if (child->connected) {
                idle = ov->epoch - child->lastseen;
                if (idle >= ov->idle_warning) {
                    flux_log (ov->h,
                              LOG_CRIT,
                              "child %lu idle for %d heartbeats",
                              child->rank,
                              idle);
                }
            }
        }
    }
}

void overlay_checkin_child (struct overlay *ov, const char *uuid)
{
    struct child *child;

    foreach_overlay_child (ov, child) {
        if (!strcmp (uuid, child->uuid)) {
            child->lastseen = ov->epoch;
            if (!child->connected) {
                child->connected = true;
                overlay_monitor_notify (ov);
            }
            break;
        }
    }
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
    rc = flux_msg_sendzsock_ex (ov->child->zs, msg, true);
done:
    return rc;
}

static int overlay_mcast_child_one (void *zsock,
                                    const flux_msg_t *msg,
                                    struct child *child)
{
    flux_msg_t *cpy;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (flux_msg_enable_route (cpy) < 0)
        goto done;
    if (flux_msg_push_route (cpy, child->uuid) < 0)
        goto done;
    if (flux_msg_sendzsock_ex (zsock, cpy, true) < 0)
        goto done;
    rc = 0;
done:
    flux_msg_destroy (cpy);
    return rc;
}

void overlay_mcast_child (struct overlay *ov, const flux_msg_t *msg)
{
    struct child *child;
    int disconnects = 0;

    if (!ov->child || !ov->child->zs)
        return;
    foreach_overlay_child (ov, child) {
        if (!child->connected)
            continue;
        if (overlay_mcast_child_one (ov->child->zs, msg, child) < 0) {
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
    if (disconnects)
        overlay_monitor_notify (ov);
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
    struct child *child;

    if (!(o = json_object ()))
        goto nomem;
    foreach_overlay_child (ov, child) {
        if (!(child_o = json_pack ("{s:i}",
                                   "idle",
                                   ov->epoch - child->lastseen)))
            goto nomem;
        if (json_object_set_new (o, child->uuid, child_o) < 0) {
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

        flux_msg_handler_delvec (ov->handlers);
        endpoint_destroy (ov->parent);
        endpoint_destroy (ov->child);
        free (ov->children);
        free (ov);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,  "hb", heartbeat_cb, 0 },
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
