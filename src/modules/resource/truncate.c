/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* truncate.c - handle truncation of resource.eventlog
 *
 * The resource.eventlog will grow indefinitely if not controlled.
 *
 * Remove any events from the eventlog that are older than the most
 * recent checkpoint of drain state.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <math.h>
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/errprintf.h"

#include "resource.h"
#include "reslog.h"
#include "drain.h"
#include "rutil.h"

void truncate_eventlog (struct resource_ctx *ctx,
                        const json_t *eventlog,
                        double checkpoint_timestamp,
                        double history)
{
    json_t *a = NULL;
    size_t index;
    json_t *entry;
    char *newlog = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;

    if (!eventlog)
        return;

    if (!(a = json_array ())) {
        flux_log_error (ctx->h, "error creating truncation array");
        return;
    }

    /* user desires to keep a configured amount of history, rollback
     * checkpoint timestamp if necessary
     */
    if (history) {
        double now, tmp;
        if (get_timestamp_now (&now) < 0) {
            flux_log_error (ctx->h, "error retrieving current time");
            return;
        }
        tmp = now - history;
        if (tmp > 0 && tmp < checkpoint_timestamp)
            checkpoint_timestamp = tmp;
    }

    json_array_foreach (eventlog, index, entry) {
        double timestamp;

        if (eventlog_entry_parse (entry, &timestamp, NULL, NULL) < 0) {
            flux_log_error (ctx->h, "event parse error");
            goto out;
        }
        /* no need to keep anything before checkpoint timestamp */
        if (timestamp < checkpoint_timestamp)
            continue;
        if (json_array_append (a, entry) < 0)
            goto out;
    }

    if (!(newlog = eventlog_encode (a))
        || !(txn = flux_kvs_txn_create ())
        || flux_kvs_txn_put (txn, 0, RESLOG_KEY, newlog) < 0
        || !(f = flux_kvs_commit (ctx->h, NULL, 0, txn))
        || flux_rpc_get (f, NULL) < 0)
        flux_log_error (ctx->h, "error truncating resource eventlog");

out:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    free (newlog);
    json_decref (a);
    return;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
