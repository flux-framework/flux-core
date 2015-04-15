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
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"

#include "endpt.h"
#include "heartbeat.h"
#include "overlay.h"

void overlay_destroy (overlay_t *ov)
{
    endpt_t *ep;

    if (ov) {
        if (ov->parents) {
            while ((ep = zlist_pop (ov->parents)))
                endpt_destroy (ep);
            zlist_destroy (&ov->parents);
        }
        if (ov->child)
            endpt_destroy (ov->child);
        if (ov->right)
            endpt_destroy (ov->right);
        if (ov->event)
            endpt_destroy (ov->event);
        if (ov->relay)
            endpt_destroy (ov->relay);
        free (ov);
    }
}

overlay_t *overlay_create (void)
{
    overlay_t *ov = xzmalloc (sizeof (*ov));
    if (!(ov->parents = zlist_new ()))
        oom ();
    ov->rank = FLUX_NODEID_ANY;
    ov->parent_lastsent = -1;
    return ov;
}

void overlay_set_zctx (overlay_t *ov, zctx_t *zctx)
{
    ov->zctx = zctx;
}

void overlay_set_sec (overlay_t *ov, flux_sec_t sec)
{
    ov->sec = sec;
}

void overlay_set_rank (overlay_t *ov, uint32_t rank)
{
    ov->rank = rank;
    snprintf (ov->rankstr, sizeof (ov->rankstr), "%u", rank);
    snprintf (ov->rankstr_right, sizeof (ov->rankstr), "%ur", rank);
}

void overlay_set_zloop (overlay_t *ov, zloop_t *zloop)
{
    ov->zloop = zloop;
}

void overlay_set_heartbeat (overlay_t *ov, heartbeat_t *hb)
{
    ov->hb = hb;
}

void overlay_push_parent (overlay_t *ov, const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    endpt_t *ep = endpt_vcreate (fmt, ap);
    va_end (ap);
    if (zlist_push (ov->parents, ep) < 0)
        oom ();
}

const char *overlay_get_parent (overlay_t *ov)
{
    endpt_t *ep = zlist_first (ov->parents);
    if (!ep)
        return NULL;
    return ep->uri;
}

int overlay_sendmsg_parent (overlay_t *ov, zmsg_t **zmsg)
{
    endpt_t *ep = zlist_first (ov->parents);
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
        ov->parent_lastsent = heartbeat_get_epoch (ov->hb);
done:
    return rc;
}

int overlay_keepalive_parent (overlay_t *ov)
{
    int idle = heartbeat_get_epoch (ov->hb) - ov->parent_lastsent;
    zmsg_t *zmsg = NULL;
    int rc = -1;

    if (idle <= 0) { /* FIXME: won't this always be true if called on hb? */
        rc = 0;
        goto done;
    }
    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_KEEPALIVE)))
        goto done;
    if (flux_msg_enable_route (zmsg) < 0)
        goto done;
    rc = overlay_sendmsg_parent (ov, &zmsg);
done:
    zmsg_destroy (&zmsg);
    return rc;
}

void overlay_set_right (overlay_t *ov, const char *fmt, ...)
{
    if (ov->right)
        endpt_destroy (ov->right);
    va_list ap;
    va_start (ap, fmt);
    ov->right = endpt_vcreate (fmt, ap);
    va_end (ap);
}

static bool ring_wrap (overlay_t *ov, zmsg_t *zmsg)
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

int overlay_sendmsg_right (overlay_t *ov, zmsg_t **zmsg)
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

void overlay_set_parent_cb (overlay_t *ov, zloop_fn *cb, void *arg)
{
    ov->parent_cb = cb;
    ov->parent_arg = arg;
}

void overlay_set_child (overlay_t *ov, const char *fmt, ...)
{
    if (ov->child)
        endpt_destroy (ov->child);
    va_list ap;
    va_start (ap, fmt);
    ov->child = endpt_vcreate (fmt, ap);
    va_end (ap);
}

const char *overlay_get_child (overlay_t *ov)
{
    if (!ov->child)
        return NULL;
    return ov->child->uri;
}

void overlay_set_child_cb (overlay_t *ov, zloop_fn *cb, void *arg)
{
    ov->child_cb = cb;
    ov->child_arg = arg;
}

int overlay_sendmsg_child (overlay_t *ov, zmsg_t **zmsg)
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

void overlay_set_event (overlay_t *ov, const char *fmt, ...)
{
    if (ov->event)
        endpt_destroy (ov->event);
    va_list ap;
    va_start (ap, fmt);
    ov->event = endpt_vcreate (fmt, ap);
    va_end (ap);

    ov->event_munge = strstr (ov->event->uri, "pgm://") ? true : false;
}

