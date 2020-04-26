/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* acquire.c - let schedulers acquire resources and monitor their availability
 *
 * PROTOCOL
 *
 * Scheduler makes resource.acquire RPC.  Streaming responses are of the form:
 *
 * First response:
 *   {resources:resource_object up:idset}
 * Subsequent responses:
 *   {up?:idset down?:idset}
 *
 * Where:
 * - resource_object maps execution target ids to resources
 *   (see RESOURCE OBJECT) below
 * - idset is a set of execution target ids, encoded as a string.
 *
 * Execution targets that are excluded by configuration are omitted from
 * resource_object in the initial response.  Targets should be considered
 * "down" until they appear as a member of an "up" idset.
 *
 * As execution targets from the resource_object go online or are undrained,
 * they are marked "up".  As they go offline or are drained, they are marked
 * "down".
 *
 * If the exclusion configuration changes, any newly excluded execution
 * targets from the resource_object are marked "down".  On the next
 * scheduler reload, the resource set will omit those targets.
 *
 * RESOURCE OBJECT
 *
 * The hwloc.by_rank object format is used as a placeholder until
 * design work on a proper format is completed.  by_rank is a JSON object
 * whose top-level keys are idsets, and values are objects containing a
 * a flat hwloc inventory, e.g.
 *  {"[0-3]": {"Package": 1, "Core": 2, "PU": 2, "cpuset": "0-1"}}
 *
 * N.B. A scheduler that needs more inventory/hierarchy information than
 * is present in the by_rank inventories MAY fetch resource.hwloc.xml.<rank>
 * for each execution target listed in an idset key.
 *
 * LIMITATIONS
 *
 * Currently, only a single resource.acquire RPC is allowed to be pending
 * at a time.  Upon scheduler unload, the automatically generated disconnect
 * request frees up this slot.  If a scheduler wishes to terminate the RPC
 * sooner, it may send a resource.acquire-cancel RPC containing the matchtag
 * of the resource.acquire RPC.  Per RFC 6, the former does not receive a
 * response, and the latter receives a (terminating) ECANCELED response.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"

#include "resource.h"
#include "reslog.h"
#include "discover.h"
#include "exclude.h"
#include "drain.h"
#include "acquire.h"
#include "monitor.h"
#include "rutil.h"

struct acquire_request {
    struct acquire *acquire;

    const flux_msg_t *msg;              // orig request
    int response_count;                 // count of response messages sent

    json_t *resources;                  // resource object
    struct idset *valid;                // valid targets
    struct idset *up;                   // available targets
};

struct acquire {
    struct resource_ctx *ctx;
    flux_msg_handler_t **handlers;
    struct acquire_request *request;    // N.B. there can be only one currently
};


static void acquire_request_destroy (struct acquire_request *ar)
{
    if (ar) {
        flux_msg_decref (ar->msg);
        json_decref (ar->resources);
        idset_destroy (ar->valid);
        idset_destroy (ar->up);
        free (ar);
    }
}

static struct acquire_request *acquire_request_create (struct acquire *acquire,
                                                       const flux_msg_t *msg)
{
    struct acquire_request *ar;

    if (!(ar = calloc (1, sizeof (*ar))))
        return NULL;
    ar->acquire = acquire;
    ar->msg = flux_msg_incref (msg);
    return ar;
}

/* Initialize request context once resource object is available.
 * This may be called from acquire_cb() or reslog_cb().
 */
static int acquire_request_init (struct acquire_request *ar,
                                 const json_t *resobj)
{
    struct resource_ctx *ctx = ar->acquire->ctx;

    if (resobj == NULL) {
        errno = EINVAL;
        return -1;
    }
    ar->resources = rutil_resobj_sub (resobj, exclude_get (ctx->exclude));
    if (!ar->resources)
        return -1;
    if (!(ar->valid = rutil_idset_from_resobj (ar->resources)))
        return -1;
    if (!(ar->up = idset_copy (ar->valid)))
        return -1;
    if (rutil_idset_sub (ar->up, drain_get (ctx->drain)) < 0)
        return -1;
    if (rutil_idset_sub (ar->up, monitor_get_down (ctx->monitor)) < 0)
        return -1;
    return 0;
}

