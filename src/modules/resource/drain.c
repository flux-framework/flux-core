/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* drain.c - handle drain/undrain requests
 *
 * Drained execution targets should be temporarily excluded from scheduling,
 * but may be used for determining job request satisfiability.
 *
 * Handle RPCs from front-end commands.
 * - if a node in undrain target is not drained, request fails
 * - if a node in drain target is already drained, request succeeds
 *
 * Post events for each drain/undrain action.  Drain state is sticky
 * across module reload / instance restart.  The state is reacquired
 * by replaying the eventlog.
 *
 * In addition, monitor the 'broker.torpid' group and drain any nodes that
 * are added to the group (if they are not already drained).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <time.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"

#include "resource.h"
#include "reslog.h"
#include "exclude.h"
#include "drain.h"
#include "rutil.h"
#include "inventory.h"

struct draininfo {
    bool drained;
    double timestamp;
    char *reason;
};

struct drain {
    struct resource_ctx *ctx;
    struct draininfo *info; // rank-indexed array [0:size-1]
    flux_msg_handler_t **handlers;
    flux_future_t *f;
};

static int get_timestamp_now (double *timestamp)
{
    struct timespec ts;
    if (clock_gettime (CLOCK_REALTIME, &ts) < 0)
        return -1;
    *timestamp = (1E-9 * ts.tv_nsec) + ts.tv_sec;
    return 0;
}

static int update_draininfo_rank (struct drain *drain,
                                  unsigned int rank,
                                  bool drained,
                                  double timestamp,
                                  const char *reason)
{
    char *cpy = NULL;

    if (rank >= drain->ctx->size) {
        errno = EINVAL;
        return -1;
    }
    if (reason && !(cpy = strdup (reason)))
        return -1;
    free (drain->info[rank].reason);
    drain->info[rank].reason = cpy;
    drain->info[rank].drained = drained;
    drain->info[rank].timestamp = timestamp;
    return 0;
}

static int update_draininfo_idset (struct drain *drain,
                                   struct idset *idset,
                                   bool drained,
                                   double timestamp,
                                   const char *reason)
{
    unsigned int rank;

    rank = idset_first (idset);
    while (rank != IDSET_INVALID_ID) {
        if (update_draininfo_rank (drain, rank, drained, timestamp, reason) < 0)
            return -1;
        rank = idset_next (idset, rank);
    }
    return 0;
}

static bool is_drained (struct drain *drain, unsigned int rank)
{
    if (rank < drain->ctx->size && drain->info[rank].drained)
        return true;
    return false;
}

static void broker_torpid_cb (flux_future_t *f, void *arg)
{
    struct drain *drain = arg;
    flux_t *h = drain->ctx->h;
    const char *members;
    struct idset *ids;
    unsigned int rank;
    double timestamp;
    const char *reason = "broker was unresponsive";
    char *idstr = NULL;

    if (flux_rpc_get_unpack (f, "{s:s}", "members", &members) < 0) {
        flux_log_error (h, "drain: group.get failed");
        return;
    }
    if (!(ids = idset_decode (members))) {
        flux_log_error (h, "drain: unable to decode group.get response");
        goto done;
    }
    rank = idset_first (ids);
    while (rank != IDSET_INVALID_ID) {
        if (is_drained (drain, rank)) {
            if (idset_clear (ids, rank) < 0) {
                flux_log_error (h, "error building torpid idset");
                goto done;
            }
        }
        rank = idset_next (ids, rank);
    }
    if (idset_count (ids) > 0) {
        if (get_timestamp_now (&timestamp) < 0
            || update_draininfo_idset (drain,
                                       ids,
                                       true,
                                       timestamp,
                                       reason) < 0) {
            flux_log_error (h, "error draining torpid nodes");
            goto done;
        }
        if (!(idstr = idset_encode (ids, IDSET_FLAG_RANGE))
            || reslog_post_pack (drain->ctx->reslog,
                                 NULL,
                                 timestamp,
                                 "drain",
                                 "{s:s s:s}",
                                 "idset", idstr,
                                 "reason", reason) < 0) {
            flux_log_error (h, "error posting drain event for torpid nodes");
            goto done;
        }
    }
done:
    idset_destroy (ids);
    free (idstr);
    flux_future_reset (f);
}

json_t *drain_get_info (struct drain *drain)
{
    json_t *o;
    unsigned int rank;

    if (!(o = json_object ()))
        goto nomem;
    for (rank = 0; rank < drain->ctx->size; rank++) {
        if (drain->info[rank].drained) {
            char *reason = drain->info[rank].reason;
            json_t *val;
            if (!(val = json_pack ("{s:f s:s}",
                                   "timestamp",
                                   drain->info[rank].timestamp,
                                   "reason",
                                   reason ? reason : "")))
                goto nomem;
            if (rutil_idkey_insert_id (o, rank, val) < 0) {
                ERRNO_SAFE_WRAP (json_decref, val);
                goto error;
            }
            json_decref (val);
        }
    }
    return o;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return NULL;
}