const char *overlay_get_event (overlay_t *ov)
{
    if (!ov->event)
        return NULL;
    return ov->event->uri;
}

void overlay_set_event_cb (overlay_t *ov, zloop_fn *cb, void *arg)
{
    ov->event_cb = cb;
    ov->event_arg = arg;
}

int overlay_sendmsg_event (overlay_t *ov, zmsg_t **zmsg)
{
    int rc = -1;

    if (!ov->event || !ov->event->zs) {
        errno = EINVAL;
        goto done;
    }
    if (ov->event_munge) {
        if (flux_sec_munge_zmsg (ov->sec, zmsg) < 0) {
            errno = EIO;
            goto done;
        }
    }
    rc = zmsg_send (zmsg, ov->event->zs);
done:
    return rc;
}

zmsg_t *overlay_recvmsg_event (overlay_t *ov)
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

void overlay_set_relay (overlay_t *ov, const char *fmt, ...)
{
    if (ov->relay)
        endpt_destroy (ov->relay);
    va_list ap;
    va_start (ap, fmt);
    ov->relay = endpt_vcreate (fmt, ap);
    va_end (ap);
}

const char *overlay_get_relay (overlay_t *ov)
{
    if (!ov->relay)
        return NULL;
    return ov->relay->uri;
}

int overlay_sendmsg_relay (overlay_t *ov, zmsg_t **zmsg)
{
    int rc = -1;

    if (!ov->relay || !ov->relay->zs) {
        errno = EINVAL;
        goto done;
    }
    rc = zmsg_send (zmsg, ov->relay->zs);
done:
    return rc;
}

static int bind_child (overlay_t *ov, endpt_t *ep)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

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
    zp.socket = ep->zs;
    if (zloop_poller (ov->zloop, &zp, ov->child_cb, ov->child_arg) < 0)
        err_exit ("zloop_poller");
    return 0;
}

static int bind_event_pub (overlay_t *ov, endpt_t *ep)
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

static int connect_event_sub (overlay_t *ov, endpt_t *ep)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(ep->zs = zsocket_new (ov->zctx, ZMQ_SUB)))
        err_exit ("zsocket_new");
    if (flux_sec_csockinit (ov->sec, ep->zs) < 0) /* no-op for epgm */
        msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ov->sec));
    zsocket_set_rcvhwm (ep->zs, 0);
    if (zsocket_connect (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    zsocket_set_subscribe (ep->zs, "");
    zp.socket = ep->zs;
    if (zloop_poller (ov->zloop, &zp, ov->event_cb, ov->event_arg) < 0)
        err_exit ("zloop_poller");
    return 0;
}

static int connect_parent (overlay_t *ov, endpt_t *ep)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };
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
    zp.socket = ep->zs;
    if (zloop_poller (ov->zloop, &zp, ov->parent_cb, ov->parent_arg) < 0)
        goto error;
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

static int connect_right (overlay_t *ov, endpt_t *ep)
{
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .revents = 0, .fd = -1 };

    if (!(ep->zs = zsocket_new (ov->zctx, ZMQ_DEALER)))
        err_exit ("zsocket_new");
    if (flux_sec_csockinit (ov->sec, ep->zs) < 0)
        msg_exit ("flux_sec_csockinit: %s", flux_sec_errstr (ov->sec));
    zsocket_set_hwm (ep->zs, 0);
    zsocket_set_identity (ep->zs, ov->rankstr_right);
    if (zsocket_connect (ep->zs, "%s", ep->uri) < 0)
        err_exit ("%s", ep->uri);
    zp.socket = ep->zs;
    if (zloop_poller (ov->zloop, &zp, ov->parent_cb, ov->parent_arg) < 0)
        err_exit ("zloop_poller");
    return 0;
}

int overlay_connect (overlay_t *ov)
{
    int rc = -1;
    endpt_t *ep;

    if (!ov->zctx || !ov->sec || !ov->zloop || ov->rank == FLUX_NODEID_ANY
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

int overlay_bind (overlay_t *ov)
{
    int rc = -1;

    if (!ov->zctx || !ov->sec || !ov->zloop || ov->rank == FLUX_NODEID_ANY
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
 * upstream requests.  Leave old parent(s) wired in to zloop to make
 * it possible to transition off a healthy node without losing replies.
 */
int overlay_reparent (overlay_t *ov, const char *uri, bool *recycled)
{
    endpt_t *ep;
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
        ep = endpt_create ("%s", uri);
        if (connect_parent (ov, ep) < 0) {
            endpt_destroy (ep);
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
