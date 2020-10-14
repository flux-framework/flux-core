/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* resource.c - resource discovery and monitoring service
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"

#include "resource.h"
#include "inventory.h"
#include "reslog.h"
#include "discover.h"
#include "monitor.h"
#include "drain.h"
#include "exclude.h"
#include "acquire.h"
#include "rutil.h"

/* Parse [resource] table.
 *
 * exclude = "idset"
 *   Exclude specified broker rank(s) from scheduling
 *
 * path = "/path"
 *   Set path to resource object
 */
static int parse_config (struct resource_ctx *ctx,
                         const flux_conf_t *conf,
                         const char **excludep,
                         json_t **R,
                         char *errbuf,
                         int errbufsize)
{
    flux_conf_error_t error;
    const char *exclude  = NULL;
    const char *path = NULL;
    json_t *o = NULL;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?:{s?:s s?:s !}}",
                          "resource",
                            "path",
                            &path,
                            "exclude",
                            &exclude) < 0) {
        (void)snprintf (errbuf,
                        errbufsize,
                        "error parsing [resource] configuration: %s",
                        error.errbuf);
        return -1;
    }
    if (path) {
        json_error_t e;

        if (!(o = json_load_file (path, 0, &e))) {
            (void)snprintf (errbuf,
                            errbufsize,
                            "%s: %s on line %d",
                            e.source,
                            e.text,
                            e.line);
            return -1;
        }
        if (!R)
            json_decref (o);
    }

    *excludep = exclude;
    if (R)
        *R = o;
    return 0;
}

/* Broker is sending us a new config object because 'flux config reload'
 * was run.  Parse it and respond with human readable errors.
 * If events are posted, block until they complete so that:
 * - any KVS commit errors are captured by 'flux config reload'
 * - tests can look for eventlog entry after running 'flux config reload'
 */
static void config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct resource_ctx *ctx = arg;
    const flux_conf_t *conf;
    const char *exclude;
    char errbuf[256];
    const char *errstr = NULL;

    if (flux_conf_reload_decode (msg, &conf) < 0)
        goto error;
    if (parse_config (ctx, conf, &exclude, NULL, errbuf, sizeof (errbuf)) < 0) {
        errstr = errbuf;
        goto error;
    }
    if (ctx->rank == 0) {
        if (exclude_update (ctx->exclude,
                            exclude,
                            errbuf,
                            sizeof (errbuf)) < 0) {
            errstr = errbuf;
            goto error;
        }
        if (reslog_sync (ctx->reslog) < 0) {
            errstr = "error posting to eventlog for reconfig";
            goto error;
        }
    }
    if (flux_set_conf (h, flux_conf_incref (conf)) < 0) {
        errstr = "error updating cached configuration";
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

/* Handle client disconnect.
 * Abort a streaming resource.acquire RPC, if it matches.
 */
static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct resource_ctx *ctx = arg;

    if (ctx->acquire)
        acquire_disconnect (ctx->acquire, msg);
}

static void resource_ctx_destroy (struct resource_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        acquire_destroy (ctx->acquire);
        drain_destroy (ctx->drain);
        discover_destroy (ctx->discover);
        monitor_destroy (ctx->monitor);
        exclude_destroy (ctx->exclude);
        reslog_destroy (ctx->reslog);
        inventory_destroy (ctx->inventory);
        flux_msg_handler_delvec (ctx->handlers);
        free (ctx);
        errno = saved_errno;
    }
}

static struct resource_ctx *resource_ctx_create (flux_t *h)
{
    struct resource_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    return ctx;
}