struct idset *drain_get (struct drain *drain)
{
    unsigned int rank;
    struct idset *ids;

    if (!(ids = idset_create (drain->ctx->size, 0)))
        return NULL;
    for (rank = 0; rank < drain->ctx->size; rank++) {
        if (drain->info[rank].drained) {
            if (idset_set (ids, rank) < 0) {
                idset_destroy (ids);
                return NULL;
            }
        }
    }
    return ids;
}

/* Decode string-encoded idset from drain/undrain request.
 * Catch various errors common to both requests.
 * On success, return idset object (caller must free).
 * On error, capture human readable error in 'errbuf', set errno, return NULL.
 */
static struct idset *drain_idset_decode (struct drain *drain,
                                         const char *ranks,
                                         char *errbuf,
                                         int errbufsize)
{
    struct idset *idset;

    if (!(idset = inventory_targets_to_ranks (drain->ctx->inventory,
                                              ranks, errbuf, errbufsize)))
        return NULL;
    if (idset_count (idset) == 0) {
        (void)snprintf (errbuf, errbufsize, "idset is empty");
        errno = EINVAL;
        goto error;
    }
    if (idset_last (idset) >= drain->ctx->size) {
        (void)snprintf (errbuf, errbufsize, "idset is out of range");
        errno = EINVAL;
        goto error;
    }
    return idset;
error:
    idset_destroy (idset);
    return NULL;
}

/* Drain a set of ranked execution targets.
 */
static void drain_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    int rc;
    struct drain *drain = arg;
    const char *s;
    const char *reason = NULL;
    struct idset *idset = NULL;
    const char *errstr = NULL;
    char *idstr = NULL;
    char errbuf[256];
    double timestamp;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s?:s}",
                             "targets",
                             &s,
                             "reason",
                             &reason) < 0)
        goto error;
    if (!(idset = drain_idset_decode (drain, s, errbuf, sizeof (errbuf)))) {
        errstr = errbuf;
        goto error;
    }
    if (get_timestamp_now (&timestamp) < 0)
        goto error;
    if (update_draininfo_idset (drain, idset, true, timestamp, reason) < 0)
        goto error;
    if (!(idstr = idset_encode (idset, IDSET_FLAG_RANGE)))
        goto error;

    /*  If draining with no reason, do not encode 'reason' in the
     *   eventlog so that it can be replayed as reason=NULL.
     */
    if (reason)
        rc = reslog_post_pack (drain->ctx->reslog,
                               msg,
                               timestamp,
                               "drain",
                               "{s:s s:s}",
                               "idset", idstr,
                               "reason", reason);
    else
        rc = reslog_post_pack (drain->ctx->reslog,
                               msg,
                               timestamp,
                               "drain",
                               "{s:s}",
                               "idset", idstr);
    if (rc < 0)
        goto error;
    free (idstr);
    idset_destroy (idset);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to drain request");
    free (idstr);
    idset_destroy (idset);
}

int drain_rank (struct drain *drain, uint32_t rank, const char *reason)
{
    char rankstr[16];
    double timestamp;

    if (rank >= drain->ctx->size || !reason) {
        errno = EINVAL;
        return -1;
    }
    if (get_timestamp_now (&timestamp) < 0)
        return -1;
    if (update_draininfo_rank (drain, rank, true, timestamp, reason) < 0)
        return -1;
    snprintf (rankstr, sizeof (rankstr), "%ju", (uintmax_t)rank);
    if (reslog_post_pack (drain->ctx->reslog,
                          NULL,
                          timestamp,
                          "drain",
                          "{s:s s:s}",
                          "idset",
                          rankstr,
                          "reason",
                          reason) < 0)
        return -1;
    if (reslog_sync (drain->ctx->reslog) < 0)
        return -1;
    return 0;
}

/* Un-drain a set of ranked execution targets.
 * If any of the ranks are not drained, fail the whole request.
 */
