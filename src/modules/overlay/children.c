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
#include <zmq.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/librouter/rpc_track.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libzmqutil/sockopt.h"
#include "src/common/libzmqutil/cert.h"
#include "src/common/libzmqutil/monitor.h"
#include "src/common/libzmqutil/zap.h"
#include "src/common/libzmqutil/msg_zsock.h"
#include "src/common/libzmqutil/zwatcher.h"

#include "children.h"

#define FLUX_ZAP_DOMAIN "flux"

static void zaplogger (int severity, const char *message, void *arg)
{
    struct children *ctx = arg;

    flux_log (ctx->h, severity, "%s", message);
}

static void bind_monitor_cb (struct zmqutil_monitor *mon, void *arg)
{
    struct children *ctx = arg;
    struct monitor_event event;

    if (zmqutil_monitor_get (mon, &event) == 0) {
        flux_log (ctx->h,
                  zmqutil_monitor_iserror (&event) ? LOG_ERR : LOG_DEBUG,
                  "child sockevent %s %s%s%s",
                  event.endpoint,
                  event.event_str,
                  *event.value_str ? ": " : "",
                  event.value_str);
    }
}

struct children *children_create (flux_t *h, struct topology *topo)
{
    struct children *ctx;
    int *child_ranks = NULL;
    int count = topology_get_child_ranks (topo, NULL, 0);
    struct child *child;

    if (count < 1) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;  // borrowed reference
    ctx->topo = topo;  // borrowed reference
    ctx->count = count;

    if (!(child_ranks = calloc (ctx->count, sizeof (child_ranks[0]))))
        goto error;
    if (topology_get_child_ranks (topo, child_ranks, ctx->count) < 0)
        goto error;
    if (!(ctx->children = calloc (ctx->count, sizeof (ctx->children[0]))))
        goto error;

    int child_index = 0;
    children_foreach (ctx, child) {
        child->rank = child_ranks[child_index++];
        child->status = SUBTREE_STATUS_OFFLINE;
        monotime (&child->status_timestamp);
        child->tracker = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG);
        if (!child->tracker)
            goto error;

        if (topology_rank_aux_set (topo,
                                   child->rank,
                                   "child",
                                   child,
                                   NULL) < 0)
            goto error;
    }

    if (!(ctx->hash = zhashx_new ()))
        goto error;
    zhashx_set_key_duplicator (ctx->hash, NULL);
    zhashx_set_key_destructor (ctx->hash, NULL);

    free (child_ranks);
    return ctx;

error:
    ERRNO_SAFE_WRAP (free, child_ranks);
    children_destroy (ctx);
    return NULL;
}

void children_destroy (struct children *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        zmqutil_zap_destroy (ctx->zap);
        if (ctx->bind_zsock)
            zmq_close (ctx->bind_zsock);
        flux_watcher_destroy (ctx->bind_w);
        zmqutil_monitor_destroy (ctx->bind_monitor);

        zhashx_destroy (&ctx->hash);
        if (ctx->children) {
            for (int i = 0; i < ctx->count; i++)
                rpc_track_destroy (ctx->children[i].tracker);
            free (ctx->children);
        }
        free (ctx);
        errno = saved_errno;
    }
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

bool child_is_online (struct child *child)
{
    if (child && subtree_is_online (child->status))
        return true;
    return false;
}

struct child *children_lookup (struct children *ctx, const char *id)
{
    if (!ctx || !id)
        return NULL;

    struct child *child;
    children_foreach (ctx, child) {
        if (streq (id, child->uuid))
            return child;
    }
    return NULL;
}

struct child *children_lookup_online (struct children *ctx, const char *id)
{
    if (!ctx || !ctx->hash || !id)
        return NULL;
    return zhashx_lookup (ctx->hash, id);
}

struct child *children_lookup_byrank (struct children *ctx, uint32_t rank)
{
    if (!ctx)
        return NULL;
    return topology_rank_aux_get (ctx->topo, rank, "child");
}

struct child *children_lookup_route (struct children *ctx, uint32_t rank)
{
    int child_rank;

    if (!ctx)
        return NULL;
    child_rank = topology_get_child_route (ctx->topo, rank);
    if (child_rank < 0)
        return NULL;
    return children_lookup_byrank (ctx, child_rank);
}

int children_get_online_count (struct children *ctx)
{
    struct child *child;
    int online = 0;

    if (ctx) {
        children_foreach (ctx, child) {
            if (subtree_is_online (child->status))
                online++;
        }
    }
    return online;
}

bool children_set_status (struct children *ctx,
                          struct child *child,
                          enum subtree_status status,
                          bool *went_offline)
{
    if (!ctx || !child)
        return false;

    if (went_offline)
        *went_offline = false;

    if (child->status != status) {
        /* Manage hash table invariant: online children are in hash */
        if (subtree_is_online (child->status) && !subtree_is_online (status)) {
            zhashx_delete (ctx->hash, child->uuid);
            if (went_offline)
                *went_offline = true;
        }
        else if (!subtree_is_online (child->status) && subtree_is_online (status))
            (void)zhashx_insert (ctx->hash, child->uuid, child);

        child->status = status;
        monotime (&child->status_timestamp);
        return true;
    }
    return false;
}

