/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <flux/core.h>
#include <flux/idset.h>

#include "resource.h"
#include "inventory.h"
#include "drain.h"
#include "rutil.h"
#include "monitor.h"
#include "exclude.h"
#include "reserve.h"
#include "status.h"
#include "reslog.h"

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/librlist/rlist.h"
#include "ccan/str/str.h"

struct status_cache {
    struct rlist *rl;   // exclusions removed
    json_t *R_all;
    json_t *R_down;
};

struct status {
    struct resource_ctx *ctx;
    flux_msg_handler_t **handlers;
    struct flux_msglist *requests;
    struct status_cache cache;
    json_t *R_empty;
};

static void invalidate_cache (struct status_cache *cache, bool all)
{
    if (all) {
        rlist_destroy (cache->rl);
        cache->rl = NULL;
        json_decref (cache->R_all);
        cache->R_all = NULL;
    }
    json_decref (cache->R_down);
    cache->R_down = NULL;
}

static json_t *prepare_status_payload (struct status *status)
{
    struct resource_ctx *ctx = status->ctx;
    const struct idset *down = monitor_get_down (ctx->monitor);
    const struct idset *torpid = monitor_get_torpid (ctx->monitor);
    const struct idset *exclude = exclude_get (ctx->exclude);
    const json_t *R;
    json_t *o = NULL;
    json_t *drain_info = NULL;

    if (!(R = inventory_get (ctx->inventory))
        || !(drain_info = drain_get_info (ctx->drain)))
        goto error;
    if (!(o = json_pack ("{s:O s:O}", "R", R, "drain", drain_info))) {
        errno = ENOMEM;
        goto error;
    }
    if (rutil_set_json_idset (o, "online", monitor_get_up (ctx->monitor)) < 0
        || rutil_set_json_idset (o, "offline", down) < 0
        || rutil_set_json_idset (o, "exclude", exclude) < 0
        || rutil_set_json_idset (o, "torpid", torpid) < 0)
        goto error;
    json_decref (drain_info);
    return o;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    ERRNO_SAFE_WRAP (json_decref, drain_info);
    return NULL;
}

static void status_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct status *status = arg;
    json_t *o = NULL;
    flux_error_t error;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        errprintf (&error, "error decoding request: %s", strerror (errno));
        goto error;
    }
    if (status->ctx->rank != 0) {
        errprintf (&error, "this RPC only works on rank 0");
        errno = EPROTO;
        goto error;
    }
    if (!(o = prepare_status_payload (status))) {
        errprintf (&error, "error preparing response: %s", strerror (errno));
        goto error;
    }
    if (flux_respond_pack (h, msg, "O", o) < 0)
        flux_log_error (h, "error responding to resource.status request");
    json_decref (o);
    return;
error:
    if (flux_respond_error (h, msg, errno, error.text) < 0)
        flux_log_error (h, "error responding to resource.status request");
    json_decref (o);
}

/* Mark the ranks in 'ids' DOWN in the resource set 'rl'.
 */
static int mark_down (struct rlist *rl, const struct idset *ids)
{
    if (ids) {
        char *s;

        if (!(s = idset_encode (ids, IDSET_FLAG_RANGE)))
            return -1;
        if (rlist_mark_down (rl, s) < 0) {
            free (s);
            errno = EINVAL;
            return -1;
        }
        free (s);
    }
    return 0;
}

/* Get an Rv1 resource object that includes all resources.
 */
static const json_t *get_all (struct status *status, const struct rlist *rl)
{

    if (!status->cache.R_all)
        status->cache.R_all = rlist_to_R (rl);
    return status->cache.R_all;
}

/* Get an Rv1 resource object that includes only DOWN resources.
 * This modifies 'rl' but only to mark nodes up/down for rlist_copy_down().
 * The up/down state is not used by other users of cache->rl.
 */
