/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdarg.h>
#include <czmq.h>
#include <flux/core.h>
#include <inttypes.h>
#include <jansson.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/iterators.h"
#include "src/common/libutil/kary.h"

#include "heartbeat.h"
#include "overlay.h"
#include "attr.h"

struct endpoint {
    zsock_t *zs;
    char *uri;
    flux_watcher_t *w;
};

struct overlay_struct {
    flux_sec_t *sec;
    flux_t *h;
    zhash_t *children;          /* child_t - by uuid */
    flux_msg_handler_t *heartbeat;
    int epoch;

    uint32_t size;
    uint32_t rank;
    int tbon_k;
    int tbon_level;
    int tbon_maxlevel;
    int tbon_descendants;

    struct endpoint *parent;    /* DEALER - requests to parent */
    overlay_cb_f parent_cb;
    void *parent_arg;
    int parent_lastsent;

    struct endpoint *child;     /* ROUTER - requests from children */
    overlay_cb_f child_cb;
    void *child_arg;

    struct endpoint *event;     /* PUB for rank = 0, SUB for rank > 0 */
    overlay_cb_f event_cb;
    void *event_arg;
    bool event_munge;

    struct endpoint *relay;

    int idle_warning;
};

typedef struct {
    int lastseen;
    bool mute;
} child_t;

static void heartbeat_handler (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg);

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
    struct endpoint *ep = xzmalloc (sizeof (*ep));
    ep->uri = xvasprintf (fmt, ap);
    return ep;
}

void overlay_destroy (overlay_t *ov)
{
    if (ov) {
        if (ov->heartbeat)
            flux_msg_handler_destroy (ov->heartbeat);
        if (ov->h)
            (void)flux_event_unsubscribe (ov->h, "hb");
        endpoint_destroy (ov->parent);
        endpoint_destroy (ov->child);
        endpoint_destroy (ov->event);
        endpoint_destroy (ov->relay);
        zhash_destroy (&ov->children);
        free (ov);
    }
}

overlay_t *overlay_create ()
{
    overlay_t *ov = xzmalloc (sizeof (*ov));
    ov->rank = FLUX_NODEID_ANY;
    ov->parent_lastsent = -1;

    if (!(ov->children = zhash_new ()))
        oom ();
    return ov;
}

void overlay_init (overlay_t *overlay,
                   uint32_t size, uint32_t rank, int tbon_k)
{
    overlay->size = size;
    overlay->rank = rank;
    overlay->tbon_k = tbon_k;
    overlay->tbon_level = kary_levelof (tbon_k, rank);
    overlay->tbon_maxlevel = kary_levelof (tbon_k, size - 1);
    overlay->tbon_descendants = kary_sum_descendants (tbon_k, size, rank);
}

void overlay_set_sec (overlay_t *ov, flux_sec_t *sec)
{
    ov->sec = sec;
}

uint32_t overlay_get_rank (overlay_t *ov)
{
    return ov->rank;
}

uint32_t overlay_get_size (overlay_t *ov)
{
    return ov->size;
}

void overlay_set_flux (overlay_t *ov, flux_t *h)
{
    struct flux_match match = FLUX_MATCH_EVENT;

    ov->h = h;

    match.topic_glob = "hb";
    if (!(ov->heartbeat = flux_msg_handler_create (ov->h, match,
                                                   heartbeat_handler, ov)))
        log_err_exit ("flux_msg_handler_create");
    flux_msg_handler_start (ov->heartbeat);
    if (flux_event_subscribe (ov->h, "hb") < 0)
        log_err_exit ("flux_event_subscribe");
}

void overlay_set_idle_warning (overlay_t *ov, int heartbeats)
{
    ov->idle_warning = heartbeats;
}

char *overlay_lspeer_encode (overlay_t *ov)
{
    json_t *o = NULL;
    json_t *child_o;
    const char *uuid;
    child_t *child;
    char *json_str;

    if (!(o = json_object ()))
        goto nomem;
    FOREACH_ZHASH (ov->children, uuid, child) {
        if (!(child_o = json_pack ("{s:i}", "idle",
                                   ov->epoch - child->lastseen)))
            goto nomem;
        if (json_object_set_new (o, uuid, child_o) < 0) {
            json_decref (child_o);
            goto nomem;
        }
    }
    if (!(json_str = json_dumps (o, 0)))
        goto nomem;
    return json_str;
nomem:
    json_decref (o);
    errno = ENOMEM;
    return NULL;
}

void overlay_log_idle_children (overlay_t *ov)
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

void overlay_mute_child (overlay_t *ov, const char *uuid)
{
    child_t *child = zhash_lookup (ov->children, uuid);
    if (child)
        child->mute = true;
}

void overlay_checkin_child (overlay_t *ov, const char *uuid)
{
    child_t *child  = zhash_lookup (ov->children, uuid);
    if (!child) {
        child = xzmalloc (sizeof (*child));
        zhash_update (ov->children, uuid, child);
        zhash_freefn (ov->children, uuid, (zhash_free_fn *)free);
    }
    child->lastseen = ov->epoch;
}