/* reslog_cb() says 'name' event occurred.
 * If anything changed with respect to target availability, populate
 * up and/or down idsets with the changes.
 * Replace ar->up with new set of available targets.
 */
static int acquire_request_update (struct acquire_request *ar,
                                   const char *name,
                                   struct idset **up,
                                   struct idset **dn)
{
    struct resource_ctx *ctx = ar->acquire->ctx;
    struct idset *new_up;

    if (!(new_up = idset_copy (ar->valid)))
        return -1;
    if (rutil_idset_sub (new_up, drain_get (ctx->drain)) < 0)
        goto error;
    if (rutil_idset_sub (new_up, monitor_get_down (ctx->monitor)) < 0)
        goto error;
    if (rutil_idset_sub (new_up, exclude_get (ctx->exclude)) < 0)
        goto error;
    if (rutil_idset_diff (ar->up, new_up, up, dn) < 0)
        goto error;
    idset_destroy (ar->up);
    ar->up = new_up;
    return 0;
error:
    idset_destroy (new_up);
    return -1;
}

/* Send the first response to resource.acquire request.  This presumes
 * that acquire_request_init() has already prepared ar->resources and ar->up.
 */
static int acquire_respond_first (struct acquire_request *ar)
{
    json_t *o = NULL;

    if (!(o = json_object()))
        goto nomem;
    if (json_object_set (o, "resources", ar->resources) < 0)
        goto nomem;
    if (rutil_set_json_idset (o, "up", ar->up) < 0)
        goto error;
    if (flux_respond_pack (ar->acquire->ctx->h, ar->msg, "O", o) < 0)
        goto error;
    json_decref (o);
    ar->response_count++;
    return 0;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return -1;
}

/* Send a subsequent response to resource.acquire request, driven by
 * reslog_cb().
 */
static int acquire_respond_next (struct acquire_request *ar,
                                 struct idset *up,
                                 struct idset *down)
{
    json_t *o;

    if (!(o = json_object()))
        goto nomem;
    if (up && rutil_set_json_idset (o, "up", up) < 0)
        goto error;
    if (down && rutil_set_json_idset (o, "down", down) < 0)
        goto error;
    if (flux_respond_pack (ar->acquire->ctx->h, ar->msg, "O", o) < 0)
        goto error;
    json_decref (o);
    ar->response_count++;
    return 0;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return -1;
}

/* Handle a resource.acquire request.
 * Currently there is only one request slot.
 * The response is deferred until resources are available.
 */
static void acquire_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct acquire *acquire = arg;
    const char *errmsg = NULL;
    const json_t *resobj;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (acquire->request) {
        errno = EBUSY;
        goto error;
    }
    if (!(acquire->request = acquire_request_create (acquire, msg)))
        goto error;
    if (!(resobj = discover_get (acquire->ctx->discover)))
        return; // defer response until discover event

    if (acquire_request_init (acquire->request, resobj) < 0) {
        acquire_request_destroy (acquire->request);
        acquire->request = NULL;
        goto error;
    }
    if (acquire_respond_first (acquire->request) < 0)
        flux_log_error (h, "error responding to acquire request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to acquire request");
}

/* Cancellation protocol per RFC 6.
 * This RPC does not receive a response.  If a matching resource.acquire
 * RPC is found, it is terminated with an ECANCELED response.
 */
static void cancel_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct acquire *acquire = arg;
    uint32_t matchtag1, matchtag2;

    if (flux_request_unpack (msg, NULL, "{s:i}", "matchtag", &matchtag1) == 0
            && acquire->request != NULL
            && flux_msg_get_matchtag (acquire->request->msg, &matchtag2) == 0
            && matchtag1 == matchtag2
            && rutil_match_request_sender (acquire->request->msg, msg)) {

        if (flux_respond_error (h, acquire->request->msg, ECANCELED, NULL) < 0)
            flux_log_error (h, "error responding to acquire request");
        acquire_request_destroy (acquire->request);
        acquire->request = NULL;
        flux_log (h, LOG_DEBUG, "%s: resource.acquire canceled", __func__);
    }
}