static json_t *get_down (struct status *status, struct rlist *rl)
{
    if (!status->cache.R_down) {
        struct rlist *rdown = NULL;
        struct idset *drain = NULL;
        const struct idset *down = monitor_get_down (status->ctx->monitor);
        const struct idset *torpid = monitor_get_torpid (status->ctx->monitor);

        if ((drain = drain_get (status->ctx->drain))
            && rlist_mark_up (rl, "all") >= 0
            && mark_down (rl, down) == 0
            && mark_down (rl, torpid) == 0
            && mark_down (rl, drain) == 0
            && (rdown = rlist_copy_down (rl)))
            status->cache.R_down = rlist_to_R (rdown);
        idset_destroy (drain);
        rlist_destroy (rdown);
    }
    return status->cache.R_down;
}

/* Create an empty but valid Rv1 object.
 */
static json_t *get_empty_set (void)
{
    struct rlist *rl;
    json_t *o;

    if (!(rl = rlist_create ()))
        return NULL;
    o = rlist_to_R (rl);
    rlist_destroy (rl);
    return o;
}

/* Update property 'name' in 'alloc' resource set.
 * Take the intersection of the alloc ranks vs the property ranks,
 * and if non-empty, add properties to 'alloc' for those ranks.
 */
static int update_one_property (struct rlist *alloc,
                                struct idset *alloc_ranks,
                                struct idset *prop_ranks,
                                const char *name)
{
    struct idset *ids;
    char *targets = NULL;
    int rc = -1;

    if (!(ids = idset_intersect (alloc_ranks, prop_ranks))
        || idset_count (ids) == 0) {
        rc = 0;
        goto done;
    }
    if (!(targets = idset_encode (ids, IDSET_FLAG_RANGE)))
        goto done;
    if (rlist_add_property (alloc, NULL, name, targets) < 0)
        goto done;
    rc = 0;
done:
    free (targets);
    idset_destroy (ids);
    return rc;
}

/* Fetch properties from a resource set in JSON form.
 */
static json_t *get_properties (const struct rlist *rl)
{
    char *s;
    json_t *o = NULL;

    if ((s = rlist_properties_encode (rl)))
        o = json_loads (s, 0, NULL);
    free (s);
    return o;
}

/* Given a resource set 'all' with properties, assign any to 'alloc'
 * that have matching ranks.
 */
static int update_properties (struct rlist *alloc, const struct rlist *all)
{
    struct idset *alloc_ranks;
    json_t *props;
    const char *name;
    json_t *val;

    if (!(alloc_ranks = rlist_ranks (alloc)))
        return -1;
    if (!(props = get_properties (all))
        || json_object_size (props) == 0) {
        json_decref (props);
        idset_destroy (alloc_ranks);
        return 0;
    }
    json_object_foreach (props, name, val) {
        struct idset *prop_ranks;

        if (!(prop_ranks = idset_decode (json_string_value (val))))
            continue;
        if (update_one_property (alloc, alloc_ranks, prop_ranks, name) < 0) {
            idset_destroy (prop_ranks);
            goto error;
        }
        idset_destroy (prop_ranks);
    }
    idset_destroy (alloc_ranks);
    json_decref (props);
    return 0;
error:
    idset_destroy (alloc_ranks);
    json_decref (props);
    return -1;
}

static json_t *update_properties_json (json_t *R, const struct rlist *all)
{
    struct rlist *alloc;
    json_t *R2 = NULL;

    if (!(alloc = rlist_from_json (R, NULL)))
        return NULL;
    if (update_properties (alloc, all) < 0)
        goto done;
    R2 = rlist_to_R (alloc);
done:
    rlist_destroy (alloc);
    return R2;
}

/* Create an rlist object from R.  Omit the scheduling key.  Then:
 * - exclude the ranks in 'exclude' (if non-NULL)
 * - exclude resources in 'reserved' (if non-NULL)
 */