void overlay_set_parent (overlay_t *ov, const char *fmt, ...)
{
    if (ov->parent)
        endpoint_destroy (ov->parent);
    va_list ap;
    va_start (ap, fmt);
    ov->parent = endpoint_vcreate (fmt, ap);
    va_end (ap);
}

const char *overlay_get_parent (overlay_t *ov)
{
    if (!ov->parent)
        return NULL;
    return ov->parent->uri;
}

int overlay_sendmsg_parent (overlay_t *ov, const flux_msg_t *msg)
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

static int overlay_keepalive_parent (overlay_t *ov)
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

static void heartbeat_handler (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    overlay_t *ov = arg;

    if (flux_heartbeat_decode (msg, &ov->epoch) < 0)
        return;
    overlay_keepalive_parent (ov);
    overlay_log_idle_children (ov);
}

void overlay_set_parent_cb (overlay_t *ov, overlay_cb_f cb, void *arg)
{
    ov->parent_cb = cb;
    ov->parent_arg = arg;
}

void overlay_set_child (overlay_t *ov, const char *fmt, ...)
{
    if (ov->child)
        endpoint_destroy (ov->child);
    va_list ap;
    va_start (ap, fmt);
    ov->child = endpoint_vcreate (fmt, ap);
    va_end (ap);
}

const char *overlay_get_child (overlay_t *ov)
{
    if (!ov->child)
        return NULL;
    return ov->child->uri;
}

void overlay_set_child_cb (overlay_t *ov, overlay_cb_f cb, void *arg)
{
    ov->child_cb = cb;
    ov->child_arg = arg;
}

int overlay_sendmsg_child (overlay_t *ov, const flux_msg_t *msg)
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

int overlay_mcast_child (overlay_t *ov, const flux_msg_t *msg)
{
    flux_msg_t *cpy = NULL;
    const char *uuid;
    child_t *child;
    int rc = -1;

    if (!ov->child || !ov->child->zs || !ov->children)
        return 0;
    FOREACH_ZHASH (ov->children, uuid, child) {
        if (!child->mute) {
            if (!(cpy = flux_msg_copy (msg, true)))
                oom ();
            if (flux_msg_enable_route (cpy) < 0)
                goto done;
            if (flux_msg_push_route (cpy, uuid) < 0)
                goto done;
            if (flux_msg_sendzsock (ov->child->zs, cpy) < 0)
                goto done;
            flux_msg_destroy (cpy);
            cpy = NULL;
        }
    }
    rc = 0;
done:
    flux_msg_destroy (cpy);
    return rc;
}

void overlay_set_event (overlay_t *ov, const char *fmt, ...)
{
    if (ov->event)
        endpoint_destroy (ov->event);
    va_list ap;
    va_start (ap, fmt);
    ov->event = endpoint_vcreate (fmt, ap);
    va_end (ap);

    ov->event_munge = strstr (ov->event->uri, "pgm://") ? true : false;
}

const char *overlay_get_event (overlay_t *ov)
{
    if (!ov->event)
        return NULL;
    return ov->event->uri;
}

void overlay_set_event_cb (overlay_t *ov, overlay_cb_f cb, void *arg)
{
    ov->event_cb = cb;
    ov->event_arg = arg;
}

int overlay_sendmsg_event (overlay_t *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->event || !ov->event->zs)
        return 0;
    if (ov->event_munge) {
        if (flux_msg_sendzsock_munge (ov->event->zs, msg, ov->sec) < 0)
            goto done;
    } else {
        if (flux_msg_sendzsock (ov->event->zs, msg) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

flux_msg_t *overlay_recvmsg_event (overlay_t *ov)
{
    flux_msg_t *msg = NULL;
    if (!ov->event || !ov->event->zs) {
        errno = EINVAL;
        goto done;
    }
    if (ov->event_munge) {
        if (!(msg = flux_msg_recvzsock_munge (ov->event->zs, ov->sec)))
            goto done;
    } else {
        if (!(msg = flux_msg_recvzsock (ov->event->zs)))
            goto done;
    }
done:
    return msg;
}

void overlay_set_relay (overlay_t *ov, const char *fmt, ...)
{
    if (ov->relay)
        endpoint_destroy (ov->relay);
    va_list ap;
    va_start (ap, fmt);
    ov->relay = endpoint_vcreate (fmt, ap);
    va_end (ap);
}

const char *overlay_get_relay (overlay_t *ov)
{
    if (!ov->relay)
        return NULL;
    return ov->relay->uri;
}

int overlay_sendmsg_relay (overlay_t *ov, const flux_msg_t *msg)
{
    int rc = -1;

    if (!ov->relay || !ov->relay->zs) {
        errno = EINVAL;
        goto done;
    }
    rc = flux_msg_sendzsock (ov->relay->zs, msg);
done:
    return rc;
}

static void child_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    void *zsock = flux_zmq_watcher_get_zsock (w);
    overlay_t *ov = arg;
    if (ov->child_cb)
        ov->child_cb (ov, zsock, ov->child_arg);
}

static int bind_child (overlay_t *ov, struct endpoint *ep)
{
    if (!(ep->zs = zsock_new_router (NULL)))
        log_err_exit ("zsock_new_router");
    if (flux_sec_ssockinit (ov->sec, ep->zs) < 0)
        log_msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ov->sec));
    if (zsock_bind (ep->zs, "%s", ep->uri) < 0)
        log_err_exit ("%s", ep->uri);
    if (strchr (ep->uri, '*')) { /* capture dynamically assigned port */
        free (ep->uri);
        ep->uri = zsock_last_endpoint (ep->zs);
    }
    if (!(ep->w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
                                           ep->zs, FLUX_POLLIN, child_cb, ov)))
        log_err_exit ("flux_zmq_watcher_create");
    flux_watcher_start (ep->w);
    return 0;
}