static void undrain_cb (flux_t *h,
                        flux_msg_handler_t *mh,
                        const flux_msg_t *msg,
                        void *arg)
{
    struct drain *drain = arg;
    const char *s;
    struct idset *idset = NULL;
    unsigned int id;
    char *idstr = NULL;
    const char *errstr = NULL;
    char errbuf[256];

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s}",
                             "targets",
                             &s) < 0)
        goto error;
    if (!(idset = drain_idset_decode (drain, s, errbuf, sizeof (errbuf)))) {
        errstr = errbuf;
        goto error;
    }
    id = idset_first (idset);
    while (id != IDSET_INVALID_ID) {
        if (!drain->info[id].drained) {
            (void)snprintf (errbuf, sizeof (errbuf), "rank %u not drained", id);
            errno = EINVAL;
            errstr = errbuf;
            goto error;
        }
        id = idset_next (idset, id);
    }
    if (update_draininfo_idset (drain, idset, false, 0., NULL) < 0)
        goto error;
    if (!(idstr = idset_encode (idset, IDSET_FLAG_RANGE)))
        goto error;
    if (reslog_post_pack (drain->ctx->reslog,
                          msg,
                          0.,
                          "undrain",
                          "{s:s}",
                          "idset",
                          idstr) < 0)
        goto error;
    free (idstr);
    idset_destroy (idset);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to undrain request");
    free (idstr);
    idset_destroy (idset);
}

static int replay_map (unsigned int id, json_t *val, void *arg)
{
    struct drain *drain = arg;
    const char *reason;
    double timestamp;
    char *cpy;

    if (id >= drain->ctx->size) {
        errno = EINVAL;
        return -1;
    }
    if (json_unpack (val,
                     "{s:f s:s}",
                     "timestamp",
                     &timestamp,
                     "reason",
                     &reason) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (!(cpy = strdup (reason))) // in this object, reason="" if unset
        return -1;
    free (drain->info[id].reason);
    drain->info[id].reason = cpy;
    drain->info[id].timestamp = timestamp;
    drain->info[id].drained = true;
    return 0;
}

/* Recover drained idset from eventlog.
 */
static int replay_eventlog (struct drain *drain, const json_t *eventlog)
{
    size_t index;
    json_t *entry;

    if (eventlog) {
        json_array_foreach (eventlog, index, entry) {
            double timestamp;
            const char *name;
            json_t *context;
            const char *s;
            const char *reason = NULL;
            json_t *draininfo = NULL;
            struct idset *idset;

            if (eventlog_entry_parse (entry, &timestamp, &name, &context) < 0)
                return -1;
            if (!strcmp (name, "resource-init")) {
                if (json_unpack (context, "{s:o}", "drain", &draininfo) < 0) {
                    errno = EPROTO;
                    return -1;
                }
                if (rutil_idkey_map (draininfo, replay_map, drain) < 0)
                    return -1;
            }
            else if (!strcmp (name, "drain")) {
                if (json_unpack (context,
                                 "{s:s s?s}",
                                 "idset", &s,
                                 "reason", &reason) < 0) {
                    errno = EPROTO;
                    return -1;
                }
                if (!(idset = idset_decode (s)))
                    return -1;
                if (update_draininfo_idset (drain,
                                            idset,
                                            true,
                                            timestamp,
                                            reason) < 0) {
                    idset_destroy (idset);
                    return -1;
                }
                idset_destroy (idset);
            }
            else if (!strcmp (name, "undrain")) {
                if (json_unpack (context, "{s:s}", "idset", &s) < 0) {
                    errno = EPROTO;
                    return -1;
                }
                if (!(idset = idset_decode (s)))
                    return -1;
                if (update_draininfo_idset (drain,
                                            idset,
                                            false,
                                            timestamp,
                                            NULL) < 0) {
                    idset_destroy (idset);
                    return -1;
                }
                idset_destroy (idset);
            }
        }
    }
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "resource.drain", drain_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  "resource.undrain", undrain_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

void drain_destroy (struct drain *drain)
{
    if (drain) {
        int saved_errno = errno;
        flux_msg_handler_delvec (drain->handlers);
        if (drain->info) {
            unsigned int rank;
            for (rank = 0; rank < drain->ctx->size; rank++)
                free (drain->info[rank].reason);
            free (drain->info);
        }
        flux_future_destroy (drain->f);
        free (drain);
        errno = saved_errno;
    }
}

struct drain *drain_create (struct resource_ctx *ctx, const json_t *eventlog)
{
    struct drain *drain;

    if (!(drain = calloc (1, sizeof (*drain))))
        return NULL;
    drain->ctx = ctx;
    if (!(drain->info = calloc (ctx->size, sizeof (drain->info[0]))))
        goto error;
    if (ctx->rank == 0) {
        if (!(drain->f = flux_rpc_pack (ctx->h,
                                        "groups.get",
                                        FLUX_NODEID_ANY,
                                        FLUX_RPC_STREAMING,
                                        "{s:s}",
                                        "name", "broker.torpid"))
            || flux_future_then (drain->f, -1, broker_torpid_cb, drain) < 0)
            goto error;
    }
    if (replay_eventlog (drain, eventlog) < 0) {
        flux_log_error (ctx->h, "problem replaying eventlog drain state");
        goto error;
    }
    if (flux_msg_handler_addvec (ctx->h, htab, drain, &drain->handlers) < 0)
        goto error;
    return drain;
error:
    drain_destroy (drain);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
