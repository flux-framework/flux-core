/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* topo.c - load and verify the local rank's hwloc topology
 *
 * If resources are known at module load time, verify the topology against
 * this rank's portion of the resource object (unless noverify is set).
 *
 * Reduce r_local from each rank, leaving the result in topo->reduce->rl
 * on rank 0.  If resources are not known, then this R is set in inventory.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/librlist/rhwloc.h"
#include "src/common/librlist/rlist.h"
#include "ccan/str/str.h"

#include "resource.h"
#include "inventory.h"
#include "reslog.h"
#include "drain.h"
#include "rutil.h"
#include "topo.h"

struct reduction {
    int count;          // number of ranks represented
    int descendants;    // number of TBON descendants
    struct rlist *rl;   // resources: self + descendants
};

struct topo {
    struct resource_ctx *ctx;
    flux_msg_handler_t **handlers;
    char *xml;
    struct rlist *r_local;

    struct reduction reduce;
};

static int drain_self (struct topo *topo, const char *reason)
{
    flux_log (topo->ctx->h, LOG_ERR, "draining: %s", reason);

    if (topo->ctx->rank == 0) {
        if (drain_rank (topo->ctx->drain, topo->ctx->rank, reason) < 0)
            return -1;
    }
    else {
        char rankstr[16];
        flux_future_t *f;

        snprintf (rankstr, sizeof (rankstr), "%ju", (uintmax_t)topo->ctx->rank);
        if (!(f = flux_rpc_pack (topo->ctx->h,
                                 "resource.drain",
                                 0,
                                 0,
                                 "{s:s s:s s:s}",
                                 "targets", rankstr,
                                 "reason", reason,
                                 "mode", "update")))
            return -1;
        if (flux_rpc_get (f, NULL) < 0) {
            flux_future_destroy (f);
            return -1;
        }
        flux_future_destroy (f);
    }
    return 0;
}

static int topo_verify (struct topo *topo, json_t *R, bool nodrain)
{
    json_error_t e;
    struct rlist *rl = NULL;
    struct rlist *rl_cores = NULL;
    struct rlist *r_local_cores = NULL;
    flux_error_t error;
    int rc = -1;

    if (!(rl = rlist_from_json (R, &e))) {
        flux_log (topo->ctx->h, LOG_ERR, "R: %s", e.text);
        errno = EINVAL;
        return -1;
    }

    /*  Only verify cores (and rank hostname) for now.
     *
     *  This is to allow GPUs to be configured or set in a job's allocated
     *  R even when the system installed libhwloc fails to detect GPUs
     *  due to lack of appropriately configured backend or other reason.
     *  (See flux-core issue #4181 for more details)
     */
    if (!(r_local_cores = rlist_copy_cores (topo->r_local))
        || !(rl_cores = rlist_copy_cores (rl))) {
        flux_log_error (topo->ctx->h, "rlist_copy_cores");
        goto out;
    }
    rc = rlist_verify (&error, rl_cores, r_local_cores);
    if (rc < 0 && !nodrain) {
        if (drain_self (topo, error.text) < 0)
            goto out;
    }
    else if (rc != 0)
        flux_log (topo->ctx->h, LOG_ERR, "verify: %s", error.text);
    rc = 0;
out:
    rlist_destroy (rl_cores);
    rlist_destroy (r_local_cores);
    rlist_destroy (rl);
    return rc;
}

/* Call this on any rank when there are no more descendants reporting.
 * On rank 0, this finalizes the reduction.
 * On other ranks, the reduction is sent upstream.
 */
static int topo_reduce_finalize (struct topo *topo)
{
    json_t *resobj = NULL;

    if (!(resobj = rlist_to_R (topo->reduce.rl))) {
        flux_log (topo->ctx->h, LOG_ERR, "error converting reduced rlist");
        errno = EINVAL;
        return -1;
    }
    if (topo->ctx->rank == 0) {
        if (!inventory_get (topo->ctx->inventory)) {
            if (inventory_put (topo->ctx->inventory,
                               resobj,
                               "dynamic-discovery") < 0) {
                flux_log_error (topo->ctx->h,
                                "error setting reduced resource object");
                goto error;
            }
        }
    }
    else {
        flux_future_t *f;

        if (!(f = flux_rpc_pack (topo->ctx->h,
                                 "resource.topo-reduce",
                                 FLUX_NODEID_UPSTREAM,
                                 FLUX_RPC_NORESPONSE,
                                 "{s:i s:O}",
                                 "count", topo->reduce.count,
                                 "resource", resobj))) {
            flux_log_error (topo->ctx->h,
                            "resource.topo-reduce: error sending request");
            goto error;
        }
        flux_future_destroy (f);
    }
    json_decref (resobj);
    return 0;
error:
    ERRNO_SAFE_WRAP (json_decref, resobj);
    return -1;
}

/* Accept reduction input from downstream ranks.
 */
