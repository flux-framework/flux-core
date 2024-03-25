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
#include "src/common/libutil/errprintf.h"
#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/librlist/rlist.h"
#include "ccan/str/str.h"

#include "resource.h"
#include "inventory.h"
#include "reslog.h"
#include "topo.h"
#include "monitor.h"
#include "drain.h"
#include "exclude.h"
#include "acquire.h"
#include "rutil.h"
#include "status.h"

/* Parse [resource] table.
 *
 * exclude = "targets"
 *   Exclude specified broker rank(s) or hosts from scheduling
 *
 * [[resource.confg]]
 *   Resource configuration array
 *
 * path = "/path"
 *   Set path to resource object (if no resource.config array)
 *
 * noverify = true
 *   Skip verification that configured resources match local hwloc
 *
 * norestrict = false
 *   When generating hwloc topology XML, do not restrict to current cpumask
 *
 * no-update-watch = false
 *   For testing purposes, simulate missing job-info.update-watch service
 *   in parent instance by sending to an invalid service name.
 */
static int parse_config (struct resource_ctx *ctx,
                         const flux_conf_t *conf,
                         const char **excludep,
                         json_t **R,
                         bool *noverifyp,
                         bool *norestrictp,
                         bool *no_update_watchp,
                         flux_error_t *errp)
{
    flux_error_t error;
    const char *exclude  = NULL;
    const char *path = NULL;
    int noverify = 0;
    int norestrict = 0;
    int no_update_watch = 0;
    json_t *o = NULL;
    json_t *config = NULL;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?s s?o s?s s?b s?b s?b !}}",
                          "resource",
                            "path", &path,
                            "config", &config,
                            "exclude", &exclude,
                            "norestrict", &norestrict,
                            "noverify", &noverify,
                            "no-update-watch", &no_update_watch) < 0) {
        errprintf (errp,
                   "error parsing [resource] configuration: %s",
                   error.text);
        return -1;
    }
    if (config) {
        struct rlist *rl = rlist_from_config (config, &error);
        if (!rl) {
            errprintf (errp,
                       "error parsing [resource.config] array: %s",
                       error.text);
            return -1;
        }
        if (!(o = rlist_to_R (rl)))
            return errprintf (errp, "rlist_to_R: %s", strerror (errno));
        rlist_destroy (rl);
    }
    else if (path) {
        FILE *f;
        json_error_t e;

        if (!(f = fopen (path, "r"))) {
            errprintf (errp,
                       "%s: %s",
                       path,
                       strerror (errno));
            return -1;
        }
        o = json_loadf (f, 0, &e);
        fclose (f);
        if (!o) {
            errprintf (errp,
                       "%s: %s on line %d",
                       e.source,
                       e.text,
                       e.line);
            return -1;
        }
    }
    if (excludep)
        *excludep = exclude;
    if (noverifyp)
        *noverifyp = noverify ? true : false;
    if (norestrictp)
        *norestrictp = norestrict ? true : false;
    if (no_update_watchp)
        *no_update_watchp = no_update_watch ? true : false;
    if (R)
        *R = o;
    else
        json_decref (o);
    return 0;
}

/* Broker is sending us a new config object because 'flux config reload'
 * was run.  Parse it and respond with human readable errors.
 * At the moment this doesn't do much - just cache the new config.
 */
static void config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct resource_ctx *ctx = arg;
    const flux_conf_t *conf;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_conf_reload_decode (msg, &conf) < 0)
        goto error;
    if (parse_config (ctx, conf, NULL, NULL, NULL, NULL, NULL, &error) < 0) {
        errstr = error.text;
        goto error;
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
 */
static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct resource_ctx *ctx = arg;

    if (ctx->acquire)
        acquire_disconnect (ctx->acquire, msg);
    if (ctx->status)
        status_disconnect (ctx->status, msg);
}

flux_t *resource_parent_handle_open (struct resource_ctx *ctx)
{
    if (!ctx->parent_h) {
        const char *uri = flux_attr_get (ctx->h, "parent-uri");
        if (!uri || !flux_attr_get (ctx->h, "jobid")) {
            errno = ENOENT;
            return NULL;
        }
        if (!(ctx->parent_h = flux_open (uri, 0)))
            flux_log_error (ctx->h, "error opening %s", uri);
    }
    ctx->parent_refcount++;
    return ctx->parent_h;
}

void resource_parent_handle_close (struct resource_ctx *ctx)
{
    if (ctx && --ctx->parent_refcount == 0) {
        flux_close (ctx->parent_h);
        ctx->parent_h = NULL;
    }
}

