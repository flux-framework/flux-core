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
 *   {up?:idset down?:idset shrink?:idset}
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
 * "down". If the resource-define method is anything except "configuration",
 * resources will never come back online, so they are added to the "shrink"
 * idset.
 *
 * If the exclusion configuration changes, any newly excluded execution
 * targets from the resource_object are marked "down".  On the next
 * scheduler reload, the resource set will omit those targets.
 *
 * RESOURCE OBJECT
 *
 * The Rv1 format described in RFC 20 is used.
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
#include "src/common/librlist/rlist.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "resource.h"
#include "reslog.h"
#include "inventory.h"
#include "exclude.h"
#include "drain.h"
#include "acquire.h"
#include "monitor.h"
#include "rutil.h"

/* Stored as aux item in request message.
 */
struct acquire_request {
    int response_count;                 // count of response messages sent
    json_t *resources;                  // resource object
    struct idset *valid;                // valid targets
    struct idset *up;                   // available targets
    struct idset *removed;              // targets removed due to shrink
};

struct acquire {
    struct resource_ctx *ctx;
    flux_msg_handler_t **handlers;
    struct flux_msglist *requests;      // N.B. there can be only one currently
    bool mute;                          // suspend responses during shutdown
    bool shrink_down_ranks;             // shrink down ranks in acquire resp
};


static void acquire_request_destroy (struct acquire_request *ar)
{
    if (ar) {
        int saved_errno = errno;
        json_decref (ar->resources);
        idset_destroy (ar->valid);
        idset_destroy (ar->up);
        idset_destroy (ar->removed);
        free (ar);
        errno = saved_errno;
    }
}

/* Initialize request context once resource object is available.
 * This may be called from acquire_cb() or reslog_cb().
 */
static int acquire_request_init (struct acquire_request *ar,
                                 struct acquire *acquire,
                                 json_t *resobj)
{
    struct resource_ctx *ctx = acquire->ctx;
    const struct idset *exclude = exclude_get (ctx->exclude);
    json_error_t e;
    struct rlist *rl;
    struct idset *drain = NULL;

    if (resobj == NULL || !(rl = rlist_from_json (resobj, &e))) {
        errno = EINVAL;
        return -1;
    }
    if (exclude && idset_count (exclude) > 0) {
        (void)rlist_remove_ranks (rl, (struct idset *)exclude);
        if (!(ar->resources = rlist_to_R (rl))) {
            errno = ENOMEM;
            goto error;
        }
    }
    else {
        if (!(ar->resources = json_copy (resobj)))
            goto nomem;
    }
    if (!(ar->valid = rlist_ranks (rl))) // excluded ranks are not valid
        goto nomem;
    if (!(ar->up = idset_copy (ar->valid))) // and up omits excluded ranks
        goto error;
    if (!(drain = drain_get (ctx->drain)))
        goto error;
    if (idset_subtract (ar->up, drain) < 0)
        goto error;
    if (idset_subtract (ar->up, monitor_get_down (ctx->monitor)) < 0)
        goto error;
    if (idset_subtract (ar->up, monitor_get_torpid (ctx->monitor)) < 0)
        goto error;

    /* Remove lost ranks from valid set if acquire->shrink_down_ranks is true:
     */
    if (acquire->shrink_down_ranks) {
        if (!(ar->removed = idset_copy (monitor_get_lost (ctx->monitor)))
            || idset_subtract (ar->valid, ar->removed) < 0)
            goto error;
    }

    rlist_destroy (rl);
    idset_destroy (drain);
    return 0;
nomem:
    errno = ENOMEM;
error:
    rlist_destroy (rl);
    idset_destroy (drain);
    return -1;
}

/* reslog_cb() says 'name' event occurred.
 * If anything changed with respect to target availability, populate
 * up and/or down idsets with the changes.
 * Replace ar->up with new set of available targets.
 */
static int acquire_request_update (struct acquire_request *ar,
                                   struct acquire *acquire,
                                   const char *name,
                                   struct idset **up,
                                   struct idset **dn,
                                   struct idset **shrink)
{
    struct resource_ctx *ctx = acquire->ctx;
    struct idset *new_up;
    struct idset *drain = NULL;
    const struct idset *lost;

    if (!(new_up = idset_copy (ar->valid)))
        return -1;
    if (!(drain = drain_get (ctx->drain)))
        goto error;
    if (idset_subtract (new_up, drain) < 0)
        goto error;
    if (idset_subtract (new_up, monitor_get_down (ctx->monitor)) < 0)
        goto error;
    if (idset_subtract (new_up, monitor_get_torpid (ctx->monitor)) < 0)
        goto error;
    if (idset_subtract (new_up, exclude_get (ctx->exclude)) < 0)
        goto error;
    if (rutil_idset_diff (ar->up, new_up, up, dn) < 0)
        goto error;

