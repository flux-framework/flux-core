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
 * - if a node in undrain target is excluded, request fails
 * - if a node in drain target is already drained, request status depends
 *   on setting of optional 'mode' member:
 *    - If mode is not set, request fails
 *    - If mode=overwrite, request succeeds and reason is updated
 *    - If mode=force-overwrite, request succeeds and timestamp and reason
 *      are updated
 *    - If mode=update, request succeeds and reason is updated only for
 *      those target that are not drained or do not have reason set.
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
#include "src/common/libutil/errprintf.h"
#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"
#include "ccan/str/str.h"

#include "resource.h"
#include "reslog.h"
#include "exclude.h"
#include "drain.h"
#include "rutil.h"
#include "inventory.h"
#include "drainset.h"

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

struct drain_init_args {
    struct drain *drain;
    const struct idset *exclude;
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
                                  const char *reason,
                                  int overwrite)
{
    char *cpy = NULL;

    if (rank >= drain->ctx->size) {
        errno = EINVAL;
        return -1;
    }
    /*  Skip rank if it is already drained with an existing reason
     *   and the overwrite flag is not set.
     */
    if (!overwrite
        && drain->info[rank].drained
        && drain->info[rank].reason)
        return 0;

    if (reason && !(cpy = strdup (reason)))
        return -1;

    free (drain->info[rank].reason);
    drain->info[rank].reason = cpy;
    if (drain->info[rank].drained != drained || overwrite == 2) {
        drain->info[rank].drained = drained;
        drain->info[rank].timestamp = timestamp;
    }
    return 0;
}

static int update_draininfo_idset (struct drain *drain,
                                   struct idset *idset,
                                   bool drained,
                                   double timestamp,
                                   const char *reason,
                                   int overwrite)
{
    unsigned int rank;

    rank = idset_first (idset);
    while (rank != IDSET_INVALID_ID) {
        if (update_draininfo_rank (drain,
                                   rank,
                                   drained,
                                   timestamp,
                                   reason,
                                   overwrite) < 0)
            return -1;
        rank = idset_next (idset, rank);
    }
    return 0;
}

/*  Check if all targets in idset are either not drained, or do
 *   not have a reason currently set. If one or more ranks do
 *   not meet this criteria then the function returns -1 and
 *   calls out the ranks in the returned errstr.
 */
static int check_draininfo_idset (struct drain *drain,
                                  struct idset *idset,
                                  flux_error_t *errp)
{
    int rc = 0;
    unsigned int rank;
    bool was_excluded = false;
    bool was_drained = false;
    const struct idset *exclude;
    struct idset *errids = idset_create (0, IDSET_FLAG_AUTOGROW);

    errp->text[0] = '\0';

    if (!errids)
        return -1;
    exclude = exclude_get (drain->ctx->exclude);

    rank = idset_first (idset);
    while (rank != IDSET_INVALID_ID) {
        bool is_error = false;
        if (idset_test (exclude, rank)) {
            was_excluded = true;
            is_error = true;
        }
        if (drain->info[rank].drained && drain->info[rank].reason) {
            was_drained = true;
            is_error = true;
        }
        if (is_error) {
            rc = -1;
            if (idset_set (errids, rank) < 0)
                flux_log_error (drain->ctx->h,
                                "check_draininfo_idset: idset_set(%d)",
                                rank);
        }
        rank = idset_next (idset, rank);
    }
    if (rc < 0) {
        char *s;
        int n = idset_count (errids);

        if (!(s = idset_encode (errids, IDSET_FLAG_RANGE)))
            flux_log_error (drain->ctx->h,
                            "check_draininfo_idset: idset_encode");
        errprintf (errp,
                   "rank%s %s %s%s%s",
                   n > 1 ? "s" : "",
                   s ? s : "(unknown)",
                   was_drained ? "already drained" : "",
                   was_drained && was_excluded ? " or " : "",
                   was_excluded ? "excluded" : "");
        free (s);

        /*  If any node was drained, then return EEXIST as a hint of this
         *  fact. Otherwise, an attempt to drain an excluded node was made,
         *  and that is invalid, so return EINVAL.
         */
        errno = was_drained ? EEXIST : EINVAL;
    }
    idset_destroy (errids);
    return rc;
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
                                       reason,
                                       0) < 0) {
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
    json_t *o = NULL;
    struct drainset *ds = drainset_create ();
    if (!ds)
        goto error;
    for (unsigned int rank = 0; rank < drain->ctx->size; rank++) {
        if (drain->info[rank].drained) {
            if (drainset_drain_rank (ds,
                                     rank,
                                     drain->info[rank].timestamp,
                                     drain->info[rank].reason) < 0)
                goto error;
        }
    }
    o = drainset_to_json (ds);
error:
    drainset_destroy (ds);
    return o;
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
                                         flux_error_t *errp)
{
    struct idset *idset;

    if (!(idset = inventory_targets_to_ranks (drain->ctx->inventory,
                                              ranks, errp)))
        return NULL;
    if (idset_count (idset) == 0) {
        errprintf (errp, "idset is empty");
        errno = EINVAL;
        goto error;
    }
    if (idset_last (idset) >= drain->ctx->size) {
        errprintf (errp, "idset is out of range");
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
    const char *mode = NULL;
    const char *reason = NULL;
    struct idset *idset = NULL;
    const char *errstr = NULL;
    char *idstr = NULL;
    flux_error_t error;
    double timestamp;
    int overwrite = 0;
    int update_only = 0;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s?s s?s}",
                             "targets", &s,
                             "reason", &reason,
                             "mode", &mode) < 0)
        goto error;
    if (!(idset = drain_idset_decode (drain, s, &error))) {
        errstr = error.text;
        goto error;
    }
    if (get_timestamp_now (&timestamp) < 0)
        goto error;

    if (mode) {
        errstr = "Invalid mode specified";
        if (streq (mode, "update"))
            update_only = 1;
        else if (streq (mode, "overwrite"))
            overwrite = 1;
        else if (streq (mode, "force-overwrite"))
            overwrite = 2;
        else {
            errno = EINVAL;
            goto error;
        }
    }

    /*  If neither overwrite or update_only are set, then return error unless
     *   none of the target ranks are already drained.
     */
    if (!overwrite &&
        !update_only &&
        check_draininfo_idset (drain, idset, &error) < 0) {
        errstr = error.text;
        goto error;
    }
    if (update_draininfo_idset (drain,
                                idset,
                                true,
                                timestamp,
                                reason,
                                overwrite) < 0)
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
                               "{s:s s:s s:i}",
                               "idset", idstr,
                               "reason", reason,
                               "overwrite", overwrite);
    else
        rc = reslog_post_pack (drain->ctx->reslog,
                               msg,
                               timestamp,
                               "drain",
                               "{s:s s:i}",
                               "idset", idstr,
                               "overwrite", overwrite);
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
    if (update_draininfo_rank (drain, rank, true, timestamp, reason, 0) < 0)
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