static struct rlist *create_rlist (const json_t *R,
                                   const struct idset *exclude,
                                   const struct rlist *reserved)
{
    json_t *cpy;
    struct rlist *rl;

    if (!(cpy = json_copy ((json_t *)R))) { // thin copy - to del top level key
        errno = ENOMEM;
        return NULL;
    }
    (void)json_object_del (cpy, "scheduling");

    if (!(rl = rlist_from_json (cpy, NULL)))
        goto error;

    if (exclude) {
        if (rlist_remove_ranks (rl, (struct idset *)exclude) < 0)
            goto error;
    }
    if (reserved) {
        if (rlist_subtract (rl, reserved) < 0)
            goto error;
    }
    json_decref (cpy);
    return rl;
error:
    json_decref (cpy);
    rlist_destroy (rl);
    errno = EINVAL;
    return NULL;
}

static struct rlist *get_resource_list (struct status *status)
{
    if (!status->cache.rl) {
        const json_t *R;
        const struct idset *exclude = exclude_get (status->ctx->exclude);
        const struct rlist *reserved = reserve_get (status->ctx->reserve);

        if ((R = inventory_get (status->ctx->inventory)))
            status->cache.rl = create_rlist (R, exclude, reserved);
    }
    return status->cache.rl;
}

/* See issue #5776 for an example of what the sched.resource-status
 * RPC returns.  This payload intended to be identical, except 'allocated'
 * is the calculated set provided by the job manager rather than the actual
 * one from the scheduler itself (for performance reasons).
 */
static json_t *prepare_sched_status_payload (struct status *status,
                                             json_t *allocated)
{
    struct rlist *rl;
    const json_t *all;
    const json_t *down;
    json_t *o;
    json_t *result = NULL;

    if (!(rl = get_resource_list (status))
        || !(all = get_all (status, rl))
        || !(down = get_down (status, rl)))
        goto error;
    if (!(result = json_pack ("{s:O s:O}",
                              "all", all,
                              "down", down))) {
        errno = ENOMEM;
        goto error;
    }
    if (allocated)
        o = update_properties_json (allocated, rl);
    else
        o = json_incref (status->R_empty);
    if (!o || json_object_set_new (result, "allocated", o) < 0) {
        json_decref (o);
        goto error;
    }
    return result;
error:
    ERRNO_SAFE_WRAP (json_decref, result);
    return NULL;
}

static void remove_request (struct flux_msglist *ml, const flux_msg_t *msg)
{
    const flux_msg_t *m;

    m = flux_msglist_first (ml);
    while (m) {
        if (m == msg) {
            flux_msglist_delete (ml); // delete @cursor
            break;
        }
        m = flux_msglist_next (ml);
    }
}

/* The job-manager.resource-status RPC has completed.
 * Finish handling resource.sched-status.  Notes:
 * - Treat ENOSYS from job-manager.resource-status as the empty set.  This
 *   could happen IRL because the resource module loads before job-manager.
 * - Both the future and the message are unreferenced/destroyed
 *   when msg is removed from the status->requests list.
 */
static void sched_status_continuation (flux_future_t *f, void *arg)
{
    const flux_msg_t *msg = flux_future_aux_get (f, "flux::request");
    struct status *status = arg;
    flux_t *h = status->ctx->h;
    flux_error_t error;
    json_t *allocated = NULL;
    json_t *o = NULL;

    if (flux_rpc_get_unpack (f, "{s:o}", "allocated", &allocated) < 0
        && errno != ENOSYS) {
        errprintf (&error,
                   "job-manager.resource-status request failed: %s",
                   future_strerror (f, errno));
        goto error;
    }
    if (!(o = prepare_sched_status_payload (status, allocated))) {
        errprintf (&error, "error preparing response: %s", strerror (errno));
        goto error;
    }
    if (flux_respond_pack (h, msg, "O", o) < 0)
        flux_log_error (h, "error responding to resource.sched-status");
    json_decref (o);
    remove_request (status->requests, msg);
    return;
error:
    if (flux_respond_error (h, msg, EINVAL, error.text) < 0)
        flux_log_error (h, "error responding to resource.sched-status");
    json_decref (o);
    remove_request (status->requests, msg);
}

