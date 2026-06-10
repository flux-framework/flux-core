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
#include <flux/core.h>
#include <stdlib.h>
#include <string.h>
#include <zmq.h>

#include "src/common/librouter/rpc_track.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libzmqutil/monitor.h"
#include "src/common/libzmqutil/sockopt.h"
#include "src/common/libzmqutil/cert.h"
#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libzmqutil/zwatcher.h"

#include "parent.h"

#define FLUX_ZAP_DOMAIN "flux"

static void parent_monitor_cb (struct zmqutil_monitor *mon, void *arg)
{
    struct parent *parent = arg;
    struct monitor_event event;

    if (zmqutil_monitor_get (mon, &event) == 0) {
        flux_log (parent->h,
                  zmqutil_monitor_iserror (&event) ? LOG_ERR : LOG_DEBUG,
                  "parent sockevent %s %s%s%s",
                  event.endpoint,
                  event.event_str,
                  *event.value_str ? ": " : "",
                  event.value_str);
    }
}

struct parent *parent_create (flux_t *h, uint32_t rank)
{
    struct parent *parent;

    if (!(parent = calloc (1, sizeof (*parent))))
        return NULL;
    parent->h = h;  // borrowed reference
    parent->lastsent = -1;
    parent->rank = rank;
    parent->tracker = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG);
    if (!parent->tracker) {
        free (parent);
        return NULL;
    }
    return parent;
}

void parent_destroy (struct parent *parent)
{
    if (parent) {
        int saved_errno = errno;
        if (parent->zsock)
            zmq_close (parent->zsock);
        free (parent->uri);
        flux_watcher_destroy (parent->w);
        free (parent->pubkey);
        zmqutil_monitor_destroy (parent->monitor);
        rpc_track_destroy (parent->tracker);
        free (parent);
        errno = saved_errno;
    }
}

int parent_set_uri (struct parent *parent, const char *uri)
{
    char *new_uri;

    if (!parent || !uri) {
        errno = EINVAL;
        return -1;
    }
    if (!(new_uri = strdup (uri)))
        return -1;
    free (parent->uri);
    parent->uri = new_uri;
    return 0;
}

int parent_set_pubkey (struct parent *parent, const char *pubkey)
{
    char *new_pubkey;

    if (!parent || !pubkey) {
        errno = EINVAL;
        return -1;
    }
    if (!(new_pubkey = strdup (pubkey)))
        return -1;
    free (parent->pubkey);
    parent->pubkey = new_pubkey;
    return 0;
}

bool parent_error (struct parent *parent)
{
    if (parent) {
        if (((parent->hello_responded && parent->hello_error))
            || parent->offline)
            return true;
    }
    return false;
}

int parent_connect (struct parent *parent,
                    void *zctx,
                    struct cert *cert,
                    const char *uuid,
                    const struct ovconf *config)
{
    if (!parent || !parent->uri || !parent->pubkey) {
        errno = EINVAL;
        return -1;
    }

    if (!(parent->zsock = zmq_socket (zctx, ZMQ_DEALER))
        || zsetsockopt_int (parent->zsock, ZMQ_SNDHWM, 0) < 0
        || zsetsockopt_int (parent->zsock, ZMQ_RCVHWM, 0) < 0
        || zsetsockopt_int (parent->zsock, ZMQ_LINGER, 5) < 0
        || zsetsockopt_int (parent->zsock, ZMQ_IPV6, config->enable_ipv6) < 0
        || zsetsockopt_str (parent->zsock, ZMQ_IDENTITY, uuid) < 0
        || zsetsockopt_str (parent->zsock,
                            ZMQ_ZAP_DOMAIN,
                            FLUX_ZAP_DOMAIN) < 0
        || zsetsockopt_str (parent->zsock,
                            ZMQ_CURVE_SERVERKEY,
                            parent->pubkey) < 0)
        return -1;

    /* The socket monitor is only used for logging.
     * Setup may fail if libzmq is too old.
     */
    if (config->zmqdebug) {
        parent->monitor = zmqutil_monitor_create (zctx,
                                                   parent->zsock,
                                                   flux_get_reactor (parent->h),
                                                   parent_monitor_cb,
                                                   parent);
    }

#ifdef ZMQ_CONNECT_TIMEOUT
    if (config->connect_timeout > 0) {
        if (zsetsockopt_int (parent->zsock,
                             ZMQ_CONNECT_TIMEOUT,
                             config->connect_timeout * 1000) < 0)
            return -1;
    }
#endif

#ifdef ZMQ_TCP_MAXRT
    if (config->tcp_user_timeout > 0) {
        if (zsetsockopt_int (parent->zsock,
                             ZMQ_TCP_MAXRT,
                             config->tcp_user_timeout * 1000) < 0)
            return -1;
    }
#endif

    if (cert_apply (cert, parent->zsock) < 0)
        return -1;

    if (zmq_connect (parent->zsock, parent->uri) < 0)
        return -1;

    flux_log (parent->h, LOG_DEBUG, "connecting to %s", parent->uri);

    return 0;
}

void parent_disconnect (struct parent *parent)
{
    if (parent && parent->zsock) {
        (void)zmq_disconnect (parent->zsock, parent->uri);
        parent->offline = true;
    }
}

int parent_watch (struct parent *parent, flux_watcher_f cb, void *arg)
{
    if (!parent || !parent->zsock) {
        errno = EINVAL;
        return -1;
    }
    if (!(parent->w = zmqutil_watcher_create (flux_get_reactor (parent->h),
                                              parent->zsock,
                                              FLUX_POLLIN,
                                              cb,
                                              arg)))
        return -1;
    flux_watcher_start (parent->w);
    return 0;
}

int parent_sendmsg (struct parent *parent, const flux_msg_t *msg)
{
    if (!parent
        || !parent->zsock
        || parent->offline
        || parent->goodbye_sent) {
        errno = EHOSTUNREACH;
        return -1;
    }
    if (zmqutil_msg_send (parent->zsock, msg) < 0)
        return -1;
    parent->lastsent = flux_reactor_now (flux_get_reactor (parent->h));
    return 0;
}

flux_msg_t *parent_recvmsg (struct parent *parent)
{
    if (!parent || !parent->zsock) {
        errno = EINVAL;
        return NULL;
    }
    return zmqutil_msg_recv (parent->zsock);
}

void parent_set_hello_responded (struct parent *parent, bool error)
{
    if (parent) {
        parent->hello_responded = true;
        parent->hello_error = error;
    }
}

bool parent_hello_responded (struct parent *parent)
{
    return parent && parent->hello_responded;
}

bool parent_goodbye_sent (struct parent *parent)
{
    return parent && parent->goodbye_sent;
}

void parent_set_goodbye_sent (struct parent *parent)
{
    if (parent)
        parent->goodbye_sent = true;
}

// vi:ts=4 sw=4 expandtab
