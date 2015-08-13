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

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

#include "heartbeat.h"
#include "overlay.h"

struct endpoint {
    void *zs;
    char *uri;
    flux_zmq_watcher_t *w;
};

struct overlay_struct {
    zctx_t *zctx;
    flux_sec_t sec;
    flux_t h;
    heartbeat_t heartbeat;
    zhash_t *children;          /* child_t - by uuid */

    uint32_t rank;
    char rankstr[16];
    char rankstr_right[16];

    zlist_t *parents;           /* DEALER - requests to parent */
                                /*  (reparent pushes new parent on head) */
    struct endpoint *right;     /* DEALER - requests to rank overlay */
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
};

typedef struct {
    int lastseen;
    bool mute;
} child_t;

static void endpoint_destroy (struct endpoint *ep)
{
    if (ep) {
        if (ep->uri)
            free (ep->uri);
        if (ep->w)
            flux_zmq_watcher_destroy (ep->w);
        /* N.B. ep->zp will be cleaned up with zctx_t destroy */
    }
}

static struct endpoint *endpoint_vcreate (const char *fmt, va_list ap)
{
    struct endpoint *ep = xzmalloc (sizeof (*ep));
    ep->uri = xvasprintf (fmt, ap);
    return ep;
}

static struct endpoint *endpoint_create (const char *fmt, ...)
{
    struct endpoint *ep;
    va_list ap;
    va_start (ap, fmt);
    ep = endpoint_vcreate (fmt, ap);
    va_end (ap);
    return ep;
}

void overlay_destroy (overlay_t ov)
{
    struct endpoint *ep;

    if (ov) {
        if (ov->parents) {
            while ((ep = zlist_pop (ov->parents)))
                endpoint_destroy (ep);
            zlist_destroy (&ov->parents);
        }
        endpoint_destroy (ov->child);
        endpoint_destroy (ov->right);
        endpoint_destroy (ov->event);
        endpoint_destroy (ov->relay);
        zhash_destroy (&ov->children);
        free (ov);
    }
}

overlay_t overlay_create (void)
{
    overlay_t ov = xzmalloc (sizeof (*ov));
    if (!(ov->parents = zlist_new ()))
        oom ();
    ov->rank = FLUX_NODEID_ANY;
    ov->parent_lastsent = -1;
    if (!(ov->children = zhash_new ()))
        oom ();
    return ov;
}

void overlay_set_zctx (overlay_t ov, zctx_t *zctx)
{
    ov->zctx = zctx;
}

void overlay_set_sec (overlay_t ov, flux_sec_t sec)
{
    ov->sec = sec;
}

void overlay_set_rank (overlay_t ov, uint32_t rank)
{
    ov->rank = rank;
    snprintf (ov->rankstr, sizeof (ov->rankstr), "%u", rank);
    snprintf (ov->rankstr_right, sizeof (ov->rankstr), "%ur", rank);
}

void overlay_set_reactor (overlay_t ov, flux_t h)
{
    ov->h = h;
}

void overlay_set_heartbeat (overlay_t ov, heartbeat_t h)
{
    ov->heartbeat = h;
}

json_object *overlay_lspeer_encode (overlay_t ov)
{
    int now = heartbeat_get_epoch (ov->heartbeat);
    JSON out = Jnew ();
    zlist_t *uuids;
    char *uuid;
    child_t *child;

    if (!(uuids = zhash_keys (ov->children)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        if ((child = zhash_lookup (ov->children, uuid))) {
            JSON o = Jnew ();
            Jadd_int (o, "idle", now - child->lastseen);
            Jadd_obj (out, uuid, o);
            Jput (o);
        }
        uuid = zlist_next (uuids);
    }
    zlist_destroy (&uuids);
    return out;
}

void overlay_mute_child (overlay_t ov, const char *uuid)
{
    child_t *child = zhash_lookup (ov->children, uuid);
    if (child)
        child->mute = true;
}

void overlay_checkin_child (overlay_t ov, const char *uuid)
{
    int now = heartbeat_get_epoch (ov->heartbeat);
    child_t *child  = zhash_lookup (ov->children, uuid);
    if (!child) {
        child = xzmalloc (sizeof (*child));
        zhash_update (ov->children, uuid, child);
        zhash_freefn (ov->children, uuid, (zhash_free_fn *)free);
    }
    child->lastseen = now;
}

void overlay_push_parent (overlay_t ov, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    struct endpoint *ep = endpoint_vcreate (fmt, ap);
    va_end (ap);
    if (zlist_push (ov->parents, ep) < 0)
        oom ();
}

const char *overlay_get_parent (overlay_t ov)
{
    struct endpoint *ep = zlist_first (ov->parents);
    if (!ep)
        return NULL;
    return ep->uri;
}

int overlay_sendmsg_parent (overlay_t ov, zmsg_t **zmsg)
{
    struct endpoint *ep = zlist_first (ov->parents);
    int rc = -1;

    if (!ep || !ep->zs) {
        if (ov->rank == 0)
            errno = ENOSYS;
        else
            errno = EHOSTUNREACH;
        goto done;
    }
    rc = zmsg_send (zmsg, ep->zs);
    if (rc == 0)
        ov->parent_lastsent = heartbeat_get_epoch (ov->heartbeat);
done:
    return rc;
}

int overlay_keepalive_parent (overlay_t ov)
{
    struct endpoint *ep = zlist_first (ov->parents);
    int idle = heartbeat_get_epoch (ov->heartbeat) - ov->parent_lastsent;
    zmsg_t *zmsg = NULL;
    int rc = -1;

    if (!ep || !ep->zs || idle <= 1)
        return 0;
    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_KEEPALIVE)))
        goto done;
    if (flux_msg_enable_route (zmsg) < 0)
        goto done;
    rc = zmsg_send (&zmsg, ep->zs);