static const struct flux_msg_handler_spec htab[] = {
    {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .topic_glob = "resource.config-reload",
        .cb = config_reload_cb,
        .rolemask = 0
    },
    {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .topic_glob = "resource.disconnect",
        .cb = disconnect_cb,
        .rolemask = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

/* Post 'resource-init' event that summarizes the current discover, monitor,
 * drain, and exclude state.  For replay purposes, all events prior to the
 * most recent 'resource-init' can be ignored.
 */
int post_restart_event (struct resource_ctx *ctx, int restart)
{
    json_t *o;

    if (!(o = json_pack ("{s:b s:b}",
                         "restart",
                         restart,
                         "hwloc-discover",
                         discover_get (ctx->discover) ? 1 : 0)))
        goto nomem;
    if (rutil_set_json_idset (o, "online", monitor_get_up (ctx->monitor)) < 0)
        goto error;
    if (rutil_set_json_idset (o, "drain", drain_get (ctx->drain)) < 0)
        goto error;
    if (rutil_set_json_idset (o, "exclude", exclude_get (ctx->exclude)) < 0)
        goto error;
    if (reslog_post_pack (ctx->reslog,
                          NULL,
                          "resource-init",
                          "O",
                          o) < 0)
        goto error;
    json_decref (o);
    return 0;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return -1;
}

/* Remove entries prior to the most recent 'resource-init' event from
 * 'eventlog'. N.B. they remain in the KVS.
 */
static int prune_eventlog (json_t *eventlog)
{
    size_t index;
    json_t *entry;
    size_t last_entry = json_array_size (eventlog);
    const char *name;

    json_array_foreach (eventlog, index, entry) {
        if (eventlog_entry_parse (entry, NULL, &name, NULL) == 0
                && !strcmp (name, "resource-init"))
            last_entry = index;
    }
    if (last_entry < json_array_size (eventlog)) {
        for (index = 0; index < last_entry; index++) {
            if (json_array_remove (eventlog, 0) < 0)
                return -1;
        }
    }
    return 0;
}

/* Synchronously read resource.eventlog, and parse into
 * a JSON array for replay by the various subsystems.
 * 'eventlog' is set to NULL if it doesn't exist (no error).
 */
static int reload_eventlog (flux_t *h, json_t **eventlog)
{
    flux_future_t *f;
    const char *s;
    json_t *o;

    if (!(f = flux_kvs_lookup (h, NULL, 0, RESLOG_KEY)))
        return -1;
    if (flux_kvs_lookup_get (f, &s) < 0) {
        if (errno != ENOENT) {
            flux_log_error (h, "%s: lookup error", RESLOG_KEY);
            goto error;
        }
        o = NULL;
    }
    else {
        if (!(o = eventlog_decode (s))) {
            flux_log_error (h, "%s: decode error", RESLOG_KEY);
            goto error;
        }
        if (prune_eventlog (o) < 0) {
            flux_log (h, LOG_ERR, "%s: pruning error", RESLOG_KEY);
            ERRNO_SAFE_WRAP (json_decref, o);
            goto error;
        }
    }
    *eventlog = o;
    flux_future_destroy (f);
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

int parse_args (flux_t *h,
                int argc,
                char **argv,
                bool *monitor_force_up)
{
    int i;
    for (i = 0; i < argc; i++) {
        /* Test option to force all ranks to be marked online in the initial
         * 'restart' event posted to resource.eventlog.
         */
        if (!strcmp (argv[i], "monitor-force-up"))
            *monitor_force_up = true;
        else  {
            flux_log (h, LOG_ERR, "unknown option: %s", argv[i]);
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}


int mod_main (flux_t *h, int argc, char **argv)
{
    struct resource_ctx *ctx;
    char errbuf[256];
    const char *exclude_idset;
    json_t *eventlog = NULL;
    bool monitor_force_up = false;
    json_t *R_from_config;

    if (!(ctx = resource_ctx_create (h)))
        goto error;
    if (parse_args (h, argc, argv, &monitor_force_up) < 0)
        goto error;
    if (flux_get_size (h, &ctx->size) < 0)
        goto error;
    if (flux_get_rank (h, &ctx->rank) < 0)
        goto error;
    if (parse_config (ctx,
                      flux_get_conf (h),
                      &exclude_idset,
                      &R_from_config,
                      errbuf,
                      sizeof (errbuf)) < 0) {
        flux_log (h, LOG_ERR, "%s", errbuf);
        goto error;
    }
    if (ctx->rank == 0) {
        if (!(ctx->reslog = reslog_create (h)))
            goto error;
        if (reload_eventlog (h, &eventlog) < 0)
            goto error;
    }
    if (!(ctx->inventory = inventory_create (ctx, R_from_config)))
        goto error;
    if (!(ctx->monitor = monitor_create (ctx, monitor_force_up)))
        goto error;
    if (ctx->rank == 0) {
        if (!(ctx->discover = discover_create (ctx))) // uses monitor
            goto error;
        if (!(ctx->drain = drain_create (ctx, eventlog)))
            goto error;
        if (!(ctx->acquire = acquire_create (ctx)))
            goto error;
        if (!(ctx->exclude = exclude_create (ctx, exclude_idset)))
            goto error;
        if (post_restart_event (ctx, eventlog ? 1 : 0) < 0)
            goto error;
        if (reslog_sync (ctx->reslog) < 0)
            goto error;
    }
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto error;
    }
    resource_ctx_destroy (ctx);
    json_decref (eventlog);
    return 0;
error:
    resource_ctx_destroy (ctx);
    json_decref (eventlog);
    return -1;
}

MOD_NAME ("resource");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
