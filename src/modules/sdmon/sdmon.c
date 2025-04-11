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
#include "ccan/array_size/array_size.h"
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
    struct sdmon_ctx *ctx;
    zhashx_t *units; // unit name => (struct unit *)
};

struct sdmon_ctx {
    flux_t *h;
    uint32_t rank;
    flux_msg_handler_t **handlers;
    struct sdmon_bus sys;
    struct sdmon_bus usr;
    bool group_joined;
    bool cleanup_needed;
    flux_future_t *fg;
};

static void sdmon_bus_restart (struct sdmon_bus *bus);

static const char *path_prefix = "/org/freedesktop/systemd1/unit";

static const char *sys_glob = "flux-*";
static const char *usr_glob = "*shell-*"; // match with and without imp- prefix

static const char *unit_allow[] = {
    "flux-housekeeping",
    "flux-prolog",
    "flux-epilog",
    "imp-shell-",
    "shell-",
};

static const char *group_name = "sdmon.online";

static bool match_unit_name (const char *name)
{
    for (int i = 0; i < ARRAY_SIZE (unit_allow); i++) {
        if (strstarts (name, unit_allow[i]))
            return true;
    }
    return false;
}

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
 * - the unit hashes are empty
 */
static void sdmon_group_join_if_ready (struct sdmon_ctx *ctx)
{
    if (ctx->group_joined
        || !ctx->sys.unmute_property_updates
        || !ctx->usr.unmute_property_updates
        || zhashx_size (ctx->sys.units) > 0
        || zhashx_size (ctx->usr.units) > 0)
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

static int add_units (json_t *units, struct sdmon_bus *bus)
{
    struct unit *unit;

    unit = zhashx_first (bus->units);
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
            return -1;
        }
        unit = zhashx_next (bus->units);
    }
    return 0;
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

    if (!(units = json_array ())
        || add_units (units, &ctx->usr) < 0
        || add_units (units, &ctx->sys) < 0)
        goto error;
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
                  errno == EAGAIN ? LOG_INFO : LOG_ERR,
                  "%s: %s",
                  bus->service,
                  future_strerror (f, errno));
        if (errno == EAGAIN)
            goto restart;
        goto fatal;
    }
    if (!bus->unmute_property_updates)
        goto done;
    name = basename_simple (path);
    if (!match_unit_name (name))
        goto done;
    if (!(unit = zhashx_lookup (bus->units, name))) {
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
            if (zhashx_insert (bus->units, name, unit) < 0) {
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
            zhashx_delete (bus->units, name);
    }
    sdmon_group_join_if_ready (ctx);
done:
    flux_future_reset (f);
    return;
restart:
    sdmon_bus_restart (bus);
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
                  errno == EAGAIN ? LOG_INFO : LOG_ERR,
                  "%s.call: %s",
                  bus->service,
                  future_strerror (f, errno));
        if (errno == EAGAIN)
            goto restart;
        goto fatal;
    }
    while (sdexec_list_units_next (f, &info)) {
        struct unit *unit;

        if (!match_unit_name (info.name))
            continue;
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
            if (zhashx_insert (bus->units, info.name, unit) < 0) {
                flux_log_error (ctx->h, "error tracking unit %s", info.name);
                sdexec_unit_destroy (unit);
                continue;
            }
        }
    }
    bus->unmute_property_updates = true;
    sdmon_group_join_if_ready (ctx);
    return;
restart:
    sdmon_bus_restart (bus);
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
    zhashx_destroy (&bus->units);
}

/* Send sdbus.subscribe and sdbus.call (ListUnitsByPatterns).
 * N.B. The subscribe request must be sent before the list request to avoid
 * missing property updates that immediately follow the list response.
 * Set 'bus->unmute_property_updates' after the list response is received.
 * Any property updates received before that are ignored.
*/
static int sdmon_bus_start (struct sdmon_bus *bus, flux_error_t *error)
{
    struct sdmon_ctx *ctx = bus->ctx;
    flux_future_t *fp = NULL;
    flux_future_t *fl = NULL;
    char path[256];


    snprintf (path, sizeof (path), "%s/%s", path_prefix, bus->unit_glob);
    if (!(fp = sdexec_property_changed (ctx->h, bus->service, ctx->rank, path))
        || flux_future_then (fp, -1, sdmon_property_continuation, ctx) < 0) {
        errprintf (error, "%s.subscribe: %s", bus->service, strerror (errno));
        goto error;
    }
    if (!(fl = sdexec_list_units (ctx->h,
                                  bus->service,
                                  ctx->rank,
                                  bus->unit_glob))
        || flux_future_then (fl, -1, sdmon_list_continuation, ctx) < 0) {
        errprintf (error, "%s.call: %s", bus->service, strerror (errno));
        goto error;
    }
    bus->fp = fp;
    bus->fl = fl;
    return 0;
error:
    flux_future_destroy (fp);
    flux_future_destroy (fl);
    return -1;
}

/* This bus is Bantha poodoo.  sdbus blocks this request while it retries
 * the connect to d-bus, so there is no need to backoff/retry here.
 */
static void sdmon_bus_restart (struct sdmon_bus *bus)
{
    flux_error_t error;

    flux_log (bus->ctx->h,
              LOG_INFO,
              "%s: restarting bus monitor after non-fatal error",
              bus->service);

    flux_future_destroy (bus->fp);
    flux_future_destroy (bus->fl);
    bus->fp = bus->fl = NULL;

    bus->unmute_property_updates = false;
    zhashx_purge (bus->units);

    if (sdmon_bus_start (bus, &error) < 0) {
        flux_log (bus->ctx->h, LOG_ERR, "%s", error.text);
        flux_reactor_stop_error (flux_get_reactor (bus->ctx->h));
    }
}

static int sdmon_bus_initialize (struct sdmon_bus *bus,
                                 struct sdmon_ctx *ctx,
                                 const char *service,
                                 const char *unit_glob)
{
    bus->service = service;
    bus->unit_glob = unit_glob;
    bus->ctx = ctx;
    if (!(bus->units = zhashx_new ())) {
        errno = ENOMEM;
        return -1;
    }
    zhashx_set_destructor (bus->units, sdmon_unit_destructor);
    return 0;
}


static void sdmon_ctx_destroy (struct sdmon_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        sdmon_bus_finalize (&ctx->sys);
        sdmon_bus_finalize (&ctx->usr);
        flux_future_destroy (ctx->fg);
        flux_msg_handler_delvec (ctx->handlers);
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
    if (sdbus_is_loaded (h, "sdbus-sys", ctx->rank, &error) < 0
        || sdbus_is_loaded (h, "sdbus-sys", ctx->rank, &error) < 0) {
        flux_log_error (h, "%s", error.text);
        goto error;
    }
    if (sdmon_bus_initialize (&ctx->sys, ctx, "sdbus-sys", sys_glob) < 0
        || sdmon_bus_initialize (&ctx->usr, ctx, "sdbus", usr_glob) < 0) {
        flux_log_error (h, "failed to initialize bus objects");
        goto error;
    }
    if (sdmon_bus_start (&ctx->sys, &error) < 0
        || sdmon_bus_start (&ctx->usr, &error) < 0) {
        flux_log (h, LOG_ERR, "%s", error.text);
        goto error;
    }
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