static void resource_ctx_destroy (struct resource_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        status_destroy (ctx->status);
        acquire_destroy (ctx->acquire);
        drain_destroy (ctx->drain);
        topo_destroy (ctx->topology);
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
        .rolemask = FLUX_ROLE_USER
    },
    FLUX_MSGHANDLER_TABLE_END,
};

/* Post 'resource-init' event that summarizes the current monitor,
 * drain, and exclude state.  For replay purposes, all events prior to the
 * most recent 'resource-init' can be ignored.
 */
int post_restart_event (struct resource_ctx *ctx, int restart)
{
    json_t *o;
    json_t *drain;

    if (!(drain = drain_get_info (ctx->drain)))
        return -1;
    if (!(o = json_pack ("{s:b s:o}", "restart", restart, "drain", drain))) {
        json_decref (drain);
        goto nomem;
    }
    if (rutil_set_json_idset (o, "online", monitor_get_up (ctx->monitor)) < 0)
        goto error;
    if (rutil_set_json_idset (o, "exclude", exclude_get (ctx->exclude)) < 0)
        goto error;
    if (reslog_post_pack (ctx->reslog,
                          NULL,
                          0.,
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
                && streq (name, "resource-init"))
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
                bool *monitor_force_up,
                bool *noverify,
                bool *no_update_watch)
{
    int i;
    for (i = 0; i < argc; i++) {
        /* Test option to force all ranks to be marked online in the initial
         * 'restart' event posted to resource.eventlog.
         */
        if (streq (argv[i], "monitor-force-up"))
            *monitor_force_up = true;
        else if (streq (argv[i], "noverify"))
            *noverify = true;
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
    flux_error_t error;
    const char *exclude_idset;
    json_t *eventlog = NULL;
    bool monitor_force_up = false;
    bool noverify = false;
    bool norestrict = false;
    bool no_update_watch = false;
    json_t *R_from_config;

    if (!(ctx = resource_ctx_create (h)))
        goto error;
    if (flux_get_size (h, &ctx->size) < 0)
        goto error;
    if (flux_get_rank (h, &ctx->rank) < 0)
        goto error;
    if (parse_config (ctx,
                      flux_get_conf (h),
                      &exclude_idset,
                      &R_from_config,
                      &noverify,
                      &norestrict,
                      &no_update_watch,
                      &error) < 0) {
        flux_log (h, LOG_ERR, "%s", error.text);
        goto error;
    }
    if (parse_args (h,
                    argc,
                    argv,
                    &monitor_force_up,
                    &noverify,
                    &no_update_watch) < 0)
        goto error;
    if (flux_attr_get (ctx->h, "broker.recovery-mode"))
        noverify = true;

    /*  Note: Order of creation of resource subsystems is important.
     *  Create inventory on all ranks first, since it is required by
     *  the exclude and drain subsystems on rank 0.
     */
    if (!(ctx->inventory = inventory_create (ctx,
                                             R_from_config,
                                             no_update_watch)))
        goto error;
    /*  Done with R_from_config now, so free it.
     */
    json_decref (R_from_config);
    if (ctx->rank == 0) {
        /*  Create reslog and reload eventlog before initializing
         *  acquire, exclude, and drain subsystems, since these
         *  are required by acquire and exclude.
         */
        if (!(ctx->reslog = reslog_create (h)))
            goto error;
        if (reload_eventlog (h, &eventlog) < 0)
            goto error;
        if (!(ctx->acquire = acquire_create (ctx)))
            goto error;

        /*  Initialize exclude subsystem before drain since drain uses
         *  the exclude idset to ensure drained ranks that are now
         *  excluded are ignored.
         */
        if (!(ctx->exclude = exclude_create (ctx, exclude_idset)))
            goto error;
        if (!(ctx->drain = drain_create (ctx, eventlog)))
            goto error;
    }
    /*  topology is initialized after exclude/drain etc since this
     *  rank may attempt to drain itself due to a topology mismatch.
     */
    if (!(ctx->topology = topo_create (ctx, noverify, norestrict)))
        goto error;
    if (!(ctx->monitor = monitor_create (ctx,
                                         inventory_get_size (ctx->inventory),
                                         monitor_force_up)))
        goto error;
    if (!(ctx->status = status_create (ctx)))
        goto error;
    if (ctx->rank == 0) {
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
    ERRNO_SAFE_WRAP (json_decref, eventlog);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