static void topo_reduce_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct topo *topo = arg;
    json_t *resobj;
    struct rlist *rl = NULL;
    int count;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i s:o}",
                             "count", &count,
                             "resource", &resobj) < 0
        || !(rl = rlist_from_json (resobj, NULL))) {
        flux_log (h, LOG_ERR, "error decoding topo-reduce request");
        return;
    }
    if (rlist_append (topo->reduce.rl, rl) < 0) {
        /* N.B. log nothing in this case as this error will occur naturally
         * when the resource module is reloaded and resource object is a dup.
         */
        goto done;
    }
    topo->reduce.count += count;
    if (topo->reduce.count == topo->reduce.descendants + 1) {
        if (topo_reduce_finalize (topo) < 0) // logs its own errors
            goto done;
    }
done:
    rlist_destroy (rl);
}

/* Set up for reduction of distributed topo->r_local to inventory.
 * Ranks with descendants wait for all of them to report in, then roll
 * up their own and their descendants' contributions into one object and
 * report that.  N.B. This is not a "timed batch" style reduction since the
 * final result cannot be obtained without the participation of all ranks.
 */
static int topo_reduce (struct topo *topo)
{
    const char *val;

    if (!(val = flux_attr_get (topo->ctx->h, "tbon.descendants")))
        return -1;
    errno = 0;
    topo->reduce.descendants = strtoul (val, NULL, 10);
    if (errno > 0)
        return -1;

    topo->reduce.count = 1;
    if (!(topo->reduce.rl = rlist_copy_empty (topo->r_local)))
        goto nomem;

    if (topo->reduce.descendants == 0) {
        if (topo_reduce_finalize (topo) < 0)
            return -1;
    }
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

static void topo_get_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct topo *topo = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_respond (h, msg, topo->xml) < 0)
        flux_log_error (h, "error responding to topo-get request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to topo-get request");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "resource.topo-reduce",  topo_reduce_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "resource.topo-get", topo_get_cb, FLUX_ROLE_USER },
    FLUX_MSGHANDLER_TABLE_END,
};


void topo_destroy (struct topo *topo)
{
    if (topo) {
        int saved_errno = errno;
        flux_msg_handler_delvec (topo->handlers);
        free (topo->xml);
        rlist_destroy (topo->reduce.rl);
        rlist_destroy (topo->r_local);
        free (topo);
        errno = saved_errno;
    }
}

static char *topo_get_local_xml (struct resource_ctx *ctx,
                                 struct resource_config *config)
{
    flux_t *parent_h;
    flux_future_t *f = NULL;
    char *result = NULL;
    const char *xml;

    errno = 0;
    if (!(parent_h = resource_parent_handle_open (ctx))
        || !(f = flux_rpc (parent_h,
                           "resource.topo-get",
                           NULL,
                           FLUX_NODEID_ANY,
                           0))
        || flux_rpc_get (f, &xml) < 0) {
        rhwloc_flags_t flags = config->norestrict ? RHWLOC_NO_RESTRICT : 0;
        /*  ENOENT just means there is no parent instance.
         *  No need for an error.
         */
        if (errno && errno != ENOENT)
            flux_log (ctx->h,
                      LOG_DEBUG,
                      "resource.topo-get to parent failed: %s",
                      strerror (errno));
        result = rhwloc_local_topology_xml (flags);
        goto out;
    }
    flux_log (ctx->h,
              LOG_INFO,
              "retrieved local hwloc XML from parent (norestrict=%s)",
              config->norestrict ? "true" : "false");
    if (config->norestrict) {
        result = strdup (xml);
        goto out;
    }
    /*  restrict topology to current CPU binding
     */
    result = rhwloc_topology_xml_restrict (xml);
out:
    flux_future_destroy (f);
    resource_parent_handle_close (ctx);
    return result;
}

struct topo *topo_create (struct resource_ctx *ctx,
                          struct resource_config *config)
{
    struct topo *topo;
    json_t *R;

    if (!(topo = calloc (1, sizeof (*topo))))
        return NULL;
    topo->ctx = ctx;
    if (!(topo->xml = topo_get_local_xml (ctx, config))) {
        flux_log (ctx->h, LOG_ERR, "error loading hwloc topology");
        goto error;
    }
    if (!(topo->r_local = rlist_from_hwloc (ctx->rank, topo->xml))) {
        flux_log_error (ctx->h, "error creating local resource object");
        goto error;
    }
    /* If global resource object is known now, use it to verify topo.
     */
    if ((R = inventory_get (ctx->inventory))) {
        const char *method = inventory_get_method (ctx->inventory);
        bool nodrain = false;

        if (method && streq (method, "job-info"))
            nodrain = true;
        if (!config->noverify && topo_verify (topo, R, nodrain) < 0)
            goto error;
    }
    /* Reduce topo to rank 0 unconditionally in case it is needed.
     */
    if (topo_reduce (topo) < 0) {
        flux_log_error (ctx->h, "error setting up topo reduction");
        goto error;
    }
    if (flux_msg_handler_addvec (ctx->h, htab, topo, &topo->handlers) < 0)
        goto error;
    return topo;
error:
    topo_destroy (topo);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
