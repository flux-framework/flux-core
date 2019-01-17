/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <jansson.h>

static void aggregate_check (flux_future_t *f, void *arg)
{
    int count, total;
    const void *result;
    flux_future_t *f_orig = arg;

    if (flux_future_get (f, &result) < 0 ||
        flux_kvs_lookup_get_unpack (f, "{s:i, s:i}",
                                       "count", &count,
                                       "total", &total) < 0) {
        flux_future_fulfill_error (f_orig, errno, NULL);
        flux_kvs_lookup_cancel (f);
        return;
    }
    if (count == total) {
        flux_future_fulfill (f_orig, NULL, NULL);
        flux_kvs_lookup_cancel (f);
    }
}

static void initialize_cb (flux_future_t *f, void *arg)
{
    const char *key = arg;
    flux_future_t *f2 = NULL;
    flux_t *h = flux_future_get_flux (f);

    f2 = flux_kvs_lookup (h, FLUX_KVS_WATCH|FLUX_KVS_WAITCREATE, key);
    if ((f2 == NULL)
        || (flux_future_then (f2, -1., aggregate_check, f) < 0)
        || (flux_future_aux_set (f, "flux::lookup_f", f2,
                                 (flux_free_f) flux_future_destroy) < 0)) {
        flux_future_destroy (f2);
        flux_future_fulfill_error (f, errno, NULL);
    }
}

flux_future_t *aggregate_wait (flux_t *h, const char *key)
{
    flux_future_t *f = flux_future_create (initialize_cb, (void *) key);
    if (f == NULL)
        return NULL;
    flux_future_set_flux (f, h);
    return (f);
}

flux_future_t *aggregate_get (flux_future_t *f)
{
    return (flux_future_aux_get (f, "flux::lookup_f"));
}

int aggregate_unpack (flux_future_t *f_orig, const char *path)
{
    int errnum = 0;
    int rc = 0;
    json_t *entries = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *fkvs = NULL;
    flux_future_t *f = aggregate_get (f_orig);
    flux_t *h = flux_future_get_flux (f);

    if ((flux_kvs_lookup_get_unpack (f, "{s:o}", "entries", &entries) < 0)
        || !(txn = flux_kvs_txn_create ())
        || (flux_kvs_txn_pack (txn, 0, path, "O", entries) < 0)
        || (flux_kvs_txn_unlink (txn, 0, flux_kvs_lookup_get_key (f)) < 0)
        || !(fkvs = flux_kvs_commit (h, 0, txn))
        || (flux_future_get (fkvs, NULL) < 0)) {
        errnum = errno;
        rc = -1;
    }
    flux_future_destroy (fkvs);
    flux_kvs_txn_destroy (txn);
    errno = errnum;
    return (rc);
}

flux_future_t *aggregator_push_json (flux_t *h, const char *key, json_t *o)
{
    uint32_t size;
    uint32_t rank;
    int n;
    char rankstr [16]; /* aggregator expects ranks as string */

    if ((flux_get_size (h, &size) < 0)
        || (flux_get_rank (h, &rank) < 0)
        || ((n = snprintf (rankstr, sizeof (rankstr), "%d", rank)) < 0)
        || (n >= sizeof (rankstr)))
        return NULL;

    return flux_rpc_pack (h, "aggregator.push", FLUX_NODEID_ANY, 0,
                             "{s:s,s:i,s:{s:o}}",
                             "key", key,
                             "total", size,
                             "entries", rankstr, o);
}

/* vi: ts=4 sw=4 expandtab
 */
