/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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

static void usage (void)
{
    fprintf (stderr, "Usage: fence_invalid prefix\n");
    exit (1);
}

int main (int argc, char *argv[])
{
    flux_t *h = NULL;
    char *fence_name = NULL;
    uint32_t rank;
    char *key1 = NULL;
    char *key2 = NULL;
    flux_future_t *f1 = NULL;
    flux_future_t *f2 = NULL;
    flux_kvs_txn_t *txn1 = NULL;
    flux_kvs_txn_t *txn2 = NULL;

    log_init (basename (argv[0]));

    if (argc != 2)
        usage ();

    prefix = argv[1];

    if (!(h = flux_open (NULL, 0))) {
        log_err_exit ("flux_open");
        goto done;
    }

    if (flux_get_rank (h, &rank) < 0) {
        log_err ("flux_get_rank");
        goto done;
    }

    if (!(txn1 = flux_kvs_txn_create ())) {
        log_err ("flux_kvs_txn_create");
        goto done;
    }

    if (!(txn2 = flux_kvs_txn_create ())) {
        log_err ("flux_kvs_txn_create");
        goto done;
    }

    fence_name = xasprintf ("%s-%d", prefix, rank);
    key1 = xasprintf ("%s.1.%d", prefix, rank);
    key2 = xasprintf ("%s.2.%d", prefix, rank);

    if (flux_kvs_txn_pack (txn1, 0, key1, "i", 42) < 0) {
        log_err ("%s: flux_kvs_txn_pack", key1);
        goto done;
    }

    if (flux_kvs_txn_pack (txn2, 0, key2, "i", 42) < 0) {
        log_err ("%s: flux_kvs_txn_pack", key2);
        goto done;
    }

    /* alter flags to generate an error on second fence */

    if (!(f1 = flux_kvs_fence (h, 0x1, fence_name, 2, txn1))) {
        log_err ("flux_kvs_fence");
        goto done;
    }

    if (!(f2 = flux_kvs_fence (h, 0x2, fence_name, 2, txn2))) {
        log_err ("flux_kvs_fence");
        goto done;
    }

    if (flux_rpc_get (f2, NULL) < 0) {
        printf ("flux_rpc_get: %s\n", flux_strerror (errno));
        goto done;
    }

done:
    flux_future_destroy (f1);
    flux_future_destroy (f2);
    free (key1);
    free (key2);
    free (fence_name);
    flux_kvs_txn_destroy (txn1);
    flux_kvs_txn_destroy (txn2);
    flux_close (h);
    log_fini ();

    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