done:
    zmsg_destroy (&zmsg);
    return rc;
}

void overlay_set_right (overlay_t ov, const char *fmt, ...)
{
    if (ov->right)
        endpoint_destroy (ov->right);
    va_list ap;
    va_start (ap, fmt);
    ov->right = endpoint_vcreate (fmt, ap);
    va_end (ap);
}

static bool ring_wrap (overlay_t ov, zmsg_t *zmsg)
{
    zframe_t *zf;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) > 0) {
        if (zframe_streq (zf, ov->rankstr_right)) /* cycle detected! */
            return true;
        zf = zmsg_next (zmsg);
    }
    return false;
}

int overlay_sendmsg_right (overlay_t ov, zmsg_t **zmsg)
{
    int rc = -1;

    if (!ov->right || !ov->right->zs || ring_wrap (ov, *zmsg)) {
        errno = EHOSTUNREACH;
        goto done;
    }
    rc = zmsg_send (zmsg, ov->right->zs);
done:
    return rc;
}

void overlay_set_parent_cb (overlay_t ov, overlay_cb_f cb, void *arg)
{
    ov->parent_cb = cb;
    ov->parent_arg = arg;
}

void overlay_set_child (overlay_t ov, const char *fmt, ...)
{
    if (ov->child)
        endpoint_destroy (ov->child);
    va_list ap;
    va_start (ap, fmt);
    ov->child = endpoint_vcreate (fmt, ap);
    va_end (ap);
}

const char *overlay_get_child (overlay_t ov)
{
    if (!ov->child)
        return NULL;
    return ov->child->uri;
}

void overlay_set_child_cb (overlay_t ov, overlay_cb_f cb, void *arg)
{
    ov->child_cb = cb;
    ov->child_arg = arg;
}

int overlay_sendmsg_child (overlay_t ov, zmsg_t **zmsg)
{
    int rc = -1;

    if (!ov->child || !ov->child->zs) {
        errno = EINVAL;
        goto done;
    }
    rc = zmsg_send (zmsg, ov->child->zs);
done:
    return rc;
}

int overlay_mcast_child (overlay_t ov, zmsg_t *zmsg)
{
    zmsg_t *cpy = NULL;
    zlist_t *uuids = NULL;
    char *uuid;
    int rc = -1;

    if (!ov->child || !ov->child->zs || !ov->children)
        return 0;
    if (!(uuids = zhash_keys (ov->children)))
        oom ();
    uuid = zlist_first (uuids);
    while (uuid) {
        child_t *child = zhash_lookup (ov->children, uuid);
        if (child && !child->mute) {
            if (!(cpy = zmsg_dup (zmsg)))
                oom ();
            if (flux_msg_enable_route (cpy) < 0)
                goto done;
            if (flux_msg_push_route (cpy, uuid) < 0)
                goto done;
            if (zmsg_send (&cpy, ov->child->zs) < 0)
                goto done;
            zmsg_destroy (&cpy);
        }
        uuid = zlist_next (uuids);
    }
    rc = 0;
done:
    zlist_destroy (&uuids);
    zmsg_destroy (&cpy);
    return rc;
}

void overlay_set_event (overlay_t ov, const char *fmt, ...)
{
    if (ov->event)
        endpoint_destroy (ov->event);
    va_list ap;
    va_start (ap, fmt);
    ov->event = endpoint_vcreate (fmt, ap);
    va_end (ap);

    ov->event_munge = strstr (ov->event->uri, "pgm://") ? true : false;
}