    /* If "shrink" is enabled, and there are "lost" ranks, then add ranks
     * that are not already in the removed set to the "shrink" key in this
     * response.
     */
    *shrink = NULL;
    lost = monitor_get_lost (ctx->monitor);
    if (acquire->shrink_down_ranks && idset_count (lost) > 0) {
        struct idset *to_remove;
        if (!(to_remove = idset_difference (lost, ar->removed))
            || idset_add (ar->removed, to_remove))
            goto error;

        /* If there are ranks to remove, subtract them from ar->valid and
         * return them in the shrink key of the acquisition response.
         */
        if (idset_count (to_remove) > 0) {
            if (idset_subtract (ar->valid, to_remove) < 0)
                goto error;
            *shrink = to_remove;
        }
        else
            idset_destroy (to_remove);
    }

    idset_destroy (ar->up);
    ar->up = new_up;
    idset_destroy (drain);
    return 0;
error:
    idset_destroy (new_up);
    idset_destroy (drain);
    return -1;
}

/* Send the first response to resource.acquire request.  This presumes
 * that acquire_request_init() has already prepared ar->resources and ar->up.
 */
static int acquire_respond_first (flux_t *h, const flux_msg_t *msg)
{
    struct acquire_request *ar = flux_msg_aux_get (msg, "acquire");
    json_t *o = NULL;

    if (!(o = json_object()))
        goto nomem;
    if (json_object_set (o, "resources", ar->resources) < 0)
        goto nomem;
    if (rutil_set_json_idset (o, "up", ar->up) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "O", o) < 0)
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
static int acquire_respond_next (flux_t *h,
                                 const flux_msg_t *msg,
                                 struct idset *up,
                                 struct idset *down,
                                 struct idset *shrink)
{
    struct acquire_request *ar = flux_msg_aux_get (msg, "acquire");
    json_t *o;

    if (!(o = json_object()))
        goto nomem;
    if (up && rutil_set_json_idset (o, "up", up) < 0)
        goto error;
    if (down && rutil_set_json_idset (o, "down", down) < 0)
        goto error;
    if (shrink && rutil_set_json_idset (o, "shrink", shrink) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "O", o) < 0)
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
    struct acquire_request *ar;
    json_t *resobj;

    if (!(ar = calloc (1, sizeof (*ar))))
        goto error;
    if (flux_request_decode (msg, NULL, NULL) < 0
        || flux_msg_aux_set (msg,
                             "acquire",
                             ar,
                             (flux_free_f)acquire_request_destroy) < 0) {
        acquire_request_destroy (ar);
        goto error;
    }
    if (flux_msglist_count (acquire->requests) == 1) {
        errno = EBUSY;
        goto error;
    }
    if (flux_msglist_append (acquire->requests, msg) < 0)
        goto error;
    if (!(resobj = inventory_get (acquire->ctx->inventory)))
        return; // defer response until resource-define event

    if (acquire_request_init (ar, acquire, resobj) < 0)
        goto error;
    if (acquire_respond_first (h, msg) < 0)
        flux_log_error (h, "error responding to acquire request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to acquire request");
}

/* Handle resource.acquire-cancel request.
 */
static void cancel_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct acquire *acquire = arg;
    int count;

    if ((count = flux_msglist_cancel (h, acquire->requests, msg)) < 0)
        flux_log_error (h, "error handling discnonect request");
    if (count > 0)
        flux_log (h, LOG_DEBUG, "canceled %d resource.acquire", count);
}

/* Suspend resource.acquire responses during shutdown
 */
static void mute_cb (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    struct acquire *acquire = arg;
    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    acquire->mute = true;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to acquire-mute request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to acquire-mute request");
}

/* Handle resource.disconnect message.
 */
void acquire_disconnect (struct acquire *acquire, const flux_msg_t *msg)
{
    if (acquire) { // acquire is NULL on rank > 0
        flux_t *h = acquire->ctx->h;
        int count;

        if ((count = flux_msglist_disconnect (acquire->requests, msg)) < 0)
            flux_log_error (h, "error handling discnonect request");
        if (count > 0)
            flux_log (h, LOG_DEBUG, "aborted %d resource.acquire(s)", count);
    }
}

