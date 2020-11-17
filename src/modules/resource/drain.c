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
 * Drained execution targets should be excluded from scheduling,
 * but may be used for determining job request satisfiability.
 *
 * Handle RPCs from front-end commands.
 * - if a node in undrain target is not drained, request fails
 * - if a node in drain target is already drained, request succeeds
 * - if a node in drain/undrain target is "excluded", request fails
 *
 * Post events for each drain/undrain action.  Drain state is sticky
 * across module reload / instance restart.  The state is reacquired
 * by replaying the eventlog.
 *
 * N.B. the 'reason' for drain is recorded in the eventlog but is not
 * part of the in-memory state here.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libidset/idset.h"
#include "src/common/libeventlog/eventlog.h"

#include "resource.h"
#include "reslog.h"
#include "exclude.h"
#include "drain.h"
#include "rutil.h"
#include "inventory.h"

struct drain {
    struct resource_ctx *ctx;
    struct idset *idset;
    flux_msg_handler_t **handlers;
};

const struct idset *drain_get (struct drain *drain)
{
    return drain->idset;
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
    unsigned int id;

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
    id = idset_first (idset);
    while (id != IDSET_INVALID_ID) {
        if (exclude_test (drain->ctx->exclude, id)) {
            (void)snprintf (errbuf,
                            errbufsize,
                            "%u is excluded by configuration",
                            id);
            errno = EINVAL;
            goto error;
        }
        id = idset_next (idset, id);
    }
    return idset;
error:
    idset_destroy (idset);
    return NULL;
}

/* Drain a set of ranked execution targets.
 * If a reason was provided it is recorded in the eventlog (only).
 */
static void drain_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct drain *drain = arg;
    const char *s;
    const char *reason = NULL;
    struct idset *idset = NULL;
    const char *errstr = NULL;
    char *idstr = NULL;
    char errbuf[256];

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
    if (rutil_idset_add (drain->idset, idset) < 0)
        goto error;
    if (!(idstr = idset_encode (idset, IDSET_FLAG_RANGE)))
        goto error;
    if (reslog_post_pack (drain->ctx->reslog,
                          msg,
                          0.,
                          "drain",
                          "{s:s s:s}",
                          "idset",
                          idstr,
                          "reason",
                          reason ? reason : "unknown") < 0)
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

int drain_rank (struct drain *drain, uint32_t rank, const char *reason)
{
    char rankstr[16];

    snprintf (rankstr, sizeof (rankstr), "%ju", (uintmax_t)rank);

    if (rank >= drain->ctx->size) {
        errno = EINVAL;
        return -1;
    }
    if (idset_set (drain->idset, rank) < 0)
        return -1;
    if (reslog_post_pack (drain->ctx->reslog,
                          NULL,
                          0.,
                          "drain",
                          "{s:s s:s}",
                          "idset",
                          rankstr,
                          "reason",
                          reason ? reason : "unknown") < 0)
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
        if (!idset_test (drain->idset, id)) {
            (void)snprintf (errbuf, sizeof (errbuf), "rank %u not drained", id);
            errno = EINVAL;
            errstr = errbuf;
            goto error;
        }
        id = idset_next (idset, id);
    }
    if (rutil_idset_sub (drain->idset, idset) < 0)
        goto error;
    if (!(idstr = idset_encode (idset, IDSET_FLAG_RANGE)))
        goto error;
    if (reslog_post_pack (drain->ctx->reslog,
                          msg,
                          0.,
                          "undrain",
                          "{s:s}",
                          "idset",
                          idstr) < 0) {
        int saved_errno = errno;
        (void)rutil_idset_add (drain->idset, idset); // restore orig.
        errno = saved_errno;
        goto error;
    }
    free (idstr);
    idset_destroy (idset);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to undrain request");
    free (idstr);
    idset_destroy (idset);
}

/* Recover drained idset from eventlog.
 */
static int replay_eventlog (struct drain *drain, const json_t *eventlog)
{
    size_t index;
    json_t *entry;

    if (eventlog) {
        json_array_foreach (eventlog, index, entry) {
            const char *name;
            json_t *context;
            const char *s;
            struct idset *idset;

            if (eventlog_entry_parse (entry, NULL, &name, &context) < 0)
                return -1;
            if (!strcmp (name, "resource-init")) {
                if (json_unpack (context, "{s:s}", "drain", &s) < 0) {
                    errno = EPROTO;
                    return -1;
                }
                idset_destroy (drain->idset);
                if (!(drain->idset = idset_decode (s)))
                    return -1;
            }
            else if (!strcmp (name, "drain")) {
                if (json_unpack (context, "{s:s}", "idset", &s) < 0) {
                    errno = EPROTO;
                    return -1;
                }
                if (!(idset = idset_decode (s)))
                    return -1;
                if (rutil_idset_add (drain->idset, idset) < 0) {
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
                if (rutil_idset_sub (drain->idset, idset) < 0) {
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
        idset_destroy (drain->idset);
        flux_msg_handler_delvec (drain->handlers);
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
    if (!(drain->idset = idset_create (ctx->size, 0)))
        goto error;
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