const char *overlay_get_event (overlay_t ov)
{
    if (!ov->event)
        return NULL;
    return ov->event->uri;
}

void overlay_set_event_cb (overlay_t ov, overlay_cb_f cb, void *arg)
{
    ov->event_cb = cb;
    ov->event_arg = arg;
}

int overlay_sendmsg_event (overlay_t ov, zmsg_t *zmsg)
{
    int rc = -1;
    zmsg_t *cpy = NULL;

    if (!ov->event || !ov->event->zs)
        return 0;
    if (!(cpy = zmsg_dup (zmsg)))
        oom ();
    if (ov->event_munge) {
        if (flux_sec_munge_zmsg (ov->sec, &cpy) < 0) {
            errno = EIO;
            goto done;
        }
    }
    rc = zmsg_send (&cpy, ov->event->zs);
done:
    zmsg_destroy (&cpy);
    return rc;
}

zmsg_t *overlay_recvmsg_event (overlay_t ov)
{
    zmsg_t *zmsg = NULL;
    if (!ov->event || !ov->event->zs) {
        errno = EINVAL;
        goto done;
    }
    zmsg = zmsg_recv (ov->event->zs);
    if (ov->event_munge) {
        if (flux_sec_unmunge_zmsg (ov->sec, &zmsg) < 0) {
            //flux_log (ctx->h, LOG_ERR, "dropping malformed event: %s",
            //          flux_sec_errstr (ctx->sec));
            zmsg_destroy (&zmsg);
            errno = EPROTO;
            goto done;
        }
    }
done:
    return zmsg;
}

void overlay_set_relay (overlay_t ov, const char *fmt, ...)
{
    if (ov->relay)
        endpoint_destroy (ov->relay);
    va_list ap;
    va_start (ap, fmt);
    ov->relay = endpoint_vcreate (fmt, ap);
    va_end (ap);
}

const char *overlay_get_relay (overlay_t ov)
{
    if (!ov->relay)
        return NULL;
    return ov->relay->uri;
}

int overlay_sendmsg_relay (overlay_t ov, zmsg_t *zmsg)
{
    int rc = -1;
    zmsg_t *cpy = NULL;

    if (!ov->relay || !ov->relay->zs) {
        errno = EINVAL;
        goto done;
    }
    if (!(cpy = zmsg_dup (zmsg)))
        oom ();
    rc = zmsg_send (&cpy, ov->relay->zs);
done:
    zmsg_destroy (&cpy);
    return rc;
}

static void child_cb (flux_t h, flux_zmq_watcher_t *w,
                      void *zsock, int revents, void *arg)
{
    overlay_t ov = arg;
    if (ov->child_cb)
        ov->child_cb (ov, zsock, ov->child_arg);
}