/* An event was committed to resource.eventlog.
 * Generate response to acquire requests as appropriate.
 * FWIW, this function is not called until after the eventlog KVS
 * commit completes.
 */
static void reslog_cb (struct reslog *reslog,
                       const char *name,
                       json_t *context,
                       void *arg)
{
    struct acquire *acquire = arg;
    struct resource_ctx *ctx = acquire->ctx;
    flux_t *h = ctx->h;
    const char *errmsg = NULL;
    json_t *resobj;
    const flux_msg_t *msg;
    const char *method;

    /* Enable "shrink" of ranks that transition from online->offline
     * if resource-define method is anything but "configuration".
     */
    if (streq (name, "resource-define")
        && json_unpack (context, "{s:s}", "method", &method) == 0
        && !streq (method, "configuration"))
        acquire->shrink_down_ranks = true;

    if (acquire->mute)
        return;

    msg = flux_msglist_first (acquire->requests);
    while (msg) {
        struct acquire_request *ar = flux_msg_aux_get (msg, "acquire");

        if (streq (name, "resource-define")) {
            if (ar->response_count == 0) {
                if (!(resobj = inventory_get (ctx->inventory))) {
                    errmsg = "resource discovery failed or interrupted";
                    errno = ENOENT;
                    goto error;
                }
                if (acquire_request_init (ar, acquire, resobj) < 0) {
                    errmsg = "error preparing first resource.acquire response";
                    goto error;

                }
                if (acquire_respond_first (h, msg) < 0) {
                    flux_log_error (h,
                                    "error responding to resource.acquire (%s)",
                                    name);
                }
            }
        }
        else if (streq (name, "resource-update")) {
            double expiration = -1.;

            /*  Handle resource-update event. Currently the only supported
             *  context of such an event is an expiration update
             */
            if (json_unpack (context,
                             "{s?F}",
                             "expiration", &expiration) < 0) {
                errmsg = "error preparing resource.acquire update response";
                goto error;
            }
            if (expiration >= 0.
                && flux_respond_pack (h,
                                      msg,
                                      "{s:f}",
                                      "expiration", expiration) < 0) {
                    flux_log_error (h,
                                    "error responding to resource.acquire (%s)",
                                    name);
                    goto error;
            }
        }
        else if (streq (name, "online")
                 || streq (name, "offline")
                 || streq (name, "drain")
                 || streq (name, "undrain")
                 || streq (name, "torpid")
                 || streq (name, "lively")) {

            if (ar->response_count > 0) {
                struct idset *up, *dn, *shrink;

                if (acquire_request_update (ar,
                                            acquire,
                                            name,
                                            &up,
                                            &dn,
                                            &shrink) < 0) {
                    errmsg = "error preparing resource.acquire update response";
                    goto error;
                }
                if (up || dn || shrink) {
                    if (acquire_respond_next (h, msg, up, dn, shrink) < 0) {
                        flux_log_error (h,
                                    "error responding to resource.acquire (%s)",
                                    name);
                    }
                }
                idset_destroy (up);
                idset_destroy (dn);
                idset_destroy (shrink);
            }
        }
        msg = flux_msglist_next (acquire->requests);
    }

    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to acquire request");
    flux_msglist_delete (acquire->requests);
}

int acquire_clients (struct acquire *acquire)
{
    return flux_msglist_count (acquire->requests);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "resource.acquire", acquire_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  "resource.acquire-cancel", cancel_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  "resource.acquire-mute", mute_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void acquire_destroy (struct acquire *acquire)
{
    if (acquire) {
        int saved_errno = errno;

        flux_msg_handler_delvec (acquire->handlers);
        reslog_remove_callback (acquire->ctx->reslog, reslog_cb, acquire);

        if (acquire->requests) {
            const flux_msg_t *msg;
            flux_t *h = acquire->ctx->h;

            msg = flux_msglist_first (acquire->requests);
            while (msg) {
                if (flux_respond_error (h,
                                        msg,
                                        ECANCELED,
                                        "the resource module was unloaded") < 0)
                    flux_log_error (h, "error responding to acquire request");
                flux_msglist_delete (acquire->requests);
                msg = flux_msglist_next (acquire->requests);
            }
            flux_msglist_destroy (acquire->requests);
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
    if (!(acquire->requests = flux_msglist_create ()))
        goto error;
    if (flux_msg_handler_addvec (ctx->h,
                                 htab,
                                 acquire,
                                 &acquire->handlers) < 0)
        goto error;
    if (reslog_add_callback (ctx->reslog, reslog_cb, acquire) < 0)
        goto error;
    return acquire;
error:
    acquire_destroy (acquire);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
