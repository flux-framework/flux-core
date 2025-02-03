/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sdmon.c - create and maintain a list of running flux systemd units
 *
 * This monitors two instances of systemd:
 * - the user one, running as user flux (where jobs are run)
 * - the system one (where housekeeping, prolog, epilog run)
 *
 * A list of units matching flux unit globs is requested at initialization,
 * and a subscription to property updates on those globs is obtained.
 * After the initial list, monitoring is driven solely by property updates.
 *
 * Join the sdmon.online broker group once the unit list responses have been
 * received and there are no Flux units running on the node.  This lets the
 * resource module on rank 0 hold back nodes that require cleanup from the
 * scheduler.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include "ccan/str/str.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/basename.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libsdexec/list.h"
#include "src/common/libsdexec/property.h"
#include "src/common/libsdexec/unit.h"
#include "src/common/libsdexec/state.h"

struct sdmon_bus {
    flux_future_t *fp;      // SERVICE.subscribe
    flux_future_t *fl;      // SERVICE.call ListUnitsByPattern
    bool unmute_property_updates; // set true after list response is received
    const char *service;    // sdbus or sdbus-sys
    const char *unit_glob;
};

struct sdmon_ctx {
    flux_t *h;
    uint32_t rank;
    flux_msg_handler_t **handlers;
    struct sdmon_bus sys;
    struct sdmon_bus usr;
    zhashx_t *units; // unit name => (struct unit *)
    bool group_joined;
    bool cleanup_needed;
    flux_future_t *fg;
};

static const char *path_prefix = "/org/freedesktop/systemd1/unit";

static const char *sys_glob = "flux-*";
static const char *usr_glob = "*shell-*"; // match with and without imp- prefix

static const char *group_name = "sdmon.online";

/* Process a group response.  This is very unlikely to fail but if it does,
 * make sure we get a log message.
 */
static void sdmon_join_continuation (flux_future_t *f, void *arg)
{
    struct sdmon_ctx *ctx = arg;
    if (flux_future_get (f, NULL) < 0) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "groups.join request failed: %s",
                  future_strerror (f, errno));
    }
}

/* Send a broker groups.join request IFF:
 * - we haven't joined yet
 * - both busses have their initial list responses (prop updates unmuted)
 * - the unit hash is empty
 */
static void sdmon_group_join_if_ready (struct sdmon_ctx *ctx)
{
    if (ctx->group_joined
        || !ctx->sys.unmute_property_updates
        || !ctx->usr.unmute_property_updates
        || zhashx_size (ctx->units) > 0)
        return;

    // unit(s) needing cleanup were logged, so indicate they are resolved now.
    ctx->group_joined = true;
    if (ctx->cleanup_needed)
        flux_log (ctx->h, LOG_ERR, "cleanup complete - resources are online");

    flux_future_destroy (ctx->fg);
    if (!(ctx->fg = flux_rpc_pack (ctx->h,
                                   "groups.join",
                                   ctx->rank,
                                   0,
                                   "{s:s}",
                                   "name", group_name))
        || flux_future_then (ctx->fg, -1, sdmon_join_continuation, ctx) < 0)
        flux_log_error (ctx->h, "error sending groups.join request");
}

/* List the units that sdmon thinks are running and their state.substate.
 */
static void sdmon_stats_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct sdmon_ctx *ctx = arg;
    json_t *units;
    struct unit *unit;

    if (!(units = json_array ()))
        goto error;
    unit = zhashx_first (ctx->units);
    while (unit) {
        json_t *o;
        char state[64];

        snprintf (state,
                  sizeof (state),
                  "%s.%s",
                  sdexec_statetostr (sdexec_unit_state (unit)),
                  sdexec_substatetostr (sdexec_unit_substate (unit)));

        if (!(o = json_pack ("{s:s s:s}",
                             "name", sdexec_unit_name (unit),
                             "state", state))
            || json_array_append_new (units, o) < 0) {
            json_decref (o);
            errno = ENOMEM;
            goto error;
        }
        unit = zhashx_next (ctx->units);
    }
    if (flux_respond_pack (h, msg, "{s:O}", "units", units) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (units);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (units);
}

