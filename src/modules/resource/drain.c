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
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <time.h>
#include <math.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libidset/idset.h"
#include "src/common/libhostlist/hostlist.h"
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
                                     round(drain->info[rank].timestamp),
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
    char *nodelist = NULL;
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
    if (!(idstr = idset_encode (idset, IDSET_FLAG_RANGE))
        || !(nodelist = flux_hostmap_lookup (h, idstr, NULL)))
        goto error;

    /*  If draining with no reason, do not encode 'reason' in the
     *   eventlog so that it can be replayed as reason=NULL.
     */
    if (reason)
        rc = reslog_post_pack (drain->ctx->reslog,
                               msg,
                               timestamp,
                               "drain",
                               0,
                               "{s:s s:s s:s s:i}",
                               "idset", idstr,
                               "nodelist", nodelist,
                               "reason", reason,
                               "overwrite", overwrite);
    else
        rc = reslog_post_pack (drain->ctx->reslog,
                               msg,
                               timestamp,
                               "drain",
                               0,
                               "{s:s s:s s:i}",
                               "idset", idstr,
                               "nodelist", nodelist,
                               "overwrite", overwrite);
    if (rc < 0)
        goto error;
    free (nodelist);
    free (idstr);
    idset_destroy (idset);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to drain request");
    free (nodelist);
    free (idstr);
    idset_destroy (idset);
}

int drain_rank (struct drain *drain, uint32_t rank, const char *reason)
{
    char rankstr[16];
    double timestamp;
    char *nodelist = NULL;
    int rc = -1;

    if (rank >= drain->ctx->size || !reason) {
        errno = EINVAL;
        return -1;
    }
    if (get_timestamp_now (&timestamp) < 0)
        return -1;
    if (update_draininfo_rank (drain, rank, true, timestamp, reason, 0) < 0)
        return -1;
    snprintf (rankstr, sizeof (rankstr), "%ju", (uintmax_t)rank);
    if (!(nodelist = flux_hostmap_lookup (drain->ctx->h, rankstr, NULL)))
        return -1;
    if (reslog_post_pack (drain->ctx->reslog,
                          NULL,
                          timestamp,
                          "drain",
                          0,
                          "{s:s s:s s:s}",
                          "idset", rankstr,
                          "nodelist", nodelist,
                          "reason", reason) < 0
        || reslog_sync (drain->ctx->reslog) < 0)
        goto done;
    rc = 0;
done:
    ERRNO_SAFE_WRAP (free, nodelist);
    return rc;
}