static int bind_child (overlay_t ov, struct endpoint *ep)
{
    if (!(ep->zs = zsocket_new (ov->zctx, ZMQ_ROUTER)))
        err_exit ("zsocket_new");
    if (flux_sec_ssockinit (ov->sec, ep->zs) < 0)
        msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ov->sec));
    zsocket_set_hwm (ep->zs, 0);
    if (zsocket_bind (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    if (strchr (ep->uri, '*')) { /* capture dynamically assigned port */
        free (ep->uri);
        ep->uri = zsocket_last_endpoint (ep->zs);
    }
    if (!(ep->w = flux_zmq_watcher_create (ep->zs, FLUX_POLLIN, child_cb, ov)))
        err_exit ("flux_zmq_watcher_create");
    flux_zmq_watcher_start (ov->h, ep->w);
    return 0;
}

static int bind_event_pub (overlay_t ov, struct endpoint *ep)
{
    if (!(ep->zs = zsocket_new (ov->zctx, ZMQ_PUB)))
        err_exit ("zsocket_new");
    if (flux_sec_ssockinit (ov->sec, ep->zs) < 0) /* no-op for epgm */
        msg_exit ("flux_sec_ssockinit: %s", flux_sec_errstr (ov->sec));
    zsocket_set_sndhwm (ep->zs, 0);
    if (zsocket_bind (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s: %s", __FUNCTION__, ep->uri);
    if (strchr (ep->uri, '*')) { /* capture dynamically assigned port */
        free (ep->uri);
        ep->uri = zsocket_last_endpoint (ep->zs);
    }
    return 0;
}

static void event_cb (flux_t h, flux_zmq_watcher_t *w,
                      void *zsock, int revents, void *arg)
{
    overlay_t ov = arg;
    if (ov->event_cb)
        ov->event_cb (ov, zsock, ov->event_arg);
}

static int connect_event_sub (overlay_t ov, struct endpoint *ep)
{
    if (!(ep->zs = zsocket_new (ov->zctx, ZMQ_SUB)))
        err_exit ("zsocket_new");
    if (flux_sec_csockinit (ov->sec, ep->zs) < 0) /* no-op for epgm */
        msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ov->sec));
    zsocket_set_rcvhwm (ep->zs, 0);
    if (zsocket_connect (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    zsocket_set_subscribe (ep->zs, "");
    if (!(ep->w = flux_zmq_watcher_create (ep->zs, FLUX_POLLIN, event_cb, ov)))
        err_exit ("flux_zmq_watcher_create");
    flux_zmq_watcher_start (ov->h, ep->w);
    return 0;
}

static void parent_cb (flux_t h, flux_zmq_watcher_t *w,
                       void *zsock, int revents, void *arg)
{
    overlay_t ov = arg;
    if (ov->parent_cb)
        ov->parent_cb (ov, zsock, ov->parent_arg);
}

static int connect_parent (overlay_t ov, struct endpoint *ep)
{
    int savederr;

    if (!(ep->zs = zsocket_new (ov->zctx, ZMQ_DEALER)))
        goto error;
    if (flux_sec_csockinit (ov->sec, ep->zs) < 0) {
        savederr = errno;
        msg ("flux_sec_csockinit: %s", flux_sec_errstr (ov->sec));
        errno = savederr;
        goto error;
    }
    zsocket_set_hwm (ep->zs, 0);
    zsocket_set_identity (ep->zs, ov->rankstr);
    if (zsocket_connect (ep->zs, "%s", ep->uri) < 0)
        goto error;
    if (!(ep->w = flux_zmq_watcher_create (ep->zs, FLUX_POLLIN, parent_cb, ov)))
        goto error;
    flux_zmq_watcher_start (ov->h, ep->w);
    return 0;
error:
    if (ep->zs) {
        savederr = errno;
        zsocket_destroy (ov->zctx, ep->zs);
        ep->zs = NULL;
        errno = savederr;
    }
    return -1;
}

static int connect_right (overlay_t ov, struct endpoint *ep)
{
    if (!(ep->zs = zsocket_new (ov->zctx, ZMQ_DEALER)))
        err_exit ("zsocket_new");
    if (flux_sec_csockinit (ov->sec, ep->zs) < 0)
        msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ov->sec));
    zsocket_set_hwm (ep->zs, 0);
    zsocket_set_identity (ep->zs, ov->rankstr_right);
    if (zsocket_connect (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    if (!(ep->w = flux_zmq_watcher_create (ep->zs, FLUX_POLLIN, parent_cb, ov)))
        err_exit ("flux_zmq_watcher_create");
    flux_zmq_watcher_start (ov->h, ep->w);
    return 0;
}

int overlay_connect (overlay_t ov)
{
    int rc = -1;
    struct endpoint *ep;

    if (!ov->zctx || !ov->sec || !ov->h || ov->rank == FLUX_NODEID_ANY
                  || !ov->parent_cb || !ov->event_cb) {
        errno = EINVAL;
        goto done;
    }
    if (ov->event && !ov->event->zs && ov->rank > 0)
        connect_event_sub (ov, ov->event);

    if (ov->right && !ov->right->zs)
        connect_right (ov, ov->right);

    if ((ep = zlist_first (ov->parents))) {
        if (connect_parent (ov, ep) < 0)
            err_exit ("%s", ep->uri);
    }
    rc = 0;
done:
    return rc;
}

int overlay_bind (overlay_t ov)
{
    int rc = -1;

    if (!ov->zctx || !ov->sec || !ov->h || ov->rank == FLUX_NODEID_ANY
                  || !ov->child_cb) {
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

/* Establish connection with a new parent and begin using it for all
 * upstream requests.  Leave old parent(s) wired in to reactor to make
 * it possible to transition off a healthy node without losing replies.
 */
int overlay_reparent (overlay_t ov, const char *uri, bool *recycled)
{
    struct endpoint *ep;
    bool old = false;

    if (uri == NULL || !strstr (uri, "://")) {
        errno = EINVAL;
        return -1;
    }
    ep = zlist_first (ov->parents);
    while (ep) {
        if (!strcmp (ep->uri, uri))
            break;
        ep = zlist_next (ov->parents);
    }
    if (ep) {
        zlist_remove (ov->parents, ep);
        old = true;
    } else {
        ep = endpoint_create ("%s", uri);
        if (connect_parent (ov, ep) < 0) {
            endpoint_destroy (ep);
            return -1;
        }
    }
    if (zlist_push (ov->parents, ep) < 0)
        oom ();
    if (recycled)
        *recycled = old;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