static int undrain_rank_idset (struct drain *drain,
                               const flux_msg_t *msg,
                               struct idset *idset)
{
    char *idstr;
    int rc;

    if (idset_count (idset) == 0)
        return 0;
    if (update_draininfo_idset (drain, idset, false, 0., NULL, 1) < 0)
        return -1;
    if (!(idstr = idset_encode (idset, IDSET_FLAG_RANGE)))
        return -1;
    rc = reslog_post_pack (drain->ctx->reslog,
                           msg,
                           0.,
                           "undrain",
                           "{s:s}",
                           "idset",
                           idstr);
    free (idstr);
    return rc;
}

int undrain_ranks (struct drain *drain, const struct idset *ranks)
{
    struct idset *drained = NULL;
    struct idset *undrain_ranks = NULL;
    int rc = -1;

    if (!(drained = drain_get (drain))
        || !(undrain_ranks = idset_intersect (ranks, drained)))
        goto out;
    rc = undrain_rank_idset (drain, NULL, undrain_ranks);
out:
    idset_destroy (drained);
    idset_destroy (undrain_ranks);
    return rc;
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
    const char *errstr = NULL;
    flux_error_t error;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s}",
                             "targets",
                             &s) < 0)
        goto error;
    if (!(idset = drain_idset_decode (drain, s, &error))) {
        errstr = error.text;
        goto error;
    }
    id = idset_first (idset);
    while (id != IDSET_INVALID_ID) {
        if (!drain->info[id].drained) {
            errprintf (&error, "rank %u not drained", id);
            errno = EINVAL;
            errstr = error.text;
            goto error;
        }
        id = idset_next (idset, id);
    }
    if (undrain_rank_idset (drain, msg, idset) < 0)
        goto error;
    idset_destroy (idset);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to undrain request");
    idset_destroy (idset);
}

static int replay_map (unsigned int id, json_t *val, void *arg)
{
    struct drain_init_args *args = arg;
    struct drain *drain = args->drain;
    const char *reason;
    double timestamp;
    char *cpy;

    /* Ignore excluded ranks */
    if (idset_test (args->exclude, id))
        return 0;

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
    const struct idset *exclude = exclude_get (drain->ctx->exclude);

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
            if (streq (name, "resource-init")) {
                struct drain_init_args args = {
                    .drain = drain,
                    .exclude = exclude
                };
                if (json_unpack (context, "{s:o}", "drain", &draininfo) < 0) {
                    errno = EPROTO;
                    return -1;
                }
                if (rutil_idkey_map (draininfo, replay_map, &args) < 0)
                    return -1;
            }
            else if (streq (name, "drain")) {
                int overwrite = 1;
                if (json_unpack (context,
                                 "{s:s s?s s?i}",
                                 "idset", &s,
                                 "reason", &reason,
                                 "overwrite", &overwrite) < 0) {
                    errno = EPROTO;
                    return -1;
                }
                if (!(idset = idset_decode (s)))
                    return -1;
                if (exclude && idset_subtract (idset, exclude) < 0)
                    return -1;
                if (update_draininfo_idset (drain,
                                            idset,
                                            true,
                                            timestamp,
                                            reason,
                                            overwrite) < 0) {
                    idset_destroy (idset);
                    return -1;
                }
                idset_destroy (idset);
            }
            else if (streq (name, "undrain")) {
                if (json_unpack (context, "{s:s}", "idset", &s) < 0) {
                    errno = EPROTO;
                    return -1;
                }
                if (!(idset = idset_decode (s)))
                    return -1;
                if (exclude && idset_subtract (idset, exclude) < 0)
                    return -1;
                if (update_draininfo_idset (drain,
                                            idset,
                                            false,
                                            timestamp,
                                            NULL,
                                            1) < 0) {
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
