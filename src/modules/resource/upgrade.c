/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* upgrade.c - update resource.eventlog with new format
 *
 * The resource.eventlog format changed in 0.62.0.  If a 'resource-init'
 * event is found, it is the older format and can be upgraded:
 * - drop all events prior to the last resource-init
 * - convert drain summary of the last resource-init into discrete drain events
 * - remove all remaining events that are no longer valid
 * - add a nodelist to drain/undrain events, if missing
 *
 * If an upgrade occurred, rewrite the kvs resource.eventlog.  This eliminates
 * the risk of drain events referring to the wrong hosts if the rank:host
 * mapping changes in the future.  This rewrite will only occur once as the
 * upgrade code does nothing if a resource-init event is not found and they
 * are no longer produced as of 0.62.0.
 *
 * N.B. the new format consisting only of drain/undrain/resource-define
 * events can be parsed by old flux-core releases so a flux-core downgrade
 * is possible.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/timestamp.h"
#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"
#include "ccan/str/str.h"

#include "resource.h"
#include "reslog.h"
#include "upgrade.h"

static int rewrite_eventlog (flux_t *h, json_t *newlog)
{
    char *s;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    int rc = -1;

    if (!(s = eventlog_encode (newlog))
        || !(txn = flux_kvs_txn_create ())
        || flux_kvs_txn_put (txn, 0, RESLOG_KEY, s) < 0
        || !(f = flux_kvs_commit (h, NULL, 0, txn))
        || flux_rpc_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    ERRNO_SAFE_WRAP (free, s);
    return rc;
}

/* Add nodelist to (un)drain context.
 * If any ranks are invalid, the entire event is thrown out (and logged).
 * See also flux-framework/flux-core#4791.
 */
static int upgrade_drain_context (const char *name,
                                  double ts,
                                  json_t *context,
                                  flux_t *h)
{
    const char *idset;
    const char *nodelist = NULL;
    const char *reason = NULL;

    if (json_unpack (context,
                     "{s:s s?s s?s}",
                     "idset", &idset,
                     "nodelist", &nodelist,
                     "reason", &reason) < 0) {
        flux_log (h,
                  LOG_WARNING,
                  "dropping old %s event with invalid context",
                  name);
        return -1;
    }
    if (!nodelist) {
        char *nl;
        json_t *o = NULL;

        if (!(nl = flux_hostmap_lookup (h, idset, NULL))
            || !(o = json_string (nl))
            || json_object_set_new (context, "nodelist", o) < 0) {
            char timebuf[64] = { 0 };
            timestamp_tostr ((time_t)ts, timebuf, sizeof (timebuf));
            flux_log (h,
                      LOG_WARNING,
                      "dropping old %s event with invalid ranks"
                      " (ranks=%s timestamp=%s UTC reason=%s)",
                      name,
                      idset,
                      timebuf,
                      reason ? reason : "");
            json_decref (o);
            free (nl);
            return -1;
        }
        free (nl);
    }
    return 0;
}

static ssize_t upgrade_insert_index (json_t *eventlog, double timestamp)
{
    size_t index;
    json_t *entry;
    json_array_foreach (eventlog, index, entry) {
        double ts;
        if (eventlog_entry_parse (entry, &ts, NULL, NULL) < 0)
            return -1;
        if (ts >= timestamp)
            return index;
    }
    return 0;
}

static int upgrade_insert_drain_event (json_t *eventlog,
                                       double timestamp,
                                       const char *idset,
                                       const char *nodelist,
                                       const char *reason)
{
    ssize_t index;
    json_t *entry;

    if ((index = upgrade_insert_index (eventlog, timestamp)) < 0)
        return -1;
    if (reason) {
        entry = eventlog_entry_pack (timestamp,
                                     "drain",
                                     "{s:s s:s s:s}",
                                     "idset", idset,
                                     "nodelist", nodelist,
                                     "reason", reason);
        }
    else {
        entry = eventlog_entry_pack (timestamp,
                                     "drain",
                                     "{s:s s:s}",
                                     "idset", idset,
                                     "nodelist", nodelist);
    }
    if (!entry)
        return -1;
    if (json_array_insert_new (eventlog, index, entry) < 0) {
        json_decref (entry);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* Add drain events to the eventlog that are reconstructed from the drain
 * summary object of a legacy 'resource-init' event.
 */
static int upgrade_resource_init (json_t *context, json_t *eventlog, flux_t *h)
{
    const char *idset;
    json_t *drain;
    json_t *o;

    if (json_unpack (context, "{s:o}", "drain", &drain) < 0) {
        errno = EPROTO;
        return -1;
    }
    json_object_foreach (drain, idset, o) {
        double ts;
        char *reason = NULL;
        char *nl;

        if (json_unpack (o,
                         "{s:f s?s}",
                         "timestamp", &ts,
                         "reason", &reason) < 0) {
            errno = EPROTO;
            return -1;
        }
        if (!(nl = flux_hostmap_lookup (h, idset, NULL))) {
            char timebuf[64] = { 0 };
            timestamp_tostr ((time_t)ts, timebuf, sizeof (timebuf));
            flux_log (h,
                      LOG_WARNING,
                      "dropping old drain data with invalid ranks"
                      " (ranks=%s timestamp=%s UTC reason=%s)",
                      idset,
                      timebuf,
                      reason ? reason : NULL);
            continue;
        }
        if (upgrade_insert_drain_event (eventlog, ts, idset, nl, reason) < 0) {
            ERRNO_SAFE_WRAP (free, nl);
            return -1;
        }
        free (nl);
    }
    return 0;
}

int upgrade_eventlog (flux_t *h, json_t **eventlog)
{
    json_t *newlog = NULL;
    ssize_t index;
    json_t *entry;
    double ts;
    const char *name;
    json_t *context;

    if (!*eventlog)
        return 0;
    /* Scan backwards for resource-init.  If not found, nothing to do.
     */
    for (index = json_array_size (*eventlog) - 1; index >= 0; index--) {
        if (!(entry = json_array_get (*eventlog, index))
            || eventlog_entry_parse (entry, NULL, &name, &context) < 0)
            goto parse_error;
        if (streq (name, "resource-init"))
            break;
    }
    if (index < 0)
        return 0;

    /* Create new eventlog containing only expanded drain summary from last
     * resource-init.  Ignore events prior to that one.
     */
    if (!(newlog = json_array ()))
        goto nomem;
    if (upgrade_resource_init (context, newlog, h) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "%s: fatal error processing resource-init on line %zu",
                  RESLOG_KEY,
                  index + 1);
        goto error;
    }

    /* Append more valid events, augmenting drain/undrain with nodelist
     * as needed.
     */
    for (index = index + 1; index < json_array_size (*eventlog); index++) {
        if (!(entry = json_array_get (*eventlog, index))
            || eventlog_entry_parse (entry, &ts, &name, &context) < 0)
            goto parse_error;
        if (streq (name, "drain") || streq (name, "undrain")) {
            // logs any drain/undrain events that could not be upgraded
            if (upgrade_drain_context (name, ts, context, h) < 0)
                continue;
        }
        else if (!streq (name, "resource-define"))
            continue;
        if (json_array_append (newlog, entry) < 0)
            goto nomem;
    }
    if (rewrite_eventlog (h, newlog) < 0)
        goto error;
    size_t oldsize = json_array_size (*eventlog);
    size_t newsize = json_array_size (newlog);
    flux_log (h,
              LOG_INFO,
              "%s: reduced from %zu to %zu entries",
              RESLOG_KEY,
              oldsize,
              newsize);
    json_decref (*eventlog);
    *eventlog = newlog;
    return 0;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, newlog);
    return -1;
parse_error:
    flux_log (h, LOG_ERR, "%s: parse error on line %zu", RESLOG_KEY, index + 1);
    json_decref (newlog);
    errno = EINVAL;
    return -1;
}

// vi:ts=4 sw=4 expandtab