static int undrain_rank_idset (struct drain *drain,
                               const flux_msg_t *msg,
                               struct idset *idset,
                               const char *reason)
{
    char *idstr;
    char *nodelist = NULL;
    int rc = -1;

    if (idset_count (idset) == 0)
        return 0;
    if (update_draininfo_idset (drain, idset, false, 0., NULL, 1) < 0)
        return -1;
    if (!(idstr = idset_encode (idset, IDSET_FLAG_RANGE))
        || !(nodelist = flux_hostmap_lookup (drain->ctx->h, idstr, NULL)))
        goto done;
    if (reason)
        rc = reslog_post_pack (drain->ctx->reslog,
                               msg,
                               0.,
                               "undrain",
                               0,
                               "{s:s s:s s:s}",
                               "idset", idstr,
                               "nodelist", nodelist,
                               "reason", reason);
    else
        rc = reslog_post_pack (drain->ctx->reslog,
                               msg,
                               0.,
                               "undrain",
                               0,
                               "{s:s s:s}",
                               "idset", idstr,
                               "nodelist", nodelist);
done:
    ERRNO_SAFE_WRAP (free, nodelist);
    ERRNO_SAFE_WRAP (free, idstr);
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
    const char *mode = NULL;
    const char *reason = NULL;
    struct idset *idset = NULL;
    struct idset *undrained = NULL;
    unsigned int id;
    const char *errstr = NULL;
    flux_error_t error;
    bool force = false;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s?s s?s}",
                             "targets", &s,
                             "mode", &mode,
                             "reason", &reason) < 0)
        goto error;
    if (!(idset = drain_idset_decode (drain, s, &error))) {
        errstr = error.text;
        goto error;
    }
    if (mode) {
        if (streq (mode, "force"))
            force = true;
        else {
            errprintf (&error, "invalid undrain mode '%s' specified", mode);
            errno = EINVAL;
            errstr = error.text;
            goto error;
        }
    }
    if (!force && !(undrained = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        errprintf (&error,
                   "failed to create idset for undrained ranks: %s",
                   strerror (errno));
        errstr = error.text;
        goto error;
    }
    id = idset_first (idset);
    while (id != IDSET_INVALID_ID) {
        if (!drain->info[id].drained) {
            int rc;
            /* This rank is already undrained, remove it from targets
             * if mode=force. Otherwise, add rank to undrained idset.
             */
            rc = force ? idset_clear (idset, id) : idset_set (undrained, id);
            if (rc < 0) {
                errprintf (&error, "failed to update undrain target idset");
                errstr = error.text;
                goto error;
            }
        }
        id = idset_next (idset, id);
    }
    if (!force && idset_count (undrained) > 0) {
        char *nodelist = NULL;
        char *ranks = idset_encode (undrained, IDSET_FLAG_RANGE);
        if (ranks)
            nodelist = flux_hostmap_lookup (h, ranks, NULL);
        errprintf (&error,
                   "%s (rank%s %s) not drained",
                   nodelist ? nodelist : "unknown",
                   idset_count (undrained) > 1 ? "s" : "",
                   ranks ? ranks : "unknown");
        free (ranks);
        free (nodelist);
        errstr = error.text;
        errno = EINVAL;
        goto error;
    }
    if (idset_count (idset) == 0) {
        /* If idset is now empty then no targets are drained and
         * mode=force was used. Therefore, immediately return success:
         */
        if (flux_respond (h, msg, NULL) < 0)
            flux_log_error (h, "error responding to undrain request");
    }
    else if (undrain_rank_idset (drain, msg, idset, reason) < 0)
        goto error;
    idset_destroy (idset);
    idset_destroy (undrained);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to undrain request");
    idset_destroy (idset);
    idset_destroy (undrained);
}

/* Add rank to ids, adjusting rank if the rank:host mapping has changed.
 * Don't add the rank if the host no longer exists, or if it exceeds
 * the instance size.
 *
 * N.B. When running multiple brokers per node, flux_get_rankbyhost()
 * returns the first rank on 'host', so its result cannot be directly
 * used as the new rank.  Instead, first check that flux_get_hostbyrank()
 * differs from 'host'.
 */
static void add_target (struct idset *ids,
                        unsigned int rank,
                        const char *host,
                        flux_t *h)
{
    if (host) {
        const char *nhost = flux_get_hostbyrank (h, rank);
        int nrank;

        if (!streq (host, nhost)) { // nhost could be "(null)" on bad rank
            if ((nrank = flux_get_rankbyhost (h, host)) < 0)
                return;
            rank = nrank;
        }
    }
    (void)idset_set (ids, rank); // no-op if rank exceeds fixed set size
}

/* Return an idset containing decoded 'ranks', possibly adjusted based on
 * 'nodelist' and the instance size.  Any ranks that are invalid are simply
 * not added (not treated as an error).
 */
static struct idset *decode_targets (struct drain *drain,
                                     const char *ranks,
                                     const char *nodelist)
{
    struct idset *ids;
    struct hostlist *nl = NULL;
    struct idset *newids = NULL;
    unsigned int rank;
    int index;

    if (!(ids = idset_decode (ranks))
        || !(nl = hostlist_decode (nodelist))
        || !(newids = idset_create (drain->ctx->size, 0)))
        goto done;

    index = 0;
    rank = idset_first (ids);
    while (rank != IDSET_INVALID_ID) {
        const char *host = hostlist_nth (nl, index++);

        add_target (newids, rank, host, drain->ctx->h);
        rank = idset_next (ids, rank);
    }
done:
    hostlist_destroy (nl);
    idset_destroy (ids);
    return newids;
}

/* Recover drained idset from eventlog.
 */
