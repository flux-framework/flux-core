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

#include <stdarg.h>

#include <flux/core.h>
#include <jansson.h>

static void aggregate_wait_set_errnum (flux_future_t *f, int errnum)
{
    if (flux_future_aux_set (f, "aggregate::errnum",
                             (void *) (intptr_t) errnum, NULL) < 0) {
        /* Not much we can do here but immediately fulfill future and
         *  hope for the best.
         */
        flux_future_fulfill_error (f, errnum, NULL);
    }
}

static intptr_t aggregate_wait_get_errnum (flux_future_t *f)
{
    void *v = flux_future_aux_get (f, "aggregate::errnum");
    if (v == NULL)
        return (0);
    return ((intptr_t) v);
}

static void aggregate_fulfill_finalize (flux_future_t *f)
{
    intptr_t errnum = aggregate_wait_get_errnum (f);
    if (errnum)
        flux_future_fulfill_error (f, (int) errnum, NULL);
    else
        flux_future_fulfill (f, NULL, NULL);
}

static void aggregate_check (flux_future_t *f, void *arg)
{
    int count, total;
    const char *result;
    json_t *o = NULL;
    flux_future_t *f_orig = arg;

    if (flux_kvs_lookup_get (f, &result) < 0) {
        if (errno == ENODATA) {
            /* flux_kvs_lookup is now canceled, so it is now safe to destroy
             *  the lookup future and finalize fulfillment of the
             *  aggregate_wait future.
             */
            flux_future_destroy (f);
            aggregate_fulfill_finalize (f_orig);
            return;
        }
        flux_kvs_lookup_cancel (f);
        aggregate_wait_set_errnum (f_orig, errno);
    }
    else if (!(o = json_loads (result, 0, NULL))
            || (json_unpack (o, "{s:i,s:i}",
                                "count", &count,
                                "total", &total) < 0)) {
        flux_kvs_lookup_cancel (f);
        aggregate_wait_set_errnum (f_orig, errno);
    }
    else if (count == total) {
        flux_future_aux_set (f_orig, "aggregate::json_t",
                             json_incref (o), (flux_free_f) json_decref);
        flux_kvs_lookup_cancel (f);
        /*  f_orig will be fulfilled by aggregate_fulfill_finalize() once
         *   flux_kvs_lookup_get() returns ENODATA. This ensures there
         *   are no stray RPC responses and allows safe deletion of the
         *   kvs lookup future
         */
    }
    flux_future_reset (f);
    json_decref (o);
}

static void initialize_cb (flux_future_t *f, void *arg)
{
    const char *key = arg;
    flux_future_t *f2 = NULL;
    flux_t *h = flux_future_get_flux (f);

    f2 = flux_kvs_lookup (h, FLUX_KVS_WATCH|FLUX_KVS_WAITCREATE, key);
    if ((f2 == NULL)
        || (flux_future_then (f2, -1., aggregate_check, f) < 0)) {
        flux_future_destroy (f2);
        flux_future_fulfill_error (f, errno, NULL);
    }
}

flux_future_t *aggregate_wait (flux_t *h, const char *key)
{
    char *s;
    flux_future_t *f = flux_future_create (initialize_cb, (void *) key);
    if ((f == NULL) || !(s = strdup (key))) {
        flux_future_destroy (f);
        return NULL;
    }
    flux_future_set_flux (f, h);
    flux_future_aux_set (f, "aggregate::key", s, free);
    return (f);
}

int aggregate_wait_get_unpack (flux_future_t *f, const char *fmt, ...)
{
    int rc;
    va_list ap;
    json_t *o = NULL;
    if (!(o = flux_future_aux_get (f, "aggregate::json_t")))
        return -1;
    va_start (ap, fmt);
    rc = json_vunpack_ex (o, NULL, 0, fmt, ap);
    va_end (ap);
    return rc;
}

const char *aggregate_wait_get_key (flux_future_t *f)
{
    return flux_future_aux_get (f, "aggregate::key");
}

int aggregate_unpack_to_kvs (flux_future_t *f, const char *path)
{
    int errnum = 0;
    int rc = 0;
    json_t *entries = NULL;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *fkvs = NULL;
    flux_t *h = flux_future_get_flux (f);

    if ((aggregate_wait_get_unpack (f, "{s:o}", "entries", &entries) < 0)
        || !(txn = flux_kvs_txn_create ())
        || (flux_kvs_txn_pack (txn, 0, path, "O", entries) < 0)
        || (flux_kvs_txn_unlink (txn, 0, aggregate_wait_get_key (f)) < 0)
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

flux_future_t *aggregator_push_json (flux_t *h, int fwd_count,
                                     const char *key, json_t *o)
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
                             "{s:s,s:i,s:i,s:{s:o}}",
                             "key", key,
                             "total", size,
                             "fwd_count", fwd_count,
                             "entries", rankstr, o);
}

/* vi: ts=4 sw=4 expandtab
 */
