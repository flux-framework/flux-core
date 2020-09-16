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
 *
 * Populate resource.hwloc and provide access to resource.hwloc.by_rank
 * on demand.
 *
 * At initialization, the eventlog is replayed.  If events (or lack thereof)
 * indicate that resource.hwloc is not already populated, run
 * "flux hwloc reload".  Since that requires all ranks to be up, it is not run
 * until the monitor subsystem says they are.
 *
 * In addition, once resource.hwloc is populated, the 'resource.hwloc.by_rank'
 * object is looked up from the KVS and made available via discover_get().

 * For testing, resource discovery may be defeated by populating
 * resource.hwloc.by_rank with dummy resources and posting a fake
 * hwloc-discover-finish event to 'resource.eventlog' before loading
 * this module.
 *
 * Caveats:
 * - resource.by_rank is a stand-in for future Flux concrete resource object
 * - no support for statically configured resources yet
 * - no support for obtaining resources from enclosing instance
 * - all ranks have to be online before 'flux hwloc reload' can run
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"

#include "resource.h"
#include "reslog.h"
#include "discover.h"
#include "monitor.h"

struct discover {
    struct resource_ctx *ctx;
    flux_subprocess_t *p;
    bool ready;             // all broker ranks are online
    bool loaded;            // resource.hwloc is populated
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
    struct resource_ctx *ctx = discover->ctx;

    if (flux_future_get (f, NULL) < 0)
        flux_log_error (ctx->h, "hwloc.by_rank");
    /* discover->f remains valid so by_rank can be accessed later */
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

/* Post the end time of flux hwloc reload (and success status).
 * This event is parsed by replay_eventlog() below.
 */
static void hwloc_reload_completion (flux_subprocess_t *p)
{
    struct discover *discover = flux_subprocess_aux_get (p, auxkey);
    struct resource_ctx *ctx = discover->ctx;
    int rc;
    int signal = 0;
    const char *cmd = "hwloc-reload";

    if ((rc = flux_subprocess_exit_code (p)) == 0)
        discover->loaded = true;
    else if (rc > 0)
        flux_log (ctx->h, LOG_ERR, "%s exited with rc=%d", cmd, rc);
    else if ((signal = flux_subprocess_signaled (p)) > 0)
        flux_log (ctx->h, LOG_ERR, "%s %s", cmd, strsignal (signal));
    else
        flux_log (ctx->h, LOG_ERR, "%s completed (not signal or exit)", cmd);
    if (reslog_post_pack (ctx->reslog,
                          NULL,
                          "hwloc-discover-finish",
                          "{s:b}",
                          "loaded",
                          discover->loaded ? 1 : 0) < 0)
        flux_log_error (ctx->h, "posting hwloc-discover-finish event");
    if (discover->loaded) {
        if (lookup_hwloc (discover) < 0)
            flux_log_error (ctx->h, "resource.hwloc.by_rank");
    }
    flux_subprocess_destroy (p);
    discover->p = NULL;
}

/* Post the start time of flux hwloc reload (and pid) for debugging.
 */
static void hwloc_reload_state_change (flux_subprocess_t *p,
                                       flux_subprocess_state_t state)
{
    struct discover *discover = flux_subprocess_aux_get (p, auxkey);

    if (state == FLUX_SUBPROCESS_RUNNING) {
        if (reslog_post_pack (discover->ctx->reslog,
                              NULL,
                              "hwloc-discover-start",
                              "{s:i}",
                              "pid",
                              flux_subprocess_pid (discover->p)) < 0)
            flux_log_error (discover->ctx->h,
                            "posting hwloc-discover-start event");
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
 * On not-ready to ready (all brokers up) transition, initiate hwloc
 * discover if not already done.
 */
static void monitor_cb (struct monitor *monitor, void *arg)
{
    struct discover *discover = arg;

    if (!discover->ready) {
        const struct idset *down = monitor_get_down (monitor);
        if (!down || idset_count (down) == 0) {
            discover->ready = true;
            if (!discover->loaded)
                if (hwloc_reload (discover) < 0) {
                    flux_log_error (discover->ctx->h,
                                    "error starting flux hwloc reload");
                }
        }
    }
}

/* If restarting with eventlog, scan it for events that indicate that
 * resource.hwloc is already populated.
 */
static int replay_eventlog (struct discover *discover, const json_t *eventlog)
{
    size_t index;
    json_t *entry;
    const char *name;
    json_t *context;
    int loaded = 0;

    if (eventlog) {
        json_array_foreach (eventlog, index, entry) {
            if (eventlog_entry_parse (entry, NULL, &name, &context) < 0)
                return -1;
            if (!strcmp (name, "resource-init")) {
                if (json_unpack (context,
                                 "{s:b}",
                                 "hwloc-discover",
                                 &loaded) < 0)
                    return -1;
            }
            else if (!strcmp (name, "hwloc-discover-finish")) {
                if (json_unpack (context,
                                 "{s:b}",
                                 "loaded",
                                 &loaded) < 0)
                    return -1;
            }
        }
        discover->loaded = loaded;
    }
    return 0;
}

/* rank 0 broker entered SHUTDOWN state.  If resource discovery is
 * still in progress, ensure that:
 * - running "flux hwloc reload" is terminated
 * - hwloc-discover-finish is posted (loaded=false) to fail resource.acquire
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
        /* hwloc_reload_completion() will post event */
    }
    else if (!discover->loaded) {
        if (reslog_post_pack (discover->ctx->reslog,
                              NULL,
                              "hwloc-discover-finish",
                              "{s:b}",
                              "loaded",
                              0) < 0)
            flux_log_error (h, "Error posting hwloc-discover-finish");
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

struct discover *discover_create (struct resource_ctx *ctx,
                                  const json_t *eventlog)
{
    struct discover *discover;
    const struct idset *down;

    if (!(discover = calloc (1, sizeof (*discover))))
        return NULL;
    discover->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h,
                                 htab,
                                 discover,
                                 &discover->handlers) < 0)
        goto error;
    if (flux_event_subscribe (ctx->h, "shutdown") < 0)
        goto error;
    if (replay_eventlog (discover, eventlog) < 0)
        goto error;
    if (discover->loaded) {
        if (lookup_hwloc (discover) < 0) {
            flux_log_error (ctx->h, "resource.hwloc.by_rank");
            goto error;
        }
    }
    else if (!(down = monitor_get_down (ctx->monitor))
                                            || idset_count (down) == 0) {
        if (hwloc_reload (discover) < 0) {
            flux_log_error (ctx->h, "error starting flux hwloc reload");
            goto error;
        }
    }
    monitor_set_callback (ctx->monitor, monitor_cb, discover);
    return discover;
error:
    discover_destroy (discover);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
