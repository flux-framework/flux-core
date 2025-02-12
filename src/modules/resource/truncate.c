/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
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

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libeventlog/eventlog.h"
#include "ccan/str/str.h"

#include "truncate.h"
#include "drainset.h"
//#include "inventory.h"

/* Truncate event needs to hold dropped information due to
 * truncation of the resource eventlog.
 *
 * Most items are updated directly in the context, which is initialized
 * from the first dropped event (possibly itself a truncate event), then
 * updated with each event as they are dropped during a truncate operation.
 */
struct truncate_info {
    double timestamp;

    /* Only online+torpid idsets and drain info is actively tracked.
     * Other data is held and updated in the event context to avoid
     * unnecessary decode/encode:
     */
    struct idset *online;
    struct idset *torpid;
    struct drainset *drainset;

    json_t *context;
};

void truncate_info_destroy (struct truncate_info *ti)
{
    if (ti) {
        int saved_errno = errno;
        idset_destroy (ti->online);
        idset_destroy (ti->torpid);
        json_decref (ti->context);
        drainset_destroy (ti->drainset);
        free (ti);
        errno = saved_errno;
    }
}

struct truncate_info *truncate_info_create (void)
{
    struct truncate_info *ti;

    if (!(ti = calloc (1, sizeof (*ti)))
        || !(ti->context = json_object ())
        || !(ti->online = idset_create (0, IDSET_FLAG_AUTOGROW))
        || !(ti->torpid = idset_create (0, IDSET_FLAG_AUTOGROW))
        || !(ti->drainset = drainset_create ()))
        goto error;
    return ti;
error:
    truncate_info_destroy (ti);
    return NULL;
}

static int add_idset_from_context (struct idset *idset,
                                   const char *key,
                                   json_t *context)
{
    char *ids;
    if (json_unpack (context, "{s:s}", key, &ids) < 0
        || idset_decode_add (idset, ids, -1, NULL) < 0)
        return -1;
    return 0;
}

static int subtract_idset_from_context (struct idset *idset,
                                        const char *key,
                                        json_t *context)
{
    char *ids;
    if (json_unpack (context, "{s:s}", key, &ids) < 0
        || idset_decode_subtract (idset, ids, -1, NULL) < 0)
        return -1;
    return 0;
}

static int truncate_reset_idsets (struct truncate_info *ti)
{
    if (idset_clear_all (ti->online) < 0
        || idset_clear_all (ti->torpid) < 0)
        return -1;
    return 0;
}

/* Update truncate info with restart event:
 * - update context with restart event context (updates rank, nodelist, online)
 * - then reinitialize ti->online from context.online
 * - clear torpid ranks
 */
static int process_restart (struct truncate_info *ti, json_t *context)
{
    if (json_object_update (ti->context, context) < 0
        || truncate_reset_idsets (ti) < 0
        || add_idset_from_context (ti->online, "online", context) < 0)
        return -1;

    /* No need to update "torpid" in context (if it exists) since the
     * key will be replaced on encode.
     */
    return 0;
}

static int process_truncate (struct truncate_info *ti, json_t *context)
{
    json_t *drain;
    struct drainset *ds = NULL;

    if (json_object_update (ti->context, context) < 0
        || truncate_reset_idsets (ti) < 0
        || add_idset_from_context (ti->online, "online", context) < 0
        || add_idset_from_context (ti->torpid, "torpid", context) < 0
        || json_unpack (context, "{s:o}", "drain", &drain) < 0
        || !(ds = drainset_from_json (drain)))
        return -1;

    drainset_destroy (ti->drainset);
    ti->drainset = ds;

    return 0;
}

static int process_undrain (struct truncate_info *ti, json_t *context)
{
    struct idset *ranks = NULL;
    unsigned int rank;
    const char *ids;
    int rc = -1;

    if (json_unpack (context, "{s:s}", "idset", &ids) < 0
        || !(ranks = idset_decode (ids)))
        return -1;

    rank = idset_first (ranks);
    while (rank != IDSET_INVALID_ID) {
        if (drainset_undrain (ti->drainset, rank) < 0)
            goto out;
        rank = idset_next (ranks, rank);
    }

    rc = 0;
out:
    idset_destroy (ranks);
    return rc;
}

