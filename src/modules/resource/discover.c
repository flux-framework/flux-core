/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* discover.c - dynamic resource discovery
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/librlist/rlist.h"
#include "src/common/libeventlog/eventlog.h"

#include "resource.h"
#include "reslog.h"
#include "discover.h"
#include "monitor.h"
#include "inventory.h"

struct discover {
    struct resource_ctx *ctx;
    flux_subprocess_t *p;
    flux_future_t *f;
    flux_msg_handler_t **handlers;
};

static const char *auxkey = "flux::discover";

const json_t *discover_get (struct discover *discover)
{
    json_t *by_rank;

    if (!discover->f)
        return NULL;
    if (flux_kvs_lookup_get_unpack (discover->f, "o", &by_rank) < 0) {
        flux_log_error (discover->ctx->h, "hwloc.by_rank");
        return NULL;
    }
    return by_rank;
}

static void lookup_hwloc_continuation (flux_future_t *f, void *arg)
{
    struct discover *discover = arg;
    flux_t *h = discover->ctx->h;
    const char *by_rank;
    struct rlist *rl;
    json_t *R;

    if (flux_kvs_lookup_get (f, &by_rank) < 0) {
        flux_log_error (h, "hwloc.by_rank");
        return;
    }
    if (!(rl = rlist_from_hwloc_by_rank (by_rank, false))
            || !(R = rlist_to_R (rl))) {
        flux_log (h, LOG_ERR, "error converting from by_rank format");
        rlist_destroy (rl);
        return;
    }
    if (inventory_put (discover->ctx->inventory, R, NULL) < 0)
        flux_log_error (h, "inventory_put");
    json_decref (R);
    rlist_destroy (rl);
}

static int lookup_hwloc (struct discover *discover)
{
    struct resource_ctx *ctx = discover->ctx;

    if (!(discover->f = flux_kvs_lookup (ctx->h,
                                         NULL,
                                         0,
                                         "resource.hwloc.by_rank")))
        return -1;
    if (flux_future_then (discover->f,
                          -1,
                          lookup_hwloc_continuation,
                          discover) < 0) {
        flux_future_destroy (discover->f);
        discover->f = NULL;
        return -1;
    }
    return 0;
}

static void hwloc_reload_completion (flux_subprocess_t *p)
{
    struct discover *discover = flux_subprocess_aux_get (p, auxkey);
    struct resource_ctx *ctx = discover->ctx;
    int rc;
    int signal = 0;
    const char *cmd = "hwloc-reload";

    if ((rc = flux_subprocess_exit_code (p)) == 0) {
        flux_log (ctx->h, LOG_DEBUG, "%s exited with rc=%d", cmd, rc);
        if (lookup_hwloc (discover) < 0)
            flux_log_error (ctx->h, "resource.hwloc.by_rank");
    }
    else if (rc > 0)
        flux_log (ctx->h, LOG_ERR, "%s exited with rc=%d", cmd, rc);
    else if ((signal = flux_subprocess_signaled (p)) > 0)
        flux_log (ctx->h, LOG_ERR, "%s %s", cmd, strsignal (signal));
    else
        flux_log (ctx->h, LOG_ERR, "%s completed (not signal or exit)", cmd);

    flux_subprocess_destroy (p);
    discover->p = NULL;
}

static void hwloc_reload_state_change (flux_subprocess_t *p,
                                       flux_subprocess_state_t state)
{
    struct discover *discover = flux_subprocess_aux_get (p, auxkey);

    if (state == FLUX_SUBPROCESS_RUNNING) {
        flux_log (discover->ctx->h,
                  LOG_DEBUG,
                  "hwloc-reload started pid=%d",
                  flux_subprocess_pid (discover->p));
    }
}

flux_subprocess_ops_t hwloc_reload_ops = {
    .on_completion      = hwloc_reload_completion,
    .on_state_change    = hwloc_reload_state_change,
};

static int hwloc_reload (struct discover *discover)
{
    flux_t *h = discover->ctx->h;
    static char *argv[] = { "flux", "hwloc", "reload", NULL };
    int argc = 3;
    flux_cmd_t *cmd;
    char path[1024];

    if (!(cmd = flux_cmd_create (argc, argv, environ)))
        return -1;
    if (flux_cmd_setcwd (cmd, getcwd (path, sizeof (path))) < 0)
        goto error;
    if (!(discover->p = flux_rexec (h, 0, 0, cmd, &hwloc_reload_ops)))
        goto error;
    if (flux_subprocess_aux_set (discover->p, auxkey, discover, NULL) < 0)
        goto error;
    flux_cmd_destroy (cmd);
    return 0;
error:
    flux_cmd_destroy (cmd);
    return -1;
}

/* This is called when the idset of available brokers changes.
 * Kick off flux hwloc reload if all brokers are up and we've not
 * already done it.
 */
static void monitor_cb (struct monitor *monitor, void *arg)
{
    struct discover *discover = arg;
    const struct idset *down;

    if (inventory_get (discover->ctx->inventory))
        return;

    if (!(down = monitor_get_down (monitor)) || idset_count (down) == 0) {
        if (hwloc_reload (discover) < 0) {
            flux_log_error (discover->ctx->h,
                            "error starting flux hwloc reload");
        }
    }
}

/* rank 0 broker entered SHUTDOWN state.  If resource discovery is
 * still in progress, ensure that it is terminated.
 */
static void shutdown_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct discover *discover = arg;

    if (discover->p) {
        flux_future_t *f = flux_subprocess_kill (discover->p, SIGKILL);
        if (!f)
            flux_log_error (h, "Error killing flux hwloc reload subproc");
        flux_future_destroy (f);
        /*  hwloc_reload_completion() will be called on completion */
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT , "shutdown",  shutdown_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void discover_destroy (struct discover *discover)
{
    if (discover) {
        int saved_errno = errno;
        monitor_set_callback (discover->ctx->monitor, NULL, NULL);
        flux_subprocess_destroy (discover->p);
        flux_future_destroy (discover->f);
        flux_msg_handler_delvec (discover->handlers);
        free (discover);
        errno = saved_errno;
    }
}

struct discover *discover_create (struct resource_ctx *ctx)
{
    struct discover *discover;
    const struct idset *down;

    if (!(discover = calloc (1, sizeof (*discover))))
        return NULL;
    discover->ctx = ctx;

    if (inventory_get (ctx->inventory))
        goto done;

    if (flux_msg_handler_addvec (ctx->h,
                                 htab,
                                 discover,
                                 &discover->handlers) < 0)
        goto error;
    if (flux_event_subscribe (ctx->h, "shutdown") < 0)
        goto error;
    if (!(down = monitor_get_down (ctx->monitor)) || idset_count (down) == 0) {
        if (hwloc_reload (discover) < 0) {
            flux_log_error (ctx->h, "error starting flux hwloc reload");
            goto error;
        }
    }
    monitor_set_callback (ctx->monitor, monitor_cb, discover);
done:
    return discover;
error:
    discover_destroy (discover);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