/* To answer this query, an RPC must be sent to the job manager to get
 * the set of allocated resources.  Get that started, then place the request
 * on status->requests and continue answering in the RPC continuation.
 * The rest of the information required is local.
 */
static void sched_status_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct status *status = arg;
    flux_future_t *f;
    flux_error_t error;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        errprintf (&error, "error decoding request: %s", strerror (errno));
        goto error;
    }
    if (status->ctx->rank != 0) {
        errprintf (&error, "this RPC only works on rank 0");
        errno = EPROTO;
        goto error;
    }
    if (!(f = flux_rpc (h, "job-manager.resource-status", NULL, 0, 0))
        || flux_future_then (f, -1, sched_status_continuation, status) < 0
        || flux_future_aux_set (f, "flux::request", (void *)msg, NULL) < 0
        || flux_msg_aux_set (msg,
                             NULL,
                             f,
                             (flux_free_f)flux_future_destroy) < 0) {
        errprintf (&error,
                   "error sending job-manager.resource-status request: %s",
                   strerror (errno));
        flux_future_destroy (f);
        goto error;
    }
    if (flux_msglist_append (status->requests, msg) < 0) {
        errprintf (&error, "error saving request mesg: %s", strerror (errno));
        goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, error.text) < 0)
        flux_log_error (h, "error responding to resource.sched-status");
}

/* Watch for resource eventlog events that might invalidate cached data.
 * resource-define
 *   could be called in test from 'flux resource reload'
 * resource-update
 *   expiration only at this time - ignore
 * online, offline, drain, undrain
 *   invalidate R_down only
 */
static void reslog_cb (struct reslog *reslog,
                       const char *name,
                       json_t *context,
                       void *arg)
{
    struct status *status = arg;

    if (streq (name, "resource-define"))
        invalidate_cache (&status->cache, true);
    else if (streq (name, "online")
        || streq (name, "offline")
        || streq (name, "drain")
        || streq (name, "undrain")
        || streq (name, "torpid")
        || streq (name, "lively"))
        invalidate_cache (&status->cache, false);
}

/* Disconnect hook called from resource module's main disconnect
 * message handler.
 */
void status_disconnect (struct status *status, const flux_msg_t *msg)
{
    (void)flux_msglist_disconnect (status->requests, msg);
}

static const struct flux_msg_handler_spec htab[] = {
    {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .topic_glob = "resource.status",
        .cb = status_cb,
        .rolemask = FLUX_ROLE_USER,
    },
    {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .topic_glob = "resource.sched-status",
        .cb = sched_status_cb,
        .rolemask = FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void status_destroy (struct status *status)
{
    if (status) {
        int saved_errno = errno;
        flux_msg_handler_delvec (status->handlers);
        flux_msglist_destroy (status->requests);
        reslog_remove_callback (status->ctx->reslog, reslog_cb, status);
        invalidate_cache (&status->cache, true);
        json_decref (status->R_empty);
        free (status);
        errno = saved_errno;
    }
}

struct status *status_create (struct resource_ctx *ctx)
{
    struct status *status;

    if (!(status = calloc (1, sizeof (*status))))
        return NULL;
    status->ctx = ctx;
    if (!(status->requests = flux_msglist_create ()))
        goto error;
    if (flux_msg_handler_addvec (ctx->h, htab, status, &status->handlers) < 0)
        goto error;
    if (ctx->rank == 0) {
        if (reslog_add_callback (ctx->reslog, reslog_cb, status) < 0)
            goto error;
        if (!(status->R_empty = get_empty_set ()))
            goto error;
    }
    return status;
error:
    status_destroy (status);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
