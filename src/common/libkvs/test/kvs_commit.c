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

#include <string.h>
#include <errno.h>

#include "src/common/libflux/flux.h"
#include "kvs_commit.h"
#include "kvs_txn.h"
#include "src/common/libtap/tap.h"

void errors (void)
{
    flux_kvs_txn_t *txn;

    /* check simple error cases */

    errno = 0;
    ok (flux_kvs_fence (NULL, NULL, 0, NULL, 0, NULL) == NULL && errno == EINVAL,
        "flux_kvs_fence fails on bad params");

    errno = 0;
    ok (flux_kvs_fence (NULL, NULL, 0, "foo", 1, NULL) == NULL && errno == EINVAL,
        "flux_kvs_fence fails on bad handle");

    errno = 0;
    ok (flux_kvs_commit (NULL, NULL, 0, NULL) == NULL && errno == EINVAL,
        "flux_kvs_commit fails on bad params");

    txn = flux_kvs_txn_create ();

    errno = 0;
    ok (flux_kvs_commit (NULL, NULL, 0, txn) == NULL && errno == EINVAL,
        "flux_kvs_commit fails on bad handle");

    errno = 0;
    ok (flux_kvs_commit_get_treeobj (NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_commit_get_treeobj fails on bad input");

    errno = 0;
    ok (flux_kvs_commit_get_sequence (NULL, NULL) < 0 && errno == EINVAL,
        "flux_kvs_commit_get_sequence fails on bad input");

    flux_kvs_txn_destroy (txn);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    errors ();

    done_testing ();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
