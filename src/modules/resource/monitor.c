/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* monitor.c - resource monitoring
 *
 * Use the 'state-machine.quorum-monitor' RPC to track broker ranks that come
 * online.  Maintain the set of "up" brokers internally, and call a callback
 * each time that set changes.
 *
 * Post online/offline events with incremental changes to execution
 * target availability since resource-init was posted.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"

#include "resource.h"
#include "reslog.h"
#include "monitor.h"
#include "rutil.h"


struct monitor {
    struct resource_ctx *ctx;
    flux_future_t *f;
    struct idset *up;
    struct idset *down; // only updated on access

    monitor_cb_f cb;
    void *cb_arg;
};

const struct idset *monitor_get_up (struct monitor *monitor)
{
    return monitor->up;
}

const struct idset *monitor_get_down (struct monitor *monitor)
{
    uint32_t size = monitor->ctx->size;
    unsigned int id;

    if (!monitor->down) {
        if (!(monitor->down = idset_create (size, 0)))
            return NULL;
    }
    for (id = 0; id < size; id++) {
        if (idset_test (monitor->up, id))
            (void)idset_clear (monitor->down, id);
        else
            (void)idset_set (monitor->down, id);
    }
    return monitor->down;
}

/* Handle updates to the idset of available brokers.
 * Post online/offline events for any newly up or down ranks.
 * Update monitor->up and call callback if any.
 */
static void quorum_monitor_continuation (flux_future_t *f, void *arg)
{
    struct monitor *monitor = arg;
    flux_t *h = monitor->ctx->h;
    const char *s;
    struct idset *idset, *up, *dn;

    if (flux_rpc_get_unpack (f, "{s:s}", "idset", &s) < 0
            || !(idset = idset_decode (s))) {
        flux_log_error (h, "error parsing quorum-monitor response");
        return;
    }
    if (rutil_idset_diff (monitor->up, idset, &up, &dn) < 0) {
        flux_log_error (h, "error analyzing quorum_monitor response");
        idset_destroy (idset);
        return;
    }
    if (up && idset_count (up) > 0) {
        char *online;
        if (!(online = idset_encode (up, IDSET_FLAG_RANGE))
            || reslog_post_pack (monitor->ctx->reslog,
                                 NULL,
                                 "online",
                                 "{s:s}",
                                 "idset",
                                 online) < 0)
            flux_log_error (h, "error posting online event");
        free (online);
        idset_destroy (up);
    }
    if (dn && idset_count (dn) > 0) {
        char *offline;
        if (!(offline = idset_encode (dn, IDSET_FLAG_RANGE))
            || reslog_post_pack (monitor->ctx->reslog,
                                 NULL,
                                 "offline",
                                 "{s:s}",
                                 "idset",
                                 offline) < 0)
            flux_log_error (h, "error posting offline event");
        free (offline);
        idset_destroy (dn);
    }
    idset_destroy (monitor->up);
    monitor->up = idset;
    if (monitor->cb)
        monitor->cb (monitor, monitor->cb_arg);
    flux_future_reset (monitor->f);
}

/* Send state-machine.quorum-monitor request, process the first response
 * (synchronously), set up continuation for next responses.
 */
static int quorum_monitor_start (struct monitor *monitor)
{
    flux_future_t *f;
    const char *s;
    struct idset *idset = NULL;

    if (!(f = flux_rpc (monitor->ctx->h,
                        "state-machine.quorum-monitor",
                        NULL,
                        0,
                        FLUX_RPC_STREAMING)))
        return -1;
    if (flux_rpc_get_unpack (f, "{s:s}", "idset", &s) < 0)
        goto error;
    if (!(idset = idset_decode (s)))
        goto error;
    flux_future_reset (f);
    if (flux_future_then (f, -1, quorum_monitor_continuation, monitor) < 0)
        goto error;
    monitor->f = f;
    monitor->up = idset;
    return 0;
error:
    ERRNO_SAFE_WRAP (idset_destroy, idset);
    flux_future_destroy (f);
    return -1;
}

void monitor_set_callback (struct monitor *monitor, monitor_cb_f cb, void *arg)
{
    monitor->cb = cb;
    monitor->cb_arg = arg;
}

void monitor_destroy (struct monitor *monitor)
{
    if (monitor) {
        int saved_errno = errno;
        flux_future_destroy (monitor->f);
        idset_destroy (monitor->up);
        idset_destroy (monitor->down);
        free (monitor);
        errno = saved_errno;
    }
}

struct monitor *monitor_create (struct resource_ctx *ctx)
{
    struct monitor *monitor;

    if (!(monitor = calloc (1, sizeof (*monitor))))
        return NULL;
    monitor->ctx = ctx;
    if (quorum_monitor_start (monitor) < 0) {
        flux_log_error (ctx->h, "state-machine.quorum-monitor during initialization");
        goto error;
    }
    return monitor;
error:
    monitor_destroy (monitor);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