// zhashx_destructor_fn footprint
static void sdmon_unit_destructor (void **item)
{
    if (*item) {
        sdexec_unit_destroy (*item);
        *item = NULL;
    }
}

/* Determine if a unit is considered "running" for purposes of this module.
 */
static bool sdmon_unit_is_running (struct unit *unit)
{
    bool running = false;

    switch (sdexec_unit_state (unit)) {
        case STATE_ACTIVATING:
        case STATE_ACTIVE:
        case STATE_DEACTIVATING:
            running = true;
            break;
        case STATE_UNKNOWN:
        case STATE_INACTIVE:
        case STATE_FAILED:
            break;
    }
    return running;
}

/* A unit matching a subscribed-to glob (on either bus) has changed properties.
 * If it's a new, running unit, add it to the units hash.
 * If it's a known unit that is no longer running, remove it.
 * Join the group if the unit hash transitions to empty.
 */
static void sdmon_property_continuation (flux_future_t *f, void *arg)
{
    struct sdmon_ctx *ctx = arg;
    struct sdmon_bus *bus = f == ctx->usr.fp ? &ctx->usr : &ctx->sys;
    const char *path;
    const char *name;
    json_t *dict;
    struct unit *unit;
    bool unit_is_new = false;


    if (!(path = sdexec_property_changed_path (f))
        || (!(dict = sdexec_property_changed_dict (f)))) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "%s.subscribe: %s",
                  bus->service,
                  future_strerror (f, errno));
        goto fatal;
    }
    if (!bus->unmute_property_updates)
        goto done;
    name = basename_simple (path);
    if (!(unit = zhashx_lookup (ctx->units, name))) {
        if (!(unit = sdexec_unit_create (name))) {
            flux_log_error (ctx->h, "error creating unit %s", name);
            goto done;
        }
        unit_is_new = true;
    }
    if (!sdexec_unit_update (unit, dict) && !unit_is_new)
        goto done; // nothing changed

    if (sdmon_unit_is_running (unit)) {
        if (unit_is_new) {
            if (zhashx_insert (ctx->units, name, unit) < 0) {
                flux_log (ctx->h, LOG_ERR, "error tracking unit %s", name);
                sdexec_unit_destroy (unit);
                goto done;
            }
        }
    }
    else {
        if (unit_is_new)
            sdexec_unit_destroy (unit);
        else
            zhashx_delete (ctx->units, name);
    }
    sdmon_group_join_if_ready (ctx);
done:
    flux_future_reset (f);
    return;
fatal:
    flux_reactor_stop_error (flux_get_reactor (ctx->h));
}

/* Process the initial list of units that match our glob (on either bus).
 * Add any running units to the unit hash, then unmute property updates.
 * Join the group if the unit hash is empty after that.
 */
static void sdmon_list_continuation (flux_future_t *f, void *arg)
{
    struct sdmon_ctx *ctx = arg;
    struct sdmon_bus *bus = f == ctx->usr.fl ? &ctx->usr : &ctx->sys;
    struct unit_info info;

    if (flux_future_get (f, NULL) < 0) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "%s.call: %s",
                  bus->service,
                  future_strerror (f, errno));
        goto fatal;
    }

    while (sdexec_list_units_next (f, &info)) {
        struct unit *unit;

        if (!(unit = sdexec_unit_create (info.name))) {
            flux_log_error (ctx->h, "error creating unit %s", info.name);
            continue;
        }
        (void)sdexec_unit_update_frominfo (unit, &info);
        if (sdmon_unit_is_running (unit)) {
            flux_log (ctx->h,
                      LOG_ERR,
                      "%s needs cleanup - resources are offline",
                      info.name);
            ctx->cleanup_needed = true;
            if (zhashx_insert (ctx->units, info.name, unit) < 0) {
                flux_log_error (ctx->h, "error tracking unit %s", info.name);
                sdexec_unit_destroy (unit);
                continue;
            }
        }
    }
    bus->unmute_property_updates = true;
    sdmon_group_join_if_ready (ctx);
    return;
fatal:
    flux_reactor_stop_error (flux_get_reactor (ctx->h));
}