/* If disconnect notification matches acquire->request, destroy the request
 * to make the single request slot available.
 */
void acquire_disconnect (struct acquire *acquire, const flux_msg_t *msg)
{
    flux_t *h = acquire->ctx->h;

    if (acquire->request
            && rutil_match_request_sender (acquire->request->msg, msg)) {
        acquire_request_destroy (acquire->request);
        acquire->request = NULL;
        flux_log (h, LOG_DEBUG, "%s: resource.acquire aborted", __func__);
    }
}

/* An event was committed to resource.eventlog.
 * Generate response to acquire->request as appropriate.
 * FWIW, this function is not called until after the eventlog KVS
 * commit completes.
 */
static void reslog_cb (struct reslog *reslog, const char *name, void *arg)
{
    struct acquire *acquire = arg;
    struct resource_ctx *ctx = acquire->ctx;
    const char *errmsg = NULL;

    flux_log (ctx->h, LOG_DEBUG, "%s: %s event posted", __func__, name);

    if (!acquire->request)
        return;

    if (!strcmp (name, "hwloc-discover-finish")) {
        if (acquire->request->response_count == 0) {
            if (acquire_request_init (acquire->request,
                                      discover_get (ctx->discover)) < 0) {
                errmsg = "error preparing first resource.acquire response";
                goto error;

            }
            if (acquire_respond_first (acquire->request) < 0) {
                flux_log_error (ctx->h,
                                "error responding to resource.acquire (%s)",
                                name);
            }
        }
    }
    else if (!strcmp (name, "online") || !strcmp (name, "offline")
            || !strcmp (name, "exclude") || !strcmp (name, "unexclude")
            || !strcmp (name, "drain") || !strcmp (name, "undrain")) {
        if (acquire->request->response_count > 0) {
            struct idset *up, *dn;
            if (acquire_request_update (acquire->request, name, &up, &dn) < 0) {
                errmsg = "error preparing resource.acquire update response";
                goto error;
            }
            if (up || dn) {
                if (acquire_respond_next (acquire->request, up, dn) < 0) {
                    flux_log_error (ctx->h,
                                    "error responding to resource.acquire (%s)",
                                    name);
                }
                idset_destroy (up);
                idset_destroy (dn);
            }
        }
    }
    return;
error:
    if (flux_respond_error (ctx->h, acquire->request->msg, errno, errmsg) < 0)
        flux_log_error (ctx->h, "error responding to acquire request");
    acquire_request_destroy (acquire->request);
    acquire->request = NULL;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  MODULE_NAME ".acquire", acquire_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  MODULE_NAME ".acquire-cancel", cancel_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void acquire_destroy (struct acquire *acquire)
{
    if (acquire) {
        int saved_errno = errno;
        flux_msg_handler_delvec (acquire->handlers);
        if (acquire->request) {
            if (flux_respond_error (acquire->ctx->h,
                                    acquire->request->msg,
                                    ECANCELED,
                                    "the resource module was unloaded") < 0)
                flux_log_error (acquire->ctx->h,
                                "error responding to acquire request");
            acquire_request_destroy (acquire->request);
        }
        free (acquire);
        errno = saved_errno;
    }
}

struct acquire *acquire_create (struct resource_ctx *ctx)
{
    struct acquire *acquire;

    if (!(acquire = calloc (1, sizeof (*acquire))))
        return NULL;
    acquire->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, acquire, &acquire->handlers) < 0)
        goto error;
    reslog_set_callback (ctx->reslog, reslog_cb, acquire);
    return acquire;
error:
    acquire_destroy (acquire);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