static int process_drain (struct truncate_info *ti, json_t *context)
{
    struct idset *ranks = NULL;
    int overwrite;
    const char *ids;
    const char *reason = "";
    unsigned int rank;
    int rc = -1;

    if (json_unpack (context,
                     "{s:s s?s s:i}",
                     "idset", &ids,
                     "reason", &reason,
                     "overwrite", &overwrite) < 0)
        return -1;

    if (!(ranks = idset_decode (ids))
        || overwrite < 0
        || overwrite > 2)
        goto out;

    rank = idset_first (ranks);
    while (rank != IDSET_INVALID_ID) {
        if (drainset_drain_ex (ti->drainset,
                               rank,
                               ti->timestamp,
                               reason,
                               overwrite) < 0)
            if (errno != EEXIST)
                goto out;
        rank = idset_next (ranks, rank);
    }
    rc = 0;
out:
    idset_destroy (ranks);
    return rc;
}

static int process_resource_define (struct truncate_info *ti,
                                    json_t *context)
{
    /* Add method and R to context */
    // TODO: json_t *R = json_incref (inventory_get (reslog->ctx->inventory));
    json_t *method;

    if (!(method = json_object_get (context, "method"))) {
        errno = EPROTO;
        return -1;
    }
    if (json_object_set (ti->context, "discovery-method", method) < 0)
        return -1;
    return 0;
}

int truncate_info_update (struct truncate_info *ti, json_t *event)
{
    const char *name;
    json_t *context;
    int rc = -1;

    if (!ti || !event) {
        errno = EINVAL;
        return -1;
    }

    if (eventlog_entry_parse (event, &ti->timestamp, &name, &context) < 0)
        return -1;

    if (streq (name, "truncate"))
        rc = process_truncate (ti, context);
    else if (streq (name, "restart"))
        rc = process_restart (ti, context);
    else if (streq (name, "resource-define"))
        rc = process_resource_define (ti, context);
    else if (streq (name, "drain"))
        rc = process_drain (ti, context);
    else if (streq (name, "undrain"))
        rc = process_undrain (ti, context);
    else if (streq (name, "online"))
        rc = add_idset_from_context (ti->online, "idset", context);
    else if (streq (name, "offline"))
        rc = subtract_idset_from_context (ti->online, "idset", context);
    else if (streq (name, "torpid"))
        rc = add_idset_from_context (ti->torpid, "idset", context);
    else if (streq (name, "lively"))
        rc = subtract_idset_from_context (ti->torpid, "idset", context);
    else
        errno = ENOENT;

    if (rc < 0)
        fprintf (stderr, "truncate_info_update %s failed\n", name);
    return rc;
}

json_t *truncate_info_event (struct truncate_info *ti)
{
    char *online = NULL;
    char *torpid = NULL;
    json_t *drainset = NULL;
    json_t *entry = NULL;
    json_t *o = NULL;

    if (!ti) {
        errno = EINVAL;
        return NULL;
    }

    if (!(online = idset_encode (ti->online, IDSET_FLAG_RANGE))
        || !(torpid = idset_encode (ti->torpid, IDSET_FLAG_RANGE))
        || !(drainset = drainset_to_json (ti->drainset))
        || !(o = json_pack ("{s:s s:s s:O}",
                            "online", online,
                            "torpid", torpid,
                            "drain", drainset))
        || json_object_update (ti->context, o) < 0)
        goto error;
    entry = eventlog_entry_pack (ti->timestamp, "truncate", "O", ti->context);
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    ERRNO_SAFE_WRAP (json_decref, drainset);
    ERRNO_SAFE_WRAP (free, torpid);
    ERRNO_SAFE_WRAP (free, online);
    return entry;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