/* Check if the sdbus module is loaded on the local rank by pinging its
 * stats-get method.  N.B. sdbus handles its D-bus connect asynchronously
 * so stats-get should be responsive even if D-Bus is not.
 */
static int sdbus_is_loaded (flux_t *h,
                            const char *service,
                            uint32_t rank,
                            flux_error_t *error)
{
    flux_future_t *f;
    char topic[256];

    snprintf (topic, sizeof (topic), "%s.stats-get", service);
    if (!(f = flux_rpc (h, topic, NULL, rank, 0))
        || flux_rpc_get (f, NULL) < 0) {
        if (errno == ENOSYS)
            errprintf (error, "%s module is not loaded", service);
        else
            errprintf (error, "%s: %s", service, future_strerror (f, errno));
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

static void sdmon_bus_finalize (struct sdmon_bus *bus)
{
    flux_future_destroy (bus->fp);
    flux_future_destroy (bus->fl);
}

/* Send sdbus.subscribe and sdbus.call (ListUnitsByPatterns).
 * N.B. The subscribe request must be sent before the list request to avoid
 * missing property updates that immediately follow the list response.
 * Set 'bus->unmute_property_updates' after the list response is received.
 * Any property updates received before that are ignored.
*/
static int sdmon_bus_init (struct sdmon_bus *bus,
                           struct sdmon_ctx *ctx,
                           const char *service,
                           const char *pattern,
                           flux_error_t *error)
{
    flux_future_t *fp = NULL;
    flux_future_t *fl = NULL;
    char path[256];

    if (sdbus_is_loaded (ctx->h, service, ctx->rank, error) < 0)
        return -1;
    snprintf (path, sizeof (path), "%s/%s", path_prefix, pattern);
    if (!(fp = sdexec_property_changed (ctx->h, service, ctx->rank, path))
        || flux_future_then (fp, -1, sdmon_property_continuation, ctx) < 0) {
        errprintf (error, "%s.subscribe: %s", service, strerror (errno));
        goto error;
    }
    if (!(fl = sdexec_list_units (ctx->h, service, ctx->rank, pattern))
        || flux_future_then (fl, -1, sdmon_list_continuation, ctx) < 0) {
        errprintf (error, "%s.call: %s", service, strerror (errno));
        goto error;
    }
    bus->service = service;
    bus->fp = fp;
    bus->fl = fl;
    return 0;
error:
    flux_future_destroy (fp);
    flux_future_destroy (fl);
    return -1;
}

static void sdmon_ctx_destroy (struct sdmon_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        sdmon_bus_finalize (&ctx->sys);
        sdmon_bus_finalize (&ctx->usr);
        flux_future_destroy (ctx->fg);
        flux_msg_handler_delvec (ctx->handlers);
        zhashx_destroy (&ctx->units);
        free (ctx);
        errno = saved_errno;
    }
}

static struct sdmon_ctx *sdmon_ctx_create (flux_t *h)
{
    struct sdmon_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (flux_get_rank (h, &ctx->rank) < 0)
        goto error;
    if (!(ctx->units = zhashx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zhashx_set_destructor (ctx->units, sdmon_unit_destructor);
    ctx->h = h;
    return ctx;
error:
    sdmon_ctx_destroy (ctx);
    return NULL;
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,
      "stats-get",
      sdmon_stats_cb,
      0
    },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t *h, int argc, char **argv)
{
    struct sdmon_ctx *ctx;
    flux_error_t error;
    const char *modname = flux_aux_get (h, "flux::name");
    int rc = -1;

    if (!(ctx = sdmon_ctx_create (h)))
        goto error;
    if (flux_msg_handler_addvec_ex (h, modname, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (sdmon_bus_init (&ctx->sys, ctx, "sdbus-sys", sys_glob, &error) < 0)
        goto error;
    if (sdmon_bus_init (&ctx->usr, ctx, "sdbus", usr_glob, &error) < 0)
        goto error;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "reactor exited abnormally");
        goto error;
    }
    rc = 0;
error:
    sdmon_ctx_destroy (ctx);
    return rc;
}

// vi:ts=4 sw=4 expandtab