static int replay_eventlog (struct drain *drain,
                            const json_t *eventlog,
                            flux_error_t *error)
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
            struct idset *idset;

            if (eventlog_entry_parse (entry, &timestamp, &name, &context) < 0) {
                errprintf (error, "line %zu: event parse error", index + 1);
                return -1;
            }
            if (streq (name, "drain")) {
                int overwrite = 1;
                const char *nodelist;
                if (json_unpack (context,
                                 "{s:s s:s s?s s?i}",
                                 "idset", &s,
                                 "nodelist", &nodelist,
                                 "reason", &reason,
                                 "overwrite", &overwrite) < 0) {
                    errprintf (error, "line %zu: drain parse error", index + 1);
                    errno = EPROTO;
                    return -1;
                }
                if (!(idset = decode_targets (drain, s, nodelist))) {
                    errprintf (error,
                               "line %zu: drain target decode error",
                               index + 1);
                    return -1;
                }
                if (update_draininfo_idset (drain,
                                            idset,
                                            true,
                                            timestamp,
                                            reason,
                                            overwrite) < 0) {
                    errprintf (error,
                               "line %zu: drain update error",
                               index + 1);
                    idset_destroy (idset);
                    return -1;
                }
                idset_destroy (idset);
            }
            else if (streq (name, "undrain")) {
                const char *nodelist = NULL;
                if (json_unpack (context,
                                 "{s:s s:s}",
                                 "idset", &s,
                                 "nodelist", &nodelist) < 0) {
                    errprintf (error,
                               "line %zu: undrain parse error",
                               index + 1);
                    errno = EPROTO;
                    return -1;
                }
                if (!(idset = decode_targets (drain, s, nodelist))) {
                    errprintf (error,
                               "line %zu: undrain target decode error",
                               index + 1);
                    return -1;
                }
                if (update_draininfo_idset (drain,
                                            idset,
                                            false,
                                            timestamp,
                                            NULL,
                                            1) < 0) {
                    errprintf (error,
                               "line %zu: undrain update error",
                               index + 1);
                    idset_destroy (idset);
                    return -1;
                }
                idset_destroy (idset);
            }
        }
    }
    return 0;
}

/* Excluded targets may not be drained.  If, after replaying the eventlog,
 * any excluded nodes are drained, undrain them.  Besides updating the current
 * drain state, an undrain event must be posted to resource.eventlog so that
 * if the target is unexcluded later on, it starts out undrained.
 */
static int reconcile_excluded (struct drain *drain,
                               const struct idset *exclude,
                               flux_error_t *error)
{
    struct idset *drained;
    struct idset *undrain_ranks = NULL;
    char *s = NULL;
    char *nodelist = NULL;
    int rc = -1;

    if (!exclude)
        return 0;
    if (!(drained = drain_get (drain))
        || !(undrain_ranks = idset_intersect (drained, exclude))) {
        errprintf (error,
                   "error calculating drained ∩ excluded: %s",
                   strerror (errno));
        goto done;
    }
    if (idset_count (undrain_ranks) > 0) {
        double timestamp;
        if (get_timestamp_now (&timestamp) < 0
            || update_draininfo_idset (drain,
                                       undrain_ranks,
                                       false,
                                       timestamp,
                                       NULL,
                                       1) < 0) {
            errprintf (error,
                       "error draining excluded nodes: %s",
                       strerror (errno));
            goto done;
        }
        if (!(s = idset_encode (undrain_ranks, IDSET_FLAG_RANGE))
            || !(nodelist = flux_hostmap_lookup (drain->ctx->h, s, NULL))
            || reslog_post_pack (drain->ctx->reslog,
                                 NULL,
                                 timestamp,
                                 "undrain",
                                 0,
                                 "{s:s s:s}",
                                 "idset", s,
                                 "nodelist", nodelist) < 0) {
            errprintf (error,
                       "error posting drain event for excluded nodes: %s",
                       strerror (errno));
            goto done;
        }
    }
    rc = 0;
done:
    ERRNO_SAFE_WRAP (free, nodelist);
    ERRNO_SAFE_WRAP (free, s);
    idset_destroy (undrain_ranks);
    idset_destroy (drained);
    return rc;
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
        free (drain);
        errno = saved_errno;
    }
}

struct drain *drain_create (struct resource_ctx *ctx, const json_t *eventlog)
{
    struct drain *drain;
    flux_error_t error;

    if (!(drain = calloc (1, sizeof (*drain))))
        return NULL;
    drain->ctx = ctx;
    if (!(drain->info = calloc (ctx->size, sizeof (drain->info[0]))))
        goto error;
    if (replay_eventlog (drain, eventlog, &error) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: %s", RESLOG_KEY, error.text);
        goto error;
    }
    if (reconcile_excluded (drain, exclude_get (ctx->exclude), &error) < 0) {
        flux_log (ctx->h, LOG_ERR, "%s", error.text);
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
