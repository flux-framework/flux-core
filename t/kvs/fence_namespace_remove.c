/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* ensure fences that have not yet completed get errors when a
 * namespace is removed */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <libgen.h>
#include <pthread.h>
#include <getopt.h>
#include <inttypes.h>
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/oom.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/monotime.h"
#include "src/common/libutil/tstat.h"

static char *prefix = NULL;
static char *ns = NULL;

static void usage (void)
{
    fprintf (stderr, "Usage: fence_namespace_remove namespace prefix\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h = NULL;
    uint32_t rank;
    char *key = NULL;
    char *fence_name = NULL;
    flux_future_t *f = NULL;
    flux_kvs_txn_t *txn = NULL;

    log_init (basename (argv[0]));

    if (argc != 3)
        usage ();

    ns = argv[1];
    prefix = argv[2];

    if (!(h = flux_open (NULL, 0))) {
        log_err_exit ("flux_open");
        goto done;
    }

    if (flux_get_rank (h, &rank) < 0) {
        log_err ("flux_get_rank");
        goto done;
    }

    if (!(txn = flux_kvs_txn_create ())) {
        log_err ("flux_kvs_txn_create");
        goto done;
    }

    key = xasprintf ("%s.%d", prefix, rank);
    fence_name = xasprintf ("%s-%d", prefix, rank);

    if (flux_kvs_txn_pack (txn, 0, key, "i", 42) < 0) {
        log_err ("%s: flux_kvs_txn_pack", key);
        goto done;
    }

    /* nprocs = 2, but we call flux_kvs_fence only once, so the
     * flux_future_get() below should hang until an error occurs
     */

    if (!(f = flux_kvs_fence (h, ns, 0, fence_name, 2, txn))) {
        log_err ("flux_kvs_fence");
        goto done;
    }

    if (flux_future_get (f, NULL) < 0) {
        printf ("flux_future_get: %s\n", flux_strerror (errno));
        goto done;
    }

done:
    flux_future_destroy (f);
    free (key);
    free (fence_name);
    flux_kvs_txn_destroy (txn);
    flux_close (h);
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