static int bind_event_pub (overlay_t *ov, struct endpoint *ep)
{
    if (!(ep->zs = zsock_new_pub (NULL)))
        log_err_exit ("zsock_new_pub");
    if (flux_sec_ssockinit (ov->sec, ep->zs) < 0) /* no-op for epgm */
        log_msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ov->sec));
    if (zsock_bind (ep->zs, "%s", ep->uri) < 0)
        log_err_exit ("%s: %s", __FUNCTION__, ep->uri);
    if (strchr (ep->uri, '*')) { /* capture dynamically assigned port */
        free (ep->uri);
        ep->uri = zsock_last_endpoint (ep->zs);
    }
    return 0;
}

static void event_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    void *zsock = flux_zmq_watcher_get_zsock (w);
    overlay_t *ov = arg;
    if (ov->event_cb)
        ov->event_cb (ov, zsock, ov->event_arg);
}

static int connect_event_sub (overlay_t *ov, struct endpoint *ep)
{
    if (!(ep->zs = zsock_new_sub (NULL, NULL)))
        log_err_exit ("zsock_new_sub");
    if (flux_sec_csockinit (ov->sec, ep->zs) < 0) /* no-op for epgm */
        log_msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ov->sec));
    if (zsock_connect (ep->zs, "%s", ep->uri) < 0)
        log_err_exit ("%s", ep->uri);
    zsock_set_subscribe (ep->zs, "");
    if (!(ep->w = flux_zmq_watcher_create (flux_get_reactor (ov->h),
                                           ep->zs, FLUX_POLLIN, event_cb, ov)))
        log_err_exit ("flux_zmq_watcher_create");
    flux_watcher_start (ep->w);
    return 0;
}

static void parent_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    void *zsock = flux_zmq_watcher_get_zsock (w);
    overlay_t *ov = arg;
    if (ov->parent_cb)
        ov->parent_cb (ov, zsock, ov->parent_arg);
}

static int connect_parent (overlay_t *ov, struct endpoint *ep)
{
    int savederr;
    char rankstr[16];

    if (!(ep->zs = zsock_new_dealer (NULL)))
        goto error;
    if (flux_sec_csockinit (ov->sec, ep->zs) < 0) {
        savederr = errno;
        log_msg ("flux_sec_csockinit: %s", flux_sec_errstr (ov->sec));
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

int overlay_connect (overlay_t *ov)
{
    int rc = -1;

    if (!ov->sec || !ov->h || ov->rank == FLUX_NODEID_ANY
                 || !ov->parent_cb || !ov->event_cb) {
        errno = EINVAL;
        goto done;
    }
    if (ov->event && !ov->event->zs && ov->rank > 0)
        connect_event_sub (ov, ov->event);
    if (ov->parent && !ov->parent->zs) {
        if (connect_parent (ov, ov->parent) < 0)
            log_err_exit ("%s", ov->parent->uri);
    }
    rc = 0;
done:
    return rc;
}

int overlay_bind (overlay_t *ov)
{
    int rc = -1;

    if (!ov->sec || !ov->h || ov->rank == FLUX_NODEID_ANY || !ov->child_cb) {
        errno = EINVAL;
        goto done;
    }
    if (ov->event && !ov->event->zs && ov->rank == 0)
        bind_event_pub (ov, ov->event);

    if (ov->child && !ov->child->zs)
        bind_child (ov, ov->child);

    if (ov->relay && !ov->relay->zs)
        bind_event_pub (ov, ov->relay);

    rc = 0;
done:
    return rc;
}

/* A callback of type attr_get_f to allow retrieving some information
 * from an overlay_t through attr_get().
 */
static int overlay_attr_get_cb (const char *name, const char **val, void *arg)
{
    overlay_t *overlay = arg;
    int rc = -1;

    if (!strcmp (name, "tbon.parent-endpoint"))
        *val = overlay_get_parent(overlay);
    else if (!strcmp (name, "mcast.relay-endpoint"))
        *val = overlay_get_relay(overlay);
    else {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int overlay_register_attrs (overlay_t *overlay, attr_t *attrs)
{
    if (attr_add_active (attrs, "tbon.parent-endpoint",
                         FLUX_ATTRFLAG_READONLY,
                         overlay_attr_get_cb, NULL, overlay) < 0)
        return -1;
    if (attr_add_active (attrs, "mcast.relay-endpoint",
                         FLUX_ATTRFLAG_IMMUTABLE,
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