static int bind_uri (struct children *ctx,
                     const char *uri,
                     char **uri_out)
{
    char *new_uri;

    if (!ctx || !uri || !uri_out) {
        errno = EINVAL;
        return -1;
    }
    if (zmq_bind (ctx->bind_zsock, uri) < 0)
        return -1;

    /* Capture URI after zmq_bind() processing, so it reflects expanded
     * wildcards and normalized addresses.
     */
    if (zgetsockopt_str (ctx->bind_zsock, ZMQ_LAST_ENDPOINT, &new_uri) < 0)
        return -1;

    flux_log (ctx->h, LOG_DEBUG, "listening on %s", new_uri);
    *uri_out = new_uri;
    return 0;
}

int children_bind (struct children *ctx,
                   void *zctx,
                   struct cert *cert,
                   const char *uri,
                   const char *uri2,
                   const struct ovconf *config,
                   char **uri_outp,
                   char **uri2_outp,
                   flux_error_t *errp)
{
    char *uri_out = NULL;
    char *uri2_out = NULL;

    if (!ctx || !zctx || !cert || !uri || !config) {
        errno = EINVAL;
        return errprintf (errp, "one or more NULL arguments");
    }
    if (ctx->bind_zsock) {
        errno = EINVAL;
        return errprintf (errp, "children already bound");
    }

    if (!(ctx->zap = zmqutil_zap_create (zctx, flux_get_reactor (ctx->h)))) {
        return errprintf (errp,
                          "error creating ZAP server: %s",
                          strerror (errno));
    }
    zmqutil_zap_set_logger (ctx->zap, zaplogger, ctx);

    if (!(ctx->bind_zsock = zmq_socket (zctx, ZMQ_ROUTER))
        || zsetsockopt_int (ctx->bind_zsock, ZMQ_SNDHWM, 0) < 0
        || zsetsockopt_int (ctx->bind_zsock,
                            ZMQ_RCVHWM,
                            config->child_rcvhwm) < 0
        || zsetsockopt_int (ctx->bind_zsock, ZMQ_LINGER, 5) < 0
        || zsetsockopt_int (ctx->bind_zsock, ZMQ_ROUTER_MANDATORY, 1) < 0
        || zsetsockopt_int (ctx->bind_zsock, ZMQ_IPV6, config->enable_ipv6) < 0
        || zsetsockopt_str (ctx->bind_zsock, ZMQ_ZAP_DOMAIN, FLUX_ZAP_DOMAIN) < 0
        || zsetsockopt_int (ctx->bind_zsock, ZMQ_CURVE_SERVER, 1) < 0) {
        return errprintf (errp,
                          "error creating zmq ROUTER socket: %s",
                          strerror (errno));
    }

    /* The socket monitor is only used for logging.
     * Setup may fail if libzmq is too old.
     */
    if (config->zmqdebug) {
        ctx->bind_monitor = zmqutil_monitor_create (zctx,
                                                     ctx->bind_zsock,
                                                     flux_get_reactor (ctx->h),
                                                     bind_monitor_cb,
                                                     ctx);
    }

#ifdef ZMQ_TCP_MAXRT
    if (config->tcp_user_timeout > 0) {
        if (zsetsockopt_int (ctx->bind_zsock,
                             ZMQ_TCP_MAXRT,
                             config->tcp_user_timeout * 1000) < 0) {
            return errprintf (errp,
                              "error setting TCP_MAXRT option"
                              " on bind socket: %s",
                              strerror (errno));
        }
    }
#endif

    if (cert_apply (cert, ctx->bind_zsock) < 0) {
        return errprintf (errp,
                          "error setting curve socket options: %s",
                          strerror (errno));
    }

    if (bind_uri (ctx, uri, &uri_out) < 0) {
        return errprintf (errp,
                          "error binding to %s: %s",
                          uri,
                          strerror (errno));
    }

    if (uri2 && bind_uri (ctx, uri2, &uri2_out) < 0) {
        ERRNO_SAFE_WRAP (free, uri_out);
        return errprintf (errp,
                          "error binding to %s: %s",
                          uri2,
                          strerror (errno));
    }
    *uri_outp = uri_out;
    if (uri2_outp)
        *uri2_outp = uri2_out;

    return 0;
}

int children_watch (struct children *ctx,
                    flux_watcher_f cb,
                    void *arg)
{
    if (!ctx || !ctx->bind_zsock) {
        errno = EINVAL;
        return -1;
    }
    if (!(ctx->bind_w = zmqutil_watcher_create (flux_get_reactor (ctx->h),
                                                ctx->bind_zsock,
                                                FLUX_POLLIN,
                                                cb,
                                                arg)))
        return -1;
    flux_watcher_start (ctx->bind_w);
    return 0;
}

int children_sendmsg (struct children *ctx, const flux_msg_t *msg)
{
    if (!ctx) {
        errno = EINVAL;
        return -1;
    }
    /* Just in case a message is routed to children before socket is bound
     * (unlikely!) return "no route to host" here for errno consistency.
     */
    if (!ctx->bind_zsock) {
        errno = EHOSTUNREACH;
        return -1;
    }
    return zmqutil_msg_send_ex (ctx->bind_zsock, msg, true);
}

flux_msg_t *children_recvmsg (struct children *ctx)
{
    if (!ctx || !ctx->bind_zsock) {
        errno = EINVAL;
        return NULL;
    }
    return zmqutil_msg_recv (ctx->bind_zsock);
}

bool children_is_bound (struct children *ctx)
{
    return ctx && ctx->bind_zsock != NULL;
}

int children_authorize (struct children *ctx,
                        const char *name,
                        const char *pubkey)
{
    if (!ctx || !ctx->zap) {
        errno = EINVAL;
        return -1;
    }
    return zmqutil_zap_authorize (ctx->zap, name, pubkey);
}

// vi:ts=4 sw=4 expandtab
